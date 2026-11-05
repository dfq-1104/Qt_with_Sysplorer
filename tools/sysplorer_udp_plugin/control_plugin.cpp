#include "control_plugin.h"
#include <mw_develop/mw_app.h>
#include <mw_develop/mw_workbench.h>
#include <mw_develop/mw_workbench_core.h>
#include <mw_develop/mw_sim_inst_panel.h>
#include <mw_develop/mw_workbench_mdiarea.h>
#include <mw_develop/mw_plot_window_manager.h>
#include <mw_class_manager/mw_class_manager.h>
#include <mw_class_manager/mw_mo_handler.h>
#include <mw_global/mw_task_manager.h>
#include <mw_sim_inst/mw_sim_control.h>
#include <mw_sim_inst/mw_sim_data.h>
#include <mw_sim_inst/mw_ani_service.h>
#include <mw_sim_inst/mw_ani_data_manager.h>
#include <mw_model_var_tree/var_info.h>
#include <mw_sim_plot/mw_sim_plot_window.h>
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUdpSocket>
#include <QUuid>
#include <QTextStream>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
template<class T> T sym(const wchar_t *dll,const char *name) {
    auto p=GetProcAddress(GetModuleHandleW(dll),name);
    if(!p) throw std::runtime_error(std::string("Missing host export: ")+name);
    return reinterpret_cast<T>(p);
}
template<class R> R get(void *self,const wchar_t *dll,const char *name) {
    return sym<R(*)(void*)>(dll,name)(self);
}
QString pointer(void *p) {return QString("0x%1").arg(quintptr(p),0,16);}
QString uuid() {return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool finite(double v) {return std::isfinite(v);}
QByteArray digest(const QByteArray &b) {return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
void require(bool yes,const char *message) {if(!yes) throw std::runtime_error(message);}
bool writeJson(const QString &path,const QJsonObject &o) {
    QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(o).toJson())>=0&&f.commit();
}
double at(const std::vector<double>& t,const std::vector<double>& v,double x) {
    if(t.empty()||t.size()!=v.size()) throw std::runtime_error("Invalid result time grid");
    auto i=std::lower_bound(t.begin(),t.end(),x);
    if(i==t.begin()) return v.front();
    if(i==t.end()) return v.back();
    size_t n=size_t(i-t.begin());double dt=t[n]-t[n-1];
    return dt<=0?v[n]:v[n-1]+(v[n]-v[n-1])*(x-t[n-1])/dt;
}
}
void BatteryListener::OnStart(MwAniService *s) {service=s;plugin->registerVariables(s,this);}
void BatteryListener::OnStep(double t,bool) {
    if(!service) return;
    std::vector<double> r;std::vector<int> i,b;
    if(service->GetDataManager()->GetData(this,t,r,i,b)) plugin->append(t,r);
}
BatteryUdpControl::~BatteryUdpControl() {shutdown=true;if(!endpoint.isEmpty()) QFile::remove(endpoint);}
bool BatteryUdpControl::initialize(QString*) {
    session=uuid();token=uuid()+uuid();heartbeat.start();
    QFile model(":/battery/BatterySim.mo");
    if(!model.open(QIODevice::ReadOnly)) return false;
    const auto bytes=model.readAll();modelText=QString::fromUtf8(bytes);modelHash=digest(bytes);
    const auto root=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/udp";
    QDir().mkpath(root);endpoint=root+"/"+QString::number(QCoreApplication::applicationPid())+".json";
    socket=new QUdpSocket(this);
    if(!socket->bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress)) return false;
    connect(socket,&QUdpSocket::readyRead,this,&BatteryUdpControl::receive);
    advertiseTimer.setInterval(2000);
    connect(&advertiseTimer,&QTimer::timeout,this,&BatteryUdpControl::advertise);advertiseTimer.start();
    collectTimer.setInterval(100);connect(&collectTimer,&QTimer::timeout,this,&BatteryUdpControl::collect);
    advertise();return true;
}
void BatteryUdpControl::extensionsInitialized() {
    QTimer::singleShot(0,this,[this]{QString e;bindHost(e);advertise();});
}
MwPlugin::ShutdownFlag BatteryUdpControl::aboutToShutdown() {
    shutdown=true;advertiseTimer.stop();collectTimer.stop();
    if(socket)socket->close();
    if(!endpoint.isEmpty())QFile::remove(endpoint);
    if(listenerAttached && control) static_cast<MwSimControl*>(control.data())->GetAniService()->RemoveListener(&listener);
    return SynchronousShutdown;
}
bool BatteryUdpControl::active() const {
    return phase=="Preparing"||phase=="Translating"||phase=="Starting"||phase=="Running"
        ||phase=="Pausing"||phase=="Paused"||phase=="Resuming"||phase=="Stopping"||phase=="Finalizing";
}
bool BatteryUdpControl::bindHost(QString &error) {
    try {
        auto app=sym<MwApp*(*)()>(L"mw_develop.dll","?Instance@MwApp@@SAPEAV1@XZ")();
        if(!app||!app->GetWorkbench()) {error="Host is starting";return false;}
        core=get<MwWorkbenchCore*>(app->GetWorkbench(),L"mw_develop.dll","?GetCore@MwWorkbench@@QEAAPEAVMwWorkbenchCore@@XZ");
        auto p=core->GetSimInstPanel();if(!p){error="Simulation panel unavailable";return false;}
        auto c=get<MwSimControl*>(p,L"mw_develop.dll","?GetSimControl@MwSimInstPanel@@QEAAPEAVMwSimControl@@XZ");
        if(!c){error="Simulation controller unavailable";return false;}
        if(control && control!=c && active()) {error="Host controller changed";return false;}
        if(control!=c) {
            if(control)disconnect(control,nullptr,this,nullptr);
            control=c;panel=p;manager=app->GetClassManager();
            connect(c,&MwSimControl::SigSimPaused,this,[this]{
                if(!active()||!ownsController())return;phase="Paused";collect();persist();});
            connect(c,&MwSimControl::SigSimResumed,this,[this]{
                if(!active()||!ownsController())return;phase="Running";persist();});
            connect(c,&MwSimControl::SigSimStopped,this,&BatteryUdpControl::simulationStopped);
            connect(p,&MwSimInstPanel::SigTranslateFinished,this,[this](MwSimData *d,bool ok){
                if(phase!="Translating")return;
                if(!ok||!d){fail("TRANSLATE_FAILED","宿主模型翻译失败，请查看 Sysplorer 输出");return;}
                data=d;preparing=false;QTimer::singleShot(0,this,&BatteryUdpControl::startSolver);
            });
        }
        return true;
    } catch(const std::exception&e){error=QString::fromUtf8(e.what());return false;}
}
QString BatteryUdpControl::controllerState() const {
    if(!control)return "Unavailable";
    auto flag=[this](const char *s){return get<bool>(control.data(),L"mw_sim_inst.dll",s);};
    if(flag("?IsSimIdle@MwSimControl@@QEAA_NXZ"))return "Idle";
    if(flag("?IsSimPaused@MwSimControl@@QEAA_NXZ"))return "Paused";
    if(flag("?IsSimPausing@MwSimControl@@QEAA_NXZ"))return "Pausing";
    if(flag("?IsSimStarting@MwSimControl@@QEAA_NXZ"))return "Starting";
    if(flag("?IsSimStopping@MwSimControl@@QEAA_NXZ"))return "Stopping";
    return "Running";
}
bool BatteryUdpControl::ownsController() const {
    return control&&data&&get<MwSimData*>(control.data(),L"mw_sim_inst.dll","?GetSimData@MwSimControl@@QEBAPEAVMwSimData@@XZ")==data.data();
}
void BatteryUdpControl::advertise() {
    if(shutdown)return;
    QString error;const bool bound=bindHost(error);
    writeJson(endpoint,{{"schema",1},{"protocol","battery-udp/1"},{"pid",double(QCoreApplication::applicationPid())},
        {"executable",QCoreApplication::applicationFilePath()},{"port",socket->localPort()},
        {"session",session},{"token",token},{"modelHash",modelHash},{"bound",bound},
        {"updated",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
}
void BatteryUdpControl::log(const QString &event,const QJsonObject &values) {
    if(jobDir.isEmpty())return;
    QJsonObject o=values;o["event"]=event;o["taskId"]=taskId;o["pid"]=double(QCoreApplication::applicationPid());
    o["utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QFile f(jobDir+"/plugin.log");if(f.open(QIODevice::WriteOnly|QIODevice::Append)){f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));f.write("\n");}
}
QJsonObject BatteryUdpControl::snapshot(int cursor) const {
    QJsonArray rows;cursor=qBound(0,cursor,live.size());int end=qMin(cursor+8,live.size());
    for(int n=cursor;n<end;++n){const auto&r=live[n];rows.append(QJsonArray{r.t,r.soc,r.current,r.voltage});}
    return {{"phase",phase},{"taskId",taskId},{"owner",owner},{"params",verifiedParams},
        {"modelHash",modelHash},{"controller",pointer(control)},{"data",pointer(data)},
        {"pid",double(QCoreApplication::applicationPid())},{"duration",duration},
        {"time",live.isEmpty()?0:live.last().t},{"cursor",cursor},{"next",end},{"total",live.size()},{"rows",rows},
        {"code",errorCode},{"message",errorMessage},{"jobDir",jobDir},{"csvHash",csvHash},
        {"nativeResult",nativeResult},{"viewerMessage",viewMessage}};
}
void BatteryUdpControl::beginTask(const QJsonObject &a,const QString &client,const QString &id) {
    require(!active()&&controllerState()=="Idle","HOST_BUSY");
    auto app=MwAppPtr;require(app&&app->GetTaskManager()&&!app->GetTaskManager()->IsRunning(),"HOST_STARTING");
    require(!QUuid(id).isNull(),"Invalid task identifier");
    require(a.value("modelHash").toString()==modelHash,"MODEL_MISMATCH");
    auto v=a["voltage"].toDouble(-1),i=a["current"].toDouble(-1),s=a["soc0"].toDouble(-1);
    auto d=a["duration"].toDouble(-1),sp=a["speed"].toDouble(20);int md=a["mode"].toInt();
    require(finite(v)&&v>=2.5&&v<=3.45&&finite(i)&&i>=0&&i<=5&&finite(s)&&s>=0&&s<=1&&(md==1||md==2),"INVALID_PARAMS");
    require(finite(d)&&d>=.1&&d<=20000&&finite(sp)&&sp>=.5&&sp<=60,"INVALID_DURATION_OR_SPEED");
    taskId=id;owner=client;voltage=v;current=i;mode=md;soc0=s;duration=d;speed=sp;
    errorCode.clear();errorMessage.clear();viewMessage.clear();csvHash.clear();nativeResult.clear();
    live.clear();verifiedParams={};data.clear();stopAt=-1;stopRequested=false;preparing=false;phase="Preparing";
    QString base=qEnvironmentVariable("BATTERY_SIM_WORKSPACE",QDir::tempPath()+"/BatteryConsoleUDP");
    for(QChar c:base)require(c.unicode()<128,"ASCII_WORKSPACE_REQUIRED");
    jobDir=QDir(base).absoluteFilePath(taskId);require(QDir().mkpath(jobDir),"Cannot create task directory");
    taskClock.start();lastClientMs=heartbeat.elapsed();persist();log("accepted",a);
    QTimer::singleShot(0,this,&BatteryUdpControl::prepare);
}
void BatteryUdpControl::prepare() {
    if(phase!="Preparing"||shutdown)return;
    try {
        QString error;require(bindHost(error),qPrintable(error));
        auto app=MwAppPtr;
        if(app->GetTaskManager()->IsRunning()){
            if(taskClock.elapsed()>60000)throw std::runtime_error("Host initialization timeout");
            QTimer::singleShot(300,this,&BatteryUdpControl::prepare);return;
        }
        auto h=manager->GetMoHandler();
        if(!modelLoaded){require(h->NewModel(modelText.toUtf8().toStdString(),MoKey(0))!=0,"Battery model load failed");modelLoaded=true;}
        auto key=h->DirectLookupType("BatterySim.SingleRun");require(key!=0,"Battery model missing");
        const std::pair<const char*,QString> args[]={{"runMode",QString::number(mode)},{"runCurrent",QString::number(current,'g',15)},
            {"runVoltage",QString::number(voltage,'g',15)},{"socStart",QString::number(soc0,'g',15)}};
        for(auto &p:args){
            require(h->SetParamValue(key,MwStrVector{p.first},p.second.toStdString()),"Parameter assignment failed");
            bool ok=false;auto back=QString::fromStdString(h->GetParamValue(key,MwStrVector{p.first})).toDouble(&ok);
            require(ok&&std::abs(back-p.second.toDouble())<1e-8,"Parameter readback mismatch");
        }
        verifiedParams={{"mode",mode},{"voltage",voltage},{"current",current},{"soc0",soc0}};
        auto p=static_cast<MwSimInstPanel*>(panel.data());
        p->SetSolverPath(jobDir+"/solver");p->SetResultPath(jobDir+"/Result.msr");
        phase="Translating";persist();preparing=true;
        if(!p->TranslateModel("BatterySim.SingleRun",false))fail("TRANSLATE_REJECTED","宿主未接受翻译请求");
    } catch(const std::exception&e){fail("PREPARE_FAILED",QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::registerVariables(MwAniService *s,MwAniListener *l) {
    if(!data)return;
    auto d=static_cast<MwSimData*>(data.data());auto info=d->MwSimData::GetVarInfo();
    require(info!=nullptr,"Variable information missing");
    std::vector<unsigned int> reals,empty;
    for(auto name:{"cell.SOC","cell.I_act","cell.V_term"}) reals.push_back(info->VariableReference(name));
    s->SetAnimationRefVars(l,d,reals,empty,empty);
}
void BatteryUdpControl::startSolver() {
    if(phase!="Translating"||!data)return;
    try {
        auto c=static_cast<MwSimControl*>(control.data());auto d=static_cast<MwSimData*>(data.data());
        c->RebindSimData(d);
        auto cfg=get<void*>(d->GetSimInstance(),L"modelica_post_processor.dll","?SimConfig@MwModelSimInstance@@QEBAPEAVMwModelSimConfig@@XZ");
        require(cfg!=nullptr,"Missing simulation config");
        sym<void(*)(void*,double)>(L"modelica_post_processor.dll","?SetStopTime@MwModelSimConfig@@QEAAXN@Z")(cfg,duration);
        sym<void(*)(void*,double)>(L"modelica_post_processor.dll","?SetSlowdownFactor@MwModelSimConfig@@QEAAXN@Z")(cfg,1.0/speed);
        sym<void(*)(void*,double)>(L"modelica_post_processor.dll","?SetCommunicationStepSize@MwModelSimConfig@@QEAAXN@Z")(cfg,1.0);
        sym<void(*)(void*,int)>(L"modelica_post_processor.dll","?SetCommuStepSizeDenominator@MwModelSimConfig@@QEAAXH@Z")(cfg,1);
        d->SetSlowdownFactor(1.0/speed);
        if(!listenerAttached){c->GetAniService()->AddListener(&listener);listenerAttached=true;}
        phase="Starting";c->AnimationStart();
        require(c->StartSimulate(MwSimControl::Sim_RealtimeMode),"Host solver rejected start");
        phase="Running";collectTimer.start();persist();log("started",snapshot());
        QTimer::singleShot(150,this,[this]{if(!active()||!ownsController()||phase!="Running")return;
            auto c=static_cast<MwSimControl*>(control.data());
            log("runtime_speed",{{"speed",speed},{"accepted",c->ResetSimConfig(MwSimControl::SIM_SLOWDOWN_FACTOR,1.0/speed)}});});
    } catch(const std::exception&e){fail("START_FAILED",QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::append(double t,const std::vector<double>&v) {
    if(!active()||v.size()!=3||!finite(t)||t<0||t>duration+.01)return;
    if(stopAt>=0&&t>stopAt+1e-6)return;
    if(!finite(v[0])||!finite(v[1])||!finite(v[2])||v[0]<-1e-5||v[0]>1.00001||std::abs(v[1])>current+1e-4)return;
    if(!live.isEmpty()&&t<=live.last().t+1e-9)return;
    live.append({t,v[0],v[1],v[2]});
}
void BatteryUdpControl::collect() {
    if(!active()||!ownsController())return;
    try {
        auto c=static_cast<MwSimControl*>(control.data());
        if(phase=="Running"||phase=="Pausing"||phase=="Paused"||phase=="Resuming"||phase=="Stopping") c->AnimationStep();
        if(lastClientMs>0&&heartbeat.elapsed()-lastClientMs>15000&&phase=="Running"){
            phase="Pausing";c->PauseSimulate();log("client_timeout_pausing");
        }
    } catch(const std::exception&e){fail("STREAM_FAILED",QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::simulationStopped(int code) {
    if(!active()||!ownsController()||phase=="Finalizing")return;
    if(!stopRequested&&code==-1){stopRequested=true;stopAt=live.isEmpty()?0:live.last().t;}
    phase="Finalizing";collectTimer.stop();persist();
    QTimer::singleShot(300,this,[this,code]{finish(code);});
}
bool BatteryUdpControl::saveResult(const QVector<LiveRow>&rows,bool complete) {
    QSaveFile f(jobDir+"/result.csv");if(!f.open(QIODevice::WriteOnly))return false;
    QByteArray bytes("time,SOC,I_act,V_term\n");
    for(const auto&r:rows)bytes+=QString("%1,%2,%3,%4\n").arg(r.t,0,'g',15).arg(r.soc,0,'g',15)
        .arg(r.current,0,'g',15).arg(r.voltage,0,'g',15).toLatin1();
    if(f.write(bytes)!=bytes.size()||!f.commit())return false;
    csvHash=digest(bytes);
    return writeJson(jobDir+"/result.json",{{"schema",1},{"source","sysplorer-host-udp"},{"taskId",taskId},{"model","BatterySim.SingleRun"},
        {"modelHash",modelHash},{"params",verifiedParams},{"complete",complete},{"duration",duration},{"points",rows.size()},
        {"lastTime",rows.isEmpty()?0:rows.last().t},{"csvHash",csvHash},{"nativeResult",nativeResult}});
}
void BatteryUdpControl::finish(int code) {
    if(shutdown||phase!="Finalizing")return;
    try {
        auto d=static_cast<MwSimData*>(data.data());require(d!=nullptr,"Host result was removed");
        std::vector<double> ts,soc,ti,amps,tv,volts;
        const bool nativeOk=d->MwSimData::GetVarData("cell.SOC",ts,soc)&&d->MwSimData::GetVarData("cell.I_act",ti,amps)
            &&d->MwSimData::GetVarData("cell.V_term",tv,volts)&&!ts.empty()&&ts.size()==soc.size()&&!ti.empty()&&!tv.empty();
        require(nativeOk||(stopRequested&&!live.isEmpty()),"Result variables unavailable");
        if(!nativeOk){ts.clear();soc.clear();}
        QVector<LiveRow> rows;
        for(size_t n=0;n<ts.size();++n){
            if(stopRequested&&ts[n]>stopAt+1e-6)break;
            rows.append({ts[n],soc[n],at(ti,amps,ts[n]),at(tv,volts,ts[n])});
        }
        if(stopRequested){
            for(const auto&r:live)if((rows.isEmpty()||r.t>rows.last().t+1e-9)&&r.t<=stopAt+1e-6)rows.append(r);
        }
        require(!rows.isEmpty(),"No valid result samples");
        double last=-1;
        for(const auto&r:rows){
            require(finite(r.t)&&finite(r.soc)&&finite(r.current)&&finite(r.voltage)&&r.t>=last&&r.soc>=-1e-5&&r.soc<=1.00001
                &&std::abs(r.current)<=current+1e-4,"Result validation failed");
            require(mode==1?r.current>=-1e-4:r.current<=1e-4,"Result current direction mismatch");last=r.t;
        }
        require((stopRequested&&rows.first().t>1e-8)||std::abs(rows.first().soc-soc0)<1e-6,"Result initial SOC mismatch");
        const bool full=!stopRequested&&code==0&&std::abs(rows.last().t-duration)<.02;
        if(!stopRequested&&!full)throw std::runtime_error("Simulation failed or stopped before requested end time");
        nativeResult=static_cast<MwSimInstPanel*>(panel.data())->GetResultPath();
        require(saveResult(rows,full),"Cannot save verified result");
        phase=full?"Completed":"Stopped";persist();showNativePlots();persist();log("finished",snapshot());
    } catch(const std::exception&e){fail("RESULT_FAILED",QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::showNativePlots() {
    try{
        auto p=static_cast<MwSimInstPanel*>(panel.data());auto result=p->GetLastResult();
        require(result!=nullptr,"Native result not available");
        auto plots=core->GetMdiArea()->GetPlotWindowManager();
        for(const QString &name:{QString("cell.SOC"),QString("cell.V_term"),QString("cell.I_act")}){
            auto w=plots->CreatePlotWin();require(w!=nullptr,"Cannot create native result plot");
            require(w->AddCurveToCurrentView(name,result)!=nullptr,"Cannot add native result variable");w->show();w->ReplotAll();
        }
        viewMessage="Sysplorer 已显示本次原生结果";
    }catch(const std::exception&e){viewMessage=QString("计算结果已保留；原生查看：%1").arg(QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::fail(const QString &code,const QString &message){
    collectTimer.stop();phase="Failed";errorCode=code;errorMessage=message;persist();log("failed",{{"code",code},{"message",message}});
}
void BatteryUdpControl::persist() {
    if(jobDir.isEmpty())return;
    auto o=snapshot(live.size());o.remove("rows");writeJson(jobDir+"/status.json",o);
}
QJsonObject BatteryUdpControl::execute(const QJsonObject&r) {
    QString e;require(bindHost(e),qPrintable(e));
    const QString cmd=r["command"].toString(),client=r["client"].toString(),id=r["taskId"].toString();
    require(!client.isEmpty(),"Missing client identifier");
    if(cmd=="hello")return {{"ok",true},{"status",snapshot()},{"modelHash",modelHash},{"ready",!MwAppPtr->GetTaskManager()->IsRunning()},{"controllerState",controllerState()}};
    if(cmd=="begin"){
        if(id==taskId&&client==owner)return {{"ok",true},{"status",snapshot()}};
        beginTask(r["args"].toObject(),client,id);return {{"ok",true},{"status",snapshot()}};
    }
    require(id==taskId&&client==owner,"TASK_OWNERSHIP_MISMATCH");lastClientMs=heartbeat.elapsed();
    if(cmd=="poll")return {{"ok",true},{"status",snapshot(r["args"].toObject()["cursor"].toInt())}};
    require(ownsController(),"HOST_TASK_CHANGED");auto c=static_cast<MwSimControl*>(control.data());
    if(cmd=="pause"){require(phase=="Running","INVALID_STATE");phase="Pausing";require(c->PauseSimulate(),"Pause rejected");}
    else if(cmd=="resume"){require(phase=="Paused","INVALID_STATE");phase="Resuming";require(c->ResumeSimulate(),"Resume rejected");}
    else if(cmd=="stop"){
        require(phase=="Running"||phase=="Paused"||phase=="Pausing"||phase=="Resuming","INVALID_STATE");
        collect();stopRequested=true;
        stopAt=get<double>(c,L"mw_sim_inst.dll","?GetCurSimTime@MwSimControl@@QEAANXZ");
        phase="Stopping";c->StopSimulate(5,false);
    } else if(cmd=="speed"){
        auto v=r["args"].toObject()["speed"].toDouble();require(finite(v)&&v>=.5&&v<=60,"INVALID_SPEED");
        require(active(),"INVALID_STATE");speed=v;static_cast<MwSimData*>(data.data())->SetSlowdownFactor(1.0/v);
        require(c->ResetSimConfig(MwSimControl::SIM_SLOWDOWN_FACTOR,1.0/v),"SPEED_REJECTED");
    } else throw std::runtime_error("UNKNOWN_COMMAND");
    persist();return {{"ok",true},{"status",snapshot()}};
}
void BatteryUdpControl::receive() {
    while(socket->hasPendingDatagrams()){
        auto d=socket->receiveDatagram(8192);if(d.senderAddress()!=QHostAddress::LocalHost||d.data().size()>4096)continue;
        auto r=QJsonDocument::fromJson(d.data()).object();
        if(r["token"].toString()!=token||r["session"].toString()!=session||r["schema"].toInt()!=1)continue;
        const auto id=r["id"].toString();if(id.isEmpty())continue;
        if(replies.contains(id)){
            if(replyRequests[id]==d.data())socket->writeDatagram(replies[id],d.senderAddress(),d.senderPort());continue;
        }
        QJsonObject out;
        try{out=execute(r);}catch(const std::exception&e){out={{"ok",false},{"code",QString::fromUtf8(e.what())},{"message",QString::fromUtf8(e.what())}};}
        out["schema"]=1;out["id"]=id;out["session"]=session;out["taskId"]=r["taskId"];out["command"]=r["command"];
        auto bytes=QJsonDocument(out).toJson(QJsonDocument::Compact);
        if(replies.size()>=512){auto old=replies.firstKey();replies.remove(old);replyRequests.remove(old);}
        replies[id]=bytes;replyRequests[id]=d.data();socket->writeDatagram(bytes,d.senderAddress(),d.senderPort());
        if(r["command"].toString()!="poll"&&r["command"].toString()!="hello")log("request",{{"command",r["command"]},{"response",out}});
    }
}

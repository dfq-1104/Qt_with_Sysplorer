#include "control_plugin.h"
#include "task_status_compat.h"
#include "remote_server.h"
#include <mw_develop/mw_app.h>
#include <mw_develop/mw_workbench.h>
#include <mw_develop/mw_workbench_core.h>
#include <mw_develop/mw_sim_inst_panel.h>
#include <mw_develop/mw_workbench_mdiarea.h>
#include <mw_develop/mw_plot_window_manager.h>
#include <mw_class_manager/mw_class_manager.h>
#include <mw_class_manager/mw_mo_handler.h>
#include <mw_class_manager/mw_core_option.h>
#include <mw_global/mw_task_manager.h>
#include <mw_sim_inst/mw_sim_control.h>
#include <mw_sim_inst/mw_sim_data.h>
#include <mw_sim_inst/mw_ani_service.h>
#include <mw_sim_inst/mw_ani_data_manager.h>
#include <mw_model_var_tree/var_info.h>
#include <mw_model_var_tree/var_tree.h>
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
    // Solver children inherit these paths even when Sysplorer is started by its
    // ordinary desktop icon. Only this host process environment is changed.
    const auto bin=QCoreApplication::applicationDirPath();const auto home=QDir(bin).absoluteFilePath("..");
    const auto solver=QDir(home).absoluteFilePath("Simulator/Bin64");
    const auto gcc=QDir(home).absoluteFilePath("Simulator/mingw/4.9.4/mingw64/bin");
    const auto path=QDir::toNativeSeparators(bin)+";"+QDir::toNativeSeparators(solver)+";"+QDir::toNativeSeparators(gcc)+";"+qEnvironmentVariable("PATH");
    SetEnvironmentVariableW(L"PATH",reinterpret_cast<LPCWSTR>(path.utf16()));
    _wputenv_s(L"PATH",reinterpret_cast<const wchar_t*>(path.utf16()));
    const auto root=qEnvironmentVariable("BATTERY_DISCOVERY_DIR",QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/udp");
    QDir().mkpath(root);endpoint=root+"/"+QString::number(QCoreApplication::applicationPid())+".json";
    socket=new QUdpSocket(this);
    if(!socket->bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress)) return false;
    connect(socket,&QUdpSocket::readyRead,this,&BatteryUdpControl::receive);
    advertiseTimer.setInterval(2000);
    connect(&advertiseTimer,&QTimer::timeout,this,[this]{modelUploads.expire(heartbeat.elapsed());advertise();});advertiseTimer.start();
    collectTimer.setInterval(100);connect(&collectTimer,&QTimer::timeout,this,&BatteryUdpControl::collect);
    remote=new RemoteServer(this);
    connect(remote,&RemoteServer::request,this,&BatteryUdpControl::receiveRemote);
    remote->start(qEnvironmentVariable("BATTERY_REMOTE_CONFIG",QCoreApplication::applicationDirPath()+"/addins/BatteryUdpControl/remote.ini"),session);
    advertise();return true;
}
void BatteryUdpControl::extensionsInitialized() {
    QTimer::singleShot(0,this,[this]{QString e;bindHost(e);advertise();});
}
MwPlugin::ShutdownFlag BatteryUdpControl::aboutToShutdown() {
    shutdown=true;advertiseTimer.stop();collectTimer.stop();
    restoreTaskPaths();
    if(socket)socket->close();
    if(remote)remote->close();
    if(!endpoint.isEmpty())QFile::remove(endpoint);
    if(listenerAttached && control) static_cast<MwSimControl*>(control.data())->GetAniService()->RemoveListener(&listener);
    return SynchronousShutdown;
}
bool BatteryUdpControl::active() const {
    return phase=="Loading"||phase=="Prepared"||phase=="Preparing"||phase=="Translating"||phase=="Starting"||phase=="Running"
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
                if(!active()||!ownsController())return;phase="Paused";collect();
                if(!pendingTune.isEmpty()&&!tuneAwaitingSample)applyTune();persist();});
            connect(c,&MwSimControl::SigSimResumed,this,[this]{
                if(!active()||!ownsController())return;phase="Running";persist();});
            connect(c,&MwSimControl::SigSimStopped,this,&BatteryUdpControl::simulationStopped);
            connect(p,&MwSimInstPanel::SigTranslateFinished,this,[this](MwSimData *d,bool ok){
                if(phase!="Translating")return;
                if(!ok||!d){fail("TRANSLATE_FAILED","宿主模型翻译失败，请查看 Sysplorer 输出");return;}
                data=d;preparing=false;phase="Prepared";persist();
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
bool BatteryUdpControl::hostTaskRunning() const {
    auto app=MwAppPtr;
    require(app&&app->GetTaskManager(),"HOST_TASK_MANAGER_UNAVAILABLE");
    // Prefer the original API; otherwise use the SDK's running-task getter.
    // Unknown hosts remain blocked rather than being assumed idle.
    const auto module=GetModuleHandleW(L"mw_global.dll");
    return batteryHostTaskRunning(app->GetTaskManager(),[module](const char* name){return GetProcAddress(module,name);});
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
    return {{"phase",phase},{"modelLoaded",modelLoaded},{"taskId",taskId},{"owner",owner},{"params",verifiedParams},
        {"runtimeParams",runtimeParams},{"tuneHistory",tuneHistory},{"tunePending",!pendingTune.isEmpty()},
        {"modelHash",modelHash},{"controller",pointer(control)},{"data",pointer(data)},
        {"pid",double(QCoreApplication::applicationPid())},{"duration",duration},
        {"time",live.isEmpty()?0:live.last().t},{"cursor",cursor},{"next",end},{"total",live.size()},{"rows",rows},
        {"stage",stage},{"code",errorCode},{"message",errorMessage},{"jobDir",jobDir},{"csvHash",csvHash},
        {"nativeResult",nativeResult},{"viewerMessage",viewMessage}};
}
void BatteryUdpControl::beginTask(const QJsonObject &a,const QString &client,const QString &id,bool loadOnly) {
    require(!active()&&controllerState()=="Idle","HOST_BUSY");
    auto app=MwAppPtr;require(app&&!hostTaskRunning(),"HOST_STARTING");
    require(!QUuid(id).isNull(),"Invalid task identifier");
    const auto requestedHash=a.value("modelHash").toString();
    const auto uploaded=modelUploads.take(client,requestedHash,heartbeat.elapsed());
    auto v=a["voltage"].toDouble(loadOnly?3.45:-1),i=a["current"].toDouble(loadOnly?2.5:-1),s=a["soc0"].toDouble(loadOnly?.5:-1);
    auto d=a["duration"].toDouble(loadOnly?120:-1),sp=a["speed"].toDouble(20);int md=a["mode"].toInt(loadOnly?1:0);
    require(finite(v)&&v>=2.5&&v<=3.45&&finite(i)&&i>=0&&i<=5&&finite(s)&&s>=0&&s<=1&&(md==1||md==2),"INVALID_PARAMS");
    require(finite(d)&&d>=.1&&d<=20000&&finite(sp)&&sp>=.5&&sp<=60,"INVALID_DURATION_OR_SPEED");
    modelText=QString::fromUtf8(uploaded);modelHash=requestedHash;
    taskId=id;owner=client;voltage=v;current=i;mode=md;soc0=s;duration=d;speed=sp;
    errorCode.clear();errorMessage.clear();viewMessage.clear();csvHash.clear();nativeResult.clear();
    live.clear();verifiedParams={};data.clear();stopAt=-1;stopRequested=false;preparing=false;phase=loadOnly?"Loading":"Preparing";
    pendingTune={};tuneHistory={};resumeAfterTune=false;tuneAwaitingSample=false;tuneBoundary={};
    runtimeParams={{"current",current},{"voltage",voltage},{"mode",mode},
        {"vMin",mode==2?voltage:2.5},{"vMax",mode==1?voltage:3.45},{"iMax",5.0}};
    QString base=qEnvironmentVariable("BATTERY_SIM_WORKSPACE",remote&&!remote->workspace().isEmpty()?remote->workspace():"C:/BatteryConsoleData/jobs");
    for(QChar c:base)require(c.unicode()<128,"ASCII_WORKSPACE_REQUIRED");
    jobDir=QDir(base).absoluteFilePath(taskId);require(QDir().mkpath(jobDir),"Cannot create task directory");
    stage=QString::fromUtf8("加载模型");taskClock.start();lastClientMs=heartbeat.elapsed();persist();log("accepted",a);
    QTimer::singleShot(0,this,&BatteryUdpControl::prepare);
}
QString BatteryUdpControl::currentModelSource() const {
    std::string text;manager->GetMoHandler()->GetFullText(MoKey(ownedModelKey),text);return QString::fromStdString(text);
}
void BatteryUdpControl::restoreTaskPaths(){
    if(!pathsChanged||!manager)return;
    auto options=manager->GetCoreOption();
    options->SetWorkPath(savedWork.toStdString());options->SetCodePath(savedCode.toStdString());options->SetSimResultPath(savedResult.toStdString());
    if(panel){auto p=static_cast<MwSimInstPanel*>(panel.data());p->SetSolverPath(savedSolverPath);p->SetResultPath(savedPanelResult);}
    QDir::setCurrent(savedCurrentDirectory);
    pathsChanged=false;
}
void BatteryUdpControl::prepare() {
    if((phase!="Preparing"&&phase!="Loading")||shutdown)return;
    const bool loadOnly=phase=="Loading";
    try {
        QString error;require(bindHost(error),qPrintable(error));
        auto app=MwAppPtr;
        if(hostTaskRunning()){
            if(taskClock.elapsed()>60000)throw std::runtime_error("Host initialization timeout");
            QTimer::singleShot(300,this,&BatteryUdpControl::prepare);return;
        }
        stage=QString::fromUtf8("加载模型");
        auto h=manager->GetMoHandler();
        const auto existing=h->DirectLookupType("BatterySim");
        if(existing==0){modelLoaded=false;ownedModelKey=0;ownedModelSource.clear();}
        if(existing!=0){
            require(ownedModelKey!=0&&quint64(existing)==ownedModelKey,"MODEL_NAME_CONFLICT: BatterySim is not owned by this plugin");
            require(currentModelSource()==ownedModelSource,"MODEL_EDIT_CONFLICT: BatterySim has user modifications");
            if(ownedModelHash!=modelHash||!modelLoaded){
                require(h->UnloadModel("BatterySim"),"MODEL_UNLOAD_FAILED");modelLoaded=false;ownedModelKey=0;
            }
        }
        if(!modelLoaded){
            const auto loaded=h->NewModel(modelText.toUtf8().toStdString(),MoKey(0));
            ownedModelKey=quint64(h->DirectLookupType("BatterySim"));
            if(ownedModelKey){ownedModelHash=modelHash;ownedModelSource=currentModelSource();}
            require(loaded!=0&&ownedModelKey!=0,"MODEL_PARSE_FAILED");
            modelLoaded=true;log("model_loaded",{{"modelHash",modelHash}});
        }else log("model_reused",{{"modelHash",modelHash}});
        auto key=h->DirectLookupType("BatterySim.SingleRun");require(key!=0,"MODEL_INTERFACE_INVALID: BatterySim.SingleRun missing");
        for(const auto name:{"runMode","runCurrent","runVoltage","socStart","liveCurrent","liveVoltage","liveMode","liveVMin","liveVMax","liveIMax","cell"})
            require(h->LookupComp(key,name)!=0,"MODEL_INTERFACE_INVALID: battery component missing");
        for(const auto name:{"SOC","I_act","V_term"})
            require(h->LookupCompAndTypeEx(key,MwStrVector{"cell",name})!=0,"MODEL_INTERFACE_INVALID: result component missing");
        if(loadOnly){phase="Loaded";persist();log("load_confirmed");return;}
        const std::pair<const char*,QString> args[]={{"runMode",QString::number(mode)},{"runCurrent",QString::number(current,'g',15)},
            {"runVoltage",QString::number(voltage,'g',15)},{"socStart",QString::number(soc0,'g',15)}};
        for(auto &p:args){
            stage=QString::fromUtf8("写入参数");
            if(!h->SetParamValue(key,MwStrVector{p.first},p.second.toStdString())){
                fail("PARAM_WRITE_FAILED",QString::fromUtf8("写参失败：%1，期望 %2").arg(QString::fromLatin1(p.first),p.second));return;
            }
            stage=QString::fromUtf8("回读参数");
            bool ok=false;auto back=QString::fromStdString(h->GetParamValue(key,MwStrVector{p.first})).toDouble(&ok);
            if(!ok||std::abs(back-p.second.toDouble())>=1e-8){
                fail("PARAM_READBACK_FAILED",QString::fromUtf8("参数回读失败：%1，期望 %2，实际 %3")
                    .arg(QString::fromLatin1(p.first),p.second,ok?QString::number(back,'g',15):QString::fromUtf8("非数值")));return;
            }
            log("parameter_verified",{{"field",p.first},{"expected",p.second},{"actual",back}});
            ownedModelSource=currentModelSource();
        }
        verifiedParams={{"mode",mode},{"voltage",voltage},{"current",current},{"soc0",soc0}};
        ownedModelSource=currentModelSource();
        auto p=static_cast<MwSimInstPanel*>(panel.data());
        stage=QString::fromUtf8("检查输出目录");require(QDir().mkpath(jobDir+"/solver"),"OUTPUT_DIRECTORY_FAILED");
        restoreTaskPaths();auto options=manager->GetCoreOption();
        savedWork=QString::fromStdString(options->WorkPath());savedCode=QString::fromStdString(options->CodePath());savedResult=QString::fromStdString(options->SimResultPath());
        savedSolverPath=p->GetSolverPath();savedPanelResult=p->GetResultPath();pathsChanged=true;
        savedCurrentDirectory=QDir::currentPath();require(QDir::setCurrent(jobDir),"OUTPUT_DIRECTORY_FAILED");
        options->SetWorkPath(jobDir.toStdString());options->SetCodePath((jobDir+"/solver").toStdString());options->SetSimResultPath(jobDir.toStdString());
        p->SetSolverPath(jobDir+"/solver");p->SetResultPath(jobDir+"/Result.msr");
        stage=QString::fromUtf8("翻译模型");phase="Translating";persist();preparing=true;
        if(!p->TranslateModel("BatterySim.SingleRun",false))fail("TRANSLATE_REJECTED","宿主未接受翻译请求");
    } catch(const std::exception&e){
        const auto reason=QString::fromUtf8(e.what());
        const auto code=reason.startsWith("MODEL_")?reason.section(':',0,0):(stage==QString::fromUtf8("加载模型")?QString("LOAD_FAILED"):QString("PREPARE_FAILED"));
        fail(code,reason);
    }
}
void BatteryUdpControl::applyTune() {
    // Never touch initial values or restart/translate the model here.
    try {
        require(phase=="Paused"&&ownsController(),"TUNE_REQUIRES_PAUSE");
        auto c=static_cast<MwSimControl*>(control.data());
        auto d=static_cast<MwSimData*>(data.data());
        auto info=d->MwSimData::GetVarInfo();require(info!=nullptr,"Missing variable information");
        auto index=[info](const char *name){
            auto ref=info->VariableReference(name);
            std::map<MWK::MwValueReference,MWK::MwVarTree> vars;
            info->GetVariables(std::set<MWK::MwValueReference>{ref},&vars);
            require(vars.count(ref)!=0,"Runtime input missing");return vars.at(ref).Index();
        };
        MWK::MwValueIndex ids[]={index("liveCurrent"),index("liveVoltage"),index("liveMode"),
            index("liveVMin"),index("liveVMax"),index("liveIMax")};
        const char *keys[]={"current","voltage","mode","vMin","vMax","iMax"};
        double values[6],back[6];for(int n=0;n<6;++n)values[n]=pendingTune[keys[n]].toDouble();
        MWK::MwValueIndex stateId[]={index("cell.SOC")};double before[1],after[1];
        require(d->GetReals(stateId,before,1),"Cannot read SOC before tuning");
        double t=get<double>(c,L"mw_sim_inst.dll","?GetCurSimTime@MwSimControl@@QEAANXZ");
        require(d->SetInputReals(ids,values,6),"Runtime input write rejected");
        require(d->GetReals(ids,back,6),"Runtime input readback failed");
        QJsonArray diagnostics;
        for(int n=0;n<6;++n)diagnostics.append(QJsonObject{{"name",keys[n]},{"index",int(ids[n])},{"expected",values[n]},{"actual",back[n]}});
        log("tune_readback",{{"values",diagnostics}});
        require(d->GetReals(stateId,after,1)&&std::abs(before[0]-after[0])<1e-12,"SOC changed during tuning");
        require(std::abs(t-get<double>(c,L"mw_sim_inst.dll","?GetCurSimTime@MwSimControl@@QEAANXZ"))<1e-9,"Time changed during tuning");
        // This SDK queues input writes for the next communication step. An
        // immediate GetReals still returns the old step. Confirm through actual
        // model input samples after the boundary before acknowledging success.
        tuneAwaitingSample=true;
        tuneBoundary={{"sequence",tuneHistory.size()+1},{"time",t},{"socBefore",before[0]},
            {"socAfter",after[0]}};
        log("tune_staged",tuneBoundary);persist();
        if(resumeAfterTune){phase="Resuming";require(c->ResumeSimulate(),"Tune resume rejected");}
    }catch(const std::exception &e){
        pendingTune={};resumeAfterTune=false;tuneAwaitingSample=false;
        // A failed write is never silently resumed with unknown inputs.
        fail("TUNE_FAILED",QString::fromUtf8(e.what()));
        if(ownsController())static_cast<MwSimControl*>(control.data())->StopSimulate(5,false);
    }
}
void BatteryUdpControl::registerVariables(MwAniService *s,MwAniListener *l) {
    if(!data)return;
    auto d=static_cast<MwSimData*>(data.data());auto info=d->MwSimData::GetVarInfo();
    require(info!=nullptr,"Variable information missing");
    std::vector<unsigned int> reals,empty;
    for(auto name:{"cell.SOC","cell.I_act","cell.V_term","liveCurrent","liveVoltage","liveMode","liveVMin","liveVMax","liveIMax"}) reals.push_back(info->VariableReference(name));
    s->SetAnimationRefVars(l,d,reals,empty,empty);
}
void BatteryUdpControl::startSolver() {
    stage=QString::fromUtf8("启动仿真");
    if(phase!="Prepared"||!data)return;
    try {
        auto c=static_cast<MwSimControl*>(control.data());auto d=static_cast<MwSimData*>(data.data());
        c->RebindSimData(d);
        c->SetOnlineParamToCurveRes(true);
        auto cfg=get<void*>(d->GetSimInstance(),L"modelica_post_processor.dll","?SimConfig@MwModelSimInstance@@QEBAPEAVMwModelSimConfig@@XZ");
        require(cfg!=nullptr,"Missing simulation config");
        sym<void(*)(void*,double)>(L"modelica_post_processor.dll","?SetStopTime@MwModelSimConfig@@QEAAXN@Z")(cfg,duration);
        sym<void(*)(void*,double)>(L"modelica_post_processor.dll","?SetSlowdownFactor@MwModelSimConfig@@QEAAXN@Z")(cfg,1.0/speed);
        sym<void(*)(void*,double)>(L"modelica_post_processor.dll","?SetCommunicationStepSize@MwModelSimConfig@@QEAAXN@Z")(cfg,1.0);
        sym<void(*)(void*,int)>(L"modelica_post_processor.dll","?SetCommuStepSizeDenominator@MwModelSimConfig@@QEAAXH@Z")(cfg,1);
        d->SetSlowdownFactor(1.0/speed);
        if(!listenerAttached){c->GetAniService()->AddListener(&listener);listenerAttached=true;}
        phase="Starting";startClock.start();c->AnimationStart();
        require(c->StartSimulate(MwSimControl::Sim_RealtimeMode),"Host solver rejected start");
        collectTimer.start();persist();log("start_accepted",snapshot());
        QTimer::singleShot(150,this,[this]{if(!active()||!ownsController()||phase!="Running")return;
            auto c=static_cast<MwSimControl*>(control.data());
            log("runtime_speed",{{"speed",speed},{"accepted",c->ResetSimConfig(MwSimControl::SIM_SLOWDOWN_FACTOR,1.0/speed)}});});
    } catch(const std::exception&e){fail("START_FAILED",QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::append(double t,const std::vector<double>&v) {
    if(!active()||v.size()!=9||!finite(t)||t<0||t>duration+.01)return;
    if(stopAt>=0&&t>stopAt+1e-6)return;
    if(!finite(v[0])||!finite(v[1])||!finite(v[2])||v[0]<-1e-5||v[0]>1.00001||std::abs(v[1])>5.0001)return;
    if(!live.isEmpty()&&t<=live.last().t+1e-9)return;
    live.append({t,v[0],v[1],v[2]});
    if(tuneAwaitingSample&&t>tuneBoundary["time"].toDouble()+1e-8){
        const char *keys[]={"current","voltage","mode","vMin","vMax","iMax"};QJsonObject actual;
        bool matched=true;
        for(int n=0;n<6;++n){actual[keys[n]]=v[n+3];matched=matched&&finite(v[n+3])&&std::abs(v[n+3]-pendingTune[keys[n]].toDouble())<1e-8;}
        if(!matched){
            log("tune_model_mismatch",{{"time",t},{"actual",actual},{"expected",pendingTune}});
            tuneAwaitingSample=false;pendingTune={};fail("TUNE_FAILED","Model inputs did not update at the next step");
            if(ownsController())static_cast<MwSimControl*>(control.data())->StopSimulate(5,false);return;
        }
        runtimeParams=actual;pendingTune={};tuneAwaitingSample=false;
        auto event=tuneBoundary;event["confirmedAt"]=t;event["params"]=actual;
        tuneHistory.append(event);persist();log("tune_applied",event);
    }
}
void BatteryUdpControl::collect() {
    if(!active()||!ownsController())return;
    try {
        auto c=static_cast<MwSimControl*>(control.data());
        if(phase=="Starting"){
            const auto actual=controllerState();
            if(actual=="Running"){stage=QString::fromUtf8("运行仿真");phase="Running";persist();log("started",snapshot());}
            else if(startClock.elapsed()>15000&&actual=="Idle"){fail("START_UNCONFIRMED",QString::fromUtf8("启动请求已接受，但宿主未确认运行；未重复启动。"));return;}
        }
        if(phase=="Starting"||phase=="Running"||phase=="Pausing"||phase=="Paused"||phase=="Resuming"||phase=="Stopping") c->AnimationStep();
        if(lastClientMs>0&&heartbeat.elapsed()-lastClientMs>15000&&phase=="Running"){
            phase="Pausing";c->PauseSimulate();log("client_timeout_pausing");
        }
    } catch(const std::exception&e){fail("STREAM_FAILED",QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::simulationStopped(int code) {
    if(!active()||!ownsController()||phase=="Finalizing")return;
    if(!stopRequested&&code==-1){stopRequested=true;stopAt=live.isEmpty()?0:live.last().t;}
    stage=QString::fromUtf8("结果处理");phase="Finalizing";collectTimer.stop();persist();
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
        {"modelHash",modelHash},{"params",verifiedParams},{"runtimeParams",runtimeParams},{"tuneHistory",tuneHistory},
        {"complete",complete},{"duration",duration},{"points",rows.size()},
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
                &&std::abs(r.current)<=5.0001,"Result validation failed");
            if(tuneHistory.isEmpty())require(mode==1?r.current>=-1e-4:r.current<=1e-4,"Result current direction mismatch");last=r.t;
        }
        require((stopRequested&&rows.first().t>1e-8)||std::abs(rows.first().soc-soc0)<1e-6,"Result initial SOC mismatch");
        const bool full=!stopRequested&&code==0&&std::abs(rows.last().t-duration)<.02;
        if(!stopRequested&&!full)throw std::runtime_error("Simulation failed or stopped before requested end time");
        nativeResult=static_cast<MwSimInstPanel*>(panel.data())->GetResultPath();
        require(saveResult(rows,full),"Cannot save verified result");
        phase=full?"Completed":"Stopped";persist();showNativePlots();persist();log("finished",snapshot());restoreTaskPaths();
    } catch(const std::exception&e){fail("RESULT_FAILED",QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::showNativePlots() {
    try{
        // Native plotting is optional. An unavailable plotting ABI must not
        // prevent plugin loading, command handling or verified CSV export.
        auto entry=GetProcAddress(GetModuleHandleW(L"mw_develop.dll"),"?CreatePlotWin@MwPlotWindowManager@@QEAAPEAVMwSimPlotWindow@@_NH0@Z");
        if(!entry){viewMessage=QString::fromUtf8("当前宿主未提供匹配的原生绘图接口；结果已保留，可在 Qt 查看和回放。");return;}
        using CreatePlot= MwSimPlotWindow*(*)(MwPlotWindowManager*,bool,int,bool);
        const auto createPlot=reinterpret_cast<CreatePlot>(entry);
        auto p=static_cast<MwSimInstPanel*>(panel.data());auto result=p->GetLastResult();
        require(result!=nullptr,"Native result not available");
        auto plots=core->GetMdiArea()->GetPlotWindowManager();
        for(const QString &name:{QString("cell.SOC"),QString("cell.V_term"),QString("cell.I_act")}){
            auto w=createPlot(plots,true,-1,false);require(w!=nullptr,"Cannot create native result plot");
            require(w->AddCurveToCurrentView(name,result)!=nullptr,"Cannot add native result variable");w->show();w->ReplotAll();
        }
        viewMessage="Sysplorer 已显示本次原生结果";
    }catch(const std::exception&e){viewMessage=QString("计算结果已保留；原生查看：%1").arg(QString::fromUtf8(e.what()));}
}
void BatteryUdpControl::fail(const QString &code,const QString &message){
    restoreTaskPaths();
    collectTimer.stop();phase="Failed";errorCode=code;errorMessage=message;persist();log("failed",{{"stage",stage},{"code",code},{"message",message}});
}
void BatteryUdpControl::persist() {
    if(jobDir.isEmpty())return;
    auto o=snapshot(live.size());o.remove("rows");writeJson(jobDir+"/status.json",o);
}
QJsonObject BatteryUdpControl::execute(const QJsonObject&r) {
    QString e;require(bindHost(e),qPrintable(e));
    const QString cmd=r["command"].toString(),client=r["client"].toString(),id=r["taskId"].toString();
    require(!client.isEmpty(),"Missing client identifier");
    if(cmd=="hello")return {{"modelLifecycle",true},{"modelLoaded",modelLoaded},{"splitLifecycle",true},{"ok",true},{"status",snapshot()},{"modelHash",modelHash},{"ready",!hostTaskRunning()},{"controllerState",controllerState()}};
    if(cmd.startsWith("model.")){
        require(!active()&&controllerState()=="Idle"&&!hostTaskRunning(),"HOST_BUSY");
        return modelUploads.execute(client,cmd,r["args"].toObject(),r["_tcp"].toBool()?ModelTransfer::TcpChunk:ModelTransfer::UdpChunk,heartbeat.elapsed());
    }
    if(cmd=="begin"||cmd=="translate"||cmd=="load"){
        if(id==taskId&&client==owner)return {{"ok",true},{"status",snapshot()}};
        beginTask(r["args"].toObject(),client,id,cmd=="load");return {{"ok",true},{"status",snapshot()}};
    }
    require(id==taskId&&client==owner,"TASK_OWNERSHIP_MISMATCH");lastClientMs=heartbeat.elapsed();
    if(cmd=="poll")return {{"ok",true},{"status",snapshot(r["args"].toObject()["cursor"].toInt())}};
    if(cmd=="download"){
        require(phase=="Completed"||phase=="Stopped","RESULT_NOT_READY");
        const auto a=r["args"].toObject();const auto name=a["file"].toString();
        require(name=="result.csv"||name=="result.json","INVALID_RESULT_FILE");
        const double offset=a["offset"].toDouble(-1);require(finite(offset)&&offset>=0&&offset==std::floor(offset)&&offset<=128*1024*1024,"INVALID_OFFSET");
        QFile f(jobDir+"/"+name);require(f.open(QIODevice::ReadOnly),"RESULT_FILE_UNAVAILABLE");
        require(f.size()>0&&f.size()<=128*1024*1024&&offset<=f.size()&&f.seek(qint64(offset)),"RESULT_FILE_LIMIT");
        const auto bytes=f.read(48*1024);require(!bytes.isEmpty()||offset==f.size(),"RESULT_READ_FAILED");
        return {{"ok",true},{"file",name},{"offset",offset},{"size",double(f.size())},{"data",QString::fromLatin1(bytes.toBase64())},
            {"eof",f.pos()==f.size()},{"chunkHash",QString::fromLatin1(digest(bytes))}};
    }
    if(cmd=="start"){
        require(phase=="Prepared"&&controllerState()=="Idle","INVALID_STATE");
        const auto currentKey=manager->GetMoHandler()->DirectLookupType("BatterySim");
        if(ownedModelKey==0||quint64(currentKey)!=ownedModelKey||currentModelSource()!=ownedModelSource){
            modelLoaded=false;fail("MODEL_EDIT_CONFLICT","Prepared model was edited; save and resolve the host model before preparing again");
            return {{"ok",false},{"code",errorCode},{"message",errorMessage},{"status",snapshot()}};
        }
        require(data&&panel&&control,"PREPARED_MODEL_REMOVED");
        const auto requestedSpeed=r["args"].toObject()["speed"].toDouble(speed);
        require(finite(requestedSpeed)&&requestedSpeed>=.5&&requestedSpeed<=60,"INVALID_SPEED");speed=requestedSpeed;
        startSolver();return {{"ok",true},{"status",snapshot()}};
    }
    if(cmd=="discard"){
        require(phase=="Prepared","INVALID_STATE");phase="Ready";restoreTaskPaths();data.clear();persist();
        return {{"ok",true},{"status",snapshot()}};
    }
    require(ownsController(),"HOST_TASK_CHANGED");auto c=static_cast<MwSimControl*>(control.data());
    if(cmd=="tune"){
        require((phase=="Running"||phase=="Paused")&&pendingTune.isEmpty(),"INVALID_STATE");
        const auto a=r["args"].toObject();
        const QStringList keys={"current","voltage","mode","vMin","vMax","iMax"};
        require(a.size()==keys.size(),"INVALID_TUNE_FIELDS");
        for(const auto &key:keys)require(a[key].isDouble()&&finite(a[key].toDouble()),"INVALID_TUNE_FIELDS");
        double md=a["mode"].toDouble(),lo=a["vMin"].toDouble(),hi=a["vMax"].toDouble();
        require(md==std::floor(md)&&md>=0&&md<=3&&a["current"].toDouble()>=0&&a["current"].toDouble()<=5
            &&lo>=2.5&&hi<=3.45&&lo<hi&&a["voltage"].toDouble()>=2.5&&a["voltage"].toDouble()<=hi
            &&a["iMax"].toDouble()>=0&&a["iMax"].toDouble()<=5,"INVALID_TUNE_PARAMS");
        pendingTune=a;resumeAfterTune=phase=="Running";
        if(resumeAfterTune){phase="Pausing";if(!c->PauseSimulate()){pendingTune={};phase="Running";throw std::runtime_error("Pause rejected");}}
        else applyTune();
    }
    else if(cmd=="pause"){require(phase=="Running","INVALID_STATE");phase="Pausing";require(c->PauseSimulate(),"Pause rejected");}
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
        if(r["id"].toString().isEmpty()||r["command"].toString()=="download")continue;
        r["_tcp"]=false;
        socket->writeDatagram(QJsonDocument(dispatch(r)).toJson(QJsonDocument::Compact),d.senderAddress(),d.senderPort());
    }
}
QJsonObject BatteryUdpControl::dispatch(const QJsonObject& r){
    const auto id=r["id"].toString();const auto request=QJsonDocument(r).toJson(QJsonDocument::Compact);
    if(replies.contains(id)){
        if(replyRequests[id]==request)return QJsonDocument::fromJson(replies[id]).object();
        return {{"schema",1},{"id",id},{"session",session},{"taskId",r["taskId"]},{"command",r["command"]},{"ok",false},{"code","REQUEST_ID_REUSED"}};
    }
    QJsonObject out;
    try{out=execute(r);}catch(const std::exception&e){out={{"ok",false},{"code",QString::fromUtf8(e.what())},{"message",QString::fromUtf8(e.what())}};}
    out["schema"]=1;out["id"]=id;out["session"]=session;out["taskId"]=r["taskId"];out["command"]=r["command"];
    // Fingerprinting must remain readable even when a host ABI/readiness check
    // rejects hello. Keep ok=false and ready=false so no task can proceed.
    if(r["command"].toString()=="hello"){
        out["modelHash"]=modelHash;out["modelLifecycle"]=true;out["splitLifecycle"]=true;out["modelLoaded"]=modelLoaded;
        out["modelTransferVersion"]=1;out["modelInterface"]=ModelTransfer::interfaceName();
        if(!out["ok"].toBool()){out["ready"]=false;out["status"]=snapshot();}
    }
    if(replies.size()>=512){auto old=replies.firstKey();replies.remove(old);replyRequests.remove(old);}
    replies[id]=QJsonDocument(out).toJson(QJsonDocument::Compact);replyRequests[id]=request;
    const auto cmd=r["command"].toString();
    if(cmd!="poll"&&cmd!="hello"&&cmd!="download")log("request",{{"command",cmd},{"response",out}});
    return out;
}
void BatteryUdpControl::receiveRemote(RemoteLink* link,const QJsonObject& value){
    auto r=value;
    if(r["schema"].toInt()!=1||r["client"].toString()!=link->clientId()||r["session"].toString()!=session||QUuid(r["id"].toString()).isNull()){
        link->socket()->abort();return;
    }
    // Authentication belongs to the TCP channel. Never publish the local UDP token.
    r["token"]=token;r["_tcp"]=true;auto response=dispatch(r);
    if(response.contains("status")){
        auto status=response["status"].toObject();status.remove("controller");status.remove("data");response["status"]=status;
    }
    link->send(response);
}

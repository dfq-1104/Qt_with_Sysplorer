#include "probe.h"
#include <mw_develop/mw_app.h>
#include <mw_develop/mw_workbench.h>
#include <mw_develop/mw_workbench_core.h>
#include <mw_develop/mw_sim_inst_panel.h>
#include <mw_class_manager/mw_class_manager.h>
#include <mw_class_manager/mw_mo_handler.h>
#include <mw_sim_inst/mw_sim_control.h>
#include <mw_sim_inst/mw_sim_data.h>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>
#include <windows.h>
#include <stdexcept>

// Call the target DLL's exported getters, including getters that were inline in
// the SDK headers. Never read private object offsets from an older SDK layout.
template<typename T> T symbol(const wchar_t *module,const char *name) {
    auto p=GetProcAddress(GetModuleHandleW(module),name);
    if(!p) throw std::runtime_error(std::string("Missing host export: ")+name);
    return reinterpret_cast<T>(p);
}
template<typename R> R getter(void *self,const wchar_t *dll,const char *name) {
    return symbol<R(*)(void*)>(dll,name)(self);
}
static QString ptr(void *p) { return QString("0x%1").arg(quintptr(p),0,16); }
void UdpHostProbe::log(QJsonObject v) {
    v["utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    v["pid"]=double(QCoreApplication::applicationPid());
    QFile f(logPath); if(f.open(QIODevice::WriteOnly|QIODevice::Append)) {
        f.write(QJsonDocument(v).toJson(QJsonDocument::Compact)); f.write("\n");
    }
}
bool UdpHostProbe::initialize(QString *) {
    token=qEnvironmentVariable("BATTERY_UDP_PROBE_TOKEN");
    logPath=qEnvironmentVariable("BATTERY_UDP_PROBE_LOG");
    // Installed probe remains inert in normal Sysplorer launches.
    if(token.size()<24 || logPath.isEmpty()) return true;
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    socket=new QUdpSocket(this);
    if(!socket->bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress)) {
        log({{"event","bind_failed"},{"error",socket->errorString()}}); return true;
    }
    connect(socket,&QUdpSocket::readyRead,this,&UdpHostProbe::receive);
    log({{"event","plugin_initialized"},{"port",socket->localPort()},
         {"executable",QCoreApplication::applicationFilePath()},
         {"qt",qVersion()},{"plugin",QStringLiteral("battery_udp_host_probe")}});
    return true;
}
void UdpHostProbe::extensionsInitialized() {
    if(!socket) return;
    QTimer::singleShot(1500,this,[this]{QString e;const bool ok=bindHost(e);
        log({{"event","host_binding"},{"ok",ok},{"error",e},{"status",ok?status():QJsonObject{}}});});
}
MwPlugin::ShutdownFlag UdpHostProbe::aboutToShutdown() {
    if(socket) socket->close();
    return SynchronousShutdown;
}
bool UdpHostProbe::bindHost(QString &error) {
    try {
        auto app=symbol<MwApp*(*)()>(L"mw_develop.dll","?Instance@MwApp@@SAPEAV1@XZ")();
        if(!app) {error="No host MwApp";return false;}
        auto work=app->GetWorkbench();
        if(!work) {error="Host workbench not ready";return false;}
        auto core=getter<MwWorkbenchCore*>(work,L"mw_develop.dll","?GetCore@MwWorkbench@@QEAAPEAVMwWorkbenchCore@@XZ");
        if(!core) {error="No host core";return false;}
        auto hostPanel=core->GetSimInstPanel();
        if(!hostPanel) {error="No host simulation panel";return false;}
        auto hostControl=getter<MwSimControl*>(hostPanel,L"mw_develop.dll","?GetSimControl@MwSimInstPanel@@QEAAPEAVMwSimControl@@XZ");
        if(!hostControl) {error="No host simulation controller";return false;}
        if(control!=hostControl) {
            if(control) disconnect(control,nullptr,this,nullptr);
            control=hostControl; panel=hostPanel; manager=app->GetClassManager();
            connect(control,SIGNAL(SigSimPaused()),this,SLOT(paused()));
            connect(control,SIGNAL(SigSimResumed()),this,SLOT(resumed()));
            connect(control,SIGNAL(SigSimStopped(int)),this,SLOT(stopped(int)));
            connect(hostPanel,&MwSimInstPanel::SigTranslateFinished,this,[this](MwSimData *d,bool ok){
                if(ok) translatedData=d; log({{"event","translated"},{"ok",ok},{"data",ptr(d)}});
            });
        }
        return true;
    } catch(const std::exception &e) {error=QString::fromUtf8(e.what());return false;}
}
QJsonObject UdpHostProbe::status() {
    auto c=control.data(); QJsonObject s;
    s["pid"]=double(QCoreApplication::applicationPid()); s["controller"]=ptr(c);
    s["panel"]=ptr(panel); s["model"]=selectedModel;
    if(!c) return s;
    const auto flag=[c](const char *n){return getter<bool>(c,L"mw_sim_inst.dll",n);};
    QString state="Unknown";
    if(flag("?IsSimIdle@MwSimControl@@QEAA_NXZ")) state="Idle";
    else if(flag("?IsSimPaused@MwSimControl@@QEAA_NXZ")) state="Paused";
    else if(flag("?IsSimPausing@MwSimControl@@QEAA_NXZ")) state="Pausing";
    else if(flag("?IsSimStarting@MwSimControl@@QEAA_NXZ")) state="Starting";
    else if(flag("?IsSimStopping@MwSimControl@@QEAA_NXZ")) state="Stopping";
    else if(flag("?IsSimRunning@MwSimControl@@QEAA_NXZ")) state="Running";
    s["state"]=state;
    s["time"]=getter<double>(c,L"mw_sim_inst.dll","?GetCurSimTime@MwSimControl@@QEAANXZ");
    auto data=getter<MwSimData*>(c,L"mw_sim_inst.dll","?GetSimData@MwSimControl@@QEBAPEAVMwSimData@@XZ");
    s["data"]=ptr(data);
    if(data) s["solverPath"]=QString::fromStdWString(data->GetSolverInstPath());
    // Re-acquire the controller through the host panel for an identity check.
    auto again=getter<MwSimControl*>(panel.data(),L"mw_develop.dll","?GetSimControl@MwSimInstPanel@@QEAAPEAVMwSimControl@@XZ");
    s["sameAsHostPanel"]=again==c;
    return s;
}
QJsonObject UdpHostProbe::execute(const QJsonObject &r) {
    QString error; if(!bindHost(error)) return {{"ok",false},{"error",error}};
    const QString command=r["command"].toString();
    auto c=static_cast<MwSimControl*>(control.data());
    const auto before=status(); const auto state=before["state"].toString();
    QJsonObject out{{"ok",false},{"before",before}};
    if(command=="status") out["ok"]=true;
    else if(command=="load") {
        if(state!="Idle") throw std::runtime_error("load requires Idle");
        const QString model=r["model"].toString("UdpProbeModel");
        if(model!="UdpProbeModel" && model!="BatterySim.SingleRun") throw std::runtime_error("Probe allows only its two test models");
        if(!loadedModels.contains(model)) {
            QByteArray text;
            if(model=="UdpProbeModel") text="model UdpProbeModel Real x(start=0,fixed=true); equation der(x)=1; annotation(experiment(StartTime=0,StopTime=120,Interval=0.1)); end UdpProbeModel;";
            else {QFile f(":/probe/BatterySim.mo");if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Model resource missing");text=f.readAll();}
            const auto key=manager->GetMoHandler()->NewModel(text.toStdString(),MoKey(0));
            if(!key) throw std::runtime_error("Host NewModel failed");
            loadedModels.insert(model);
        }
        selectedModel=model; out["ok"]=true;
    } else if(command=="translate") {
        if(state!="Idle" || selectedModel.isEmpty()) throw std::runtime_error("translate requires Idle and a loaded test model");
        out["ok"]=static_cast<MwSimInstPanel*>(panel.data())->TranslateModel(selectedModel.toStdString(),false);
    } else if(command=="start") {
        if(state!="Idle") throw std::runtime_error("start requires Idle");
        if(!translatedData) throw std::runtime_error("No translated host simulation data"); c->RebindSimData(static_cast<MwSimData*>(translatedData.data()));
                auto inst=static_cast<MwSimData*>(translatedData.data())->GetSimInstance();
        auto config=getter<void*>(inst,L"modelica_post_processor.dll","?SimConfig@MwModelSimInstance@@QEBAPEAVMwModelSimConfig@@XZ");
        if(!config) throw std::runtime_error("Host simulation config missing");
        symbol<void(*)(void*,double)>(L"modelica_post_processor.dll","?SetStopTime@MwModelSimConfig@@QEAAXN@Z")(config,120.0);
        out["stopTime"]=getter<double>(config,L"modelica_post_processor.dll","?StopTime@MwModelSimConfig@@QEBANXZ");
        out["ok"]=c->StartSimulate(MwSimControl::Sim_RealtimeMode);
    } else if(command=="pause") {
        if(state!="Running") throw std::runtime_error("pause requires Running");
        out["ok"]=c->PauseSimulate();
    } else if(command=="resume") {
        if(state!="Paused") throw std::runtime_error("resume requires Paused");
        out["ok"]=c->ResumeSimulate();
    } else if(command=="stop") {
        if(state=="Idle") out["ok"]=true;
        else {c->StopSimulate(5.0,false);out["ok"]=true;}
    } else throw std::runtime_error("Unknown command");
    out["after"]=status();return out;
}
void UdpHostProbe::receive() {
    while(socket && socket->hasPendingDatagrams()) {
        const auto d=socket->receiveDatagram(8192);
        if(d.senderAddress()!=QHostAddress::LocalHost || d.data().size()>4096) continue;
        const auto r=QJsonDocument::fromJson(d.data()).object();
        if(r["token"].toString()!=token) continue;
        const QString id=r["id"].toString(); if(id.isEmpty()) continue;
        if(responses.contains(id)) {socket->writeDatagram(responses[id],d.senderAddress(),d.senderPort());continue;}
        QJsonObject out; try {out=execute(r);} catch(const std::exception &e){out={{"ok",false},{"error",QString::fromUtf8(e.what())}};}
        out["id"]=id;out["command"]=r["command"];out["pid"]=double(QCoreApplication::applicationPid());
        log({{"event","command"},{"response",out}});
        const auto bytes=QJsonDocument(out).toJson(QJsonDocument::Compact);
        if(responses.size()>=256) responses.erase(responses.begin());
        responses[id]=bytes;socket->writeDatagram(bytes,d.senderAddress(),d.senderPort());
    }
}
void UdpHostProbe::stopped(int code){log({{"event","stopped"},{"exitCode",code},{"status",status()}});}
void UdpHostProbe::paused(){log({{"event","paused"},{"status",status()}});}
void UdpHostProbe::resumed(){log({{"event","resumed"},{"status",status()}});}
void UdpHostProbe::translated(QObject*,bool){}



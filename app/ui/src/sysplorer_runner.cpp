#include "sysplorer_runner.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif
namespace {
QString uid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QString hash(const QByteArray&b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
bool samePath(const QString&a,const QString&b){return QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(QDir::cleanPath(QDir::fromNativeSeparators(b)),Qt::CaseInsensitive)==0;}
bool processMatches(qint64 pid,const QString&exe){
#ifdef Q_OS_WIN
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));if(!h)return false;
    wchar_t buf[32768];DWORD len=32768;bool ok=QueryFullProcessImageNameW(h,0,buf,&len);
    DWORD exitCode=0;ok=ok&&GetExitCodeProcess(h,&exitCode)&&exitCode==STILL_ACTIVE;
    CloseHandle(h);return ok&&samePath(QString::fromWCharArray(buf,int(len)),exe);
#else
    Q_UNUSED(pid);Q_UNUSED(exe);return false;
#endif
}
}
SysplorerRunner::SysplorerRunner(QObject*p):QObject(p),m_clientId(uid()){
    QSettings config(QCoreApplication::applicationDirPath()+"/sysplorer.ini",QSettings::IniFormat);
    bool timeoutOk=false;const int timeout=qEnvironmentVariable("BATTERY_HOST_TIMEOUT_MS",config.value("Host/TimeoutMs",90000).toString()).toInt(&timeoutOk);
    if(timeoutOk)m_hostTimeoutMs=qBound(1000,timeout,600000);
    qRegisterMetaType<QVector<ReplaySample>>("QVector<ReplaySample>");
    m_socket.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress);
    connect(&m_socket,&QUdpSocket::readyRead,this,&SysplorerRunner::receive);
    m_timer.setInterval(50);connect(&m_timer,&QTimer::timeout,this,&SysplorerRunner::pump);
}
SysplorerRunner::~SysplorerRunner(){m_timer.stop();bestEffortStop();}
QString SysplorerRunner::guiExecutable() const {
    QSettings config(QCoreApplication::applicationDirPath()+"/sysplorer.ini",QSettings::IniFormat);
    const auto home=qEnvironmentVariable("SYSPLORER_HOME",
        config.value("Host/Home","C:/Program Files/MWORKS/Sysplorer 2026a").toString());
    return QDir(home).absoluteFilePath("Bin64/mworks.exe");
}
QString SysplorerRunner::modelFingerprint() const {
    QFile f(":/models/BatterySim.mo");if(!f.open(QIODevice::ReadOnly))return {};return hash(f.readAll());
}
void SysplorerRunner::trace(const QString&event,const QJsonObject&values){
    if(!m_log.isOpen())return;auto o=values;o["event"]=event;o["taskId"]=m_taskId;o["phase"]=m_phase;
    o["utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_log.write(QJsonDocument(o).toJson(QJsonDocument::Compact));m_log.write("\n");m_log.flush();
}
void SysplorerRunner::setPhase(const QString&p){
    if(m_phase==p)return;m_phase=p;m_phaseClock.restart();trace("phase",{{"phase",p}});emit stateChanged(p);
}
void SysplorerRunner::connectHost(){
    if(m_busy)return;
    m_modelLoaded=false;m_modelHash=modelFingerprint();
    const auto base=qEnvironmentVariable("BATTERY_SIM_WORKSPACE",QDir::tempPath()+"/BatteryConsoleUDP");
    m_jobDir=QDir(base).absoluteFilePath("startup/"+m_clientId);QDir().mkpath(m_jobDir);
    m_log.close();m_log.setFileName(m_jobDir+"/client.log");m_log.open(QIODevice::WriteOnly|QIODevice::Append);
    m_connectOnly=true;m_port=0;m_pending.clear();m_clock.restart();
    setPhase("Connecting");m_timer.start();pump();
}
void SysplorerRunner::loadModel(){
    if(m_busy||m_connectOnly)return;
    prepareSimulation(3.45,2.5,1,.5,120);m_loadOnly=true;
}
void SysplorerRunner::beginSimulation(){
    if(isPrepared()&&!controlPending()){m_control="start";setPhase("Starting");}
}
void SysplorerRunner::invalidatePreparation(){
    if(isPrepared()&&!controlPending())m_control="discard";
}
void SysplorerRunner::prepareSimulation(double voltage,double current,int mode,double soc0,double duration,const QString&){
    if(m_busy)return;
    m_loadOnly=false;m_modelLoaded=false;
    m_connectOnly=false;m_busy=true;m_begun=false;m_launched=false;m_launchedPid=0;m_hostPid=0;
    m_busyHosts.clear();
    m_tuned=false;m_tunePending=false;m_tuneSequence=0;m_tuneArgs={};
    m_pending.clear();m_control.clear();m_cursor=0;m_samples.clear();m_port=0;m_session.clear();m_token.clear();
    m_taskId=uid();m_modelHash=modelFingerprint();m_phase.clear();m_voltage=voltage;m_current=current;m_mode=mode;
    m_soc0=soc0;m_duration=duration;m_clock.start();m_phaseClock.start();m_log.close();
    if(!std::isfinite(voltage)||voltage<2.5||voltage>3.45||!std::isfinite(current)||current<0||current>5
       ||(mode!=1&&mode!=2)||!std::isfinite(soc0)||soc0<0||soc0>1||!std::isfinite(duration)||duration<.1||duration>20000){
        QTimer::singleShot(0,this,[this]{fail(QString::fromUtf8("仿真参数不在允许范围内。"));});return;
    }
    const auto base=qEnvironmentVariable("BATTERY_SIM_WORKSPACE",QDir::tempPath()+"/BatteryConsoleUDP");
    for(auto c:base)if(c.unicode()>127){QTimer::singleShot(0,this,[this]{fail(QString::fromUtf8("检查输出目录 · OUTPUT_DIRECTORY_INVALID：仿真目录必须使用英文路径。"));});return;}
    m_jobDir=QDir(base).absoluteFilePath(m_taskId);
    if(!QDir().mkpath(m_jobDir)){QTimer::singleShot(0,this,[this]{fail(QString::fromUtf8("检查输出目录 · OUTPUT_DIRECTORY_FAILED：无法创建任务目录。"));});return;}
    m_log.setFileName(m_jobDir+"/client.log");m_log.open(QIODevice::WriteOnly|QIODevice::Append);
    trace("request",{{"mode",mode},{"voltage",voltage},{"current",current},{"soc0",soc0},{"duration",duration},{"modelHash",m_modelHash},{"hostExecutable",guiExecutable()}});
    setPhase("Connecting");m_timer.start();QTimer::singleShot(0,this,&SysplorerRunner::pump);
}
bool SysplorerRunner::discover(){
    const auto folder=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/udp";
    const auto files=QDir(folder).entryInfoList({"*.json"},QDir::Files,QDir::Time);
    for(const auto&f:files){
        QFile file(f.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))continue;
        auto o=QJsonDocument::fromJson(file.readAll()).object();
        const auto updated=QDateTime::fromString(o["updated"].toString(),Qt::ISODateWithMs);
        const qint64 pid=qint64(o["pid"].toDouble());
        if(o["schema"].toInt()!=1||o["protocol"].toString()!="battery-udp/1"||!updated.isValid()
            ||updated.secsTo(QDateTime::currentDateTimeUtc())>20||!processMatches(pid,guiExecutable()))continue;
        if(m_launchedPid&&pid!=m_launchedPid)continue;
        if(m_busyHosts.contains(pid))continue;
        if(!o["bound"].toBool())return true; // Existing host is still starting; do not spawn duplicates.
        const int port=o["port"].toInt();
        if(port<=0||port>65535||o["session"].toString().isEmpty()||o["token"].toString().size()<24)continue;
        m_port=quint16(port);m_hostPid=pid;m_session=o["session"].toString();m_token=o["token"].toString();
        trace("connected",{{"pid",double(pid)},{"port",port},{"session",m_session}});
        send("hello");return true;
    }
    return false;
}
bool SysplorerRunner::launchHost(){
    const auto exe=guiExecutable();
    const auto plugin=QFileInfo(exe).absolutePath()+"/addins/BatteryUdpControl/battery_udp_control.dll";
    if(!QFileInfo::exists(exe)){fail(QString::fromUtf8("找不到本地 Sysplorer：%1").arg(exe));return false;}
    if(!QFileInfo::exists(plugin)){fail(QString::fromUtf8("未安装 BatteryUdpControl 插件，请先运行插件部署脚本。"));return false;}
    auto env=QProcessEnvironment::systemEnvironment();
    for(const auto&key:{"QT_QPA_PLATFORM","QT_QPA_PLATFORM_PLUGIN_PATH","QT_PLUGIN_PATH","PYTHONHOME","PYTHONPATH"})env.remove(key);
    env.insert("PATH",QDir::toNativeSeparators(QFileInfo(exe).absolutePath())+";"+env.value("PATH"));
    auto base=qEnvironmentVariable("BATTERY_SIM_WORKSPACE",QDir::tempPath()+"/BatteryConsoleUDP");
    if(!QDir().mkpath(QDir(base).absoluteFilePath("hosts"))){fail(QString::fromUtf8("无法创建 Sysplorer 独立工作目录。"));return false;}
    // Native compiler tools are sensitive to long generated paths. QTemporaryDir
    // provides a short, unique directory without sharing another task's files.
    QTemporaryDir hostWork(QDir(base).absoluteFilePath("hosts/hXXXXXX"));
    if(!hostWork.isValid()){fail(QString::fromUtf8("无法创建 Sysplorer 独立工作目录：%1").arg(hostWork.errorString()));return false;}
    hostWork.setAutoRemove(false);const auto work=hostWork.path();
    QProcess process;process.setProcessEnvironment(env);process.setProgram(exe);
    process.setArguments({"-d",work,"-w",work});process.setWorkingDirectory(work);
    if(!process.startDetached(&m_launchedPid)){fail(QString::fromUtf8("无法启动 Sysplorer：%1").arg(process.errorString()));return false;}
    m_launched=true;trace("launched",{{"pid",double(m_launchedPid)},{"workDir",work}});return true;
}
void SysplorerRunner::send(const QString&command,const QJsonObject&args){
    if(!m_pending.isEmpty()||!m_port)return;
    m_pendingId=uid();m_pendingCommand=command;
    QJsonObject r{{"schema",1},{"id",m_pendingId},{"session",m_session},{"token",m_token},{"client",m_clientId},
        {"taskId",m_taskId},{"command",command},{"args",args}};
    m_pending=QJsonDocument(r).toJson(QJsonDocument::Compact);m_attempts=1;m_pendingClock.start();
    m_socket.writeDatagram(m_pending,QHostAddress::LocalHost,m_port);
    if(command!="poll")trace("send",{{"command",command},{"id",m_pendingId}});
}
void SysplorerRunner::pump(){
    if(!m_busy&&!m_connectOnly)return;
    if(m_connectOnly){
        if(!m_pending.isEmpty()){
            if(m_pendingClock.elapsed()>3000){m_pending.clear();m_port=0;}
            else return;
        }
        if(m_clock.elapsed()>m_hostTimeoutMs&&!m_connectionWarned){
            m_connectionWarned=true;m_launchedPid=0;
            emit hostConnectionFailed(QString::fromUtf8("未能打开 Sysplorer，请自行启动。"));
        }
        if(discover())return;
        if(!m_autoAttempted){m_autoAttempted=true;launchHost();}
        return;
    }
    if(!m_pending.isEmpty()){
        if(m_pendingClock.elapsed()>1500){
            if(m_attempts>=6){fail(QString::fromUtf8("UDP 通信超时，未收到 %1 的确认。任务 %2；日志 %3/client.log")
                .arg(m_pendingCommand,m_taskId,m_jobDir));return;}
            ++m_attempts;m_pendingClock.restart();m_socket.writeDatagram(m_pending,QHostAddress::LocalHost,m_port);
        }return;
    }
    if(!m_port){
        if(m_clock.elapsed()>m_hostTimeoutMs){fail(QString::fromUtf8("Sysplorer 插件连接超时，请检查插件部署与启动日志。"));return;}
        if(!discover()&&!m_launched)launchHost();return;
    }
    if(!m_begun){
        if(m_clock.elapsed()>m_hostTimeoutMs){fail(QString::fromUtf8("Sysplorer 尚未就绪，请查看宿主输出。"));return;}
        send("hello");return;
    }
    if(m_phase=="Translating"&&m_phaseClock.elapsed()>300000){fail(QString::fromUtf8("模型翻译超时。"));return;}
    if(!m_control.isEmpty()){
        auto cmd=m_control;m_control.clear();
        send(cmd,cmd=="tune"?m_tuneArgs:(cmd=="speed"||cmd=="start")?QJsonObject{{"speed",m_speed}}:QJsonObject{});return;
    }
    send("poll",{{"cursor",m_cursor}});
}
void SysplorerRunner::receive(){
    while(m_socket.hasPendingDatagrams()){
        auto d=m_socket.receiveDatagram(16384);
        if((!m_busy&&!m_connectOnly)||m_pending.isEmpty()||d.senderAddress()!=QHostAddress::LocalHost||d.senderPort()!=m_port)continue;
        auto r=QJsonDocument::fromJson(d.data()).object();
        if(r["schema"].toInt()!=1||r["id"].toString()!=m_pendingId||r["session"].toString()!=m_session
            ||r["taskId"].toString()!=m_taskId)continue;
        const auto cmd=m_pendingCommand;m_pending.clear();m_pendingCommand.clear();
        if(m_connectOnly){
            if(r["ok"].toBool()&&r["ready"].toBool()){
                const auto hostPhase=r["status"].toObject()["phase"].toString();
                if(!r["modelLifecycle"].toBool()||r["modelHash"].toString()!=modelFingerprint()){
                    fail(QString::fromUtf8("宿主插件缺少独立加载/翻译能力，或模型版本不一致；请更新配套插件。"));m_port=0;continue;
                }
                if(r["controllerState"].toString()!="Idle"||(hostPhase!="Ready"&&hostPhase!="Loaded"&&hostPhase!="Completed"&&hostPhase!="Stopped"&&hostPhase!="Failed")){
                    fail(QString::fromUtf8("Sysplorer 正被其他任务占用，未接管该任务。"));m_port=0;continue;
                }
                m_modelLoaded=r["modelLoaded"].toBool();m_connectOnly=false;m_timer.stop();setPhase("Connected");
            }else m_port=0;
            continue;
        }
        if(!r["ok"].toBool()){
            auto code=r["code"].toString();
            if((cmd=="begin"||cmd=="load"||cmd=="translate")&&code=="HOST_STARTING")continue;
            if(cmd=="tune"){m_tunePending=false;emit tuneRejected(r["message"].toString());emit stateChanged(m_phase);continue;}
            fail(QString::fromUtf8("Sysplorer 操作 %1 失败：%2。任务 %3；日志 %4/client.log").arg(cmd,code+": "+r["message"].toString(),m_taskId,m_jobDir));continue;
        }
        if(cmd=="hello"){
            if(!r["splitLifecycle"].toBool()||!r["modelLifecycle"].toBool()){
                fail(QString::fromUtf8("宿主插件版本过旧，请部署支持独立加载／翻译／开始的新插件。"));continue;
            }
            if(r["modelHash"].toString()!=m_modelHash){
                trace("host_model_mismatch",{{"pid",double(m_hostPid)},{"expected",m_modelHash},{"actual",r["modelHash"]}});
                if(m_launchedPid){
                    fail(QString::fromUtf8("所配置宿主的插件模型版本不一致。宿主：%1；客户端模型：%2；宿主模型：%3。请更新隔离测试包，日志：%4/client.log")
                        .arg(guiExecutable(),m_modelHash,r["modelHash"].toString(),m_jobDir));
                }else{
                    m_busyHosts.insert(m_hostPid);m_port=0;m_hostPid=0;m_session.clear();m_token.clear();
                }
                continue;
            }
            const auto st=r["status"].toObject();
            const auto p=st["phase"].toString();
            const bool idlePhase=p=="Ready"||p=="Loaded"||p=="Completed"||p=="Stopped"||p=="Failed";
            // A bound plugin can still be initializing. Wait on this instance
            // instead of classifying initialization as another client's task.
            if(idlePhase&&r["controllerState"].toString()=="Idle"&&!r["ready"].toBool())continue;
            if((p!="Ready"&&p!="Loaded"&&p!="Completed"&&p!="Stopped"&&p!="Failed")||r["controllerState"].toString()!="Idle"
                ||(!m_launchedPid&&(p=="Failed"||!r["ready"].toBool()))){
                if(m_launchedPid){fail(QString::fromUtf8("新启动的 Sysplorer 求解器仍被占用，请查看宿主任务。"));continue;}
                trace("host_busy_skipped",{{"pid",double(m_hostPid)},{"phase",p},{"controllerState",r["controllerState"]}});
                m_busyHosts.insert(m_hostPid);m_port=0;m_hostPid=0;m_session.clear();m_token.clear();
                // Keep the other client's simulation intact. Discovery tries
                // other hosts, then starts one dedicated instance if needed.
                continue;
            }
            if(!r["ready"].toBool())continue;
            m_modelLoaded=r["modelLoaded"].toBool();
            send(m_loadOnly?"load":"translate",{{"modelHash",m_modelHash},{"mode",m_mode},{"voltage",m_voltage},{"current",m_current},
                {"soc0",m_soc0},{"duration",m_duration},{"speed",m_speed}});
        }else{
            if(cmd=="discard"){
                m_busy=false;m_timer.stop();m_begun=false;setPhase("Connected");continue;
            }
            if(cmd=="begin"||cmd=="load"||cmd=="translate")m_begun=true;
            consume(r["status"].toObject());
            if(m_busy)emit stateChanged(m_phase);
        }
    }
}
void SysplorerRunner::consume(const QJsonObject&s){
    if(s["taskId"].toString()!=m_taskId||s["owner"].toString()!=m_clientId||s["modelHash"].toString()!=m_modelHash){
        fail(QString::fromUtf8("收到不属于本次任务的结果。"));return;
    }
    if(!s["jobDir"].toString().isEmpty())m_jobDir=s["jobDir"].toString();
    const auto p=s["params"].toObject();
    const auto history=s["tuneHistory"].toArray();
    if(history.size()>m_tuneSequence){
        const auto event=history.last().toObject();m_tuneSequence=history.size();m_tuned=true;m_tunePending=false;
        emit tuneApplied(event["confirmedAt"].toDouble(event["time"].toDouble()),event["params"].toObject());
    }
    if(!p.isEmpty()&&(p["mode"].toInt()!=m_mode||std::abs(p["voltage"].toDouble()-m_voltage)>1e-8
        ||std::abs(p["current"].toDouble()-m_current)>1e-8||std::abs(p["soc0"].toDouble()-m_soc0)>1e-8)){
        fail(QString::fromUtf8("宿主参数回读不一致。"));return;
    }
    // Non-poll acknowledgements may include an old cursor-zero preview. Only
    // consume the exact next sequence, so retries never append data twice.
    if(s["cursor"].toInt()==m_cursor){
        QVector<ReplaySample> batch;const auto rows=s["rows"].toArray();
        for(const auto&value:rows){
            auto a=value.toArray();if(a.size()!=4){fail(QString::fromUtf8("实时数据格式错误。"));return;}
            ReplaySample v{a[0].toDouble(-1),a[1].toDouble(-1),a[2].toDouble(),a[3].toDouble()};
            if(!std::isfinite(v.t)||!std::isfinite(v.soc)||!std::isfinite(v.current)||!std::isfinite(v.voltage)
                ||v.t<0||v.t>m_duration+.02||v.soc<-1e-5||v.soc>1.00001||std::abs(v.current)>(m_tuned?5.0:m_current)+1e-4
                ||(!m_samples.isEmpty()&&v.t<=m_samples.last().t)
                ||(!m_tuned&&(m_mode==1?v.current<-1e-4:v.current>1e-4))){
                fail(QString::fromUtf8("实时数据越界或时间顺序异常。"));return;
            }
            m_samples.append(v);batch.append(v);
        }
        if(s["next"].toInt()!=m_cursor+rows.size()){fail(QString::fromUtf8("实时数据序号不一致。"));return;}
        m_cursor=s["next"].toInt();if(!batch.isEmpty())emit samplesArrived(batch);
    }
    const auto phase=s["phase"].toString();m_modelLoaded=s["modelLoaded"].toBool();
    if(phase=="Loaded"&&m_loadOnly){m_busy=false;m_timer.stop();m_control.clear();trace("model_loaded");m_log.close();}
    setPhase(phase);
    if(phase=="Failed"){fail(s["stage"].toString()+" · "+s["code"].toString()+": "+s["message"].toString());return;}
    if((phase=="Completed"||phase=="Stopped")&&m_cursor>=s["total"].toInt())finalize(s);
}
void SysplorerRunner::finalize(const QJsonObject&s){
    QFile manifest(m_jobDir+"/result.json"),csv(m_jobDir+"/result.csv");
    if(!manifest.open(QIODevice::ReadOnly)||!csv.open(QIODevice::ReadOnly)){fail(QString::fromUtf8("本次结果文件缺失。"));return;}
    auto j=QJsonDocument::fromJson(manifest.readAll()).object();auto bytes=csv.readAll();
    const bool complete=s["phase"].toString()=="Completed";
    if(j["taskId"].toString()!=m_taskId||j["modelHash"].toString()!=m_modelHash||j["complete"].toBool()!=complete
        ||j["csvHash"].toString()!=hash(bytes)||j["csvHash"].toString()!=s["csvHash"].toString()){
        fail(QString::fromUtf8("本次结果标识或哈希核验失败。"));return;
    }
    const auto p=j["params"].toObject();
    if(p["mode"].toInt()!=m_mode||std::abs(p["voltage"].toDouble()-m_voltage)>1e-8
        ||std::abs(p["current"].toDouble()-m_current)>1e-8||std::abs(p["soc0"].toDouble()-m_soc0)>1e-8){
        fail(QString::fromUtf8("结果参数不一致。"));return;
    }
    ReplayData parsed;QString error;
    if(!parsed.load(m_jobDir+"/result.csv",&error)||parsed.size()!=j["points"].toInt()||parsed.isEmpty()){
        fail(QString::fromUtf8("本次结果读取失败：%1").arg(error));return;
    }
    double last=-1;
    for(const auto&r:parsed.samples()){
        if(!std::isfinite(r.t)||!std::isfinite(r.soc)||!std::isfinite(r.current)||!std::isfinite(r.voltage)
            ||r.t<last||r.soc<-1e-5||r.soc>1.00001||std::abs(r.current)>(m_tuned?5.0:m_current)+1e-4
            ||(!m_tuned&&(m_mode==1?r.current<-1e-4:r.current>1e-4))){
            fail(QString::fromUtf8("结果数据范围核验失败。"));return;
        }last=r.t;
    }
    if(std::abs(parsed.socBegin()-m_soc0)>1e-6||(complete&&std::abs(parsed.duration()-m_duration)>.02)){
        fail(QString::fromUtf8("结果起点或完成时间不一致。"));return;
    }
    m_busy=false;m_timer.stop();m_control.clear();
    const auto message=s["viewerMessage"].toString();trace("verified",{{"complete",complete},{"points",parsed.size()},{"lastTime",parsed.duration()}});
    m_log.close();
    if(complete)emit finished(true,m_jobDir+"/result.csv",message);
    else emit stopped(m_jobDir+"/result.csv",message);
}
void SysplorerRunner::bestEffortStop(){
    if(!m_busy||!m_begun||m_session.isEmpty())return;
    const QJsonObject r{{"schema",1},{"id",uid()},{"session",m_session},{"token",m_token},{"client",m_clientId},
        {"taskId",m_taskId},{"command",m_phase=="Prepared"?"discard":"stop"},{"args",QJsonObject{}}};
    auto bytes=QJsonDocument(r).toJson(QJsonDocument::Compact);
    for(int n=0;n<3;++n)m_socket.writeDatagram(bytes,QHostAddress::LocalHost,m_port);
}
void SysplorerRunner::fail(const QString&message){
    // Preflight failures still need a durable diagnostic when the requested
    // workspace itself is invalid or could not be created.
    if(!m_log.isOpen()){
        const auto fallback=QDir::tempPath()+"/BatteryConsoleUDP/diagnostics/"+m_clientId;
        if(QDir().mkpath(fallback)){
            m_jobDir=fallback;m_log.setFileName(fallback+"/client.log");m_log.open(QIODevice::WriteOnly|QIODevice::Append);
        }
    }
    if(m_connectOnly){
        if(!m_connectionWarned){m_connectionWarned=true;trace("connection_failed",{{"message",message}});emit hostConnectionFailed(QString::fromUtf8("未能打开 Sysplorer，请自行启动。\n%1").arg(message));}
        return;
    }
    if(!m_busy)return;trace("failed",{{"message",message}});bestEffortStop();m_busy=false;m_timer.stop();
    m_modelLoaded=false;m_pending.clear();m_control.clear();m_tunePending=false;setPhase("Failed");m_log.close();
    emit finished(false,{},message);
}
void SysplorerRunner::pause(){if(m_busy&&m_phase=="Running"&&!controlPending())m_control="pause";}
void SysplorerRunner::tune(const QJsonObject& params){
    if(!m_busy||(m_phase!="Running"&&m_phase!="Paused")||controlPending()||m_tunePending)return;
    m_tuneArgs=params;m_tunePending=true;m_control="tune";
}
void SysplorerRunner::resume(){if(m_busy&&m_phase=="Paused"&&!controlPending())m_control="resume";}
void SysplorerRunner::stop(){if(m_busy&&(m_phase=="Running"||m_phase=="Paused"||m_phase=="Pausing"||m_phase=="Resuming")&&!controlPending())m_control="stop";}
void SysplorerRunner::setSpeed(double s){
    if(!std::isfinite(s)||s<.5||s>60)return;m_speed=s;
    if(m_busy&&m_begun&&(m_phase=="Running"||m_phase=="Paused")&&m_control.isEmpty())m_control="speed";
}

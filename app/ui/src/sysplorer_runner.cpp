#include "sysplorer_runner.h"
#include "remote_link.h"
#include "model_transfer.h"
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
#include <wincrypt.h>
#endif
namespace {
QString uid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QString hash(const QByteArray&b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
bool hostInitializing(const QString& code){
    return code=="HOST_STARTING"||code=="Host is starting"||code=="Simulation panel unavailable"
        ||code=="Simulation controller unavailable"||code=="HOST_TASK_MANAGER_UNAVAILABLE";
}
QByteArray protectKey(const QByteArray& value,bool encrypt){
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(value.size()),reinterpret_cast<BYTE*>(const_cast<char*>(value.constData()))},output{};
    const bool ok=encrypt?CryptProtectData(&input,L"BatteryConsole connection",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)
        :CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output);
    if(!ok)return {};const QByteArray bytes(reinterpret_cast<const char*>(output.pbData),int(output.cbData));LocalFree(output.pbData);return bytes;
#else
    Q_UNUSED(value);Q_UNUSED(encrypt);return {};
#endif
}
bool samePath(const QString&a,const QString&b){
    if(QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(QDir::cleanPath(QDir::fromNativeSeparators(b)),Qt::CaseInsensitive)==0)return true;
#ifdef Q_OS_WIN
    // Windows reports the underlying image path for SUBST drives and junctions.
    // Match file identity rather than rejecting an otherwise identical executable.
    HANDLE first=CreateFileW(reinterpret_cast<LPCWSTR>(a.utf16()),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    HANDLE second=CreateFileW(reinterpret_cast<LPCWSTR>(b.utf16()),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    BY_HANDLE_FILE_INFORMATION x{},y{};
    const bool same=first!=INVALID_HANDLE_VALUE&&second!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(first,&x)&&GetFileInformationByHandle(second,&y)
        &&x.dwVolumeSerialNumber==y.dwVolumeSerialNumber&&x.nFileIndexHigh==y.nFileIndexHigh&&x.nFileIndexLow==y.nFileIndexLow;
    if(first!=INVALID_HANDLE_VALUE)CloseHandle(first);if(second!=INVALID_HANDLE_VALUE)CloseHandle(second);return same;
#else
    return false;
#endif
}
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
QJsonObject localPluginDetails(qint64 pid,const QString& executable){
    QString path=QFileInfo(executable).absolutePath()+"/addins/BatteryUdpControl/battery_udp_control.dll";
    bool loaded=false;
#ifdef Q_OS_WIN
    if(pid>0){
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,DWORD(pid));
        if(snapshot!=INVALID_HANDLE_VALUE){
            MODULEENTRY32W module{};module.dwSize=sizeof(module);
            if(Module32FirstW(snapshot,&module))do{
                if(QString::fromWCharArray(module.szModule).compare("battery_udp_control.dll",Qt::CaseInsensitive)==0){
                    path=QString::fromWCharArray(module.szExePath);loaded=true;break;
                }
            }while(Module32NextW(snapshot,&module));
            CloseHandle(snapshot);
        }
    }
#endif
    QFile file(path);const auto sha=file.open(QIODevice::ReadOnly)?hash(file.readAll()):QString();
    return {{"pluginPath",QDir::toNativeSeparators(path)},{"pluginPathFromProcess",loaded},{"pluginDiskSha256",sha}};
}
}
SysplorerRunner::SysplorerRunner(QObject*p):QObject(p),m_clientId(uid()){
    QSettings config(QCoreApplication::applicationDirPath()+"/sysplorer.ini",QSettings::IniFormat);
    bool timeoutOk=false;const int timeout=qEnvironmentVariable("BATTERY_HOST_TIMEOUT_MS",config.value("Host/TimeoutMs",180000).toString()).toInt(&timeoutOk);
    if(timeoutOk)m_hostTimeoutMs=qBound(1000,timeout,600000);
    QSettings saved(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","RemoteConnection");
    m_remote=config.value("Connection/Mode","local").toString()=="remote";
    m_remoteAddress=config.value("Connection/Address","127.0.0.1").toString();
    const int configuredPort=config.value("Connection/Port",39001).toInt();
    if(configuredPort>0&&configuredPort<=65535)m_remotePort=quint16(configuredPort);
    if(saved.value("Configured",false).toBool()){
        m_remote=saved.value("Remote",false).toBool();m_remoteAddress=saved.value("Address",m_remoteAddress).toString();
        const int port=saved.value("Port",m_remotePort).toInt();if(port>0&&port<=65535)m_remotePort=quint16(port);
        m_accessKey=QString::fromUtf8(protectKey(QByteArray::fromBase64(saved.value("ProtectedKey").toByteArray()),false));
    }
    const auto mode=qEnvironmentVariable("BATTERY_CONNECTION_MODE");if(!mode.isEmpty())m_remote=mode=="remote";
    if(qEnvironmentVariableIsSet("BATTERY_REMOTE_ADDRESS"))m_remoteAddress=qEnvironmentVariable("BATTERY_REMOTE_ADDRESS");
    if(qEnvironmentVariableIsSet("BATTERY_REMOTE_PORT")){const int port=qEnvironmentVariableIntValue("BATTERY_REMOTE_PORT");if(port>0&&port<=65535)m_remotePort=quint16(port);}
    if(qEnvironmentVariableIsSet("BATTERY_REMOTE_KEY"))m_accessKey=qEnvironmentVariable("BATTERY_REMOTE_KEY");
    qRegisterMetaType<QVector<ReplaySample>>("QVector<ReplaySample>");
    m_socket.bind(QHostAddress::LocalHost,0,QUdpSocket::DontShareAddress);
    connect(&m_socket,&QUdpSocket::readyRead,this,&SysplorerRunner::receive);
    m_timer.setInterval(50);connect(&m_timer,&QTimer::timeout,this,&SysplorerRunner::pump);
}
SysplorerRunner::~SysplorerRunner(){m_timer.stop();bestEffortStop();closeRemote(true);}
QString SysplorerRunner::guiExecutable() const {
    QSettings config(QCoreApplication::applicationDirPath()+"/sysplorer.ini",QSettings::IniFormat);
    QFile local(qEnvironmentVariable("BATTERY_LOCAL_CONFIG",QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/local-runtime.json"));
    QJsonObject saved;if(local.open(QIODevice::ReadOnly))saved=QJsonDocument::fromJson(local.readAll()).object();
    auto configured=config.value("Host/Home").toString();if(configured.isEmpty())configured=saved["sysplorerHome"].toString();
    const auto home=qEnvironmentVariable("SYSPLORER_HOME",configured);
    if(home.trimmed().isEmpty())return {};
    return QDir(home).absoluteFilePath("Bin64/mworks.exe");
}
bool SysplorerRunner::ensureLocalConfiguration(){
    if(m_localSetup)return false;
    if(m_localChecked)return true;
    // Explicit environment bindings are for isolated validation/development.
    if(qEnvironmentVariableIsSet("SYSPLORER_HOME")){m_localChecked=true;return true;}
    const auto folder=QCoreApplication::applicationDirPath();
    const auto setup=folder+"/Setup-Local.ps1";
    if(!QFileInfo::exists(setup)){m_localChecked=true;return true;}
    const auto config=qEnvironmentVariable("BATTERY_LOCAL_CONFIG",QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/local-runtime.json");
    auto process=new QProcess(this);m_localSetup=process;
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
    process->setProgram(qEnvironmentVariable("SystemRoot","C:/Windows")+"/System32/WindowsPowerShell/v1.0/powershell.exe");
    process->setArguments({"-NoProfile","-ExecutionPolicy","Bypass","-File",setup,"-ClientStatePath",config});
    connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this,process,config](int code,QProcess::ExitStatus status){
        const auto message=QString::fromLocal8Bit(process->readAllStandardError());m_localSetup=nullptr;process->deleteLater();
        if(code!=0||status!=QProcess::NormalExit){connectionFailed("LOCAL_SETUP_FAILED",QString::fromUtf8("本机配置未完成：%1").arg(message));return;}
        QFile f(config);QJsonObject o;if(f.open(QIODevice::ReadOnly))o=QJsonDocument::fromJson(f.readAll()).object();
        if(o["sysplorerHome"].toString().isEmpty()||o["workspace"].toString().isEmpty()){connectionFailed("LOCAL_SETUP_FAILED","Local runtime receipt missing");return;}
        if(!qEnvironmentVariableIsSet("BATTERY_SIM_WORKSPACE")){
            const auto work=o["workspace"].toString();
#ifdef Q_OS_WIN
            _wputenv_s(L"BATTERY_SIM_WORKSPACE",reinterpret_cast<const wchar_t*>(work.utf16()));
#else
            qputenv("BATTERY_SIM_WORKSPACE",work.toUtf8());
#endif
        }
        m_log.close();m_jobDir=QDir(qEnvironmentVariable("BATTERY_SIM_WORKSPACE")).filePath(m_connectOnly?"startup/"+m_clientId:m_taskId);
        QDir().mkpath(m_jobDir);m_log.setFileName(m_jobDir+"/client.log");m_log.open(QIODevice::WriteOnly|QIODevice::Append);
        m_localChecked=true;m_clock.restart();pump();
    });
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError e){if(e==QProcess::FailedToStart){m_localSetup=nullptr;connectionFailed("LOCAL_SETUP_FAILED",process->errorString());process->deleteLater();}});
    process->start();return false;
}
QString SysplorerRunner::modelFingerprint() const {
    QFile f(qEnvironmentVariable("BATTERY_MODEL_FILE",QCoreApplication::applicationDirPath()+"/models/BatterySim.mo"));
    if(!f.open(QIODevice::ReadOnly)||f.size()>ModelTransfer::Limit)return {};return hash(f.readAll());
}
bool SysplorerRunner::readModelSnapshot(){
    QFile f(qEnvironmentVariable("BATTERY_MODEL_FILE",QCoreApplication::applicationDirPath()+"/models/BatterySim.mo"));
    QString error;
    if(!f.open(QIODevice::ReadOnly))error=QString::fromUtf8("MODEL_FILE_MISSING：无法读取客户端模型 %1").arg(f.fileName());
    else if(f.size()>ModelTransfer::Limit)error="MODEL_SIZE_INVALID: maximum 1 MiB";
    else {m_modelSnapshot=f.readAll();try{ModelTransfer::validate(m_modelSnapshot);}catch(const std::exception& e){error=QString::fromUtf8(e.what());}}
    if(!error.isEmpty()){QTimer::singleShot(0,this,[this,error]{fail(error);});return false;}
    m_modelHash=hash(m_modelSnapshot);m_uploadId=uid();m_uploadOffset=0;m_uploadChunk=m_remote?ModelTransfer::TcpChunk:ModelTransfer::UdpChunk;return true;
}
void SysplorerRunner::nextModelChunk(){
    if(m_uploadOffset==m_modelSnapshot.size()){send("model.commit",{{"uploadId",m_uploadId}});return;}
    const auto bytes=m_modelSnapshot.mid(m_uploadOffset,m_uploadChunk);
    send("model.chunk",{{"uploadId",m_uploadId},{"offset",m_uploadOffset},{"data",QString::fromLatin1(bytes.toBase64())},{"sha256",hash(bytes)}});
}
void SysplorerRunner::sendPreparation(){
    send(m_loadOnly?"load":"translate",{{"modelHash",m_modelHash},{"mode",m_mode},{"voltage",m_voltage},{"current",m_current},
        {"soc0",m_soc0},{"duration",m_duration},{"speed",m_speed}});
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
    m_localChecked=false;
    // A new idle connection (including a mode switch) has no active task.
    // Existing result files and the window's replay cache remain available.
    m_taskId.clear();m_serverJobDir.clear();m_begun=false;m_resumePhase.clear();
    m_modelLoaded=false;m_modelHash=modelFingerprint();
    const auto base=qEnvironmentVariable("BATTERY_SIM_WORKSPACE",QDir::tempPath()+"/BatteryConsoleUDP");
    m_jobDir=QDir(base).absoluteFilePath("startup/"+m_clientId);QDir().mkpath(m_jobDir);
    m_log.close();m_log.setFileName(m_jobDir+"/client.log");m_log.open(QIODevice::WriteOnly|QIODevice::Append);
    trace("connection_environment",{{"clientExecutable",QCoreApplication::applicationFilePath()},{"worktreeRoot",qEnvironmentVariable("BATTERY_WORKTREE_ROOT")},
        {"hostExecutable",guiExecutable()},{"discoveryDirectory",qEnvironmentVariable("BATTERY_DISCOVERY_DIR")},{"mode",m_remote?"remote":"local"}});
    m_connectOnly=true;m_port=0;m_pending.clear();m_pendingCommand.clear();m_clock.restart();m_connectionWarned=false;
    m_autoAttempted=false;m_launchedPid=0;m_hostPid=0;m_busyHosts.clear();
    m_remoteLost=false;m_session.clear();m_token.clear();
    if(m_remote){closeRemote();setPhase("Connecting");m_timer.start();openRemote();return;}
    setPhase("Connecting");m_timer.start();pump();
}
void SysplorerRunner::loadModel(){
    if(m_busy||m_connectOnly)return;
    prepareSimulation(3.45,2.5,1,.5,120);m_loadOnly=true;
}
void SysplorerRunner::beginSimulation(){
    if(isPrepared()&&!controlPending()){
        if(modelFingerprint()!=m_modelHash){m_control="discard";emit remoteNotice(QString::fromUtf8("模型文本已变化，请重新翻译模型后开始。"));return;}
        m_control="start";setPhase("Starting");
    }
}
void SysplorerRunner::invalidatePreparation(){
    if(isPrepared()&&!controlPending())m_control="discard";
}
void SysplorerRunner::prepareSimulation(double voltage,double current,int mode,double soc0,double duration,const QString&){
    if(m_busy)return;
    m_localChecked=false;
    m_loadOnly=false;m_modelLoaded=false;
    m_connectOnly=false;m_busy=true;m_begun=false;m_launched=false;m_launchedPid=0;m_hostPid=0;
    m_busyHosts.clear();
    m_tuned=false;m_tunePending=false;m_tuneSequence=0;m_tuneArgs={};
    m_pending.clear();m_control.clear();m_cursor=0;m_samples.clear();m_port=0;m_session.clear();m_token.clear();
    m_taskId=uid();m_modelHash=modelFingerprint();m_phase.clear();m_voltage=voltage;m_current=current;m_mode=mode;
    m_serverJobDir.clear();m_finalStatus={};m_downloading=false;m_downloadReady=false;m_downloadFile.reset();m_remoteLost=false;
    if(m_remote)closeRemote();
    m_soc0=soc0;m_duration=duration;m_clock.start();m_phaseClock.start();m_log.close();
    if(!readModelSnapshot())return;
    if(!std::isfinite(voltage)||voltage<2.5||voltage>3.45||!std::isfinite(current)||current<0||current>5
       ||(mode!=1&&mode!=2)||!std::isfinite(soc0)||soc0<0||soc0>1||!std::isfinite(duration)||duration<.1||duration>20000){
        QTimer::singleShot(0,this,[this]{fail(QString::fromUtf8("仿真参数不在允许范围内。"));});return;
    }
    const auto base=qEnvironmentVariable("BATTERY_SIM_WORKSPACE",QDir::tempPath()+"/BatteryConsoleUDP");
    // Client cache/model paths may contain Unicode. The plugin owns the ASCII
    // compiler workspace, and local setup configures it before launching a host.
    m_jobDir=QDir(base).absoluteFilePath(m_taskId);
    if(!QDir().mkpath(m_jobDir)){QTimer::singleShot(0,this,[this]{fail(QString::fromUtf8("检查输出目录 · OUTPUT_DIRECTORY_FAILED：无法创建任务目录。"));});return;}
    m_log.setFileName(m_jobDir+"/client.log");m_log.open(QIODevice::WriteOnly|QIODevice::Append);
    trace("request",{{"mode",mode},{"voltage",voltage},{"current",current},{"soc0",soc0},{"duration",duration},{"modelHash",m_modelHash},{"hostExecutable",guiExecutable()}});
    trace("discovery_environment",{{"executable",QCoreApplication::applicationFilePath()},
        {"folder",qEnvironmentVariable("BATTERY_DISCOVERY_DIR",QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/udp")},
        {"localAppData",qEnvironmentVariable("LOCALAPPDATA")},{"target",guiExecutable()}});
    setPhase("Connecting");m_timer.start();QTimer::singleShot(0,this,&SysplorerRunner::pump);
}
bool SysplorerRunner::discover(){
    if(m_remote)return false;
    const auto folder=qEnvironmentVariable("BATTERY_DISCOVERY_DIR",QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/udp");
    static QString lastDiagnostic;
    static QString lastDiagnosticTask;
    QString diagnostic=QString("folder=%1; target=%2; ").arg(folder,guiExecutable());
    auto files=QDir(folder).entryInfoList({"*.json"},QDir::Files,QDir::Time);
#ifdef Q_OS_WIN
    if(files.isEmpty()){
        WIN32_FIND_DATAW entry={};
        const auto pattern=QDir::toNativeSeparators(folder+"/*");
        HANDLE search=FindFirstFileW(reinterpret_cast<LPCWSTR>(pattern.utf16()),&entry);
        if(search!=INVALID_HANDLE_VALUE){
            do {
                const auto name=QString::fromWCharArray(entry.cFileName);
                if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&name.endsWith(".json",Qt::CaseInsensitive))
                    files.append(QFileInfo(folder+"/"+name));
            }while(FindNextFileW(search,&entry));
            FindClose(search);
        }else diagnostic+=QString("nativeError=%1; ").arg(GetLastError());
        diagnostic+=QString("nativeEntries=%1; ").arg(files.size());
    }
#endif
    diagnostic+=QString("entries=%1; exists=%2; readable=%3; ").arg(files.size()).arg(QDir(folder).exists()).arg(QFileInfo(folder).isReadable());
    bool startingHostFound=false;
    for(const auto&f:files){
        QFile file(f.absoluteFilePath());if(!file.open(QIODevice::ReadOnly)){
            diagnostic+=QString("file=%1 openError=%2; ").arg(f.fileName(),file.errorString());continue;
        }
        auto o=QJsonDocument::fromJson(file.readAll()).object();
        const auto updated=QDateTime::fromString(o["updated"].toString(),Qt::ISODateWithMs);
        const qint64 pid=qint64(o["pid"].toDouble());
        diagnostic+=QString("pid=%1 age=%2 match=%3 bound=%4; ").arg(pid)
            .arg(updated.secsTo(QDateTime::currentDateTimeUtc())).arg(processMatches(pid,guiExecutable())).arg(o["bound"].toBool());
        if(o["schema"].toInt()!=1||o["protocol"].toString()!="battery-udp/1"||!updated.isValid()
            ||!processMatches(pid,guiExecutable()))continue;
        if(m_launchedPid&&pid!=m_launchedPid)continue;
        if(m_busyHosts.contains(pid))continue;
        // Native library initialization can block the host UI/heartbeat. A
        // verified live image with an old endpoint is still an initializing
        // candidate, never permission to start another copy of that image.
        if(updated.secsTo(QDateTime::currentDateTimeUtc())>20){startingHostFound=true;continue;}
        if(!o["bound"].toBool()){startingHostFound=true;continue;} // Scan ready hosts before waiting.
        const int port=o["port"].toInt();
        if(port<=0||port>65535||o["session"].toString().isEmpty()||o["token"].toString().size()<24)continue;
        m_port=quint16(port);m_hostPid=pid;m_session=o["session"].toString();m_token=o["token"].toString();
        trace("connected",{{"pid",double(pid)},{"port",port},{"session",m_session}});
        send("hello");return true;
    }
    if(m_log.isOpen()&&(diagnostic!=lastDiagnostic||lastDiagnosticTask!=m_taskId)){lastDiagnostic=diagnostic;lastDiagnosticTask=m_taskId;trace("discovery",{{"diagnostic",diagnostic}});}
    return startingHostFound;
}
bool SysplorerRunner::launchHost(){
    if(m_remote){fail(QString::fromUtf8("远程模式只连接指定地址，不启动本机 Sysplorer。"));return false;}
    const auto exe=guiExecutable();
    if(exe.isEmpty()){connectionFailed("LOCAL_RUNTIME_NOT_CONFIGURED",QString::fromUtf8("尚未配置本机 Sysplorer。请使用完整客户端包的 Setup-Local.ps1 完成首次配置。"));return false;}
    const auto plugin=QFileInfo(exe).absolutePath()+"/addins/BatteryUdpControl/battery_udp_control.dll";
    if(!QFileInfo::exists(exe)){connectionFailed("HOST_NOT_FOUND",QString::fromUtf8("找不到本地 Sysplorer：%1").arg(exe));return false;}
    if(!QFileInfo::exists(plugin)){connectionFailed("PLUGIN_NOT_FOUND",QString::fromUtf8("未安装配套插件：%1。请先部署插件。").arg(plugin));return false;}
#ifdef Q_OS_WIN
    DWORD binaryType=0;
    if(!GetBinaryTypeW(reinterpret_cast<LPCWSTR>(exe.utf16()),&binaryType)||binaryType!=SCS_64BIT_BINARY){
        connectionFailed("HOST_START_FAILED",QString::fromUtf8("无法启动 Sysplorer：目标不是有效的 64 位 Windows 程序：%1").arg(exe));return false;
    }
#endif
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
    if(!process.startDetached(&m_launchedPid)){connectionFailed("HOST_START_FAILED",QString::fromUtf8("无法启动 Sysplorer：%1").arg(process.errorString()));return false;}
    m_launched=true;trace("launched",{{"pid",double(m_launchedPid)},{"workDir",work}});return true;
}
void SysplorerRunner::send(const QString&command,const QJsonObject&args){
    if(!m_pending.isEmpty()||!m_port)return;
    m_pendingId=uid();m_pendingCommand=command;
    QJsonObject r{{"schema",1},{"id",m_pendingId},{"session",m_session},{"token",m_token},{"client",m_clientId},
        {"taskId",m_taskId},{"command",command},{"args",args}};
    m_pending=QJsonDocument(r).toJson(QJsonDocument::Compact);m_attempts=1;m_pendingClock.start();
    transmitPending();
    if(command!="poll")trace("send",{{"command",command},{"id",m_pendingId}});
}
void SysplorerRunner::pump(){
    if(!m_busy&&!m_connectOnly)return;
    if(!m_remote&&!ensureLocalConfiguration())return;
    if(m_remote){
        if(m_remoteLost||m_phase=="DownloadFailed")return;
        if((m_connectOnly||!m_begun)&&m_clock.elapsed()>m_hostTimeoutMs){
            connectionFailed("HOST_TIMEOUT",QString::fromUtf8("远程 Sysplorer 初始化或模型同步超时，请检查服务器状态后重试。"));return;
        }
        if(!m_link){openRemote();return;}
        if(!m_link->isReady())return;
        if(!m_pending.isEmpty()){
            if(m_pendingClock.elapsed()>10000){
                if(m_attempts>=3){remoteProblem(QString::fromUtf8("远程请求超时，原任务状态待核对。"));return;}
                ++m_attempts;m_pendingClock.restart();transmitPending();
            }return;
        }
        if(m_connectOnly||!m_begun){
            if(m_clock.elapsed()>m_hostTimeoutMs){connectionFailed("HOST_NOT_READY_TIMEOUT",QString::fromUtf8("远程 Sysplorer 就绪等待超时。"));return;}
            send("hello");return;
        }
        if(m_downloading){nextDownload();return;}
        if(m_phase=="Translating"&&m_phaseClock.elapsed()>300000){fail(QString::fromUtf8("模型翻译超时。"));return;}
        if(!m_control.isEmpty()){
            auto cmd=m_control;m_control.clear();send(cmd,cmd=="tune"?m_tuneArgs:(cmd=="speed"||cmd=="start")?QJsonObject{{"speed",m_speed}}:QJsonObject{});return;
        }
        send("poll",{{"cursor",m_cursor}});return;
    }
    if(m_connectOnly){
        if(!m_pending.isEmpty()){
            if(m_pendingClock.elapsed()>3000){m_pending.clear();m_port=0;}
            else return;
        }
        if(m_clock.elapsed()>m_hostTimeoutMs){connectionFailed("HOST_NOT_READY_TIMEOUT",QString::fromUtf8("等待本机 Sysplorer 就绪超时，请检查宿主初始化或插件状态。"));return;}
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
        if(m_clock.elapsed()>m_hostTimeoutMs){connectionFailed("HOST_NOT_READY_TIMEOUT",QString::fromUtf8("Sysplorer 插件连接超时，请检查插件部署与启动日志。"));return;}
        if(!discover()&&!m_launched)launchHost();return;
    }
    if(!m_begun){
        if(m_clock.elapsed()>m_hostTimeoutMs){connectionFailed("HOST_NOT_READY_TIMEOUT",QString::fromUtf8("Sysplorer 尚未就绪，请查看宿主输出。"));return;}
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
        const auto d=m_socket.receiveDatagram(16384);
        if(m_remote||(!m_busy&&!m_connectOnly)||m_pending.isEmpty()||d.senderAddress()!=QHostAddress::LocalHost||d.senderPort()!=m_port)continue;
        handleResponse(QJsonDocument::fromJson(d.data()).object());
    }
}
void SysplorerRunner::handleResponse(const QJsonObject& r){
    if(m_pending.isEmpty())return;
    if(r["schema"].toInt()!=1||r["id"].toString()!=m_pendingId||r["session"].toString()!=m_session
        ||r["taskId"].toString()!=m_taskId)return;
    const auto cmd=m_pendingCommand;m_pending.clear();m_pendingCommand.clear();
    if(cmd=="download"){consumeDownload(r);return;}
    if(m_connectOnly){
        if(r["ok"].toBool()&&r["ready"].toBool()){
            const auto hostPhase=r["status"].toObject()["phase"].toString();
            if(!checkCompatibility(r))return;
            if(r["controllerState"].toString()!="Idle"||(hostPhase!="Ready"&&hostPhase!="Loaded"&&hostPhase!="Completed"&&hostPhase!="Stopped"&&hostPhase!="Failed")){
                connectionFailed("HOST_BUSY",QString::fromUtf8("Sysplorer 正被其他任务占用，未接管该任务。"));return;
            }
            m_modelLoaded=r["modelLoaded"].toBool()&&r["modelHash"].toString()==modelFingerprint();m_connectOnly=false;m_timer.stop();m_connectionWarned=false;setPhase("Connected");emit connectionInfoChanged(connectionLabel());
        }else if(!r["ok"].toBool()&&!hostInitializing(r["code"].toString()))connectionFailed("HELLO_REJECTED",QString::fromUtf8("宿主拒绝连接查询：%1 %2").arg(r["code"].toString(),r["message"].toString()));
        return;
    }
    if(!r["ok"].toBool()){
        auto code=r["code"].toString();
        if((cmd=="hello"||cmd=="begin"||cmd=="load"||cmd=="translate")&&hostInitializing(code))return;
        if(cmd=="hello"){connectionFailed("HELLO_REJECTED",QString::fromUtf8("宿主拒绝连接查询：%1 %2").arg(code,r["message"].toString()));return;}
        if(cmd=="tune"){m_tunePending=false;emit tuneRejected(r["message"].toString());emit stateChanged(m_phase);return;}
        fail(QString::fromUtf8("Sysplorer 操作 %1 失败：%2。任务 %3；日志 %4/client.log").arg(cmd,code+": "+r["message"].toString(),m_taskId,m_jobDir));return;
    }
    if(cmd.startsWith("model.")){
        if(r["uploadId"].toString()!=m_uploadId||r["modelHash"].toString()!=m_modelHash){fail("MODEL_UPLOAD_RESPONSE_INVALID");return;}
        if(cmd=="model.begin")nextModelChunk();
        else if(cmd=="model.chunk"){
            if(r["offset"].toInt(-1)!=m_uploadOffset){fail("MODEL_UPLOAD_OFFSET_INVALID");return;}
            m_uploadOffset+=qMin(m_uploadChunk,m_modelSnapshot.size()-m_uploadOffset);nextModelChunk();
        }else if(cmd=="model.commit"){
            if(!r["committed"].toBool()){fail("MODEL_NOT_COMMITTED");return;}sendPreparation();
        }
        return;
    }
    if(cmd=="hello"){
        if(r["ready"].toBool()&&!checkCompatibility(r))return;
        const auto st=r["status"].toObject();
        const auto p=st["phase"].toString();
        const bool idlePhase=p=="Ready"||p=="Loaded"||p=="Completed"||p=="Stopped"||p=="Failed";
        // A bound plugin can still be initializing. Wait on this instance
        // instead of classifying initialization as another client's task.
        if(idlePhase&&r["controllerState"].toString()=="Idle"&&!r["ready"].toBool())return;
        if((p!="Ready"&&p!="Loaded"&&p!="Completed"&&p!="Stopped"&&p!="Failed")||r["controllerState"].toString()!="Idle"
            ||(!m_remote&&!m_launchedPid&&((p=="Failed"&&!st["code"].toString().startsWith("MODEL_"))||!r["ready"].toBool()))){
            if(m_remote||m_launchedPid){connectionFailed("HOST_BUSY",m_remote?QString::fromUtf8("远程 Sysplorer 正被其他任务占用，未接管该任务。")
                :QString::fromUtf8("新启动的 Sysplorer 求解器仍被占用，请查看宿主任务。"));return;}
            trace("host_busy_skipped",{{"pid",double(m_hostPid)},{"phase",p},{"controllerState",r["controllerState"]}});
            m_busyHosts.insert(m_hostPid);m_port=0;m_hostPid=0;m_session.clear();m_token.clear();
            // Keep the other client's simulation intact. Discovery tries
            // other hosts, then starts one dedicated instance if needed.
            return;
        }
        if(!r["ready"].toBool())return;
        m_modelLoaded=r["modelLoaded"].toBool();
        send("model.begin",{{"uploadId",m_uploadId},{"modelTransferVersion",1},{"modelInterface",ModelTransfer::interfaceName()},
            {"bytes",m_modelSnapshot.size()},{"sha256",m_modelHash},{"chunkSize",m_uploadChunk}});
    }else{
        if(cmd=="discard"){
            m_busy=false;m_timer.stop();m_begun=false;setPhase("Connected");return;
        }
        if(cmd=="begin"||cmd=="load"||cmd=="translate")m_begun=true;
        consume(r["status"].toObject());
        if(m_busy)emit stateChanged(m_phase);
    }
}
void SysplorerRunner::consume(const QJsonObject&s){
    if(s["taskId"].toString()!=m_taskId||s["owner"].toString()!=m_clientId||s["modelHash"].toString()!=m_modelHash){
        fail(QString::fromUtf8("收到不属于本次任务的结果。"));return;
    }
    if(!s["jobDir"].toString().isEmpty()){
        if(m_remote)m_serverJobDir=s["jobDir"].toString();else m_jobDir=s["jobDir"].toString();
    }
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

bool SysplorerRunner::checkCompatibility(const QJsonObject& response){
    QStringList missing;QJsonArray fields;
    for(const auto name:{"modelLifecycle","splitLifecycle"})if(!response[name].toBool()){missing<<QString::fromLatin1(name);fields.append(QString::fromLatin1(name));}
    QStringList reasons;
    if(!missing.isEmpty())reasons<<QString::fromUtf8("缺少插件能力：%1（要求为 true）").arg(missing.join(", "));
    if(response["modelTransferVersion"].toInt()!=1||response["modelInterface"].toString()!=ModelTransfer::interfaceName())
        reasons<<QString::fromUtf8("插件不支持模型上传接口 modelTransferVersion=1 / battery-console/1，请更新配套插件。");
    if(reasons.isEmpty())return true;
    QJsonObject details{{"missingCapabilities",fields},{"modelTransferVersion",response["modelTransferVersion"]},{"modelInterface",response["modelInterface"]}};
    QString target;
    if(!m_remote){
        const auto plugin=localPluginDetails(m_hostPid,guiExecutable());
        for(auto it=plugin.begin();it!=plugin.end();++it)details.insert(it.key(),it.value());
        target=QString::fromUtf8("\n目标本机宿主（PID %1）：%2\n%3：%4")
            .arg(m_hostPid).arg(QDir::toNativeSeparators(guiExecutable()))
            .arg(plugin["pluginPathFromProcess"].toBool()?QString::fromUtf8("进程已加载插件"):QString::fromUtf8("预期插件路径"))
            .arg(plugin["pluginPath"].toString());
    }
    connectionFailed(missing.isEmpty()?"MODEL_MISMATCH":"CAPABILITY_MISMATCH",
        QString::fromUtf8("连接失败：宿主插件不兼容。\n%1%2\n请退出目标安装目录下全部 Sysplorer，使用配套服务器包中的 Update-Plugin.ps1 同步 DLL，再重新连接。").arg(reasons.join("\n"),target),details);
    return false;
}
void SysplorerRunner::connectionFailed(const QString& code,const QString& message,const QJsonObject& details){
    if(m_phase=="ConnectionFailed")return;
    const bool task=m_busy;auto diagnostic=details;
    diagnostic["code"]=code;diagnostic["message"]=message;diagnostic["mode"]=m_remote?"remote":"local";
    diagnostic["hostExecutable"]=m_remote?QString():guiExecutable();diagnostic["hostPid"]=double(m_hostPid);
    diagnostic["address"]=m_remote?m_remoteAddress:QString("127.0.0.1");diagnostic["port"]=m_port;
    trace("connection_failed",diagnostic);
    m_timer.stop();m_busy=false;m_connectOnly=false;m_begun=false;m_modelLoaded=false;
    m_pending.clear();m_pendingCommand.clear();m_pendingId.clear();m_control.clear();m_tunePending=false;
    m_remoteLost=m_remote;m_resumePhase.clear();m_port=0;m_session.clear();m_token.clear();closeRemote();
    setPhase("ConnectionFailed");m_log.close();emit connectionInfoChanged(connectionLabel());
    if(task)emit finished(false,{},message);else emit hostConnectionFailed(message);
}
void SysplorerRunner::finalize(const QJsonObject&s){
    if(m_remote&&!m_downloadReady){if(!m_downloading)beginDownload(s);return;}
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
    if(m_remote)setPhase(complete?"Completed":"Stopped");
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
    if(m_remote){if(m_link&&m_link->isReady())m_link->send(r);return;}
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
    if(m_connectOnly||(!m_begun&&m_phase=="Connecting"&&(m_pending.isEmpty()||m_pendingCommand=="hello"))){
        connectionFailed("CONNECTION_FAILED",message);return;
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

QString SysplorerRunner::connectionLabel() const {
    if(m_phase=="ConnectionFailed")return m_remote?QString::fromUtf8("远程 %1:%2 · 连接失败").arg(m_remoteAddress).arg(m_remotePort):QString::fromUtf8("本机模式 · 连接失败");
    if(!m_remote)return m_phase=="Idle"?QString::fromUtf8("本机模式 · 未连接，操作时按需启动"):
        (m_connectOnly?QString::fromUtf8("本机模式 · 正在连接 Sysplorer"):QString::fromUtf8("本机模式 · 按需连接 Sysplorer"));
    return QString::fromUtf8("远程 %1:%2 · %3").arg(m_remoteAddress).arg(m_remotePort)
        .arg(m_remoteLost?QString::fromUtf8("连接中断，任务状态待确认"):(m_link&&m_link->isReady()?QString::fromUtf8("已认证连接"):QString::fromUtf8("未连接")));
}
bool SysplorerRunner::configureConnection(bool remote,const QString& address,int port,const QString& key,bool persist){
    const auto chosenKey=key.isEmpty()?m_accessKey:key;
    if(m_busy||port<1||port>65535||(remote&&(address.trimmed().isEmpty()||chosenKey.size()<32)))return false;
    const auto protectedKey=!persist||chosenKey.isEmpty()?QByteArray():protectKey(chosenKey.toUtf8(),true);
    if(persist&&!chosenKey.isEmpty()&&protectedKey.isEmpty())return false;
    m_timer.stop();closeRemote();m_connectOnly=false;m_pending.clear();m_pendingCommand.clear();m_control.clear();m_session.clear();
    m_remote=remote;m_remoteAddress=address.trimmed();m_remotePort=quint16(port);m_accessKey=chosenKey;m_remoteLost=false;m_modelLoaded=false;
    if(persist){
        QSettings saved(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","RemoteConnection");
        saved.setValue("Configured",true);saved.setValue("Remote",remote);saved.setValue("Address",m_remoteAddress);saved.setValue("Port",port);
        saved.setValue("ProtectedKey",protectedKey.toBase64());saved.sync();if(saved.status()!=QSettings::NoError)return false;
    }
    m_phase="Idle";emit connectionInfoChanged(connectionLabel());return true;
}
void SysplorerRunner::closeRemote(bool graceful){
    m_closingRemote=true;
    if(m_link){
        auto link=m_link.data();m_link=nullptr;disconnect(link,nullptr,this,nullptr);
        if(graceful&&link->socket()->state()==QAbstractSocket::ConnectedState){
            link->socket()->disconnectFromHost();
            if(link->socket()->state()!=QAbstractSocket::UnconnectedState)link->socket()->waitForDisconnected(150);
        }else link->socket()->abort();
        link->deleteLater();
    }
    m_closingRemote=false;
}
void SysplorerRunner::openRemote(){
    if(m_link||m_remoteLost)return;
    if(m_remoteAddress.isEmpty()||m_accessKey.size()<32){remoteProblem(QString::fromUtf8("请在连接设置中填写服务器地址、端口，以及至少 32 个字符的访问凭据。"));return;}
    auto socket=new QTcpSocket;auto link=new RemoteLink(socket,false,m_accessKey,m_clientId,QString(),this);m_link=link;m_port=m_remotePort;
    connect(socket,&QTcpSocket::connected,link,&RemoteLink::start);
    connect(link,&RemoteLink::ready,this,[this,link]{
        if(m_link!=link)return;
        if(m_busy&&!m_session.isEmpty()&&m_session!=link->session()){
            // The original host was restarted: never send old controls to its new session.
            m_begun=false;fail(QString::fromUtf8("远程 Sysplorer 会话已更换，原任务无法恢复。请确认服务器结果后重新开始。"));closeRemote();return;
        }
        m_session=link->session();m_token.clear();m_port=m_remotePort;m_remoteLost=false;m_connectionWarned=false;
        emit connectionInfoChanged(connectionLabel());
        if(m_busy&&!m_resumePhase.isEmpty()){
            // Poll first on an empty queue; a pending request retains its original id and bytes.
            m_phase="Reconnecting";emit stateChanged(m_phase);m_resumePhase.clear();
        }
        if(!m_pending.isEmpty()){m_attempts=1;m_pendingClock.restart();transmitPending();}
        m_timer.start();pump();
    });
    connect(link,&RemoteLink::message,this,&SysplorerRunner::handleResponse);
    connect(link,&RemoteLink::failed,this,[this,link](const QString& reason){if(m_link==link)remoteProblem(reason);});
    connect(link,&RemoteLink::disconnected,this,[this,link]{if(m_link==link)remoteProblem(QString::fromUtf8("远程连接已关闭，请检查 Sysplorer、网络和双方访问凭据。"));});
    socket->connectToHost(m_remoteAddress,m_remotePort);
    QTimer::singleShot(10000,link,[this,link]{if(m_link==link&&!link->isReady())remoteProblem(QString::fromUtf8("远程连接或认证超时。"));});
    emit connectionInfoChanged(connectionLabel());
}
void SysplorerRunner::remoteProblem(const QString& reason){
    if(m_closingRemote||m_remoteLost||m_phase=="ConnectionFailed")return;
    const bool establishing=!m_begun&&(m_pending.isEmpty()||m_pendingCommand=="hello");
    if(!m_busy||establishing){
        QString detail=reason;
        if(reason=="AUTH_CONNECTION_CLOSED")detail=QString::fromUtf8("服务器在认证完成前关闭连接。请在连接设置中重新填写该服务器原 access-key.txt 中的访问凭据；留空会继续复用已保存的旧值。若凭据确认无误，再检查服务器认证日志和网络。");
        else if(reason=="AUTH_FAILED")detail=QString::fromUtf8("访问凭据校验失败。请重新填写该服务器原 access-key.txt 中的凭据；留空会继续使用已保存的值。");
        else if(reason=="CLOSED_BEFORE_CHALLENGE")detail=QString::fromUtf8("服务器在发送认证挑战前关闭连接。请核对 IP、端口及目标服务是否为配套 Sysplorer 插件。");
        connectionFailed(reason.contains("AUTH")?"AUTH_FAILED":"TRANSPORT_FAILED",QString::fromUtf8("远程连接失败：%1").arg(detail));return;
    }
    m_timer.stop();
    trace("remote_disconnected",{{"message",reason}});
    m_resumePhase=m_phase;m_remoteLost=true;setPhase("ConnectionLost");
    emit remoteNotice(QString::fromUtf8("远程连接中断：%1。原任务状态待确认，未重新启动仿真。请重连；服务器运行任务失联超过 15 秒时会请求暂停。").arg(reason));
    closeRemote();emit connectionInfoChanged(connectionLabel());
}
void SysplorerRunner::reconnectRemote(){
    if(!m_remote)return;
    if(!m_busy){connectHost();return;}
    m_remoteLost=false;closeRemote();m_clock.restart();setPhase("Reconnecting");openRemote();
}
void SysplorerRunner::abandonRemoteTask(){
    if(!m_remote||(!m_remoteLost&&m_phase!="DownloadFailed"))return;
    m_busy=false;m_connectOnly=false;m_begun=false;m_timer.stop();m_pending.clear();m_pendingCommand.clear();m_control.clear();
    m_downloading=false;m_downloadFile.reset();closeRemote();m_remoteLost=false;m_modelLoaded=false;m_log.close();setPhase("Disconnected");
    emit finished(false,{},QString::fromUtf8("已结束本次远程任务跟踪；服务器任务未被重新启动或强制停止。"));
}
void SysplorerRunner::transmitPending(){
    if(m_remote){if(m_link&&m_link->isReady())m_link->send(QJsonDocument::fromJson(m_pending).object());}
    else m_socket.writeDatagram(m_pending,QHostAddress::LocalHost,m_port);
}
void SysplorerRunner::beginDownload(const QJsonObject& status){
    m_finalStatus=status;m_downloading=true;m_downloadReady=false;m_downloadIndex=0;m_downloadOffset=0;m_downloadSize=-1;m_downloadFile.reset();
    trace("download_started",{{"serverDirectory",m_serverJobDir},{"localDirectory",m_jobDir}});
    setPhase("Downloading");m_timer.start();nextDownload();
}
void SysplorerRunner::retryDownload(){
    if(!canRetryDownload())return;
    if(!m_link||!m_link->isReady()){m_resumePhase="Downloading";m_remoteLost=true;reconnectRemote();return;}
    m_pending.clear();m_pendingCommand.clear();beginDownload(m_finalStatus);
}
void SysplorerRunner::nextDownload(){
    if(!m_downloading||!m_pending.isEmpty())return;
    if(m_downloadIndex>=2){m_downloading=false;m_downloadReady=true;finalize(m_finalStatus);return;}
    const auto name=m_downloadIndex==0?QString("result.json"):QString("result.csv");
    if(!m_downloadFile){
        m_downloadFile.reset(new QSaveFile(m_jobDir+"/"+name));
        if(!m_downloadFile->open(QIODevice::WriteOnly)){downloadProblem(QString::fromUtf8("无法写入本机结果目录：%1").arg(m_jobDir));return;}
    }
    send("download",{{"file",name},{"offset",double(m_downloadOffset)}});
}
void SysplorerRunner::consumeDownload(const QJsonObject& response){
    if(!m_downloading)return;
    setPhase("Downloading");
    if(!response["ok"].toBool()){downloadProblem(response["code"].toString());return;}
    const auto name=m_downloadIndex==0?QString("result.json"):QString("result.csv");
    const double size=response["size"].toDouble(-1),offset=response["offset"].toDouble(-1);
    const auto encoded=response["data"].toString().toLatin1();const auto bytes=QByteArray::fromBase64(encoded);
    if(response["file"].toString()!=name||offset!=m_downloadOffset||!std::isfinite(size)||size<1||size>128*1024*1024||size!=std::floor(size)
        ||(m_downloadSize>=0&&size!=m_downloadSize)||bytes.size()>48*1024||bytes.toBase64()!=encoded||hash(bytes)!=response["chunkHash"].toString()
        ||m_downloadOffset+bytes.size()>size||(!response["eof"].toBool()&&bytes.isEmpty())){
        downloadProblem(QString::fromUtf8("结果分块的标识、大小或哈希无效。"));return;
    }
    m_downloadSize=qint64(size);
    if(!m_downloadFile||m_downloadFile->write(bytes)!=bytes.size()){downloadProblem(QString::fromUtf8("本机结果写入失败。"));return;}
    m_downloadOffset+=bytes.size();emit downloadProgress(m_downloadOffset,m_downloadSize);
    if(response["eof"].toBool()){
        if(m_downloadOffset!=m_downloadSize||!m_downloadFile->commit()){downloadProblem(QString::fromUtf8("结果下载不完整或保存失败。"));return;}
        m_downloadFile.reset();++m_downloadIndex;m_downloadOffset=0;m_downloadSize=-1;
    }
    nextDownload();
}
void SysplorerRunner::downloadProblem(const QString& reason){
    trace("download_failed",{{"message",reason}});
    m_timer.stop();m_downloading=false;m_downloadFile.reset();m_pending.clear();m_pendingCommand.clear();setPhase("DownloadFailed");
    emit remoteNotice(QString::fromUtf8("仿真已结束，结果下载失败：%1。可重试下载，无需重新仿真。").arg(reason));
}

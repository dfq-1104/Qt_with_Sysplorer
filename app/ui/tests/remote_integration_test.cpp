#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QUuid>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QMessageAuthenticationCode>
#include <QMessageBox>
#include <QProcess>
#include <QtEndian>
#include "remote_server.h"
#include "sysplorer_runner.h"
#include "main_window.h"
#include "battery_view.h"
#include "replay_player.h"
#include "../../../tools/sysplorer_udp_plugin/task_status_compat.h"

namespace {
QString uid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QString sha(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
const QString key=QStringLiteral("test-only-access-key-0123456789abcdef");
bool mockTaskIdle(void*){return false;}
bool mockTaskBusy(void*){return true;}
void* mockNoThread(void*){return nullptr;}
void* mockHasThread(void* self){return self;}
quint16 freePort(){QTcpServer socket;if(!socket.listen(QHostAddress::LocalHost,0))return 0;return socket.serverPort();}
class FixtureHost:public QObject {
public:
    RemoteServer server;
    QString session=uid(),phase="Ready",task,owner,model,config;
    QJsonObject params;
    QByteArray csv,manifest;
    QList<QPointer<RemoteLink>> clients;
    bool autoComplete=true,failDownload=false,badChunk=false,modelLifecycle=true,splitLifecycle=true,ready=true,dropPreparationReply=false,helloApiError=false;
    int starts=0,polls=0,downloads=0,hellos=0,preparations=0;
    explicit FixtureHost(const QString& dir){
        SysplorerRunner fingerprint;model=fingerprint.modelFingerprint();
        config=dir+"/remote.ini";QSettings settings(config,QSettings::IniFormat);
        settings.setValue("Remote/Enabled",true);settings.setValue("Remote/ListenAddress","127.0.0.1");settings.setValue("Remote/Port",freePort());
        settings.setValue("Remote/AccessKey",key);settings.setValue("Remote/StatusFile",dir+"/service.json");settings.sync();
        QObject::connect(&server,&RemoteServer::request,this,[this](RemoteLink* link,const QJsonObject& r){
            if(!clients.contains(link))clients.append(link);
            QJsonObject out{{"ok",true}};const auto cmd=r["command"].toString();
            if(cmd=="hello"){++hellos;out={{"ok",true},{"ready",ready},{"splitLifecycle",splitLifecycle},{"modelLifecycle",modelLifecycle},{"modelLoaded",false},{"modelHash",model},{"controllerState",phase=="Running"?"Running":"Idle"},{"status",snapshot(0)}};}
            else if(cmd=="translate"||cmd=="load"){
                ++preparations;
                task=r["taskId"].toString();owner=r["client"].toString();params=r["args"].toObject();phase=cmd=="load"?"Loaded":"Prepared";polls=0;out["status"]=snapshot(0);
                if(dropPreparationReply){dropPreparationReply=false;link->socket()->abort();return;}
            }else if(r["taskId"].toString()!=task||r["client"].toString()!=owner){out={{"ok",false},{"code","TASK_OWNERSHIP_MISMATCH"}};}
            else if(cmd=="download"){
                ++downloads;if(failDownload){failDownload=false;out={{"ok",false},{"code","TEST_DOWNLOAD_INTERRUPTED"}};}
                else {const auto a=r["args"].toObject();const auto name=a["file"].toString();const auto data=name=="result.json"?manifest:csv;const int offset=a["offset"].toInt();const auto bytes=data.mid(offset,23);
                    out={{"ok",true},{"file",name},{"offset",offset},{"size",data.size()},{"data",QString::fromLatin1(bytes.toBase64())},{"eof",offset+bytes.size()==data.size()},{"chunkHash",badChunk?QString("invalid"):sha(bytes)}};badChunk=false;}
            }else {
                if(cmd=="start"){++starts;phase="Running";}
                if(cmd=="pause")phase="Paused";
                if(cmd=="resume")phase="Running";
                if(cmd=="discard")phase="Ready";
                if(cmd=="stop"){phase="Stopped";save(false);}
                if(cmd=="poll"&&autoComplete&&phase=="Running"&&++polls>=3){phase="Completed";save(true);}
                out["status"]=snapshot(r["args"].toObject()["cursor"].toInt());
            }
            if(cmd=="hello"&&helloApiError){out["ok"]=false;out["ready"]=false;out["code"]="HOST_TASK_STATUS_API_UNSUPPORTED";}
            out["schema"]=1;out["id"]=r["id"];out["session"]=session;out["taskId"]=r["taskId"];out["command"]=cmd;link->send(out);
        });
        server.start(config,session);
    }
    QJsonObject snapshot(int cursor){
        const bool samples=phase=="Running"||phase=="Paused"||phase=="Stopped"||phase=="Completed";
        const int total=samples?2:0;cursor=qMin(cursor,total);QJsonArray rows;
        const double duration=params["duration"].toDouble(2),soc=params["soc0"].toDouble(.5),amps=params["current"].toDouble(2);
        for(int i=cursor;i<total;++i)rows.append(QJsonArray{i?duration:0.,soc+i*.0001,amps,3.1});
        return {{"phase",phase},{"taskId",task},{"owner",owner},{"modelHash",model},{"modelLoaded",phase!="Ready"},{"params",params},
            {"cursor",cursor},{"next",total},{"total",total},{"rows",rows},{"jobDir","Z:/server-only/never-read-by-client"},{"csvHash",sha(csv)}};
    }
    void save(bool complete){
        csv=QString("time,SOC,I_act,V_term\n0,%1,%2,3.1\n%3,%4,%2,3.1\n").arg(params["soc0"].toDouble(),0,'g',15).arg(params["current"].toDouble(),0,'g',15)
            .arg(params["duration"].toDouble(),0,'g',15).arg(params["soc0"].toDouble()+.0001,0,'g',15).toLatin1();
        manifest=QJsonDocument(QJsonObject{{"taskId",task},{"modelHash",model},{"complete",complete},{"params",params},{"points",2},{"csvHash",sha(csv)}}).toJson();
    }
    void drop(){for(auto client:clients)if(client)client->socket()->abort();}
};
}
class RemoteIntegration:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> dir;
    QMap<QByteArray,QByteArray> environment;
    bool installLaunchFixture(QString& marker){
        const auto source=QCoreApplication::applicationDirPath()+"/../../build_startup_fixture/release/mworks.exe";
        const auto home=dir->path()+"/launch-"+uid();
        if(!QDir().mkpath(home+"/Bin64/addins/BatteryUdpControl")||!QFile::copy(source,home+"/Bin64/mworks.exe"))return false;
        QFile plugin(home+"/Bin64/addins/BatteryUdpControl/battery_udp_control.dll");if(!plugin.open(QIODevice::WriteOnly))return false;plugin.write("launch-fixture-only");plugin.close();
        marker=home+"/launched.jsonl";
        const auto relativeHome=QDir::current().relativeFilePath(home);
        qputenv("SYSPLORER_HOME",relativeHome.toLatin1());qputenv("BATTERY_LAUNCH_SENTINEL",marker.toUtf8().toBase64());
        qputenv("BATTERY_DISCOVERY_DIR",(relativeHome+"/discovery").toLatin1());
        // A relative ASCII setting keeps fixture artifacts in the worktree. No
        // compiler is run by this recorder, even when the physical path is Unicode.
        qputenv("BATTERY_SIM_WORKSPACE",QDir::current().relativeFilePath(home+"/jobs").toUtf8());
        return qEnvironmentVariable("SYSPLORER_HOME")==relativeHome&&qEnvironmentVariable("BATTERY_DISCOVERY_DIR")==relativeHome+"/discovery";
    }
private slots:
    void taskStatusCompatibility_data(){
        QTest::addColumn<int>("api");QTest::addColumn<bool>("busy");for(int api:{0,1,2,3})for(bool busy:{false,true})QTest::newRow(qPrintable(QString::number(api)+(busy?"-busy":"-idle")))<<api<<busy;
    }
    void taskStatusCompatibility(){
        QFETCH(int,api);QFETCH(bool,busy);const char* names[]={"?IsRunning@MwTaskManager@@QEAA_NXZ","?IsRunning@MwTaskManager@@QEBA_NXZ","?GetRunningThread@MwTaskManager@@QEAAPEAVMwThread@@XZ","?GetRunningThread@MwTaskManager@@QEBAPEAVMwThread@@XZ"};
        int manager=0;using Entry=void(*)();auto resolve=[&](const char* name)->Entry{
            if(QByteArray(name)!=names[api])return nullptr;
            return api<2?(busy?reinterpret_cast<Entry>(&mockTaskBusy):reinterpret_cast<Entry>(&mockTaskIdle)):(busy?reinterpret_cast<Entry>(&mockHasThread):reinterpret_cast<Entry>(&mockNoThread));
        };
        QCOMPARE(batteryHostTaskRunning(&manager,resolve),busy);
    }
    void taskStatusUnknownIsRejected(){
        int manager=0;using Entry=void(*)();auto missing=[](const char*)->Entry{return nullptr;};
        QVERIFY_EXCEPTION_THROWN(batteryHostTaskRunning(&manager,missing),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(batteryHostTaskRunning(nullptr,missing),std::runtime_error);
        auto preferred=[](const char* name)->Entry{return QByteArray(name).startsWith("?IsRunning@")?reinterpret_cast<Entry>(&mockTaskIdle):reinterpret_cast<Entry>(&mockHasThread);};
        QVERIFY(!batteryHostTaskRunning(&manager,preferred));
    }
    void modelPreflightGate_data(){
        QTest::addColumn<int>("defect");for(int defect:{0,1,2,3,4})QTest::newRow(qPrintable(QString::number(defect)))<<defect;
    }
    void modelPreflightGate(){
        QFETCH(int,defect);FixtureHost host(dir->path());if(defect==1||defect==3)host.model="old-model";if(defect==2||defect==3)host.modelLifecycle=false;host.helloApiError=defect==4;
        const auto credential=dir->path()+"/preflight-test-key.txt",report=dir->path()+"/preflight-report.json";
        QFile file(credential);QVERIFY(file.open(QIODevice::WriteOnly));file.write(key.toUtf8());file.close();
        QProcess probe;probe.setProgram(qEnvironmentVariable("SystemRoot")+"/System32/WindowsPowerShell/v1.0/powershell.exe");
        probe.setArguments({"-NoProfile","-ExecutionPolicy","Bypass","-File",QDir::current().absoluteFilePath("tools/deployment/Test-RemoteModel.ps1"),"-Address","127.0.0.1","-Port",QString::number(host.server.port()),"-KeyFile",credential,"-OutputFile",report});
        probe.start();QVERIFY(probe.waitForStarted());QTRY_COMPARE_WITH_TIMEOUT(probe.state(),QProcess::NotRunning,15000);
        QFile result(report);QVERIFY2(result.open(QIODevice::ReadOnly),qPrintable(QString::fromLocal8Bit(probe.readAllStandardError())));auto bytes=result.readAll();if(bytes.startsWith("\xef\xbb\xbf"))bytes.remove(0,3);
        const auto diagnostic=QJsonDocument::fromJson(bytes).object();QVERIFY2(diagnostic["authenticated"].toBool(),qPrintable(QString::fromLocal8Bit(probe.readAllStandardError())));
        QVERIFY(diagnostic["modelChecked"].toBool());QCOMPARE(diagnostic["modelMatches"].toBool(),defect!=1&&defect!=3);QCOMPARE(diagnostic["capabilitiesMatch"].toBool(),defect!=2&&defect!=3);
        QCOMPARE(diagnostic["capabilities"].toObject()["modelLifecycle"].toBool(),defect!=2&&defect!=3);QVERIFY(diagnostic["capabilities"].toObject()["splitLifecycle"].toBool());
        QCOMPARE(diagnostic["lastTask"].toObject()["phase"].toString(),QString("Ready"));
        QCOMPARE(probe.exitCode(),defect==0?0:1);QCOMPARE(host.hellos,1);QCOMPARE(host.preparations,0);QCOMPARE(host.starts,0);QVERIFY(!bytes.contains(key.toUtf8()));
    }
    void initTestCase(){
        const auto base=qEnvironmentVariable("BATTERY_TEST_ROOT",QDir::tempPath());QDir().mkpath(base);dir.reset(new QTemporaryDir(base+"/remote-XXXXXX"));QVERIFY(dir->isValid());
        dir->setAutoRemove(false);qRegisterMetaType<RemoteLink*>("RemoteLink*");QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir->path()+"/settings");
        qputenv("BATTERY_CONNECTION_MODE","local");qputenv("BATTERY_SIM_WORKSPACE",QDir::current().relativeFilePath(dir->path()+"/client-results").toUtf8());
    }
    void init(){
        environment.clear();
        for(const auto name:{"SYSPLORER_HOME","BATTERY_LAUNCH_SENTINEL","BATTERY_SIM_WORKSPACE","BATTERY_DISCOVERY_DIR","BATTERY_CONNECTION_MODE","BATTERY_REMOTE_ADDRESS","BATTERY_REMOTE_PORT","BATTERY_REMOTE_KEY","BATTERY_HOST_TIMEOUT_MS","BATTERY_FIXTURE_UDP","BATTERY_FIXTURE_MODEL_HASH","BATTERY_FIXTURE_MISSING_MODEL","BATTERY_FIXTURE_MISSING_SPLIT","BATTERY_FIXTURE_READY_AFTER_MS","BATTERY_FIXTURE_BUSY"})environment.insert(name,qgetenv(name));
    }
    void cleanup(){
        for(auto it=environment.cbegin();it!=environment.cend();++it){if(it.value().isNull())qunsetenv(it.key().constData());else qputenv(it.key().constData(),it.value());}
        QSettings settings(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","RemoteConnection");settings.clear();settings.sync();
    }
    void localStartupDoesNotLaunchHost(){
        QString marker;QVERIFY(installLaunchFixture(marker));MainWindow window;window.show();auto runner=window.findChild<SysplorerRunner*>();
        QSignalSpy phases(runner,&SysplorerRunner::stateChanged);QTest::qWait(1200);
        QCOMPARE(runner->phase(),QString("Idle"));QVERIFY(!runner->isConnecting());QVERIFY(!runner->isRunning());QVERIFY(runner->taskId().isEmpty());QVERIFY(runner->lastRunDirectory().isEmpty());
        QCOMPARE(phases.count(),0);QVERIFY(!QFileInfo::exists(marker));QVERIFY(!window.findChild<QPlainTextEdit*>("errorPanel")->isVisible());
        for(const auto name:{"btnLoad","btnPrepare","btnPlay"})QVERIFY(window.findChild<QPushButton*>(name)->isEnabled());
    }
    void localActionsLaunchOnDemand_data(){
        QTest::addColumn<QString>("action");for(const auto action:{"connect","btnLoad","btnPrepare","btnPlay"})QTest::newRow(action)<<QString(action);
    }
    void localActionsLaunchOnDemand(){
        QFETCH(QString,action);QString marker;QVERIFY(installLaunchFixture(marker));MainWindow window;window.show();QTest::qWait(150);QVERIFY(!QFileInfo::exists(marker));
        if(action=="connect"){
            QTimer::singleShot(40,&window,[&window]{auto dialog=window.findChild<QDialog*>("connectionDialog");if(dialog)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
            window.findChild<QPushButton*>("btnConnection")->click();
        }else window.findChild<QPushButton*>(action)->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker),5000);QFile file(marker);QVERIFY(file.open(QIODevice::ReadOnly));const auto record=QJsonDocument::fromJson(file.readLine()).object();
        QVERIFY(record["pid"].toDouble()>0);QVERIFY(record["arguments"].toArray().contains("-d"));QVERIFY(record["arguments"].toArray().contains("-w"));
        QVERIFY(!window.findChild<SysplorerRunner*>()->remoteMode());
    }
    void remoteStartupWaitsForExplicitConnection(){
        QString marker;QVERIFY(installLaunchFixture(marker));FixtureHost host(dir->path());
        qputenv("BATTERY_CONNECTION_MODE","remote");qputenv("BATTERY_REMOTE_ADDRESS","127.0.0.1");qputenv("BATTERY_REMOTE_PORT",QByteArray::number(host.server.port()));qputenv("BATTERY_REMOTE_KEY",key.toUtf8());
        MainWindow window;window.show();auto runner=window.findChild<SysplorerRunner*>();QTest::qWait(1200);
        QCOMPARE(runner->phase(),QString("Idle"));QVERIFY(!runner->isConnecting());QVERIFY(host.clients.isEmpty());QVERIFY(host.server.findChildren<RemoteLink*>().isEmpty());QVERIFY(!QFileInfo::exists(marker));
        QTimer::singleShot(40,&window,[&window]{auto dialog=window.findChild<QDialog*>("connectionDialog");if(dialog)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        window.findChild<QPushButton*>("btnConnection")->click();QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Connected"),5000);QVERIFY(!QFileInfo::exists(marker));
    }
    void remoteFailureNeverLaunchesLocalHost(){
        QString marker;QVERIFY(installLaunchFixture(marker));SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",freePort(),key,false));
        QSignalSpy failed(&runner,&SysplorerRunner::hostConnectionFailed);runner.connectHost();QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);QTest::qWait(300);
        QVERIFY(runner.remoteMode());QCOMPARE(runner.phase(),QString("ConnectionFailed"));QVERIFY(!QFileInfo::exists(marker));
    }
    void compatibilityFailureStopsRequests_data(){
        QTest::addColumn<bool>("remote");QTest::addColumn<int>("defect");QTest::addColumn<bool>("prepare");
        for(bool remote:{false,true})for(int defect:{1,2,3,4})for(bool prepare:{false,true}){
            const auto name=QString("%1-%2-%3").arg(remote?"tcp":"udp").arg(defect).arg(prepare?"prepare":"connect");
            QTest::newRow(qPrintable(name))<<remote<<defect<<prepare;
        }
    }
    void compatibilityFailureStopsRequests(){
        QFETCH(bool,remote);QFETCH(int,defect);QFETCH(bool,prepare);
        SysplorerRunner runner;QString marker;QProcess fixture;std::unique_ptr<FixtureHost> host;
        if(remote){
            host.reset(new FixtureHost(dir->path()));host->modelLifecycle=defect!=1&&defect!=3;host->splitLifecycle=defect!=4;
            if(defect==2||defect==3)host->model="old-model";
            QVERIFY(runner.configureConnection(true,"127.0.0.1",host->server.port(),key,false));
        }else{
            QVERIFY(installLaunchFixture(marker));qputenv("BATTERY_FIXTURE_UDP","1");qputenv("BATTERY_FIXTURE_MODEL_HASH",(defect==2||defect==3)?QByteArray("old-model"):runner.modelFingerprint().toLatin1());
            if(defect==1||defect==3)qputenv("BATTERY_FIXTURE_MISSING_MODEL","1");if(defect==4)qputenv("BATTERY_FIXTURE_MISSING_SPLIT","1");
            fixture.start(QDir(qEnvironmentVariable("SYSPLORER_HOME")).absoluteFilePath("Bin64/mworks.exe"));QVERIFY(fixture.waitForStarted());
            QTRY_VERIFY(QDir(qEnvironmentVariable("BATTERY_DISCOVERY_DIR")).entryList({"*.json"},QDir::Files).size()==1);
        }
        QSignalSpy failed(&runner,&SysplorerRunner::hostConnectionFailed),finished(&runner,&SysplorerRunner::finished);
        if(prepare)runner.prepareSimulation(3.45,2,1,.5,2);else runner.connectHost();
        QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("ConnectionFailed"),5000);QVERIFY(!runner.isConnecting());QVERIFY(!runner.isRunning());QVERIFY(!runner.controlPending());
        QCOMPARE(failed.count(),prepare?0:1);QCOMPARE(finished.count(),prepare?1:0);
        const auto error=prepare?finished[0][2].toString():failed[0][0].toString();
        QVERIFY(!error.contains(QString::fromUtf8("未能打开")));if(defect!=2)QVERIFY(error.contains(defect==4?"splitLifecycle":"modelLifecycle"));
        if(!remote){QVERIFY(error.contains(QDir::toNativeSeparators(QDir(qEnvironmentVariable("SYSPLORER_HOME")).absoluteFilePath("Bin64/mworks.exe"))));QVERIFY(error.contains("Update-Plugin.ps1"));}
        if(defect==2||defect==3){QVERIFY(error.contains("old-model"));QVERIFY(error.contains(runner.modelFingerprint()));}
        QFile log(runner.lastRunDirectory()+"/client.log");QVERIFY(log.open(QIODevice::ReadOnly));const auto before=log.readAll();
        if(!remote){bool found=false;for(const auto line:before.split('\n')){const auto row=QJsonDocument::fromJson(line).object();if(row["event"].toString()=="connection_failed"){found=true;QVERIFY(row["pluginPath"].toString().endsWith("battery_udp_control.dll"));QCOMPARE(row["pluginDiskSha256"].toString(),sha("launch-fixture-only"));}}QVERIFY(found);}
        QTest::qWait(220);
        log.seek(0);QCOMPARE(log.readAll(),before);
        if(remote){QCOMPARE(host->hellos,1);QCOMPARE(host->preparations,0);QCOMPARE(host->starts,0);}
        else{QFile recorded(marker);QVERIFY(recorded.open(QIODevice::ReadOnly));const auto data=recorded.readAll();QCOMPARE(data.count("\"arguments\""),1);QCOMPARE(data.count("\"command\":\"hello\""),1);QVERIFY(!data.contains("\"command\":\"translate\""));fixture.kill();QVERIFY(fixture.waitForFinished(3000));}
    }
    void localInitializationBusyTimeoutAndRetry_data(){
        QTest::addColumn<QString>("scenario");for(const auto name:{"initialize","busy","timeout","retry"})QTest::newRow(name)<<QString(name);
    }
    void localUiCompatibilityHasTarget(){
        QString marker;QVERIFY(installLaunchFixture(marker));qputenv("BATTERY_FIXTURE_UDP","1");qputenv("BATTERY_FIXTURE_MODEL_HASH","old-model");qputenv("BATTERY_FIXTURE_MISSING_MODEL","1");
        QProcess fixture;fixture.start(QDir(qEnvironmentVariable("SYSPLORER_HOME")).absoluteFilePath("Bin64/mworks.exe"));QVERIFY(fixture.waitForStarted());
        QTRY_VERIFY(QDir(qEnvironmentVariable("BATTERY_DISCOVERY_DIR")).entryList({"*.json"},QDir::Files).size()==1);
        MainWindow window;window.show();auto runner=window.findChild<SysplorerRunner*>();runner->connectHost();QTRY_COMPARE(runner->phase(),QString("ConnectionFailed"));
        auto errors=window.findChild<QPlainTextEdit*>("errorPanel");QVERIFY(errors->toPlainText().contains("old-model"));QVERIFY(errors->toPlainText().contains(QString::number(fixture.processId())));
        QVERIFY(errors->toPlainText().contains("battery_udp_control.dll"));QVERIFY(window.findChild<QLabel*>("connectionStatus")->text().contains(QString::fromUtf8("连接失败")));
        QVERIFY(!runner->remoteMode());for(const auto name:{"btnLoad","btnPrepare","btnPlay","btnConnection"})QVERIFY(window.findChild<QPushButton*>(name)->isEnabled());
        for(auto dialog:window.findChildren<QMessageBox*>())dialog->close();QTest::qWait(50);window.grab().save(dir->path()+"/local-compatibility-target.png");
        fixture.kill();QVERIFY(fixture.waitForFinished(3000));
    }
    void localInitializationBusyTimeoutAndRetry(){
        QFETCH(QString,scenario);QString marker;QVERIFY(installLaunchFixture(marker));qputenv("BATTERY_FIXTURE_UDP","1");
        SysplorerRunner fingerprint;qputenv("BATTERY_FIXTURE_MODEL_HASH",fingerprint.modelFingerprint().toLatin1());
        if(scenario=="initialize")qputenv("BATTERY_FIXTURE_READY_AFTER_MS","500");
        if(scenario=="busy")qputenv("BATTERY_FIXTURE_BUSY","1");
        if(scenario=="timeout"){qputenv("BATTERY_FIXTURE_READY_AFTER_MS","100000");qputenv("BATTERY_HOST_TIMEOUT_MS","1000");}
        if(scenario=="retry")qputenv("BATTERY_FIXTURE_MISSING_MODEL","1");
        const auto executable=QDir(qEnvironmentVariable("SYSPLORER_HOME")).absoluteFilePath("Bin64/mworks.exe");
        QProcess fixture;fixture.start(executable);QVERIFY(fixture.waitForStarted());
        QTRY_VERIFY(QDir(qEnvironmentVariable("BATTERY_DISCOVERY_DIR")).entryList({"*.json"},QDir::Files).size()==1);
        SysplorerRunner runner;QSignalSpy failed(&runner,&SysplorerRunner::hostConnectionFailed);runner.connectHost();
        if(scenario=="initialize"){
            QTest::qWait(100);QCOMPARE(runner.phase(),QString("Connecting"));QCOMPARE(failed.count(),0);QTRY_COMPARE(runner.phase(),QString("Connected"));
        }else{
            QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("ConnectionFailed"),3000);QCOMPARE(failed.count(),1);QVERIFY(!runner.isConnecting());
            if(scenario=="busy")QVERIFY(failed[0][0].toString().contains(QString::fromUtf8("占用")));
            if(scenario=="timeout")QVERIFY(failed[0][0].toString().contains(QString::fromUtf8("超时")));
            QFile log(runner.lastRunDirectory()+"/client.log");QVERIFY(log.open(QIODevice::ReadOnly));const auto before=log.readAll();QTest::qWait(250);log.seek(0);QCOMPARE(log.readAll(),before);
        }
        fixture.kill();QVERIFY(fixture.waitForFinished(3000));
        if(scenario=="retry"){
            qunsetenv("BATTERY_FIXTURE_MISSING_MODEL");fixture.start(executable);QVERIFY(fixture.waitForStarted());QTest::qWait(150);
            runner.connectHost();QTRY_COMPARE(runner.phase(),QString("Connected"));QCOMPARE(failed.count(),1);fixture.kill();QVERIFY(fixture.waitForFinished(3000));
        }
    }
    void connectionFailureKeepsUiAndResults(){
        FixtureHost host(dir->path());qputenv("BATTERY_CONNECTION_MODE","remote");qputenv("BATTERY_REMOTE_ADDRESS","127.0.0.1");
        qputenv("BATTERY_REMOTE_PORT",QByteArray::number(host.server.port()));qputenv("BATTERY_REMOTE_KEY",key.toUtf8());
        MainWindow window;window.show();auto runner=window.findChild<SysplorerRunner*>();runner->connectHost();QTRY_COMPARE(runner->phase(),QString("Connected"));
        QSignalSpy done(runner,&SysplorerRunner::finished);window.findChild<QPushButton*>("btnPlay")->click();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(done[0][0].toBool());
        auto selector=window.findChild<QComboBox*>("cmbResults");auto player=window.findChild<ReplayPlayer*>();const auto saved=player->samples();QVERIFY(!saved.isEmpty());
        const auto path=done[0][1].toString();QFile csv(path);QVERIFY(csv.open(QIODevice::ReadOnly));const auto original=csv.readAll();
        host.modelLifecycle=false;window.findChild<QPushButton*>("btnPrepare")->click();QTRY_COMPARE(done.count(),2);QCOMPARE(runner->phase(),QString("ConnectionFailed"));
        QVERIFY(window.findChild<QLabel*>("lblState")->text().contains(QString::fromUtf8("连接失败")));
        auto error=window.findChild<QPlainTextEdit*>("errorPanel");QVERIFY(error->isVisible());QVERIFY(error->toPlainText().contains("modelLifecycle"));
        QVERIFY(window.findChild<QLabel*>("connectionStatus")->text().contains(QString::fromUtf8("连接失败")));
        QVERIFY(selector->findData("cache")>=0);QVERIFY(window.findChild<QPushButton*>("btnReplay")->isEnabled());
        QCOMPARE(player->samples().size(),saved.size());QCOMPARE(player->samples().first().soc,saved.first().soc);csv.seek(0);QCOMPARE(csv.readAll(),original);
        QVERIFY(window.findChild<QPushButton*>("btnConnection")->isEnabled());QVERIFY(!window.findChild<QPushButton*>("btnPlay")->isEnabled());QVERIFY(!window.findChild<QPushButton*>("btnPrepare")->isEnabled());
        window.grab().save(dir->path()+"/connection-failed.png");const int count=host.hellos;QTest::qWait(250);QCOMPARE(host.hellos,count);
        host.modelLifecycle=true;runner->reconnectRemote();QTRY_COMPARE(runner->phase(),QString("Connected"));QVERIFY(window.findChild<QPushButton*>("btnPlay")->isEnabled());
    }
    void initializationAndRetryRemainUsable(){
        FixtureHost host(dir->path());host.ready=false;SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        QSignalSpy failed(&runner,&SysplorerRunner::hostConnectionFailed);runner.connectHost();QTest::qWait(220);QCOMPARE(runner.phase(),QString("Connecting"));QCOMPARE(failed.count(),0);
        host.ready=true;host.modelLifecycle=false;QTRY_COMPARE(runner.phase(),QString("ConnectionFailed"));QCOMPARE(failed.count(),1);const int count=host.hellos;QTest::qWait(250);QCOMPARE(host.hellos,count);
        host.modelLifecycle=true;runner.connectHost();QTRY_COMPARE(runner.phase(),QString("Connected"));QCOMPARE(failed.count(),1);
    }
    void missingHostPluginAndTimeoutAreTerminal(){
        QString marker;QVERIFY(installLaunchFixture(marker));SysplorerRunner runner;QSignalSpy failures(&runner,&SysplorerRunner::hostConnectionFailed);
        const auto home=qgetenv("SYSPLORER_HOME");qputenv("SYSPLORER_HOME","C:/missing/sysplorer");runner.connectHost();QTRY_COMPARE(runner.phase(),QString("ConnectionFailed"));QVERIFY(failures.last()[0].toString().contains(QString::fromUtf8("找不到本地")));
        qputenv("SYSPLORER_HOME",home);QVERIFY(QFile::remove(QDir(QString::fromLatin1(home)).absoluteFilePath("Bin64/addins/BatteryUdpControl/battery_udp_control.dll")));
        runner.connectHost();QTRY_COMPARE(failures.count(),2);QVERIFY(failures.last()[0].toString().contains(QString::fromUtf8("未安装配套插件")));QVERIFY(!QFileInfo::exists(marker));
        FixtureHost host(dir->path());host.ready=false;qputenv("BATTERY_HOST_TIMEOUT_MS","1000");SysplorerRunner timed;QVERIFY(timed.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        QSignalSpy timeout(&timed,&SysplorerRunner::hostConnectionFailed);timed.connectHost();QTRY_COMPARE_WITH_TIMEOUT(timed.phase(),QString("ConnectionFailed"),3000);QCOMPARE(timeout.count(),1);
        const int count=host.hellos;QTest::qWait(250);QCOMPARE(host.hellos,count);
    }
    void fullResultIsDownloadedLocally(){
        FixtureHost host(dir->path());QVERIFY(host.server.isListening());SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        QSignalSpy done(&runner,&SysplorerRunner::finished);runner.prepareSimulation(3.45,2,1,.5,2);QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared(),5000);runner.beginSimulation();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));
        QCOMPARE(host.starts,1);QVERIFY(host.downloads>2);QVERIFY(QFileInfo(done[0][1].toString()).isFile());QVERIFY(runner.lastRunDirectory()!=runner.serverRunDirectory());
        QCOMPARE(runner.serverRunDirectory(),QString("Z:/server-only/never-read-by-client"));
    }
    void failedDownloadRetriesWithoutStartingAgain(){
        FixtureHost host(dir->path());host.failDownload=true;SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        QSignalSpy done(&runner,&SysplorerRunner::finished);runner.prepareSimulation(3.45,2,1,.5,2);QTRY_VERIFY(runner.isPrepared());runner.beginSimulation();
        QTRY_VERIFY_WITH_TIMEOUT(runner.canRetryDownload(),5000);QCOMPARE(done.count(),0);QVERIFY(runner.isRunning());runner.retryDownload();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(done[0][0].toBool());QCOMPARE(host.starts,1);
    }
    void corruptChunkIsRejected(){
        FixtureHost host(dir->path());host.badChunk=true;SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        QSignalSpy done(&runner,&SysplorerRunner::finished);runner.prepareSimulation(3.45,2,1,.5,2);QTRY_VERIFY(runner.isPrepared());runner.beginSimulation();
        QTRY_VERIFY(runner.canRetryDownload());QCOMPARE(done.count(),0);runner.retryDownload();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(done[0][0].toBool());
    }
    void reconnectKeepsTaskAndControls(){
        FixtureHost host(dir->path());host.autoComplete=false;SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        runner.prepareSimulation(3.45,2,1,.5,2);QTRY_VERIFY(runner.isPrepared());runner.beginSimulation();QTRY_COMPARE(runner.phase(),QString("Running"));QTRY_VERIFY(!runner.liveSamples().isEmpty());
        const auto task=runner.taskId();host.drop();QTRY_VERIFY(runner.canReconnect());QCOMPARE(runner.phase(),QString("ConnectionLost"));
        host.phase="Paused";runner.reconnectRemote();QTRY_COMPARE(runner.phase(),QString("Paused"));QCOMPARE(runner.taskId(),task);QCOMPARE(host.starts,1);
        runner.resume();QTRY_COMPARE(runner.phase(),QString("Running"));QSignalSpy stopped(&runner,&SysplorerRunner::stopped);runner.stop();QTRY_COMPARE_WITH_TIMEOUT(stopped.count(),1,10000);QCOMPARE(host.starts,1);
    }
    void lostPreparationAcknowledgementKeepsRecovery(){
        FixtureHost host(dir->path());host.dropPreparationReply=true;SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        QSignalSpy done(&runner,&SysplorerRunner::finished);runner.prepareSimulation(3.45,2,1,.5,2);QTRY_COMPARE(runner.phase(),QString("ConnectionLost"));
        const auto task=runner.taskId();QVERIFY(runner.canReconnect());QCOMPARE(done.count(),0);QCOMPARE(host.starts,0);
        runner.reconnectRemote();QTRY_VERIFY(runner.isPrepared());QCOMPARE(runner.taskId(),task);QCOMPARE(done.count(),0);QCOMPARE(host.starts,0);
    }
    void changedSessionDoesNotReplayStart(){
        FixtureHost host(dir->path());host.autoComplete=false;SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        QSignalSpy done(&runner,&SysplorerRunner::finished);runner.prepareSimulation(3.45,2,1,.5,2);QTRY_VERIFY(runner.isPrepared());runner.beginSimulation();QTRY_COMPARE(runner.phase(),QString("Running"));
        host.server.close();QTRY_VERIFY(runner.canReconnect());host.session=uid();QVERIFY(host.server.start(host.config,host.session));runner.reconnectRemote();
        QTRY_COMPARE(done.count(),1);QVERIFY(!done[0][0].toBool());QCOMPARE(host.starts,1);
    }
    void closingClientSendsStop(){
        FixtureHost host(dir->path());host.autoComplete=false;std::unique_ptr<SysplorerRunner> runner(new SysplorerRunner);
        QVERIFY(runner->configureConnection(true,"127.0.0.1",host.server.port(),key,false));runner->prepareSimulation(3.45,2,1,.5,2);
        QTRY_VERIFY(runner->isPrepared());runner->beginSimulation();QTRY_COMPARE(runner->phase(),QString("Running"));runner.reset();
        QTRY_COMPARE(host.phase,QString("Stopped"));QCOMPARE(host.starts,1);
    }
    void wrongKeyAndBusyHostAreExplicit(){
        FixtureHost host(dir->path());SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),QString(40,'x'),false));
        QSignalSpy rejected(&runner,&SysplorerRunner::hostConnectionFailed);runner.connectHost();QTRY_COMPARE_WITH_TIMEOUT(rejected.count(),1,5000);QCOMPARE(host.starts,0);
        QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));host.phase="Running";runner.connectHost();QTRY_COMPARE(rejected.count(),2);QCOMPARE(host.starts,0);
    }
    void rejectedCredentialStopsAndExplicitRetryConnects(){
        FixtureHost host(dir->path());SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),QString(40,'x'),false));
        QSignalSpy rejected(&runner,&SysplorerRunner::hostConnectionFailed);runner.connectHost();QTRY_COMPARE_WITH_TIMEOUT(rejected.count(),1,5000);
        QCOMPARE(runner.phase(),QString("ConnectionFailed"));const auto message=rejected[0][0].toString();
        QVERIFY(message.contains(QString::fromUtf8("认证")));QVERIFY(message.contains(QString::fromUtf8("访问凭据")));QVERIFY(message.contains(QString::fromUtf8("留空")));
        QTest::qWait(700);QCOMPARE(rejected.count(),1);QCOMPARE(host.hellos,0);QCOMPARE(host.starts,0);QCOMPARE(host.preparations,0);
        QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));runner.connectHost();QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("Connected"),5000);
        QVERIFY(runner.remoteMode());QCOMPARE(host.hellos,1);QCOMPARE(host.starts,0);QCOMPARE(host.preparations,0);
    }
    void closeBeforeChallengeRemainsTransportFailure(){
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost,0));
        connect(&server,&QTcpServer::newConnection,&server,[&]{auto socket=server.nextPendingConnection();QTimer::singleShot(30,socket,[socket]{socket->abort();socket->deleteLater();});});
        SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",server.serverPort(),key,false));QSignalSpy rejected(&runner,&SysplorerRunner::hostConnectionFailed);
        runner.connectHost();QTRY_COMPARE_WITH_TIMEOUT(rejected.count(),1,5000);QCOMPARE(runner.phase(),QString("ConnectionFailed"));
        QVERIFY(rejected[0][0].toString().contains(QString::fromUtf8("认证挑战前")));QVERIFY(!rejected[0][0].toString().contains(QString::fromUtf8("旧值")));
    }
    void idleReconnectClearsPreviousTaskContext(){
        FixtureHost host(dir->path());SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));
        runner.loadModel();QTRY_COMPARE(runner.phase(),QString("Loaded"));QVERIFY(!runner.taskId().isEmpty());
        QVERIFY(runner.configureConnection(true,"127.0.0.1",host.server.port(),key,false));runner.connectHost();QTRY_COMPARE(runner.phase(),QString("Connected"));
        QVERIFY(runner.taskId().isEmpty());QVERIFY(runner.serverRunDirectory().isEmpty());QCOMPARE(host.preparations,1);QCOMPARE(host.starts,0);
    }
    void oversizedFrameIsRejected(){
        FixtureHost host(dir->path());QTcpSocket socket;socket.connectToHost("127.0.0.1",host.server.port());QVERIFY(socket.waitForConnected(2000));
        QByteArray header(4,0);qToBigEndian<quint32>(quint32(RemoteLink::MaxFrame+1),reinterpret_cast<uchar*>(header.data()));socket.write(header);socket.flush();
        QTRY_COMPARE(socket.state(),QAbstractSocket::UnconnectedState);
    }
    void fragmentedAuthenticationAndReplayProtection(){
        FixtureHost host(dir->path());QTcpSocket socket;QSignalSpy requests(&host.server,&RemoteServer::request);
        socket.connectToHost("127.0.0.1",host.server.port());QVERIFY(socket.waitForConnected(2000));QTRY_VERIFY(socket.bytesAvailable()>4);
        const auto challengeBytes=socket.readAll();const auto challenge=QJsonDocument::fromJson(challengeBytes.mid(4)).object();
        const auto client=uid();const auto nonce=QString(64,'n');
        const auto proof=[&](const QByteArray& purpose){return QMessageAuthenticationCode::hash(purpose+'\n'+challenge["nonce"].toString().toUtf8()+'\n'+nonce.toUtf8()+'\n'+client.toUtf8()+'\n'+host.session.toUtf8(),key.toUtf8(),QCryptographicHash::Sha256).toHex();};
        const auto frame=[](const QJsonObject& value){const auto data=QJsonDocument(value).toJson(QJsonDocument::Compact);QByteArray bytes(4,0);qToBigEndian<quint32>(quint32(data.size()),reinterpret_cast<uchar*>(bytes.data()));return bytes+data;};
        const auto auth=frame({{"type","authenticate"},{"protocol","battery-tcp/1"},{"client",client},{"nonce",nonce},{"proof",QString::fromLatin1(proof("client"))}});
        socket.write(auth.left(2));socket.flush();QTest::qWait(20);socket.write(auth.mid(2));socket.flush();QTRY_VERIFY(socket.bytesAvailable()>4);
        const auto authenticated=QJsonDocument::fromJson(socket.readAll().mid(4)).object();QCOMPARE(authenticated["proof"].toString().toLatin1(),proof("server"));
        const auto payload=QJsonDocument(QJsonObject{{"command","hello"},{"id",uid()},{"client",client},{"taskId",""},{"schema",1},{"session",host.session}}).toJson(QJsonDocument::Compact).toBase64();
        const auto signature=QMessageAuthenticationCode::hash("client\n1\n"+payload,proof("channel"),QCryptographicHash::Sha256).toHex();
        const auto request=frame({{"n","1"},{"payload",QString::fromLatin1(payload)},{"mac",QString::fromLatin1(signature)}});
        socket.write(request.left(7));socket.flush();QTest::qWait(20);socket.write(request.mid(7));socket.flush();QTRY_COMPARE(requests.count(),1);
        socket.write(request);socket.flush();QTRY_COMPARE(socket.state(),QAbstractSocket::UnconnectedState);QCOMPARE(requests.count(),1);
    }
    void savedCredentialsAreProtectedAndReusable(){
        SysplorerRunner first;QVERIFY(first.configureConnection(true,"127.0.0.1",39001,key,true));
        QSettings saved(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","RemoteConnection");saved.sync();
        QFile file(saved.fileName());QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(!file.readAll().contains(key.toUtf8()));
        qunsetenv("BATTERY_CONNECTION_MODE");SysplorerRunner second;QVERIFY(second.remoteMode());
        QVERIFY(second.configureConnection(true,"127.0.0.1",39001,QString(),false));
        qputenv("BATTERY_CONNECTION_MODE","local");saved.clear();saved.sync();
    }
    void layoutAndConnectionDialog(){
        FixtureHost host(dir->path());qputenv("BATTERY_CONNECTION_MODE","remote");qputenv("BATTERY_REMOTE_ADDRESS","127.0.0.1");qputenv("BATTERY_REMOTE_PORT",QByteArray::number(host.server.port()));qputenv("BATTERY_REMOTE_KEY",key.toUtf8());
        MainWindow window;window.show();QCOMPARE(window.findChild<SysplorerRunner*>()->phase(),QString("Idle"));
        QCOMPARE(window.findChild<QTabWidget*>()->count(),3);QVERIFY(window.findChild<QPushButton*>("btnPlay"));QVERIFY(window.findChild<QPushButton*>("btnConnection"));
        QTimer::singleShot(80,&window,[this,&window]{auto dialog=window.findChild<QDialog*>("connectionDialog");if(dialog){dialog->findChild<QComboBox*>("connectionMode")->setCurrentIndex(1);dialog->grab().save(dir->path()+"/connection-settings.png");dialog->reject();}});
        window.findChild<QPushButton*>("btnConnection")->click();
        for(const int width:{1280,620}){window.resize(width,780);QTest::qWait(30);window.grab().save(dir->path()+QString("/layout-%1.png").arg(width));}
        qputenv("BATTERY_CONNECTION_MODE","local");qunsetenv("BATTERY_REMOTE_ADDRESS");qunsetenv("BATTERY_REMOTE_PORT");qunsetenv("BATTERY_REMOTE_KEY");
    }
    void realHostRemoteGuiWorkflow(){
        QString credential=qEnvironmentVariable("BATTERY_REAL_REMOTE_KEY");
        if(qEnvironmentVariableIsSet("BATTERY_REAL_REMOTE_KEY_FILE")){QFile secret(qEnvironmentVariable("BATTERY_REAL_REMOTE_KEY_FILE"));QVERIFY(secret.open(QIODevice::ReadOnly));credential=QString::fromUtf8(secret.readAll()).trimmed();}
        if(credential.isEmpty())QSKIP("Real Sysplorer endpoint not configured");
        qputenv("BATTERY_CONNECTION_MODE","remote");qputenv("BATTERY_REMOTE_ADDRESS",qgetenv("BATTERY_REAL_REMOTE_ADDRESS"));
        qputenv("BATTERY_REMOTE_PORT",qgetenv("BATTERY_REAL_REMOTE_PORT"));qputenv("BATTERY_REMOTE_KEY",credential.toUtf8());
        MainWindow window;window.show();auto runner=window.findChild<SysplorerRunner*>();auto view=window.findChild<BatteryView*>("batteryView");
        QVERIFY(runner&&view);QTest::qWait(600);QVERIFY(runner->remoteMode());QCOMPARE(runner->phase(),QString("Idle"));
        QCOMPARE(window.findChild<QTabWidget*>()->count(),3);QVERIFY(window.grab().save(dir->path()+"/real-remote-ui-startup.png"));
        QSignalSpy done(runner,&SysplorerRunner::finished);view->setInputs(3.43,2.3,1,.99);view->findChild<QPushButton*>("btnApply")->click();
        QCOMPARE(runner->phase(),QString("Idle"));
        QTimer::singleShot(40,&window,[&window]{auto dialog=window.findChild<QDialog*>("connectionDialog");if(dialog)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        window.findChild<QPushButton*>("btnConnection")->click();QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Connected"),15000);
        auto load=window.findChild<QPushButton*>("btnLoad");
        if(load->isEnabled()){load->click();QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Loaded"),90000);}else QVERIFY(runner->isModelLoaded());
        window.findChild<QPushButton*>("btnPrepare")->click();QTRY_VERIFY_WITH_TIMEOUT(runner->isPrepared()||runner->phase()=="Failed",180000);QVERIFY(runner->isPrepared());
        window.findChild<QPushButton*>("btnPlay")->click();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,90000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));
        const auto csv=done[0][1].toString();QVERIFY(QFileInfo(csv).isFile());QVERIFY(runner->serverRunDirectory()!=runner->lastRunDirectory());
        QFile result(runner->lastRunDirectory()+"/result.json");QVERIFY(result.open(QIODevice::ReadOnly));const auto manifest=QJsonDocument::fromJson(result.readAll()).object();
        QVERIFY(manifest["complete"].toBool());QCOMPARE(manifest["modelHash"].toString(),runner->modelFingerprint());
        const auto params=manifest["params"].toObject();QCOMPARE(params["soc0"].toDouble(),.99);QCOMPARE(params["current"].toDouble(),2.3);QCOMPARE(params["voltage"].toDouble(),3.43);
        QFile data(csv);QVERIFY(data.open(QIODevice::ReadOnly));QCOMPARE(sha(data.readAll()),manifest["csvHash"].toString());
        QVERIFY(window.grab().save(dir->path()+"/real-remote-ui-completed.png"));
        auto player=window.findChild<ReplayPlayer*>();QVERIFY(player);auto replay=window.findChild<QPushButton*>("btnReplay");QVERIFY(replay->isEnabled());
        auto replaySpeed=window.findChild<QComboBox*>("cmbReplaySpeed");replaySpeed->setCurrentIndex(replaySpeed->count()-1);replay->click();
        QTRY_VERIFY(player->isPlaying());QTRY_VERIFY_WITH_TIMEOUT(!player->isPlaying(),60000);
        QSaveFile evidence(dir->path()+"/real-remote-gui-result.json");QVERIFY(evidence.open(QIODevice::WriteOnly));
        evidence.write(QJsonDocument(QJsonObject{{"passed",true},{"remoteAddress",runner->remoteAddress()},{"remotePort",runner->remotePort()},
            {"modelHash",runner->modelFingerprint()},{"taskId",runner->taskId()},{"csv",csv},{"clientRunDirectory",runner->lastRunDirectory()},
            {"serverRunDirectory",runner->serverRunDirectory()},{"replayPassed",true},{"manifest",manifest}}).toJson());QVERIFY(evidence.commit());
    }
    void realDualModeConnectionSwitch(){
        if(!qEnvironmentVariableIsSet("BATTERY_REAL_DUAL_MODE"))QSKIP("Explicit local/remote switch validation only");
        QFile secret(qEnvironmentVariable("BATTERY_REAL_REMOTE_KEY_FILE"));QVERIFY(secret.open(QIODevice::ReadOnly));const auto credential=QString::fromUtf8(secret.readAll()).trimmed();
        MainWindow window;window.show();auto runner=window.findChild<SysplorerRunner*>();
        auto choose=[&](bool remote){
            QTimer::singleShot(40,&window,[&window,remote,&credential]{auto dialog=window.findChild<QDialog*>("connectionDialog");if(!dialog)return;
                dialog->findChild<QComboBox*>("connectionMode")->setCurrentIndex(remote?1:0);
                if(remote){dialog->findChild<QLineEdit*>("remoteAddress")->setText(qEnvironmentVariable("BATTERY_REAL_REMOTE_ADDRESS"));dialog->findChild<QSpinBox*>("remotePort")->setValue(qEnvironmentVariableIntValue("BATTERY_REAL_REMOTE_PORT"));dialog->findChild<QLineEdit*>("remoteAccessKey")->setText(credential);}
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
            window.findChild<QPushButton*>("btnConnection")->click();
        };
        choose(true);QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Connected"),15000);QVERIFY(runner->remoteMode());
        choose(false);QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Connected"),90000);QVERIFY(!runner->remoteMode());
        runner->loadModel();QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Loaded"),90000);QVERIFY(!runner->isRunning());
        choose(true);QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Connected"),15000);QVERIFY(runner->remoteMode());QVERIFY(runner->taskId().isEmpty());
        QCOMPARE(window.findChild<QTabWidget*>()->count(),3);QVERIFY(window.grab().save(dir->path()+"/dual-mode-switch.png"));
    }
    void realHostRemoteLifecycle(){
        QString credential=qEnvironmentVariable("BATTERY_REAL_REMOTE_KEY");
        if(qEnvironmentVariableIsSet("BATTERY_REAL_REMOTE_KEY_FILE")){QFile secret(qEnvironmentVariable("BATTERY_REAL_REMOTE_KEY_FILE"));QVERIFY(secret.open(QIODevice::ReadOnly));credential=QString::fromUtf8(secret.readAll()).trimmed();}
        if(credential.isEmpty())QSKIP("Real Sysplorer endpoint not configured");
        SysplorerRunner runner;QVERIFY(runner.configureConnection(true,qEnvironmentVariable("BATTERY_REAL_REMOTE_ADDRESS","127.0.0.1"),qEnvironmentVariableIntValue("BATTERY_REAL_REMOTE_PORT"),credential,false));
        QSignalSpy done(&runner,&SysplorerRunner::finished);QSignalSpy applied(&runner,&SysplorerRunner::tuneApplied);
        QString failure;
        QJsonArray events;
        auto record=[&](const QString& event){
            events.append(QJsonObject{{"event",event},{"phase",runner.phase()},{"taskId",runner.taskId()},{"serverRunDirectory",runner.serverRunDirectory()},
                {"clientRunDirectory",runner.lastRunDirectory()},{"message",failure}});
            QSaveFile output(dir->path()+"/real-remote-lifecycle.json");
            if(output.open(QIODevice::WriteOnly)){output.write(QJsonDocument(QJsonObject{{"address",runner.remoteAddress()},{"port",runner.remotePort()},
                {"modelHash",runner.modelFingerprint()},{"events",events}}).toJson());output.commit();}
        };
        QObject evidenceContext;
        connect(&runner,&SysplorerRunner::stateChanged,&evidenceContext,[&](const QString&){record("phase");});
        connect(&runner,&SysplorerRunner::finished,&evidenceContext,[&](bool ok,const QString&,const QString& message){if(!ok)failure=message;record(ok?"completed":"failed");});
        auto terminal=[&]{return runner.phase()=="Failed"||runner.phase()=="ConnectionFailed"||runner.phase()=="ConnectionLost"||runner.phase()=="DownloadFailed";};
        runner.connectHost();QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("Connected"),90000);
        runner.loadModel();QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("Loaded"),90000);QVERIFY(!runner.isPrepared());QVERIFY(runner.liveSamples().isEmpty());
        runner.setSpeed(1);runner.prepareSimulation(3.45,2,1,.5,6);QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared()||!runner.isRunning(),180000);QVERIFY(runner.isPrepared());runner.beginSimulation();
        QTRY_VERIFY_WITH_TIMEOUT(runner.phase()=="Running"||terminal(),30000);QVERIFY2(runner.phase()=="Running",qPrintable(failure));QTRY_VERIFY_WITH_TIMEOUT(!runner.liveSamples().isEmpty(),15000);runner.pause();QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("Paused"),15000);
        runner.tune({{"current",1.5},{"voltage",3.4},{"mode",1},{"vMin",2.5},{"vMax",3.45},{"iMax",5.0}});QTest::qWait(300);runner.resume();
        QTRY_VERIFY_WITH_TIMEOUT(applied.count()==1,15000);QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));
        QVERIFY(QFileInfo(done[0][1].toString()).isFile());QVERIFY(runner.serverRunDirectory()!=runner.lastRunDirectory());
        runner.prepareSimulation(3.45,2,1,.5,30);QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared(),180000);runner.beginSimulation();QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("Running"),30000);
        QTRY_VERIFY_WITH_TIMEOUT(runner.liveSamples().size()>1,15000);QSignalSpy stopped(&runner,&SysplorerRunner::stopped);runner.stop();QTRY_COMPARE_WITH_TIMEOUT(stopped.count(),1,30000);
        record("all-checks-passed");
    }
};
QTEST_MAIN(RemoteIntegration)
#include "remote_integration_test.moc"

#include <QtTest>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QSettings>
#include <QNetworkDatagram>
#include <QProcess>
#include "model_transfer.h"
#include "remote_server.h"
#include "sysplorer_runner.h"

using namespace ModelTransfer;
class ModelTransferTest:public QObject {
    Q_OBJECT
    QByteArray text;
    std::unique_ptr<QTemporaryDir> dir;
    QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
    void modelFile(const QString& path){
#ifdef Q_OS_WIN
        _wputenv_s(L"BATTERY_MODEL_FILE",reinterpret_cast<const wchar_t*>(path.utf16()));
#else
        qputenv("BATTERY_MODEL_FILE",path.toUtf8());
#endif
    }
    QJsonObject begin(const QString& upload,const QByteArray& bytes,int chunk=UdpChunk){return {{"uploadId",upload},{"modelInterface",interfaceName()},{"modelTransferVersion",1},{"bytes",bytes.size()},{"sha256",sha(bytes)},{"chunkSize",chunk}};}
    QJsonObject chunk(const QString& upload,const QByteArray& bytes,int pos,int size=UdpChunk){const auto b=bytes.mid(pos,size);return {{"uploadId",upload},{"offset",pos},{"data",QString::fromLatin1(b.toBase64())},{"sha256",sha(b)}};}
    void put(Store& s,const QString& upload,const QByteArray& bytes,int size=UdpChunk){s.execute("client","model.begin",begin(upload,bytes,size),size,0);for(int p=0;p<bytes.size();p+=size)s.execute("client","model.chunk",chunk(upload,bytes,p,size),size,1);}
private slots:
    void initTestCase(){
        const auto root=qEnvironmentVariable("BATTERY_TEST_ROOT");QVERIFY(!root.isEmpty());QDir().mkpath(root);dir.reset(new QTemporaryDir(root+"/transfer-XXXXXX"));QVERIFY(dir->isValid());dir->setAutoRemove(false);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir->path()+"/settings");
        QFile file(qEnvironmentVariable("BATTERY_MODEL_FILE"));QVERIFY(file.open(QIODevice::ReadOnly));text=file.readAll();QVERIFY(text.size()>10000);
    }
    void reorderingDuplicateAndMissing(){
        Store s;const auto u=id();s.execute("client","model.begin",begin(u,text),UdpChunk,0);
        for(int p=((text.size()-1)/UdpChunk)*UdpChunk;p>=UdpChunk;p-=UdpChunk){auto c=chunk(u,text,p);s.execute("client","model.chunk",c,UdpChunk,1);s.execute("client","model.chunk",c,UdpChunk,2);}
        QVERIFY_EXCEPTION_THROWN(s.execute("client","model.commit",{{"uploadId",u}},UdpChunk,3),std::runtime_error);
        s.execute("client","model.chunk",chunk(u,text,0),UdpChunk,4);
        QVERIFY(s.execute("client","model.commit",{{"uploadId",u}},UdpChunk,5)["committed"].toBool());
        QCOMPARE(s.take("client",sha(text),6),text);s.execute("client","model.commit",{{"uploadId",u}},UdpChunk,7);
        QVERIFY_EXCEPTION_THROWN(s.take("stranger",sha(text),8),std::runtime_error);
    }
    void invalidTexts_data(){QTest::addColumn<int>("type");for(int t=0;t<7;++t)QTest::newRow(qPrintable(QString::number(t)))<<t;}
    void invalidTexts(){
        QFETCH(int,type);Store s;const auto u=id();auto bytes=text;
        if(type==0)bytes+="\xff";if(type==1)bytes.append('\0');if(type==2)bytes="model Wrong end Wrong;";
        if(type==6)bytes+="\xe4\xb8";
        if(type==3)bytes=QByteArray(Limit+1,'x');if(type==4)bytes.clear();
        if(type<3||type==6){put(s,u,bytes);QVERIFY_EXCEPTION_THROWN(s.execute("client","model.commit",{{"uploadId",u}},UdpChunk,3),std::runtime_error);}
        else if(type<5)QVERIFY_EXCEPTION_THROWN(put(s,u,bytes),std::runtime_error);
        else{auto a=begin(u,bytes);a["sha256"]=QString(64,'0');s.execute("client","model.begin",a,UdpChunk,0);for(int p=0;p<bytes.size();p+=UdpChunk)s.execute("client","model.chunk",chunk(u,bytes,p),UdpChunk,1);QVERIFY_EXCEPTION_THROWN(s.execute("client","model.commit",{{"uploadId",u}},UdpChunk,2),std::runtime_error);}
        QVERIFY_EXCEPTION_THROWN(s.take("client",sha(bytes),4),std::runtime_error);
    }
    void expiryAbortRestartAndBounds(){
        Store s;const auto u=id();s.execute("client","model.begin",begin(u,text),UdpChunk,0);s.expire(60000);
        QVERIFY_EXCEPTION_THROWN(s.execute("client","model.chunk",chunk(u,text,0),UdpChunk,60000),std::runtime_error);
        s.execute("client","model.begin",begin(u,text),UdpChunk,60001);auto c=chunk(u,text,0);c["offset"]=-1;
        QVERIFY_EXCEPTION_THROWN(s.execute("client","model.chunk",c,UdpChunk,60002),std::runtime_error);
        c=chunk(u,text,0);c["sha256"]=QString(64,'0');QVERIFY_EXCEPTION_THROWN(s.execute("client","model.chunk",c,UdpChunk,60003),std::runtime_error);
        c=chunk(u,text,0);c["data"]="!";QVERIFY_EXCEPTION_THROWN(s.execute("client","model.chunk",c,UdpChunk,60004),std::runtime_error);
        s.execute("client","model.abort",{{"uploadId",u}},UdpChunk,60005);QVERIFY_EXCEPTION_THROWN(s.take("client",sha(text),60006),std::runtime_error);
        auto a=begin(u,text);a["chunkSize"]=UdpChunk+1;QVERIFY_EXCEPTION_THROWN(s.execute("client","model.begin",a,UdpChunk,0),std::runtime_error);
        a=begin(u,text);a["modelInterface"]="other/1";QVERIFY_EXCEPTION_THROWN(s.execute("client","model.begin",a,UdpChunk,0),std::runtime_error);
        Store restarted;QVERIFY_EXCEPTION_THROWN(restarted.take("client",sha(text),0),std::runtime_error);
    }
    void tcpMultiChunk(){
        auto large=text;large+="\n/*";large+=QByteArray(150000,'x');large+="*/\n";Store s;auto u=id();put(s,u,large,TcpChunk);
        s.execute("client","model.commit",{{"uploadId",u}},TcpChunk,5);QCOMPARE(s.take("client",sha(large),6),large);
    }
    void udpClientRetransmitsAndRejectsChangedPreparation(){
        const auto home=dir->path()+"/udp-host",discovery=home+"/discovery",marker=home+"/requests.jsonl",source=home+"/client.mo";
        QVERIFY(QDir().mkpath(home+"/Bin64/addins/BatteryUdpControl"));
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath()+"/../../build_startup_fixture/release/mworks.exe",home+"/Bin64/mworks.exe"));
        QFile dll(home+"/Bin64/addins/BatteryUdpControl/battery_udp_control.dll");QVERIFY(dll.open(QIODevice::WriteOnly));dll.write("fixture");dll.close();
        QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));file.write(text);file.close();
        const auto oldModel=qEnvironmentVariable("BATTERY_MODEL_FILE");modelFile(source);
        qputenv("SYSPLORER_HOME",home.toUtf8());qputenv("BATTERY_DISCOVERY_DIR",discovery.toUtf8());qputenv("BATTERY_SIM_WORKSPACE",(home+"/jobs").toUtf8());
        qputenv("BATTERY_LAUNCH_SENTINEL",marker.toUtf8().toBase64());qputenv("BATTERY_FIXTURE_UDP","1");qputenv("BATTERY_FIXTURE_TRANSFER","1");
        QProcess process;process.start(home+"/Bin64/mworks.exe");QVERIFY(process.waitForStarted());
        SysplorerRunner runner;QVERIFY(runner.configureConnection(false,"127.0.0.1",39001,QString(),false));runner.prepareSimulation(3.45,2,1,.5,6);
        QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared()||!runner.isRunning(),12000);QVERIFY(runner.isPrepared());
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(text+"\n// snapshot changed\n");file.close();QSignalSpy notice(&runner,&SysplorerRunner::remoteNotice);
        runner.beginSimulation();QTRY_COMPARE(runner.phase(),QString("Connected"));QCOMPARE(notice.count(),1);
        QFile requests(marker);QVERIFY(requests.open(QIODevice::ReadOnly));auto lines=requests.readAll();requests.close();QVERIFY(lines.count("model.chunk")>(text.size()+UdpChunk-1)/UdpChunk);QVERIFY(!lines.contains("\"command\":\"start\""));
        runner.prepareSimulation(3.45,2,1,.5,6);QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared()||!runner.isRunning(),12000);QVERIFY(runner.isPrepared());
        QVERIFY(requests.open(QIODevice::ReadOnly));lines=requests.readAll();QVERIFY(lines.contains(sha(text+"\n// snapshot changed\n").toLatin1()));
        runner.invalidatePreparation();QTRY_COMPARE(runner.phase(),QString("Connected"));process.kill();QVERIFY(process.waitForFinished());
        modelFile(oldModel);for(const auto name:{"BATTERY_FIXTURE_TRANSFER","BATTERY_FIXTURE_UDP","BATTERY_LAUNCH_SENTINEL"})qunsetenv(name);
    }
    void clientTcpUploadsSnapshot_data(){QTest::addColumn<int>("padding");QTest::newRow("14KB")<<0;QTest::newRow("multi-48KiB")<<150000;}
    void clientTcpUploadsSnapshot(){
        QFETCH(int,padding);auto expected=text;if(padding){expected+="\n/*";expected+=QByteArray(padding,'x');expected+="*/\n";}
        const auto oldModel=qEnvironmentVariable("BATTERY_MODEL_FILE"),source=dir->path()+"/tcp-client.mo";
        QFile input(source);QVERIFY(input.open(QIODevice::WriteOnly));input.write(expected);input.close();modelFile(source);
        QSettings settings(dir->path()+"/server.ini",QSettings::IniFormat);QTcpServer reserve;QVERIFY(reserve.listen(QHostAddress::LocalHost,0));const auto port=reserve.serverPort();reserve.close();
        settings.setValue("Remote/Enabled",true);settings.setValue("Remote/ListenAddress","127.0.0.1");settings.setValue("Remote/Port",port);settings.setValue("Remote/AccessKey","test-only-model-transfer-0123456789abcdef");settings.setValue("Remote/StatusFile",dir->path()+"/status.json");settings.sync();
        RemoteServer server;Store store;QByteArray received;const auto session=id();bool loaded=false;
        connect(&server,&RemoteServer::request,this,[&](RemoteLink* link,const QJsonObject& r){
            QJsonObject out;const auto cmd=r["command"].toString();
            if(cmd=="hello")out={{"ok",true},{"ready",true},{"modelLifecycle",true},{"splitLifecycle",true},{"modelTransferVersion",1},{"modelInterface",interfaceName()},{"controllerState","Idle"},{"status",QJsonObject{{"phase","Ready"}}}};
            else if(cmd.startsWith("model."))out=store.execute(r["client"].toString(),cmd,r["args"].toObject(),TcpChunk,0);
            else {received=store.take(r["client"].toString(),r["args"].toObject()["modelHash"].toString(),1);loaded=true;out={{"ok",true},{"status",QJsonObject{{"phase","Loaded"},{"taskId",r["taskId"]},{"owner",r["client"]},{"modelHash",sha(received)},{"modelLoaded",true}}}};}
            out["schema"]=1;out["id"]=r["id"];out["session"]=session;out["taskId"]=r["taskId"];link->send(out);
        });QVERIFY(server.start(settings.fileName(),session));
        SysplorerRunner runner;QVERIFY(runner.configureConnection(true,"127.0.0.1",port,"test-only-model-transfer-0123456789abcdef",false));runner.loadModel();
        QTRY_VERIFY_WITH_TIMEOUT(loaded,10000);QTRY_COMPARE(runner.phase(),QString("Loaded"));QCOMPARE(received,expected);modelFile(oldModel);
    }
    void realHostClientModelVariants_data(){QTest::addColumn<bool>("remote");QTest::newRow("udp")<<false;QTest::newRow("tcp")<<true;}
    void realHostClientModelVariants(){
        if(!qEnvironmentVariableIsSet("BATTERY_REAL_HOST_ENV"))QSKIP("Requires a worktree-isolated licensed Sysplorer EXE already running");
        QFETCH(bool,remote);QFile environmentFile(qEnvironmentVariable("BATTERY_REAL_HOST_ENV"));QVERIFY(environmentFile.open(QIODevice::ReadOnly));auto eb=environmentFile.readAll();if(eb.startsWith("\xef\xbb\xbf"))eb.remove(0,3);const auto e=QJsonDocument::fromJson(eb).object();
        QFile secret(e["keyFile"].toString());QVERIFY(secret.open(QIODevice::ReadOnly));const auto key=QString::fromUtf8(secret.readAll()).trimmed();
        qputenv("SYSPLORER_HOME",e["hostHome"].toString().toUtf8());qputenv("BATTERY_DISCOVERY_DIR",e["discovery"].toString().toUtf8());
        qputenv("BATTERY_SIM_WORKSPACE",(e["data"].toString()+(remote?"/client-jobs":"/jobs")).toUtf8());
        const auto originalModel=qEnvironmentVariable("BATTERY_MODEL_FILE");QVector<double> finalSoc;
        for(int variant=0;variant<2;++variant){
            auto source=text;if(variant==1)source.replace("Q_rated=2.5*3600","Q_rated=5.0*3600");
            const auto path=dir->path()+QString("/model-%1-%2.mo").arg(remote).arg(variant);QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(source),qint64(source.size()));f.close();modelFile(path);
            SysplorerRunner runner;QCOMPARE(runner.modelFingerprint(),sha(source));QVERIFY(runner.configureConnection(remote,e["address"].toString("127.0.0.1"),e["port"].toInt(),key,false));QSignalSpy finished(&runner,&SysplorerRunner::finished);QString error;
            connect(&runner,&SysplorerRunner::finished,&runner,[&](bool,const QString&,const QString& message){error=message;});
            runner.connectHost();QTRY_COMPARE_WITH_TIMEOUT(runner.phase(),QString("Connected"),180000);runner.loadModel();QTRY_VERIFY_WITH_TIMEOUT(runner.phase()=="Loaded"||!runner.isRunning(),30000);QVERIFY2(runner.phase()=="Loaded",qPrintable(error));
            runner.setSpeed(20);runner.prepareSimulation(3.45,2,1,.5,6);QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared()||!runner.isRunning(),180000);QVERIFY2(runner.isPrepared(),qPrintable(error));
            runner.beginSimulation();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,30000);QVERIFY2(finished[0][0].toBool(),qPrintable(finished[0][2].toString()));
            QVERIFY(runner.liveSamples().size()>1);finalSoc.append(runner.liveSamples().last().soc);
            QFile result(runner.lastRunDirectory()+"/result.json");QVERIFY(result.open(QIODevice::ReadOnly));QCOMPARE(QJsonDocument::fromJson(result.readAll()).object()["modelHash"].toString(),sha(source));
            if(remote)QVERIFY(runner.lastRunDirectory()!=runner.serverRunDirectory());
        }
        QVERIFY(finalSoc[0]>.5);QVERIFY(qAbs((finalSoc[1]-.5)*2-(finalSoc[0]-.5))<1e-6);
        QFile evidence(dir->path()+QString("/real-variants-%1.json").arg(remote?"tcp":"udp"));QVERIFY(evidence.open(QIODevice::WriteOnly));evidence.write(QJsonDocument(QJsonObject{{"remote",remote},{"originalFinalSOC",finalSoc[0]},{"doubleCapacityFinalSOC",finalSoc[1]},{"passed",true}}).toJson());modelFile(originalModel);
    }
    void realHostRejectsInvalidModelsAndRecovers(){
        if(!qEnvironmentVariableIsSet("BATTERY_REAL_HOST_ENV"))QSKIP("Requires isolated licensed host");
        QFile ef(qEnvironmentVariable("BATTERY_REAL_HOST_ENV"));QVERIFY(ef.open(QIODevice::ReadOnly));auto bytes=ef.readAll();if(bytes.startsWith("\xef\xbb\xbf"))bytes.remove(0,3);const auto e=QJsonDocument::fromJson(bytes).object();
        QFile keyFile(e["keyFile"].toString());QVERIFY(keyFile.open(QIODevice::ReadOnly));const auto key=QString::fromUtf8(keyFile.readAll());
        const auto oldModel=qEnvironmentVariable("BATTERY_MODEL_FILE"),path=dir->path()+"/rejected-model.mo";
        SysplorerRunner runner;QVERIFY(runner.configureConnection(true,e["address"].toString("127.0.0.1"),e["port"].toInt(),key,false));QSignalSpy finished(&runner,&SysplorerRunner::finished);
        const QByteArray sources[]={"package BatterySim model SingleRun Real bad = ; end SingleRun; end BatterySim;","package BatterySim model SingleRun Real other; end SingleRun; end BatterySim;"};
        for(int n=0;n<2;++n){
            QFile model(path);QVERIFY(model.open(QIODevice::WriteOnly));model.write(sources[n]);model.close();modelFile(path);finished.clear();runner.loadModel();
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,30000);QVERIFY(!finished[0][0].toBool());
            QVERIFY2(finished[0][2].toString().contains(n==0?"MODEL_PARSE_FAILED":"MODEL_INTERFACE_INVALID"),qPrintable(finished[0][2].toString()));
        }
        modelFile(oldModel);QVERIFY(!runner.modelFingerprint().isEmpty());runner.loadModel();QTRY_VERIFY_WITH_TIMEOUT(runner.phase()=="Loaded"||!runner.isRunning(),30000);QCOMPARE(runner.phase(),QString("Loaded"));
    }
};
QTEST_MAIN(ModelTransferTest)
#include "model_transfer_test.moc"

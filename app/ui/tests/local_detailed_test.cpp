#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <limits>
#include "sysplorer_runner.h"

class LocalDetailed : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    QMap<QByteArray,QByteArray> environment;
    QString fixtureHome(){
        const auto folder=directory->path()+"/host-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(folder+"/Bin64/addins/BatteryUdpControl");return folder;
    }
private slots:
    void initTestCase(){
        const auto root=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS");QVERIFY(!root.isEmpty());
        QDir().mkpath(root);directory.reset(new QTemporaryDir(root+"/extra-XXXXXX"));QVERIFY(directory->isValid());directory->setAutoRemove(false);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory->path()+"/settings");
    }
    void init(){
        for(const auto name:{"SYSPLORER_HOME","BATTERY_SIM_WORKSPACE","BATTERY_CONNECTION_MODE"})environment.insert(name,qgetenv(name));
        qputenv("BATTERY_CONNECTION_MODE","local");
    }
    void cleanup(){
        for(auto it=environment.cbegin();it!=environment.cend();++it){if(it.value().isNull())qunsetenv(it.key());else qputenv(it.key(),it.value());}
    }
    void missingPluginIsTerminal(){
        const auto folder=fixtureHome();QFile exe(folder+"/Bin64/mworks.exe");QVERIFY(exe.open(QIODevice::WriteOnly));exe.write("not-executed");exe.close();
        qputenv("SYSPLORER_HOME",folder.toLatin1());SysplorerRunner runner;QSignalSpy failures(&runner,&SysplorerRunner::hostConnectionFailed);runner.connectHost();
        QTRY_COMPARE(failures.count(),1);QCOMPARE(runner.phase(),QString("ConnectionFailed"));QVERIFY(!runner.isConnecting());QVERIFY(!runner.isRunning());
        QVERIFY(failures[0][0].toString().contains(QString::fromUtf8("未安装配套插件")));QTest::qWait(300);QCOMPARE(failures.count(),1);
    }
    void invalidExecutableIsTerminal(){
        const auto folder=fixtureHome();QFile exe(folder+"/Bin64/mworks.exe"),plugin(folder+"/Bin64/addins/BatteryUdpControl/battery_udp_control.dll");
        QVERIFY(exe.open(QIODevice::WriteOnly));exe.write("invalid PE fixture");exe.close();QVERIFY(plugin.open(QIODevice::WriteOnly));plugin.write("fixture");plugin.close();
        qputenv("SYSPLORER_HOME",folder.toLatin1());SysplorerRunner runner;QSignalSpy failures(&runner,&SysplorerRunner::hostConnectionFailed);runner.connectHost();
        QTRY_COMPARE(failures.count(),1);QCOMPARE(runner.phase(),QString("ConnectionFailed"));QVERIFY(!runner.isConnecting());QVERIFY(!runner.isRunning());
        QVERIFY(failures[0][0].toString().contains(QString::fromUtf8("无法启动")));QTest::qWait(300);QCOMPARE(failures.count(),1);
    }
    void invalidParametersStopBeforeLaunch_data(){
        QTest::addColumn<double>("voltage");QTest::addColumn<double>("current");QTest::addColumn<int>("mode");QTest::addColumn<double>("soc");QTest::addColumn<double>("duration");
        QTest::newRow("voltage-low")<<2.49<<2.0<<1<<.5<<6.;QTest::newRow("voltage-high")<<3.46<<2.0<<1<<.5<<6.;
        QTest::newRow("current-low")<<3.45<<-.01<<1<<.5<<6.;QTest::newRow("current-high")<<3.45<<5.01<<1<<.5<<6.;
        QTest::newRow("soc-low")<<3.45<<2.0<<1<<-.01<<6.;QTest::newRow("soc-high")<<3.45<<2.0<<1<<1.01<<6.;
        QTest::newRow("duration-low")<<3.45<<2.0<<1<<.5<<.01;QTest::newRow("duration-high")<<3.45<<2.0<<1<<.5<<20001.;
        QTest::newRow("invalid-mode")<<3.45<<2.0<<0<<.5<<6.;QTest::newRow("nonfinite-current")<<3.45<<std::numeric_limits<double>::quiet_NaN()<<1<<.5<<6.;
    }
    void invalidParametersStopBeforeLaunch(){
        QFETCH(double,voltage);QFETCH(double,current);QFETCH(int,mode);QFETCH(double,soc);QFETCH(double,duration);
        qputenv("SYSPLORER_HOME","C:/missing/local-test-only");SysplorerRunner runner;QSignalSpy done(&runner,&SysplorerRunner::finished);
        runner.prepareSimulation(voltage,current,mode,soc,duration);QTRY_COMPARE(done.count(),1);QVERIFY(!done[0][0].toBool());
        QVERIFY(done[0][2].toString().contains(QString::fromUtf8("仿真参数不在允许范围内")));QVERIFY(!runner.isRunning());QVERIFY(!runner.isPrepared());
        QFile log(runner.lastRunDirectory()+"/client.log");QVERIFY(log.open(QIODevice::ReadOnly));QVERIFY(!log.readAll().contains("\"event\":\"launched\""));
    }
    void zeroCurrentPreservesSoc(){
        SysplorerRunner runner;QVERIFY(!runner.remoteMode());QSignalSpy done(&runner,&SysplorerRunner::finished);
        runner.setSpeed(20);runner.prepareSimulation(3.45,0,1,.5,6);QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared()||!runner.isRunning(),180000);QVERIFY(runner.isPrepared());
        runner.beginSimulation();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));
        QVERIFY(runner.liveSamples().size()>1);for(const auto& sample:runner.liveSamples()){QVERIFY(qAbs(sample.current)<1e-8);QVERIFY(qAbs(sample.soc-.5)<1e-8);}
        QFile csv(done[0][1].toString()),manifest(runner.lastRunDirectory()+"/result.json");QVERIFY(csv.open(QIODevice::ReadOnly));QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto meta=QJsonDocument::fromJson(manifest.readAll()).object();QVERIFY(meta["complete"].toBool());
        QCOMPARE(meta["csvHash"].toString(),QString::fromLatin1(QCryptographicHash::hash(csv.readAll(),QCryptographicHash::Sha256).toHex()));
    }
};
QTEST_MAIN(LocalDetailed)
#include "local_detailed_test.moc"

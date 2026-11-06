#include <QtTest>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include "main_window.h"
#include "battery_view.h"
#include "plot_widget.h"
#include "replay_player.h"
#include "sysplorer_runner.h"
class UdpIntegration:public QObject {
 Q_OBJECT
 QTemporaryDir settingsDir;
 const QByteArray testHost=qgetenv("SYSPLORER_HOME");
 const QByteArray testWorkspace=qgetenv("BATTERY_SIM_WORKSPACE");
private slots:
 void initTestCase(){QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"),10));QVERIFY(settingsDir.isValid());QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDir.path());}
 void init(){QSettings config(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","ButtonDetails");config.clear();}
 void cleanup(){
    const auto artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath()+"/BatteryButtonTests");QDir().mkpath(artifacts);
    QFile record(artifacts+"/cases_"+qEnvironmentVariable("BATTERY_TEST_RUN_ID","manual")+".jsonl");
    if(record.open(QIODevice::WriteOnly|QIODevice::Append)){
        record.write(QJsonDocument(QJsonObject{{"test",QString::fromLatin1(QTest::currentTestFunction())},{"passed",!QTest::currentTestFailed()}}).toJson(QJsonDocument::Compact));record.write("\n");
    }
    if(testHost.isNull())qunsetenv("SYSPLORER_HOME");else qputenv("SYSPLORER_HOME",testHost);
    if(testWorkspace.isNull())qunsetenv("BATTERY_SIM_WORKSPACE");else qputenv("BATTERY_SIM_WORKSPACE",testWorkspace);
 }
 void layoutAndDisclosure(){
    qputenv("SYSPLORER_HOME","C:/missing/sysplorer");
    const auto artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath()+"/BatteryButtonTests");QDir().mkpath(artifacts);
    {
      MainWindow w;w.show();QTest::qWait(100);
      auto status=w.findChild<QToolButton*>("toggleStatus"),logs=w.findChild<QToolButton*>("toggleLog");
      QVERIFY(status&&logs);QVERIFY(!status->isChecked());QVERIFY(!logs->isChecked());
      QVERIFY(!w.findChild<QLabel*>("lblState")->isVisible());QVERIFY(!w.findChild<QPlainTextEdit*>("stageLog")->isVisible());
      QVERIFY(w.findChild<QPlainTextEdit*>("errorPanel")->isVisible());
      QVERIFY(!w.findChild<QPlainTextEdit*>("stageLog")->toPlainText().isEmpty());
      QVERIFY(!w.findChild<QPushButton*>("btnPlay")->isEnabled());
      auto runner=w.findChild<SysplorerRunner*>();
      status->setChecked(true);QVERIFY(w.findChild<QLabel*>("lblState")->isVisible());QVERIFY(!logs->isChecked());
      QVERIFY(QMetaObject::invokeMethod(&w,"onHostStateChanged",Qt::DirectConnection,Q_ARG(QString,QString("Connecting"))));
      QVERIFY(status->isChecked());QVERIFY(!logs->isChecked());
      logs->setChecked(true);status->setChecked(false);
      QVERIFY(w.findChild<QPushButton*>("btnCopyLog")->isEnabled());
      const QStringList buttons={"btnLoad","btnPrepare","btnPlay","btnPause","btnStop"};
      w.resize(1280,780);QTest::qWait(80);int row=-1;
      for(const auto& name:buttons){auto b=w.findChild<QPushButton*>(name);if(row<0)row=b->geometry().y();QCOMPARE(b->geometry().y(),row);}
      w.grab().save(artifacts+"/desktop.png");
      w.resize(620,680);QTest::qWait(80);
      QList<QRect> rects;
      for(const auto& name:buttons){auto b=w.findChild<QPushButton*>(name);QVERIFY(b->parentWidget()->rect().contains(b->geometry()));for(const auto& r:rects)QVERIFY(!r.intersects(b->geometry()));rects<<b->geometry();}
      auto scenario=w.findChild<BatteryView*>("batteryView")->findChild<QLabel*>("scenarioInfo");QVERIFY(scenario);QVERIFY(!scenario->text().contains("\n"));QVERIFY(!scenario->toolTip().isEmpty());
      w.grab().save(artifacts+"/narrow.png");QVERIFY(!runner->isRunning());
    }
    {MainWindow reopened;QVERIFY(!reopened.findChild<QToolButton*>("toggleStatus")->isChecked());QVERIFY(reopened.findChild<QToolButton*>("toggleLog")->isChecked());}
 }
 void modelLoadAndReuse(){
    SysplorerRunner runner;runner.loadModel();
    QTRY_VERIFY_WITH_TIMEOUT(runner.phase()=="Loaded"||runner.phase()=="Failed",180000);
    QCOMPARE(runner.phase(),QString("Loaded"));QVERIFY(!runner.isRunning());QVERIFY(runner.isModelLoaded());QVERIFY(!runner.isPrepared());QVERIFY(runner.liveSamples().isEmpty());
    QFile first(runner.lastRunDirectory()+"/plugin.log");QVERIFY(first.open(QIODevice::ReadOnly));auto loadLog=first.readAll();
    QVERIFY(loadLog.contains("load_confirmed"));QVERIFY(!loadLog.contains("parameter_verified"));QVERIFY(!loadLog.contains("started"));
    runner.prepareSimulation(3.45,1,1,.5,6);QTRY_VERIFY_WITH_TIMEOUT(runner.isPrepared()||!runner.isRunning(),180000);QVERIFY(runner.isPrepared());
    QFile second(runner.lastRunDirectory()+"/plugin.log");QVERIFY(second.open(QIODevice::ReadOnly));auto translateLog=second.readAll();QVERIFY(translateLog.contains("model_reused"));QVERIFY(!translateLog.contains("model_loaded"));
    QVERIFY(translateLog.contains("parameter_verified"));QVERIFY(runner.liveSamples().isEmpty());
    runner.invalidatePreparation();QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(),5000);
 }

 void startIncludesPreparation(){
    MainWindow w;w.show();auto runner=w.findChild<SysplorerRunner*>();
    QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Connected"),180000);
    auto start=w.findChild<QPushButton*>("btnPlay");auto prepare=w.findChild<QPushButton*>("btnPrepare");
    auto view=w.findChild<BatteryView*>("batteryView");view->setInputs(3.45,5,1,.99);
    QSignalSpy completed(runner,&SysplorerRunner::finished);QStringList phases;
    connect(runner,&SysplorerRunner::stateChanged,this,[&](const QString& phase){if(!phases.contains(phase))phases<<phase;});
    const auto artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath()+"/BatteryButtonTests");QDir().mkpath(artifacts);w.grab().save(artifacts+"/ready.png");
    QVERIFY(start->isEnabled());QTest::mouseClick(start,Qt::LeftButton);QVERIFY(!start->isEnabled());QVERIFY(!prepare->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(),1,240000);
    QVERIFY2(completed.first()[0].toBool(),qPrintable(completed.first()[2].toString()));
    QVERIFY(phases.contains("Prepared"));QVERIFY(phases.contains("Starting"));QVERIFY(phases.contains("Running"));
    ReplayData data;QString error;QVERIFY(data.load(completed.first()[1].toString(),&error));QVERIFY(qAbs(data.socBegin()-.99)<1e-6);
    QFile log(runner->lastRunDirectory()+"/plugin.log");QVERIFY(log.open(QIODevice::ReadOnly));auto bytes=log.readAll();QVERIFY(bytes.contains("parameter_verified"));QVERIFY(bytes.contains("started"));
    QVERIFY(!w.findChild<QToolButton*>("toggleStatus")->isChecked());QVERIFY(!w.findChild<QToolButton*>("toggleLog")->isChecked());
    QTest::qWait(80);
    auto replayTime=w.findChild<QLabel*>("replayTime");QVERIFY(replayTime->width()>=replayTime->fontMetrics().horizontalAdvance(replayTime->text()));
    w.grab().save(artifacts+"/complete.png");
 }
 void automaticStartFailureKeepsStage(){
    MainWindow w;w.show();auto runner=w.findChild<SysplorerRunner*>();
    QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Connected"),180000);
    qputenv("BATTERY_SIM_WORKSPACE",QString::fromUtf8("C:/invalid/中文").toUtf8());
    QSignalSpy completed(runner,&SysplorerRunner::finished);
    QTest::mouseClick(w.findChild<QPushButton*>("btnPlay"),Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(),1,5000);QVERIFY(!completed.first()[0].toBool());
    QVERIFY(runner->liveSamples().isEmpty());QVERIFY(!runner->isRunning());QVERIFY(!runner->isPrepared());
    auto errors=w.findChild<QPlainTextEdit*>("errorPanel");QVERIFY(errors->isVisible());QVERIFY(errors->toPlainText().contains("OUTPUT_DIRECTORY_INVALID"));
    auto logs=w.findChild<QPlainTextEdit*>("stageLog");QVERIFY(!logs->isVisible());QVERIFY(logs->toPlainText().contains("OUTPUT_DIRECTORY_INVALID"));
    QFile diagnostic(runner->lastRunDirectory()+"/client.log");QVERIFY(diagnostic.open(QIODevice::ReadOnly));QVERIFY(diagnostic.readAll().contains("OUTPUT_DIRECTORY_INVALID"));
 }
 void busyHostIsSkipped() {
    SysplorerRunner occupied;
    occupied.setSpeed(1);occupied.prepareSimulation(3.45,1,1,.5,120);
    QTRY_VERIFY_WITH_TIMEOUT(occupied.isPrepared(),180000);occupied.beginSimulation();
    QTRY_VERIFY_WITH_TIMEOUT(occupied.phase()=="Running"&&occupied.liveSamples().size()>1,180000);
    occupied.pause();QTRY_COMPARE_WITH_TIMEOUT(occupied.phase(),QString("Paused"),15000);
    const auto firstTask=occupied.taskId();const auto held=occupied.liveSamples().last().t;
    SysplorerRunner next;next.setSpeed(20);QSignalSpy done(&next,&SysplorerRunner::finished);
    next.prepareSimulation(3.45,1,1,.5,6);
    QTRY_VERIFY_WITH_TIMEOUT(next.isPrepared(),180000);next.beginSimulation();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,180000);
    QVERIFY2(done.first()[0].toBool(),qPrintable(done.first()[2].toString()));
    QVERIFY(next.taskId()!=firstTask);QCOMPARE(occupied.taskId(),firstTask);
    QCOMPARE(occupied.phase(),QString("Paused"));QCOMPARE(occupied.liveSamples().last().t,held);
    QSignalSpy stopped(&occupied,&SysplorerRunner::stopped);occupied.stop();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(),1,30000);
 }
 void runtimeTuningButtons() {
    MainWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto runner=window.findChild<SysplorerRunner*>();auto view=window.findChild<BatteryView*>("batteryView");
    auto speed=window.findChild<QComboBox*>("cmbSpeed");speed->setCurrentIndex(1);
    view->setInputs(3.45,1,1,.5);
    QTest::mouseClick(view->findChild<QPushButton*>("btnApply"),Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QPushButton*>("btnPrepare")->isEnabled(),100000);
    QTest::mouseClick(window.findChild<QPushButton*>("btnPrepare"),Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->isPrepared(),180000);
    QTest::mouseClick(window.findChild<QPushButton*>("btnPlay"),Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->phase()=="Running"&&runner->liveSamples().size()>2,180000);
    const auto task=runner->taskId();
    auto tabs=window.findChild<QTabWidget*>();tabs->setCurrentIndex(2);
    auto tune=window.findChild<QPushButton*>("btnTune");QVERIFY(tune->isEnabled());
    window.findChild<QDoubleSpinBox*>("tuneCurrent")->setValue(2);
    window.findChild<QDoubleSpinBox*>("tuneIMax")->setValue(.5);
    window.findChild<QComboBox*>("tuneMode")->setCurrentIndex(2);
    QSignalSpy applied(runner,&SysplorerRunner::tuneApplied);
    QTest::mouseClick(tune,Qt::LeftButton);QVERIFY(!tune->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(),1,15000);
    QTRY_VERIFY_WITH_TIMEOUT(runner->liveSamples().last().current<-.49,15000);
    QCOMPARE(runner->taskId(),task);QVERIFY(runner->hasRuntimeChanges());
    QVERIFY(window.findChild<QLabel*>("tuneStatus")->text().contains(QString::fromUtf8("回读确认")));
    const QString artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath()+"/BatteryUdpUiTests");QDir().mkpath(artifacts);
    window.grab().save(artifacts+"/runtime_tuning.png");
    QSignalSpy stopped(runner,&SysplorerRunner::stopped);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->controlPending(),10000);
    runner->stop();QTRY_COMPARE_WITH_TIMEOUT(stopped.count(),1,30000);
    QVERIFY(!runner->isRunning());
 }
 void startupFailureKeepsUiUsable(){
    const auto old=qgetenv("SYSPLORER_HOME");qputenv("SYSPLORER_HOME","C:/missing/sysplorer");
    {
      MainWindow w;w.show();auto runner=w.findChild<SysplorerRunner*>();
      QSignalSpy failed(runner,&SysplorerRunner::hostConnectionFailed);
      QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);
      QVERIFY(!runner->isRunning());QVERIFY(runner->isConnecting());
      auto prepare=w.findChild<QPushButton*>("btnPrepare");QVERIFY(prepare->isEnabled());
      QSignalSpy preparationFailed(runner,&SysplorerRunner::finished);
      QTest::mouseClick(prepare,Qt::LeftButton);
      QTRY_COMPARE_WITH_TIMEOUT(preparationFailed.count(),1,5000);
      QVERIFY(!preparationFailed.first()[0].toBool());QVERIFY(prepare->isEnabled());
      QVERIFY(w.findChild<BatteryView*>()->isEnabled());
      QVERIFY(!w.findChild<QPushButton*>("btnReplay")->isEnabled());
      QTest::qWait(200);QCOMPARE(failed.count(),1);
    }
    if(old.isNull())qunsetenv("SYSPLORER_HOME");else qputenv("SYSPLORER_HOME",old);
 }
 void realButtonsAndTaskLifecycle(){
    MainWindow w;w.show();auto runner=w.findChild<SysplorerRunner*>();
    auto player=w.findChild<ReplayPlayer*>();auto view=w.findChild<BatteryView*>("batteryView");
    auto plot=w.findChild<PlotWidget*>("plotWidget");
    auto prepare=w.findChild<QPushButton*>("btnPrepare");auto start=w.findChild<QPushButton*>("btnPlay");
    auto pause=w.findChild<QPushButton*>("btnPause");auto stop=w.findChild<QPushButton*>("btnStop");
    auto replayPause=w.findChild<QPushButton*>("btnReplayPause");auto replayStop=w.findChild<QPushButton*>("btnReplayStop");
    auto replay=w.findChild<QPushButton*>("btnReplay");auto apply=view->findChild<QPushButton*>("btnApply");
    auto speed=w.findChild<QComboBox*>("cmbSpeed");auto tabs=w.findChild<QTabWidget*>();
    auto errors=w.findChild<QPlainTextEdit*>("errorPanel");
    QSignalSpy completed(runner,&SysplorerRunner::finished),stopped(runner,&SysplorerRunner::stopped);
    const auto artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath()+"/BatterySplitTests");QDir().mkpath(artifacts);
    connect(runner,&SysplorerRunner::stateChanged,this,[](const QString&p){qInfo()<<"phase"<<p;});
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isConnecting()&&!runner->phase().isEmpty(),100000);
    QVERIFY(start->isEnabled());QVERIFY(!replay->isEnabled());
    auto load=w.findChild<QPushButton*>("btnLoad");
    if(load->isEnabled()){QTest::mouseClick(load,Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Loaded"),180000);QVERIFY(!load->isEnabled());QVERIFY(start->isEnabled());}
    tabs->setCurrentIndex(0);view->setInputs(3.45,5,1,.99);QTest::mouseClick(apply,Qt::LeftButton);
    speed->setCurrentIndex(4);QTest::mouseClick(prepare,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->isPrepared()||!runner->isRunning(),180000);
    QVERIFY2(runner->isPrepared(),qPrintable(errors->toPlainText()));
    QVERIFY(start->isEnabled());QVERIFY(!pause->isEnabled());QVERIFY(!stop->isEnabled());
    QTest::qWait(500);QVERIFY(runner->liveSamples().isEmpty());QCOMPARE(completed.count(),0);
    // Same-value saving preserves preparation. Start can automatically retranslate edits;
    // translating consumes unsaved inputs and waits for discard acknowledgement.
    tabs->setCurrentIndex(0);QTest::mouseClick(apply,Qt::LeftButton);QVERIFY(start->isEnabled());
    view->findChild<QDoubleSpinBox*>("spinSoc0")->setValue(97);QVERIFY(start->isEnabled());
    QTest::mouseClick(prepare,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(runner->isPrepared()&&start->isEnabled(),180000);
    QVERIFY(qAbs(view->soc0()-.97)<1e-8);
    // Saving changed values also invalidates the translated instance.
    tabs->setCurrentIndex(0);view->setInputs(3.45,5,1,.98);QTest::mouseClick(apply,Qt::LeftButton);
    QVERIFY(!start->isEnabled());QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),5000);
    QTest::mouseClick(prepare,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->isPrepared()||!runner->isRunning(),180000);
    QVERIFY2(runner->isPrepared(),qPrintable(errors->toPlainText()));
    QTest::mouseClick(start,Qt::LeftButton);QVERIFY(!replay->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),180000);
    QCOMPARE(completed.count(),1);QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[2].toString()));
    QVERIFY(replay->isEnabled());QVERIFY(!player->isPlaying());const auto full=player->samples();QVERIFY(full.size()>1);
    QVERIFY(qAbs(full.first().soc-.98)<1e-6);QVERIFY(qAbs(full.first().current-5)<1e-4);
    const auto fullTask=runner->taskId();QTest::mouseClick(replay,Qt::LeftButton);
    QVERIFY(player->isPlaying());QCOMPARE(runner->taskId(),fullTask);QTRY_VERIFY(player->currentTime()>0);
    QTest::mouseClick(replayPause,Qt::LeftButton);const auto pos=player->currentTime();QTest::qWait(100);QCOMPARE(player->currentTime(),pos);
    QCOMPARE(replayPause->text(),QString::fromUtf8("继续回放"));
    QVERIFY(!pause->isEnabled());QVERIFY(!stop->isEnabled());
    auto playbackSpeed=w.findChild<QComboBox*>("cmbReplaySpeed");const double localSpeed=player->speed();speed->setCurrentIndex(2);QCOMPARE(player->speed(),localSpeed);playbackSpeed->setCurrentIndex(2);QCOMPARE(player->speed(),5.0);
    const auto hostTime=w.findChild<QLabel*>("lblTime")->text();
    QTest::mouseClick(replayPause,Qt::LeftButton);QTRY_VERIFY(player->currentTime()>pos);QCOMPARE(w.findChild<QLabel*>("lblTime")->text(),hostTime);
    QTest::mouseClick(replayStop,Qt::LeftButton);
    // A new simulation always requires preparation, even when previous results exist.
    tabs->setCurrentIndex(0);view->setInputs(2.5,1.25,2,.6);QTest::mouseClick(apply,Qt::LeftButton);speed->setCurrentIndex(3);
    QTest::mouseClick(prepare,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->isPrepared()||!runner->isRunning(),180000);
    QVERIFY2(runner->isPrepared(),qPrintable(errors->toPlainText()));QTest::mouseClick(start,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->phase()=="Running"&&runner->liveSamples().size()>4,30000);
    QVERIFY(!replay->isEnabled());QTest::mouseClick(pause,Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Paused"),15000);
    QTRY_VERIFY(pause->isEnabled());QVERIFY(!replay->isEnabled());const auto n=plot->sampleCount();QTest::qWait(500);QCOMPARE(plot->sampleCount(),n);
    w.grab().save(artifacts+"/paused.png");QTest::mouseClick(pause,Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(runner->phase(),QString("Running"),15000);QTRY_VERIFY(stop->isEnabled());QTest::mouseClick(stop,Qt::LeftButton);
    QVERIFY(!replay->isEnabled());QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),30000);QCOMPARE(stopped.count(),1);
    const auto partial=player->samples();QVERIFY(!partial.isEmpty());QVERIFY(player->duration()<runner->duration());QVERIFY(replay->isEnabled());
    w.grab().save(artifacts+"/stopped.png");
    auto results=w.findChild<QComboBox*>("cmbResults");results->setCurrentIndex(results->findData("cache"));
    QCOMPARE(player->samples().size(),full.size());
    QTest::mouseClick(replay,Qt::LeftButton);QVERIFY(player->isPlaying());
    QCOMPARE(player->samples().size(),full.size());QCOMPARE(player->samples().first().soc,full.first().soc);QTest::mouseClick(replayStop,Qt::LeftButton);
    results->setCurrentIndex(results->findData("partial:0"));
    // A failed preparation cannot replace the latest valid partial result.
    const auto old=qgetenv("SYSPLORER_HOME");qputenv("SYSPLORER_HOME","C:/missing/sysplorer");
    QTest::mouseClick(prepare,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),5000);
    QCOMPARE(completed.count(),2);QVERIFY(!completed.last()[0].toBool());
    if(old.isNull())qunsetenv("SYSPLORER_HOME");else qputenv("SYSPLORER_HOME",old);
    QTest::mouseClick(replay,Qt::LeftButton);QVERIFY(player->isPlaying());QCOMPARE(player->samples().size(),partial.size());
    QCOMPARE(player->samples().first().soc,partial.first().soc);QTest::mouseClick(replayStop,Qt::LeftButton);
 }
};
QTEST_MAIN(UdpIntegration)
#include "udp_integration_test.moc"

#include <QtTest>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include "main_window.h"
#include "battery_view.h"
#include "plot_widget.h"
#include "replay_player.h"
#include "sysplorer_runner.h"
class UdpIntegration:public QObject {
 Q_OBJECT
private slots:
 void startupFailureKeepsUiUsable(){
    const auto old=qgetenv("SYSPLORER_HOME");qputenv("SYSPLORER_HOME","C:/missing/sysplorer");
    {
      MainWindow w;w.show();auto runner=w.findChild<SysplorerRunner*>();
      QSignalSpy failed(runner,&SysplorerRunner::hostConnectionFailed);
      QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);
      QVERIFY(!runner->isRunning());QVERIFY(runner->isConnecting());
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
    auto replay=w.findChild<QPushButton*>("btnReplay");auto apply=view->findChild<QPushButton*>("btnApply");
    auto speed=w.findChild<QComboBox*>("cmbSpeed");auto tabs=w.findChild<QTabWidget*>();
    auto errors=w.findChild<QPlainTextEdit*>("errorPanel");
    QSignalSpy completed(runner,&SysplorerRunner::finished),stopped(runner,&SysplorerRunner::stopped);
    const auto artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath()+"/BatterySplitTests");QDir().mkpath(artifacts);
    connect(runner,&SysplorerRunner::stateChanged,this,[](const QString&p){qInfo()<<"phase"<<p;});
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isConnecting(),100000);
    QVERIFY(!start->isEnabled());QVERIFY(!replay->isEnabled());
    tabs->setCurrentIndex(0);view->setInputs(3.45,5,1,.99);QTest::mouseClick(apply,Qt::LeftButton);
    speed->setCurrentIndex(4);QTest::mouseClick(prepare,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->isPrepared()||!runner->isRunning(),180000);
    QVERIFY2(runner->isPrepared(),qPrintable(errors->toPlainText()));
    QVERIFY(start->isEnabled());QVERIFY(!pause->isEnabled());QVERIFY(!stop->isEnabled());
    QTest::qWait(500);QVERIFY(runner->liveSamples().isEmpty());QCOMPARE(completed.count(),0);
    // Applying parameters invalidates a translated instance; a second prepare is required.
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
    QTest::mouseClick(pause,Qt::LeftButton);const auto pos=player->currentTime();QTest::qWait(100);QCOMPARE(player->currentTime(),pos);
    QCOMPARE(pause->text(),QString::fromUtf8("继续"));QTest::mouseClick(pause,Qt::LeftButton);QTRY_VERIFY(player->currentTime()>pos);
    QTest::mouseClick(stop,Qt::LeftButton);
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
    QCOMPARE(player->samples().size(),partial.size());QCOMPARE(player->samples().first().soc,partial.first().soc);QTest::mouseClick(stop,Qt::LeftButton);
    // A failed preparation cannot replace the latest valid partial result.
    const auto old=qgetenv("SYSPLORER_HOME");qputenv("SYSPLORER_HOME","C:/missing/sysplorer");
    QTest::mouseClick(prepare,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),5000);
    QCOMPARE(completed.count(),2);QVERIFY(!completed.last()[0].toBool());
    if(old.isNull())qunsetenv("SYSPLORER_HOME");else qputenv("SYSPLORER_HOME",old);
    QTest::mouseClick(replay,Qt::LeftButton);QVERIFY(player->isPlaying());QCOMPARE(player->samples().size(),partial.size());
    QCOMPARE(player->samples().first().soc,partial.first().soc);
 }
};
QTEST_MAIN(UdpIntegration)
#include "udp_integration_test.moc"

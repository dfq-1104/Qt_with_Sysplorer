#include <QtTest>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "main_window.h"
#include "battery_view.h"
#include "plot_widget.h"
#include "replay_player.h"
#include "sysplorer_runner.h"
class UdpIntegration:public QObject {
 Q_OBJECT
private slots:
 void realButtonsAndTaskLifecycle() {
    MainWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto view=window.findChild<BatteryView*>("batteryView");
    auto plot=window.findChild<PlotWidget*>("plotWidget");
    auto runner=window.findChild<SysplorerRunner*>();
    auto player=window.findChild<ReplayPlayer*>();
    auto play=window.findChild<QPushButton*>("btnPlay");
    auto pause=window.findChild<QPushButton*>("btnPause");
    auto resume=window.findChild<QPushButton*>("btnResume");
    auto stop=window.findChild<QPushButton*>("btnStop");
    auto apply=view->findChild<QPushButton*>("btnApply");
    auto speed=window.findChild<QComboBox*>("cmbSpeed");
    auto results=window.findChild<QComboBox*>("cmbResults");
    auto state=window.findChild<QLabel*>("lblState");
    auto time=window.findChild<QLabel*>("lblTime");
    auto hint=window.findChild<QLabel*>("lblHint");
    auto errors=window.findChild<QPlainTextEdit*>("errorPanel");
    QVERIFY(view&&plot&&runner&&player&&play&&pause&&resume&&stop&&apply&&results);
    QSignalSpy completed(runner,&SysplorerRunner::finished);
    QSignalSpy stopped(runner,&SysplorerRunner::stopped);
    const QString artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath()+"/BatteryUdpUiTests");
    QDir().mkpath(artifacts);
    QJsonArray phases;
    QString previous;
    connect(runner,&SysplorerRunner::stateChanged,this,[&](const QString&p){
        if(p!=previous){qInfo()<<"Phase"<<p;phases.append(QJsonObject{{"phase",p},{"taskId",runner->taskId()}});
            previous=p;}
    });
    QVERIFY(!plot->hasData());QVERIFY(!runner->isRunning());
    speed->setCurrentIndex(4); // 60x applies to genuine real-time solver and cached replay.
    view->setInputs(3.45,5,1,.8);window.findChild<QTabWidget*>()->setCurrentIndex(0);QTest::mouseClick(apply,Qt::LeftButton);
    QCOMPARE(completed.count(),0);QVERIFY(!runner->isRunning());QVERIFY(!plot->hasData());
    QCOMPARE(view->findChild<QLabel*>("displaySoc")->text(),QStringLiteral("80.0 %"));
    QTest::mouseClick(play,Qt::LeftButton);QVERIFY(runner->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(runner->liveSamples().size()>3,180000);
    QVERIFY(runner->isRunning());QVERIFY(plot->sampleCount()>0);
    QVERIFY(runner->liveSamples().last().soc>.8);
    window.grab().save(artifacts+"/live_charge.png");
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),180000);
    QCOMPARE(completed.count(),1);
    QVERIFY2(completed.at(0).at(0).toBool(),qPrintable(completed.at(0).at(2).toString()));
    QVERIFY(!errors->isVisible());QVERIFY(!player->isPlaying());
    const auto full=player->samples();QVERIFY(full.size()>100);
    QVERIFY(qAbs(full.first().soc-.8)<1e-6);QVERIFY(qAbs(full.first().current-5)<1e-4);
    bool found60=false;
    for(const auto&s:full)if(qAbs(s.t-60)<.02){QVERIFY(qAbs(s.soc-(.8+.995*5*60/9000))<2e-4);found60=true;}
    QVERIFY(found60);const QString fullJob=runner->lastRunDirectory();
    QVERIFY(state->text().contains(QString::fromUtf8("仿真完成")));
    // Cache hit must not create a UDP task or open another native viewer.
    const QString fullTask=runner->taskId();
    QTest::mouseClick(play,Qt::LeftButton);
    QVERIFY(!runner->isRunning());QVERIFY(player->isPlaying());
    QCOMPARE(runner->taskId(),fullTask);QCOMPARE(completed.count(),1);
    QTRY_VERIFY(player->currentTime()>0);
    QTest::mouseClick(pause,Qt::LeftButton);
    const auto cacheTime=time->text();const auto cachePosition=player->currentTime();
    QTest::qWait(200);QCOMPARE(player->currentTime(),cachePosition);QCOMPARE(time->text(),cacheTime);
    QTest::mouseClick(resume,Qt::LeftButton);QTRY_VERIFY(player->currentTime()>cachePosition);
    QTest::mouseClick(stop,Qt::LeftButton);const auto stoppedPosition=player->currentTime();
    QTest::qWait(150);QCOMPARE(player->currentTime(),stoppedPosition);QVERIFY(!resume->isEnabled());
    // Different applied parameters must start a new real solver task.
    speed->setCurrentIndex(3); // 20x, enough time for user controls.
    view->setInputs(2.5,1.25,2,.6);window.findChild<QTabWidget*>()->setCurrentIndex(0);QTest::mouseClick(apply,Qt::LeftButton);
    QCOMPARE(view->findChild<QLabel*>("displaySoc")->text(),QStringLiteral("60.0 %"));
    QVERIFY(!runner->isRunning());
    QTest::mouseClick(play,Qt::LeftButton);QVERIFY(runner->isRunning());
    const auto partialTask=runner->taskId();QVERIFY(partialTask!=fullTask);
    QTRY_VERIFY_WITH_TIMEOUT(runner->phase()=="Running"&&runner->liveSamples().size()>4,180000);
    QVERIFY(runner->liveSamples().last().soc<.6);QVERIFY(pause->isEnabled());
    QTest::mouseClick(pause,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->phase()=="Paused",15000);
    QTRY_VERIFY(resume->isEnabled());
    QTest::qWait(250);const auto pausedTime=time->text();const auto pausedDrawn=plot->drawnUntil();
    const auto pausedCount=plot->sampleCount();const auto pausedSoc=view->findChild<QLabel*>("displaySoc")->text();
    QTest::qWait(700);QCOMPARE(time->text(),pausedTime);QCOMPARE(plot->drawnUntil(),pausedDrawn);
    QCOMPARE(plot->sampleCount(),pausedCount);QCOMPARE(view->findChild<QLabel*>("displaySoc")->text(),pausedSoc);
    window.grab().save(artifacts+"/live_paused.png");
    QTest::mouseClick(resume,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(runner->phase()=="Running",15000);
    QTRY_VERIFY_WITH_TIMEOUT(plot->drawnUntil()>pausedDrawn+.5,10000);
    QTest::mouseClick(stop,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),30000);
    QCOMPARE(stopped.count(),1);QCOMPARE(completed.count(),1);
    QVERIFY(state->text().contains(QString::fromUtf8("部分结果")));QVERIFY(!resume->isEnabled());
    QVERIFY(plot->drawnUntil()>0&&plot->drawnUntil()<runner->duration());
    const auto partialCount=plot->sampleCount();const auto partialTime=time->text();
    const auto partialSoc=view->findChild<QLabel*>("displaySoc")->text();
    QTest::qWait(200);QCOMPARE(plot->sampleCount(),partialCount);QCOMPARE(time->text(),partialTime);
    QCOMPARE(view->findChild<QLabel*>("displaySoc")->text(),partialSoc);
    QVERIFY(hint->text().contains(partialTask));QCOMPARE(results->count(),2);
    QFile partialFile(runner->lastRunDirectory()+"/result.json");QVERIFY(partialFile.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(partialFile.readAll()).object()["complete"].toBool());
    window.findChild<QTabWidget*>()->setCurrentIndex(1);
    window.grab().save(artifacts+"/partial_result.png");
    // View complete and partial results independently without altering cache.
    results->setCurrentIndex(results->findData("cache"));
    QCOMPARE(player->samples().size(),full.size());QCOMPARE(player->samples().first().soc,full.first().soc);
    results->setCurrentIndex(results->findData("partial:0"));
    QCOMPARE(plot->sampleCount(),partialCount);QCOMPARE(time->text(),partialTime);
    // Reapplying the full result parameters must reuse the old complete cache.
    view->setInputs(3.45,5,1,.8);window.findChild<QTabWidget*>()->setCurrentIndex(0);QTest::mouseClick(apply,Qt::LeftButton);
    const auto beforeCachedJob=runner->lastRunDirectory();
    QTest::mouseClick(play,Qt::LeftButton);
    QVERIFY(!runner->isRunning());QCOMPARE(runner->lastRunDirectory(),beforeCachedJob);
    QCOMPARE(completed.count(),1);QCOMPARE(stopped.count(),1);
    QCOMPARE(player->samples().size(),full.size());QVERIFY(qAbs(player->samples().first().soc-.8)<1e-6);
    QTest::mouseClick(stop,Qt::LeftButton);
    // Explicit local startup failure must not corrupt successful cache.
    const auto priorHome=qgetenv("SYSPLORER_HOME");
    qputenv("SYSPLORER_HOME","C:/missing/sysplorer");
    view->setInputs(3.45,4,1,.8);window.findChild<QTabWidget*>()->setCurrentIndex(0);QTest::mouseClick(apply,Qt::LeftButton);QTest::mouseClick(play,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),10000);
    QCOMPARE(completed.count(),2);QVERIFY(!completed.at(1).at(0).toBool());QVERIFY(errors->isVisible());
    if(priorHome.isNull())qunsetenv("SYSPLORER_HOME");else qputenv("SYSPLORER_HOME",priorHome);
    view->setInputs(3.45,5,1,.8);window.findChild<QTabWidget*>()->setCurrentIndex(0);QTest::mouseClick(apply,Qt::LeftButton);QTest::mouseClick(play,Qt::LeftButton);
    QVERIFY(!runner->isRunning());QCOMPARE(player->samples().size(),full.size());
    QTest::mouseClick(stop,Qt::LeftButton);
    // Zero current has no finite fill/drain time: a real 120-second stationary task.
    speed->setCurrentIndex(4);view->setInputs(3.45,0,1,0);window.findChild<QTabWidget*>()->setCurrentIndex(0);QTest::mouseClick(apply,Qt::LeftButton);
    const int beforeZero=completed.count();QTest::mouseClick(play,Qt::LeftButton);QVERIFY(runner->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),180000);
    QCOMPARE(completed.count(),beforeZero+1);QVERIFY2(completed.last().at(0).toBool(),qPrintable(completed.last().at(2).toString()));
    QVERIFY(!errors->isVisible());QVERIFY(qAbs(player->duration()-120)<.02);
    for(const auto&s:player->samples()){QVERIFY(qAbs(s.soc)<1e-6);QVERIFY(qAbs(s.current)<1e-6);}
    QJsonObject summary{{"ok",true},{"platform",QGuiApplication::platformName()},{"fullJob",fullJob},
        {"fullTask",fullTask},{"partialTask",partialTask},{"fullPoints",full.size()},
        {"partialPoints",partialCount},{"partialDisplayedTime",partialTime},{"phases",phases}};
    QFile output(artifacts+"/ui_verification.json");QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(QJsonDocument(summary).toJson());
 }
};
QTEST_MAIN(UdpIntegration)
#include "udp_integration_test.moc"

#include <QtTest>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QPlainTextEdit>
#include <QTabWidget>
#include "main_window.h"
#include "battery_view.h"
#include "plot_widget.h"
#include "replay_player.h"
#include "sysplorer_runner.h"

class IntegrationTest:public QObject {
 Q_OBJECT
private slots:
 void customSdkFlow() {
    MainWindow window;window.show();QTest::qWait(100);
    auto bv=window.findChild<BatteryView*>("batteryView");
    auto plot=window.findChild<PlotWidget*>("plotWidget");
    auto player=window.findChild<ReplayPlayer*>();
    auto runner=window.findChild<SysplorerRunner*>();
    auto apply=bv->findChild<QPushButton*>("btnApply");
    auto play=window.findChild<QPushButton*>("btnPlay");
    auto pause=window.findChild<QPushButton*>("btnPause");
    auto resume=window.findChild<QPushButton*>("btnResume");
    auto stop=window.findChild<QPushButton*>("btnStop");
    QVERIFY(!window.findChild<QComboBox*>("cmbScenario"));
    auto time=window.findChild<QLabel*>("lblTime");
    auto hint=window.findChild<QLabel*>("lblHint");
    QVERIFY(bv&&plot&&player&&runner&&apply&&play&&pause&&resume&&stop);
    QSignalSpy finished(runner,&SysplorerRunner::finished);
    QStringList dialogs;
    QTimer dialogTimer;
    connect(&dialogTimer,&QTimer::timeout,this,[&]{
        for(auto widget:QApplication::topLevelWidgets()) if(auto box=qobject_cast<QMessageBox*>(widget)) if(box->isVisible()) {
            dialogs<<box->text();box->accept();
        }
    });dialogTimer.start(20);
    QVERIFY(play->isEnabled());QVERIFY(!plot->hasData());
    QVERIFY(!pause->isEnabled()&&!resume->isEnabled()&&!stop->isEnabled());
    bv->setInputs(2.5,1,2,.3);
    QTest::mouseClick(apply,Qt::LeftButton);
    QVERIFY(!runner->isRunning());QVERIFY(runner->lastRunDirectory().isEmpty());
    QCOMPARE(bv->findChild<QLabel*>("displaySoc")->text(),QStringLiteral("30.0 %"));
    QVERIFY(bv->findChild<QLabel*>("displayAh")->text().contains("0.750"));
    QVERIFY(bv->findChild<QLabel*>("displayCurrent")->text().contains(QString::fromUtf8("设定放电 1.000 A")));
    QCOMPARE(bv->findChild<QLabel*>("displaySoc")->text(),QStringLiteral("30.0 %"));
    QCOMPARE(finished.count(),0);QVERIFY(!player->isPlaying());QVERIFY(!plot->hasData());
    QVERIFY2(hint->text().contains(QString::fromUtf8("参数已保存")),qPrintable(hint->text()));
    QTest::mouseClick(play,Qt::LeftButton);
    QVERIFY(runner->isRunning());
    QVERIFY(!play->isEnabled()&&!pause->isEnabled()&&!resume->isEnabled()&&!stop->isEnabled());
    QVERIFY(!apply->isEnabled());
    const QString firstJob=runner->lastRunDirectory();
    QVERIFY(!firstJob.isEmpty());
    QTest::mouseClick(play,Qt::LeftButton);QCOMPARE(runner->lastRunDirectory(),firstJob);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),240000);
    QVERIFY2(dialogs.isEmpty(),qPrintable(dialogs.join("\n")));
    QCOMPARE(finished.count(),1);QVERIFY(finished.at(0).at(0).toBool());
    QVERIFY(player->isPlaying());QVERIFY(plot->sampleCount()>100);
    const auto discharge=player->samples();
    QVERIFY(qAbs(discharge.first().soc-.3)<1e-6);
    QVERIFY(qAbs(discharge.first().current+1)<1e-6);
    auto checkPoint=[](const QVector<ReplaySample>& samples,double target,double expected) {
        for(const auto& s:samples) if(qAbs(s.t-target)<.01) return qAbs(s.soc-expected)<2e-4;
        return false;
    };
    QVERIFY(checkPoint(discharge,60,.3-60./9000));
    QVERIFY(discharge.last().soc<.01);
    QFile manifest(firstJob+"/solver/result.json");QVERIFY(manifest.open(QIODevice::ReadOnly));
    auto obj=QJsonDocument::fromJson(manifest.readAll()).object();
    QCOMPARE(obj.value("mode").toInt(),2);QCOMPARE(obj.value("voltage").toDouble(),2.5);
    QTRY_VERIFY(player->currentTime()>0);
    QTest::mouseClick(pause,Qt::LeftButton);double paused=player->currentTime();
    const auto pausedSoc=bv->findChild<QLabel*>("displaySoc")->text();
    QVERIFY(resume->isEnabled()&&!pause->isEnabled());QTest::qWait(150);
    QCOMPARE(player->currentTime(),paused);
    QCOMPARE(bv->findChild<QLabel*>("displaySoc")->text(),pausedSoc);
    QTest::mouseClick(resume,Qt::LeftButton);QTRY_VERIFY(player->currentTime()>paused);
    const QString stopTime=time->text();
    QTest::mouseClick(stop,Qt::LeftButton);
    const double stopped=player->currentTime();
    const double stoppedDrawn=plot->drawnUntil();
    const auto stoppedSoc=bv->findChild<QLabel*>("displaySoc")->text();
    QTest::qWait(150);
    QCOMPARE(player->currentTime(),stopped); QCOMPARE(plot->drawnUntil(),stoppedDrawn);
    QCOMPARE(bv->findChild<QLabel*>("displaySoc")->text(),stoppedSoc);
    QVERIFY(!resume->isEnabled());
    QCOMPARE(time->text(),stopTime);QVERIFY(plot->drawnUntil()>0);QVERIFY(play->isEnabled());
    QTest::mouseClick(play,Qt::LeftButton);
    QVERIFY(!runner->isRunning());QCOMPARE(runner->lastRunDirectory(),firstJob);QCOMPARE(finished.count(),1);
    QTest::mouseClick(stop,Qt::LeftButton);
    QCOMPARE(plot->sampleCount(),discharge.size());

    // A new charge request must launch another real SDK job, not reuse discharge results.
    bv->setInputs(3.45,2,1,.7);
    const int oldCount=plot->sampleCount();
    QTest::mouseClick(apply,Qt::LeftButton);
    QCOMPARE(plot->sampleCount(),oldCount);QVERIFY(!runner->isRunning());QVERIFY(!player->isPlaying());
    QTest::mouseClick(play,Qt::LeftButton);QVERIFY(runner->isRunning());
    const QString chargeJob=runner->lastRunDirectory();QVERIFY(chargeJob!=firstJob);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),240000);
    QVERIFY2(dialogs.isEmpty(),qPrintable(dialogs.join("\n")));
    QCOMPARE(finished.count(),2);QVERIFY(finished.at(1).at(0).toBool());
    QVERIFY(player->isPlaying());
    QVERIFY(qAbs(player->samples().first().soc-.7)<1e-6);
    QVERIFY(qAbs(player->samples().first().current-2)<1e-6);
    QVERIFY(checkPoint(player->samples(),60,.7+.995*2*60/9000));
    QVERIFY(player->samples().last().soc>.99);
    player->seek(player->duration()*.5);QTest::qWait(50);
    window.grab().save(qEnvironmentVariable("BATTERY_TEST_ARTIFACTS")+"/sdk_charge_replay.png");
    QTest::mouseClick(stop,Qt::LeftButton);

    // Changing only voltage must affect the solver. At 3.30 V this starting state is protected.
    bv->setInputs(3.30,2,1,.7);QTest::mouseClick(apply,Qt::LeftButton);
    QVERIFY(!runner->isRunning());QCOMPARE(runner->lastRunDirectory(),chargeJob);
    QTest::mouseClick(play,Qt::LeftButton);QVERIFY(runner->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),240000);
    QVERIFY2(dialogs.isEmpty(),qPrintable(dialogs.join("\n")));
    QCOMPARE(finished.count(),3);QVERIFY(finished.at(2).at(0).toBool());
    for(const auto& s:player->samples()) {QVERIFY(qAbs(s.current)<1e-6);QVERIFY(qAbs(s.soc-.7)<1e-6);}
    const auto protectedSamples=player->samples();
    const QString protectionJob=runner->lastRunDirectory();
    window.grab().save(qEnvironmentVariable("BATTERY_TEST_ARTIFACTS")+"/sdk_replay.png");
    QTest::mouseClick(stop,Qt::LeftButton);

    // A missing helper must report once, preserve the last successful curve and never fall back.
    const auto configuredHelper=qgetenv("BATTERY_HELPER_EXE");
    qputenv("BATTERY_HELPER_EXE","C:/missing/battery_helper.exe");
    bv->setInputs(3.30,1.5,1,.7);QTest::mouseClick(apply,Qt::LeftButton);
    QTest::mouseClick(play,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),10000);QTest::qWait(100);
    QCOMPARE(finished.count(),4);QVERIFY(!finished.at(3).at(0).toBool());
    QCOMPARE(plot->sampleCount(),protectedSamples.size());
    QCOMPARE(player->samples().first().soc,protectedSamples.first().soc);
    QVERIFY(play->isEnabled()&&apply->isEnabled());
    auto errorPanel=window.findChild<QPlainTextEdit*>("errorPanel");
    QVERIFY(errorPanel&&errorPanel->isVisible());
    QVERIFY(errorPanel->toPlainText().contains(QString::fromUtf8("无法启动")));
    if(configuredHelper.isNull()) qunsetenv("BATTERY_HELPER_EXE");
    else qputenv("BATTERY_HELPER_EXE",configuredHelper);
    // Zero is explicitly within the advertised 0–5 A range: real SDK calculation must succeed.
    bv->setInputs(3.45,0,1,0); QTest::mouseClick(apply,Qt::LeftButton);
    QVERIFY(!errorPanel->isVisible()); QVERIFY(!runner->isRunning());
    QTest::mouseClick(play,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(),240000);
    QCOMPARE(finished.count(),5); QVERIFY2(finished.at(4).at(0).toBool(),qPrintable(finished.at(4).at(2).toString()));
    QVERIFY(player->isPlaying());
    for(const auto& s:player->samples()) {QVERIFY(qAbs(s.current)<1e-6);QVERIFY(qAbs(s.soc)<1e-6);}
    qInfo()<<"SDK jobs:"<<firstJob<<chargeJob<<protectionJob;
 }
};
QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"

#include "main_window.h"
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QPushButton>
#include <memory>
#include "battery_view.h"
#include "sysplorer_runner.h"
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"),10));
    QPixmap icon(64,64); icon.fill(Qt::transparent);
    {QPainter p(&icon);p.setRenderHint(QPainter::Antialiasing);
     p.setPen(QPen(QColor("#33404f"),3));p.setBrush(QColor("#49bf7b"));
     p.drawRoundedRect(QRectF(12,13,40,45),7,7);p.drawRect(26,5,12,8);}
    app.setWindowIcon(QIcon(icon));
    MainWindow window; window.show();
    if(app.arguments().contains("--verify-remote-connection")) {
        auto runner=window.findChild<SysplorerRunner*>();
        auto finished=std::make_shared<bool>(false);
        auto report=[&app,&window,runner,finished](bool ok,const QString& message){
            if(*finished)return;*finished=true;
            const auto output=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS");
            if(!output.isEmpty()){
                QDir().mkpath(output);window.grab().save(QDir(output).filePath("remote_connection.png"));
                QSaveFile file(QDir(output).filePath("remote_connection.json"));
                if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"ok",ok},{"message",message},{"phase",runner->phase()},
                    {"remoteMode",runner->remoteMode()},{"address",runner->remoteAddress()},{"port",runner->remotePort()},
                    {"modelHash",runner->modelFingerprint()},{"taskId",runner->taskId()},{"running",runner->isRunning()}}).toJson());file.commit();}
            }
            QTimer::singleShot(50,&app,[&app,ok]{app.exit(ok?0:1);});
        };
        QObject::connect(runner,&SysplorerRunner::stateChanged,&app,[report](const QString& phase){if(phase=="Connected")report(true,QString());});
        QObject::connect(runner,&SysplorerRunner::hostConnectionFailed,&app,[report](const QString& message){report(false,message);});
        QTimer::singleShot(15000,&app,[report]{report(false,QString::fromUtf8("远程连接验证超时"));});
        QTimer::singleShot(0,&app,[runner,report]{if(!runner->remoteMode())report(false,QString::fromUtf8("此验证入口仅支持远程模式"));else runner->connectHost();});
    }
    if(app.arguments().contains("--verify-custom")) {
        auto runner=window.findChild<SysplorerRunner*>();
        QObject::connect(runner,&SysplorerRunner::finished,&app,[&app,runner](bool ok,const QString& csv,const QString& message){
            QJsonObject result{{"ok",ok},{"csv",csv},{"message",message},{"job",runner->lastRunDirectory()},
                {"platform",QGuiApplication::platformName()}};
            QSaveFile report(QDir(qEnvironmentVariable("BATTERY_TEST_ARTIFACTS",QDir::tempPath())).filePath("gui_custom_probe.json"));
            if(report.open(QIODevice::WriteOnly)) {report.write(QJsonDocument(result).toJson());report.commit();}
            QTimer::singleShot(300,&app,[&app,ok]{app.exit(ok?0:1);});
        });
        QObject::connect(runner,&SysplorerRunner::stateChanged,&window,[&window](const QString& phase){
            if(phase=="Prepared")window.findChild<QPushButton*>("btnPlay")->click();
        });
        auto* prepareTimer=new QTimer(&window);
        QObject::connect(prepareTimer,&QTimer::timeout,&window,[&window,prepareTimer]{
            auto prepare=window.findChild<QPushButton*>("btnPrepare");
            if(!prepare||!prepare->isEnabled())return;
            prepareTimer->stop();
            auto view=window.findChild<BatteryView*>("batteryView");
            view->setInputs(3.43,2.3,1,.65);
            view->findChild<QPushButton*>("btnApply")->click();
            prepare->click();
        });
        prepareTimer->start(200);
        QTimer::singleShot(330000,&app,[&app]{app.exit(2);});
    }
    if(app.arguments().contains("--smoke-test")) QTimer::singleShot(500,&app,[&]{
        const auto output=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS");
        if(!output.isEmpty()) {
            window.grab().save(QDir(output).filePath("ui_startup.png"));
            auto runner=window.findChild<SysplorerRunner*>();
            QSaveFile report(QDir(output).filePath("ui_startup.json"));
            if(report.open(QIODevice::WriteOnly)){
                report.write(QJsonDocument(QJsonObject{{"phase",runner->phase()},{"remoteMode",runner->remoteMode()},
                    {"connecting",runner->isConnecting()},{"running",runner->isRunning()},
                    {"taskId",runner->taskId()},{"jobDirectory",runner->lastRunDirectory()},{"modelHash",runner->modelFingerprint()},
                    {"remoteAddress",runner->remoteAddress()},{"remotePort",runner->remotePort()}}).toJson());report.commit();
            }
        }
        app.quit();
    });
    return app.exec();
}

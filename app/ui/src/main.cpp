#include "main_window.h"
#include "model_file.h"
#include <QTextStream>
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
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <memory>
#include "battery_view.h"
#include "sysplorer_runner.h"
#include "runtime_environment.h"
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    if(app.arguments().contains("--check-model")){
        const auto output=qEnvironmentVariable("BATTERY_MODEL_DIAGNOSTICS",QCoreApplication::applicationDirPath()+"/diagnostics");
        const auto result=ModelFile::read(ModelFile::defaultPath(),output);
        QTextStream stream(stdout);stream.setCodec("UTF-8");
        stream<<(result.ok?QString::fromUtf8("模型自检通过：合法 UTF-8，BatterySim.SingleRun 接口存在。\n"):QString::fromUtf8("模型自检失败：")+result.error+"\n");
        stream<<QString::fromUtf8("发布标识：")<<BATTERY_RELEASE_ID<<"\n"<<QString::fromUtf8("诊断文件：")<<result.diagnostic<<"\n";stream.flush();
        return result.ok?0:1;
    }
    const auto testConfig=qEnvironmentVariable("BATTERY_TEST_CONFIG_ROOT");
    if(!testConfig.isEmpty())QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,testConfig);
    QString environmentError;
    if(!prepareRuntimeEnvironment(&environmentError)){
        const auto output=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS");
        if(!output.isEmpty()){
            QDir().mkpath(output);QSaveFile file(QDir(output).filePath("runtime_bootstrap.json"));
            if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"ok",false},{"message",environmentError}}).toJson());file.commit();}
        }
        if(!app.arguments().contains("--smoke-test")&&!app.arguments().contains("--verify-remote-connection")&&!app.arguments().contains("--verify-local-connection"))QMessageBox::warning(nullptr,QString::fromUtf8("运行环境错误"),environmentError);
        return 2;
    }
    QString startupModelError;
    if(app.arguments().contains("--startup-model-check")){
        // Check in the actual UI process, using its own ordinary file access.
        // The release client reads its compiled Qt resource, never an external
        // model file. Task preparation validates the same model again.
        const auto output=qEnvironmentVariable("BATTERY_MODEL_DIAGNOSTICS",
            QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("BatteryConsole/diagnostics"));
        const auto result=ModelFile::read(ModelFile::defaultPath(),output);
        const auto artifacts=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS");
        if(!artifacts.isEmpty()){
            QDir().mkpath(artifacts);QSaveFile report(QDir(artifacts).filePath("startup_model_check.json"));
            if(report.open(QIODevice::WriteOnly)){
                report.write(QJsonDocument(QJsonObject{{"ok",result.ok},{"diagnostic",result.diagnostic},
                    {"message",result.error},{"pid",double(QCoreApplication::applicationPid())}}).toJson());report.commit();
            }
        }
        if(!result.ok)startupModelError=result.error;
    }
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"),10));
    QPixmap icon(64,64); icon.fill(Qt::transparent);
    {QPainter p(&icon);p.setRenderHint(QPainter::Antialiasing);
     p.setPen(QPen(QColor("#33404f"),3));p.setBrush(QColor("#49bf7b"));
     p.drawRoundedRect(QRectF(12,13,40,45),7,7);p.drawRect(26,5,12,8);}
    app.setWindowIcon(QIcon(icon));
    MainWindow window; window.show();
    if(!startupModelError.isEmpty()&&!app.arguments().contains("--smoke-test"))QTimer::singleShot(0,&window,[&window,startupModelError]{
        QMessageBox::warning(&window,QString::fromUtf8("模型启动检查未通过"),
            QString::fromUtf8("尚未连接或启动 Sysplorer。客户端内置模型校验失败。仍可使用连接设置；请完整解压配套客户端包后重试。\n\n")+
            startupModelError);
    });
    if(app.arguments().contains("--verify-remote-connection")||app.arguments().contains("--verify-local-connection")) {
        const bool expectedRemote=app.arguments().contains("--verify-remote-connection");
        auto runner=window.findChild<SysplorerRunner*>();
        auto finished=std::make_shared<bool>(false);
        auto report=[&app,&window,runner,finished,expectedRemote](bool ok,const QString& message){
            if(*finished)return;*finished=true;
            const auto output=qEnvironmentVariable("BATTERY_TEST_ARTIFACTS");
            if(!output.isEmpty()){
                QDir().mkpath(output);window.grab().save(QDir(output).filePath("remote_connection.png"));
                QSaveFile file(QDir(output).filePath(expectedRemote?"remote_connection.json":"local_connection.json"));
                if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"ok",ok},{"message",message},{"phase",runner->phase()},
                    {"remoteMode",runner->remoteMode()},{"address",runner->remoteAddress()},{"port",runner->remotePort()},
                    {"modelHash",runner->modelFingerprint()},{"taskId",runner->taskId()},{"running",runner->isRunning()},{"jobDirectory",runner->lastRunDirectory()},
                    {"worktreeRoot",qEnvironmentVariable("BATTERY_WORKTREE_ROOT")},{"hostHome",qEnvironmentVariable("SYSPLORER_HOME")},
                    {"discoveryDirectory",qEnvironmentVariable("BATTERY_DISCOVERY_DIR")}}).toJson());file.commit();}
            }
            QTimer::singleShot(50,&app,[&app,ok]{app.exit(ok?0:1);});
        };
        QObject::connect(runner,&SysplorerRunner::stateChanged,&app,[report](const QString& phase){if(phase=="Connected")report(true,QString());});
        QObject::connect(runner,&SysplorerRunner::hostConnectionFailed,&app,[report](const QString& message){report(false,message);});
        QTimer::singleShot(expectedRemote?15000:180000,&app,[report]{report(false,QString::fromUtf8("连接验证超时"));});
        QTimer::singleShot(0,&app,[runner,report,expectedRemote]{if(runner->remoteMode()!=expectedRemote)report(false,QString::fromUtf8("验证入口与连接模式不一致"));else runner->connectHost();});
    }
    if(app.arguments().contains("--verify-custom")) {
        auto runner=window.findChild<SysplorerRunner*>();
        // This explicit diagnostic action includes a connection. Ordinary
        // startup remains idle in both modes.
        if(runner->remoteMode())QTimer::singleShot(0,runner,&SysplorerRunner::connectHost);
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
                    {"remoteAddress",runner->remoteAddress()},{"remotePort",runner->remotePort()},
                    {"worktreeRoot",qEnvironmentVariable("BATTERY_WORKTREE_ROOT")},{"hostHome",qEnvironmentVariable("SYSPLORER_HOME")},
                    {"discoveryDirectory",qEnvironmentVariable("BATTERY_DISCOVERY_DIR")}}).toJson());report.commit();
            }
        }
        app.exit(startupModelError.isEmpty()?0:3);
    });
    return app.exec();
}

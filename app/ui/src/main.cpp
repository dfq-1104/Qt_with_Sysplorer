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
        if(!output.isEmpty()) window.grab().save(QDir(output).filePath("ui_startup.png"));
        app.quit();
    });
    return app.exec();
}

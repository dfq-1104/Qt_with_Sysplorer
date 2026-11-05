#pragma once
#include <mw_extension_system/mw_plugin.h>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QMap>
class QUdpSocket;
class MwSimControl;
class MwSimInstPanel;
class MwClassManager;
class UdpHostProbe : public MwPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "tongyuan.mworks.sysplorer" FILE "probe.json")
public:
    bool initialize(QString *error) override;
    void extensionsInitialized() override;
    ShutdownFlag aboutToShutdown() override;
private slots:
    void receive();
    void stopped(int code);
    void paused();
    void resumed();
    void translated(QObject *data, bool success);
private:
    bool bindHost(QString &error);
    QJsonObject status();
    QJsonObject execute(const QJsonObject &request);
    void log(QJsonObject value);
    QUdpSocket *socket = nullptr;
    QPointer<QObject> control;
    QPointer<QObject> panel;
    QPointer<QObject> translatedData;
    MwClassManager *manager = nullptr;
    QString token, logPath, selectedModel;
    QSet<QString> loadedModels;
    QMap<QString,QByteArray> responses;
};


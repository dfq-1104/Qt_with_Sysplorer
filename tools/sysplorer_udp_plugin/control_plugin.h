#pragma once
#include <mw_extension_system/mw_plugin.h>
#include <mw_sim_inst/mw_ani_listener.h>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
class QUdpSocket;
class RemoteServer;
class RemoteLink;
class MwSimControl;
class MwSimInstPanel;
class MwClassManager;
class MwSimData;
class MwAniService;
class MwWorkbenchCore;
struct LiveRow { double t,soc,current,voltage; };
class BatteryUdpControl;
class BatteryListener : public MwAniListener {
public:
    explicit BatteryListener(BatteryUdpControl *p):plugin(p){}
    void OnStart(MwAniService*) override;
    void OnStep(double,bool) override;
    void OnStop(MwAniService*,bool) override {}
    BatteryUdpControl *plugin;
    MwAniService *service=nullptr;
};
class BatteryUdpControl : public MwPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "tongyuan.mworks.sysplorer" FILE "plugin.json")
public:
    bool initialize(QString*) override;
    void extensionsInitialized() override;
    ShutdownFlag aboutToShutdown() override;
    ~BatteryUdpControl() override;
    void append(double,const std::vector<double>&);
    void registerVariables(MwAniService*,MwAniListener*);
    friend class BatteryListener;
private:
    bool bindHost(QString&);
    bool active() const;
    bool ownsController() const;
    bool hostTaskRunning() const;
    QString controllerState() const;
    void advertise();
    void receive();
    void receiveRemote(RemoteLink*,const QJsonObject&);
    QJsonObject dispatch(const QJsonObject&);
    QJsonObject execute(const QJsonObject&);
    QJsonObject snapshot(int cursor=0) const;
    void beginTask(const QJsonObject&,const QString&,const QString&,bool loadOnly=false);
    void prepare();
    void startSolver();
    void applyTune();
    void simulationStopped(int);
    void collect();
    void finish(int);
    void fail(const QString&,const QString&);
    void log(const QString&,const QJsonObject& = {});
    void showNativePlots();
    bool saveResult(const QVector<LiveRow>&,bool);
    void persist();
    QUdpSocket *socket=nullptr;
    RemoteServer *remote=nullptr;
    QTimer advertiseTimer,collectTimer;
    QElapsedTimer taskClock,startClock;
    QPointer<QObject> control,panel,data;
    MwClassManager *manager=nullptr;
    MwWorkbenchCore *core=nullptr;
    BatteryListener listener{this};
    QString session,token,endpoint,modelHash,modelText,taskId,owner,phase="Ready",jobDir,errorCode,errorMessage;
    QString csvHash,nativeResult,viewMessage;
    QString stage;
    double voltage=3.45,current=2.5,soc0=.5,duration=120,speed=20,stopAt=-1;
    int mode=1;
    bool modelLoaded=false,stopRequested=false,listenerAttached=false,shutdown=false,preparing=false;
    QJsonObject verifiedParams;
    QJsonObject pendingTune, runtimeParams;
    QJsonArray tuneHistory;
    bool resumeAfterTune=false;
    bool tuneAwaitingSample=false;
    QJsonObject tuneBoundary;
    QVector<LiveRow> live;
    QMap<QString,QByteArray> replies;
    QMap<QString,QByteArray> replyRequests;
    QElapsedTimer heartbeat;
    qint64 lastClientMs=0;
};

#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QFile>
#include <QSet>
#include <QPointer>
#include <memory>
#include "replay_data.h"
class RemoteLink;
class QSaveFile;

// Task client only. The host plugin owns model loading, compilation and solving.
class SysplorerRunner : public QObject {
    Q_OBJECT
public:
    explicit SysplorerRunner(QObject* parent=nullptr);
    ~SysplorerRunner() override;
    void connectHost();
    bool configureConnection(bool remote,const QString& address,int port,const QString& key,bool persist=true);
    bool remoteMode() const {return m_remote;}
    QString remoteAddress() const {return m_remoteAddress;}
    int remotePort() const {return m_remotePort;}
    QString connectionLabel() const;
    QString serverRunDirectory() const {return m_serverJobDir;}
    bool canReconnect() const {return m_remote&&m_remoteLost;}
    bool canRetryDownload() const {return m_remote&&m_phase=="DownloadFailed";}
    void reconnectRemote();
    void retryDownload();
    void abandonRemoteTask();
    void loadModel();
    bool isModelLoaded() const {return m_modelLoaded;}
    void beginSimulation();
    void invalidatePreparation();
    bool isPrepared() const {return m_busy && m_phase=="Prepared";}
    bool isConnecting() const {return m_connectOnly;}
    void prepareSimulation(double voltage,double current,int mode,double soc0,double duration,const QString& = QString());
    bool isRunning() const {return m_busy;}
    QString phase() const {return m_phase;}
    QString taskId() const {return m_taskId;}
    double duration() const {return m_duration;}
    bool controlPending() const {return (m_tunePending&&m_phase!="Paused") || !m_control.isEmpty() || (m_pendingCommand!="poll"&&m_pendingCommand!="hello"&&!m_pending.isEmpty());}
    bool tunePending() const {return m_tunePending;}
    QString modelFingerprint() const;
    QString lastRunDirectory() const {return m_jobDir;}
    QString lastModelFingerprint() const {return m_modelHash;}
    const QVector<ReplaySample>& liveSamples() const {return m_samples;}
    void pause();
    void resume();
    void stop();
    void setSpeed(double speed);
    void tune(const QJsonObject& params);
    bool hasRuntimeChanges() const {return m_tuned;}
signals:
    void tuneApplied(double time,const QJsonObject& params);
    void tuneRejected(const QString& message);
    void hostConnectionFailed(const QString& message);
    void connectionInfoChanged(const QString& message);
    void remoteNotice(const QString& message);
    void downloadProgress(qint64 received,qint64 total);
    void finished(bool ok,const QString& csvPath,const QString& message);
    void stopped(const QString& csvPath,const QString& message);
    void stateChanged(const QString& phase);
    void samplesArrived(const QVector<ReplaySample>& samples);
    void viewerFinished(const QString& jobDir,bool ok,const QString& message);
private:
    void pump();
    void receive();
    void handleResponse(const QJsonObject&);
    bool checkCompatibility(const QJsonObject&);
    void connectionFailed(const QString& code,const QString& message,const QJsonObject& details = {});
    void openRemote();
    void closeRemote(bool graceful=false);
    void remoteProblem(const QString&);
    void transmitPending();
    void beginDownload(const QJsonObject&);
    void nextDownload();
    void consumeDownload(const QJsonObject&);
    void downloadProblem(const QString&);
    bool discover();
    bool launchHost();
    void send(const QString&,const QJsonObject& = {});
    void consume(const QJsonObject&);
    void finalize(const QJsonObject&);
    void fail(const QString&);
    void setPhase(const QString&);
    void trace(const QString&,const QJsonObject& = {});
    void bestEffortStop();
    QString guiExecutable() const;
    QUdpSocket m_socket;
    QPointer<RemoteLink> m_link;
    bool m_remote=false,m_remoteLost=false,m_downloading=false,m_downloadReady=false,m_closingRemote=false;
    QString m_remoteAddress="127.0.0.1",m_accessKey,m_serverJobDir,m_resumePhase;
    quint16 m_remotePort=39001;
    QJsonObject m_finalStatus;
    std::unique_ptr<QSaveFile> m_downloadFile;
    int m_downloadIndex=0;
    qint64 m_downloadOffset=0,m_downloadSize=-1;
    QTimer m_timer;
    QElapsedTimer m_clock,m_pendingClock,m_phaseClock;
    QByteArray m_pending;
    QString m_pendingId,m_pendingCommand,m_control;
    QString m_clientId,m_session,m_token,m_taskId,m_modelHash,m_jobDir,m_phase="Idle";
    QString m_helloPhase,m_error;
    QJsonObject m_tuneArgs;
    int m_tuneSequence=0;
    bool m_tuned=false,m_tunePending=false;
    QFile m_log;
    quint16 m_port=0;
    qint64 m_hostPid=0,m_launchedPid=0;
    QSet<qint64> m_busyHosts;
    int m_attempts=0,m_cursor=0,m_mode=1;
    int m_hostTimeoutMs=180000;
    bool m_connectOnly=false,m_autoAttempted=false,m_connectionWarned=false;
    bool m_busy=false,m_launched=false,m_begun=false;
    bool m_loadOnly=false,m_modelLoaded=false;
    double m_voltage=3.45,m_current=2.5,m_soc0=.5,m_duration=120,m_speed=20;
    QVector<ReplaySample> m_samples;
};

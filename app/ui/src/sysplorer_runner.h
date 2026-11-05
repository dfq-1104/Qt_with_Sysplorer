#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QFile>
#include <QSet>
#include "replay_data.h"

// Task client only. The host plugin owns model loading, compilation and solving.
class SysplorerRunner : public QObject {
    Q_OBJECT
public:
    explicit SysplorerRunner(QObject* parent=nullptr);
    ~SysplorerRunner() override;
    void start(double voltage,double current,int mode,double soc0,double duration,const QString& = QString());
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
    void finished(bool ok,const QString& csvPath,const QString& message);
    void stopped(const QString& csvPath,const QString& message);
    void stateChanged(const QString& phase);
    void samplesArrived(const QVector<ReplaySample>& samples);
    void viewerFinished(const QString& jobDir,bool ok,const QString& message);
private:
    void pump();
    void receive();
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
    QTimer m_timer;
    QElapsedTimer m_clock,m_pendingClock,m_phaseClock;
    QByteArray m_pending;
    QString m_pendingId,m_pendingCommand,m_control;
    QString m_clientId,m_session,m_token,m_taskId,m_modelHash,m_jobDir,m_phase;
    QString m_helloPhase,m_error;
    QJsonObject m_tuneArgs;
    int m_tuneSequence=0;
    bool m_tuned=false,m_tunePending=false;
    QFile m_log;
    quint16 m_port=0;
    qint64 m_hostPid=0,m_launchedPid=0;
    QSet<qint64> m_busyHosts;
    int m_attempts=0,m_cursor=0,m_mode=1;
    bool m_busy=false,m_launched=false,m_begun=false;
    double m_voltage=3.45,m_current=2.5,m_soc0=.5,m_duration=120,m_speed=20;
    QVector<ReplaySample> m_samples;
};

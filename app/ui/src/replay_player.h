#pragma once

#include <QObject>
#include <QTimer>

#include "replay_data.h"

// 回放式控制器：预先把完整时序算好，这里只按仿真时间推进"播放"。
// play/pause/stop 的作用对象是回放进度，不是求解器。
class ReplayPlayer : public QObject
{
    Q_OBJECT

public:
    explicit ReplayPlayer(QObject* parent = nullptr);

    bool load(const QString& csvPath, QString* error = nullptr);
    // 直接加载内存中的序列（用户自定义参数实时计算的结果）
    bool loadFromSamples(const QVector<ReplaySample>& samples);
    bool isLoaded() const { return !m_data.isEmpty(); }
    // 当前装载的整条曲线（用于按场景缓存）
    const QVector<ReplaySample>& samples() const { return m_data.samples(); }

    void play();
    void pause();
    void stop();
    // 只把内部时间归零，不动界面（用于“停止后保留停止瞬间的读数”）
    void resetTime();
    void seek(double t);

    void setSpeed(double factor);
    double speed() const { return m_speed; }

    double currentTime() const { return m_time; }
    double duration() const { return m_data.duration(); }
    bool isPlaying() const { return m_timer.isActive(); }

signals:
    void tick(double t, double soc, double current, double voltage);
    void finished();

private slots:
    void onTick();

private:
    ReplayData m_data;
    QTimer m_timer;
    double m_time = 0.0;
    double m_speed = 1.0;
};


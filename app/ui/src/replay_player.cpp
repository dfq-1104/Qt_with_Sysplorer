#include "replay_player.h"

#include <QtGlobal>

ReplayPlayer::ReplayPlayer(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(50);   // 20 Hz，UI 刷新足够
    connect(&m_timer, &QTimer::timeout, this, &ReplayPlayer::onTick);
}

bool ReplayPlayer::load(const QString& csvPath, QString* error)
{
    m_timer.stop();
    m_time = 0.0;
    return m_data.load(csvPath, error);
}

bool ReplayPlayer::loadFromSamples(const QVector<ReplaySample>& samples)
{
    m_timer.stop();
    m_time = 0.0;
    m_data.setSamples(samples);
    return !samples.isEmpty();
}

void ReplayPlayer::play()
{
    if (m_data.isEmpty()) return;

    // 已经播到末尾时，再点播放从头开始
    if (m_time >= m_data.duration()) {
        m_time = 0.0;
    }
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void ReplayPlayer::pause()
{
    m_timer.stop();
}

void ReplayPlayer::stop()
{
    m_timer.stop();
    m_time = 0.0;
    const ReplaySample s = m_data.sampleAt(m_time);
    emit tick(s.t, s.soc, s.current, s.voltage);
}

void ReplayPlayer::resetTime()
{
    m_timer.stop();
    m_time = 0.0;
    // 不 emit tick：界面保留停止前那一刻的数值
}

void ReplayPlayer::seek(double t)
{
    m_time = qBound(0.0, t, m_data.duration());
    const ReplaySample s = m_data.sampleAt(m_time);
    emit tick(s.t, s.soc, s.current, s.voltage);
}

void ReplayPlayer::setSpeed(double factor)
{
    m_speed = (factor > 0.0) ? factor : 1.0;
}

void ReplayPlayer::onTick()
{
    const double dt = (m_timer.interval() / 1000.0) * m_speed;
    m_time += dt;

    if (m_time >= m_data.duration()) {
        m_time = m_data.duration();
        m_timer.stop();
        const ReplaySample s = m_data.sampleAt(m_time);
        emit tick(s.t, s.soc, s.current, s.voltage);
        emit finished();
        return;
    }

    const ReplaySample s = m_data.sampleAt(m_time);
    emit tick(s.t, s.soc, s.current, s.voltage);
}


#include "replay_data.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>

bool ReplayData::load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString::fromUtf8("无法打开文件: %1").arg(path);
        return false;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");

    m_samples.clear();
    m_samples.reserve(4096);

    bool firstLine = true;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (firstLine) {
            firstLine = false;
            // 跳过表头（含字母即认为是表头）
            if (line.contains(QString::fromUtf8("time"), Qt::CaseInsensitive)) continue;
        }

        const QStringList parts = line.split(',');
        if (parts.size() < 4) continue;

        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        ReplaySample s;
        s.t        = parts.at(0).toDouble(&ok1);
        s.soc      = parts.at(1).toDouble(&ok2);
        s.current  = parts.at(2).toDouble(&ok3);
        s.voltage  = parts.at(3).toDouble(&ok4);
        if (!(ok1 && ok2 && ok3 && ok4)) continue;

        m_samples.append(s);
    }

    if (m_samples.isEmpty()) {
        if (error) *error = QString::fromUtf8("文件中没有有效数据行");
        return false;
    }
    return true;
}

void ReplayData::setSamples(const QVector<ReplaySample>& samples)
{
    m_samples = samples;
}

double ReplayData::duration() const
{
    if (m_samples.isEmpty()) return 0.0;
    return m_samples.last().t - m_samples.first().t;
}

double ReplayData::socBegin() const
{
    return m_samples.isEmpty() ? 0.0 : m_samples.first().soc;
}

double ReplayData::socEnd() const
{
    return m_samples.isEmpty() ? 0.0 : m_samples.last().soc;
}

ReplaySample ReplayData::sampleAt(double t) const
{
    if (m_samples.isEmpty()) return ReplaySample();

    const double t0 = m_samples.first().t;
    const double t1 = m_samples.last().t;
    if (t <= t0) return m_samples.first();
    if (t >= t1) return m_samples.last();

    // 二分查找区间
    int lo = 0;
    int hi = m_samples.size() - 1;
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        if (m_samples.at(mid).t <= t) lo = mid; else hi = mid;
    }

    const ReplaySample& a = m_samples.at(lo);
    const ReplaySample& b = m_samples.at(hi);
    const double span = b.t - a.t;
    if (span <= 0.0) return a;

    const double w = (t - a.t) / span;
    ReplaySample out;
    out.t       = t;
    out.soc     = a.soc     + (b.soc     - a.soc)     * w;
    out.current = a.current + (b.current - a.current) * w;
    out.voltage = a.voltage + (b.voltage - a.voltage) * w;
    return out;
}


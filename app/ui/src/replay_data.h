#pragma once

#include <QString>
#include <QMetaType>
#include <QVector>

// 一条回放采样点
struct ReplaySample
{
    double t = 0.0;        // 仿真时间 s
    double soc = 0.0;      // 0..1
    double current = 0.0;  // A，充电为正
    double voltage = 0.0;  // V
};

// CSV 加载与插值：文件格式 time,SOC,I_act,V_term
class ReplayData
{
public:
    bool load(const QString& path, QString* error = nullptr);

    // 直接写入内存序列（用于用户自定义参数实时计算的结果）
    void setSamples(const QVector<ReplaySample>& samples);

    // 原始采样序列（用于把某场景的曲线缓存下来，切换场景时原样恢复）
    const QVector<ReplaySample>& samples() const { return m_samples; }

    bool isEmpty() const { return m_samples.isEmpty(); }
    int size() const { return m_samples.size(); }
    double duration() const;
    double socBegin() const;
    double socEnd() const;

    // 按仿真时间线性插值；超出范围时夹到端点
    ReplaySample sampleAt(double t) const;

private:
    QVector<ReplaySample> m_samples;
};


Q_DECLARE_METATYPE(ReplaySample)

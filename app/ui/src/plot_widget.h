#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

#include "replay_data.h"

// 曲线页：SOC / 端电压 / 电流 三个子图，全部自绘，无第三方依赖。
//
// 数据模型：控件持有“某个场景的完整曲线” + 一个“已播放到哪儿”的时刻。
// 绘制上只画已播放到当前时刻的那一段，不画整条曲线的浅色底图。
class PlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    void clearData();
    void appendSamples(const QVector<ReplaySample>& samples);

    // 一次性给出整条曲线；progressT = 进度标记位置，drawnT = 已经画到哪儿（默认与 progressT 相同）
    // 两者分开是为了「停止」：进度标记归位到 0，但图上的线保留。
    void setSamples(const QVector<ReplaySample>& samples, double progressT, double drawnT = -1.0);
    void setProgress(double t);   // 只移动标记，并按需延长已绘制范围
    void clearDrawn();            // 重新从头仿真时清掉旧的线
    void setDuration(double seconds);

    bool hasData() const { return !m_samples.isEmpty(); }
    int sampleCount() const { return m_samples.size(); }
    double progress() const { return m_progress; }
    double drawnUntil() const { return m_drawnUntil; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static double valueOf(const ReplaySample& s, int series);

    void drawChart(QPainter& painter,
                   const QRect& rect,
                   int series,               // 0 = SOC, 1 = 电压, 2 = 电流
                   double yMin, double yMax,
                   const QString& title,
                   const QColor& color,
                   double tMax,
                   const QString& unitFmt,
                   bool showTimeUnit);

    QVector<ReplaySample> m_samples;
    double m_progress = 0.0;    // 进度标记（虚线）位置
    double m_drawnUntil = 0.0;  // 曲线已经画到的时间
    double m_duration = 1800.0;
};


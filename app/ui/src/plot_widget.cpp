#include "plot_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

PlotWidget::PlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(320);
}

void PlotWidget::clearData()
{
    m_samples.clear();
    m_progress = 0.0;
    m_drawnUntil = 0.0;
    update();
}

void PlotWidget::setSamples(const QVector<ReplaySample>& samples, double progressT, double drawnT)
{
    m_samples = samples;
    const double tEnd = m_samples.isEmpty() ? 0.0 : m_samples.last().t;
    m_progress = qBound(0.0, progressT, tEnd);
    if (drawnT < 0.0) drawnT = m_progress;
    m_drawnUntil = qBound(0.0, drawnT, tEnd);
    if (m_drawnUntil < m_progress) m_drawnUntil = m_progress;
    update();
}

void PlotWidget::setProgress(double t)
{
    if (m_samples.isEmpty()) return;
    const double clamped = qBound(0.0, t, m_samples.last().t);
    const bool markerMoved = qAbs(clamped - m_progress) > 1e-9;
    const bool extend = clamped > m_drawnUntil;
    if (!markerMoved && !extend) return;

    m_progress = clamped;
    if (clamped > m_drawnUntil) m_drawnUntil = clamped;
    update();
}

void PlotWidget::clearDrawn()
{
    m_drawnUntil = 0.0;
    update();
}

void PlotWidget::setDuration(double seconds)
{
    m_duration = (seconds > 0.0) ? seconds : 1.0;
    update();
}

double PlotWidget::valueOf(const ReplaySample& s, int series)
{
    switch (series) {
    case 1:  return s.voltage;
    case 2:  return s.current;
    default: return s.soc * 100.0;
    }
}

void PlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(252, 253, 255));

    const int margin = 12;
    const int gap = 10;
    const int usable = height() - margin * 2 - gap * 2;
    if (usable < 60) return;

    const int h = usable / 3;
    const int w = width() - margin * 2;

    const QRect r1(margin, margin, w, h);
    const QRect r2(margin, margin + h + gap, w, h);
    const QRect r3(margin, margin + (h + gap) * 2, w, h);

    drawChart(p, r1, 0, 0.0,  100.0,  QString::fromUtf8("SOC（荷电状态）"), QColor(64, 186, 106), m_duration, QString::fromUtf8("%"), false);
    drawChart(p, r2, 1, 2.40, 3.60, QString::fromUtf8("端电压"),            QColor(70, 130, 200), m_duration, QString::fromUtf8("V"), false);
    drawChart(p, r3, 2, -6.0, 6.0,  QString::fromUtf8("电流"),              QColor(220, 120, 60), m_duration, QString::fromUtf8("A"), true);
}

void PlotWidget::drawChart(QPainter& p,
                           const QRect& rect,
                           int series,
                           double yMin, double yMax,
                           const QString& title,
                           const QColor& color,
                           double tMax,
                           const QString& unitFmt,
                           bool showTimeUnit)
{
    if (rect.width() < 60 || rect.height() < 40) return;

    const int leftPad = 64;
    const int rightPad = 16;
    const int topPad = 26;
    const int bottomPad = showTimeUnit ? 44 : 32;

    const QRect plot = rect.adjusted(leftPad, topPad, -rightPad, -bottomPad);

    // 背景与边框
    p.setPen(QPen(QColor(214, 222, 232), 1));
    p.setBrush(QColor(255, 255, 255));
    p.drawRect(rect);

    // 网格
    p.setPen(QPen(QColor(236, 241, 247), 1));
    for (int k = 1; k <= 4; ++k) {
        const int y = plot.top() + plot.height() * k / 5;
        p.drawLine(plot.left(), y, plot.right(), y);
    }
    for (int k = 1; k <= 4; ++k) {
        const int x = plot.left() + plot.width() * k / 4;
        p.drawLine(x, plot.top(), x, plot.bottom());
    }

    // 标题（单位跟在标题后面，不再和右侧刻度抢位置）
    QFont tf = p.font();
    tf.setPointSize(9);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(QColor(70, 82, 98));
    p.drawText(QRect(rect.left() + 10, rect.top() + 4, rect.width() - 20, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString::fromUtf8("%1   /   %2").arg(title, unitFmt));

    // Y 轴刻度
    QFont sf = p.font();
    sf.setPointSize(8);
    sf.setBold(false);
    p.setFont(sf);
    p.setPen(QColor(120, 132, 148));
    for (int k = 0; k <= 4; ++k) {
        const double v = yMax - (yMax - yMin) * k / 4.0;
        const int y = plot.top() + plot.height() * k / 4;
        p.drawText(QRect(rect.left() + 2, y - 8, leftPad - 12, 16),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', (yMax - yMin) > 3.0 ? 1 : 2));
    }

    // ---- 横轴时间刻度：首尾贴边对齐，中间居中，不会互相遮挡 ----
    for (int k = 0; k <= 4; ++k) {
        const double tv = tMax * k / 4.0;
        const int x = plot.left() + plot.width() * k / 4;

        p.setPen(QPen(QColor(150, 160, 175), 1));
        p.drawLine(x, plot.bottom(), x, plot.bottom() + 3);

        p.setPen(QColor(120, 132, 148));
        const int w = 70;
        QRect tr(x - w / 2, plot.bottom() + 5, w, 14);
        int flags = Qt::AlignHCenter | Qt::AlignTop;
        if (k == 0) {
            tr = QRect(x - 4, plot.bottom() + 5, w, 14);
            flags = Qt::AlignLeft | Qt::AlignTop;
        } else if (k == 4) {
            tr = QRect(x - w, plot.bottom() + 5, w, 14);
            flags = Qt::AlignRight | Qt::AlignTop;
        }
        p.drawText(tr, flags, QString::number(tv, 'f', 0));
    }

    if (showTimeUnit) {
        p.setPen(QColor(120, 132, 148));
        p.drawText(QRect(plot.left(), plot.bottom() + 22, plot.width(), 14),
                   Qt::AlignHCenter | Qt::AlignTop, QString::fromUtf8("仿真时间 / 秒"));
    }

    if (m_samples.size() < 2 || tMax <= 0.0) return;
    if (m_drawnUntil <= 0.0) return;   // 还没仿真过，不画线

    // ---- 曲线：画到已仿真到的位置（停止后线保留，只把标记归位）----
    QPainterPath path;
    bool started = false;

    p.setClipRect(plot);
    for (int i = 0; i < m_samples.size(); ++i) {
        const ReplaySample& s = m_samples.at(i);
        if (s.t > m_drawnUntil + 1e-9) break;   // 采样点按时间递增，后面的不用看了

        const double xr = s.t / tMax;
        const double yr = (valueOf(s, series) - yMin) / (yMax - yMin);
        const double x = plot.left() + qBound(0.0, xr, 1.0) * plot.width();
        const double y = plot.bottom() - qBound(0.0, yr, 1.0) * plot.height();

        if (!started) {
            path.moveTo(x, y);
            started = true;
        } else {
            path.lineTo(x, y);
        }
    }

    if (started) {
        p.setPen(QPen(color, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    p.setClipping(false);

    // 播放位置竖线
    if (m_progress > 0.0 && m_progress < tMax) {
        const double x = plot.left() + (m_progress / tMax) * plot.width();
        p.setPen(QPen(QColor(120, 150, 190, 170), 1, Qt::DashLine));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
}



void PlotWidget::appendSamples(const QVector<ReplaySample>& samples)
{
    m_samples+=samples;
    if(!m_samples.isEmpty()){m_progress=m_samples.last().t;m_drawnUntil=m_progress;}
    update();
}

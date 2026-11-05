#include "battery_view.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFont>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QVBoxLayout>

BatteryView::BatteryView(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    m_controls = new QWidget(this);
    m_controls->setObjectName("controlPanel");
    m_controls->setStyleSheet(
        "#controlPanel { background: rgba(255,255,255,0.85); border-radius: 10px; }"
        "QLabel { color: #33404f; }"
        "QDoubleSpinBox { min-height: 26px; }");

    auto* form = new QFormLayout(m_controls);
    form->setContentsMargins(16, 12, 16, 12);
    form->setSpacing(10);

    // 场景简介（跨两列）
    m_lblScenario = new QLabel(m_controls);
    m_lblScenario->setObjectName("scenarioInfo");
    m_lblScenario->setWordWrap(true);
    m_lblScenario->setStyleSheet(
        "color:#3d4a5a; background:#eaf2fb; border:1px solid #d6e4f5;"
        "border-radius:6px; padding:8px;");
    m_lblScenario->setText(QString::fromUtf8("请选择演示场景"));
    form->addRow(m_lblScenario);

    m_lblSocPct = new QLabel(QString::fromUtf8("50.0 %"), m_controls);
    QFont big = m_lblSocPct->font();
    big.setPointSize(16);
    big.setBold(true);
    m_lblSocPct->setFont(big);
    m_lblSocPct->setStyleSheet("color: #22884f;");

    m_lblSocAh = new QLabel(QString::fromUtf8("剩余 1.250 Ah"), m_controls);
    m_lblCurrent = new QLabel(QString::fromUtf8("电流 -2.500 A"), m_controls);

    m_spinVoltage = new QDoubleSpinBox(m_controls);
    m_spinVoltage->setObjectName(QStringLiteral("spinVoltage"));
    m_spinVoltage->setRange(2.00, 4.00);
    m_spinVoltage->setDecimals(3);
    m_spinVoltage->setSingleStep(0.01);
    m_spinVoltage->setSuffix(QString::fromUtf8(" V"));
    m_spinVoltage->setValue(m_voltage);

    m_spinCurrent = new QDoubleSpinBox(m_controls);
    m_spinCurrent->setObjectName(QStringLiteral("spinCurrent"));
    m_spinCurrent->setRange(0.0, 5.0);
    m_spinCurrent->setDecimals(2);
    m_spinCurrent->setSingleStep(0.1);
    m_spinCurrent->setSuffix(QString::fromUtf8(" A"));
    // 输入框只填幅值，充/放电方向由上方工况决定
    m_spinCurrent->setValue(qAbs(m_current));

    // 起始电量：所有场景都可改，充满/放空后改回来就能再演示一次
    m_spinSoc0 = new QDoubleSpinBox(m_controls);
    m_spinSoc0->setObjectName(QStringLiteral("spinSoc0"));
    m_spinSoc0->setRange(0.0, 100.0);
    m_spinSoc0->setDecimals(1);
    m_spinSoc0->setSingleStep(5.0);
    m_spinSoc0->setSuffix(QString::fromUtf8(" %"));
    m_spinSoc0->setValue(50.0);
    m_spinSoc0->setToolTip(QString::fromUtf8(
        "本次仿真从多少电量开始。保存参数只保存设置；翻译或开始时读取当前输入。"));

    // 充放电方向（仅自定义场景可用）
    auto* dirRow = new QWidget(m_controls);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->setSpacing(18);
    m_radioCharge = new QRadioButton(QString::fromUtf8("充电"), dirRow);
    m_radioDischarge = new QRadioButton(QString::fromUtf8("放电"), dirRow);
    m_radioCharge->setObjectName(QStringLiteral("radioCharge"));
    m_radioDischarge->setObjectName(QStringLiteral("radioDischarge"));
    m_radioCharge->setToolTip(QString::fromUtf8("电流为正向，电量上升，充满为止"));
    m_radioDischarge->setToolTip(QString::fromUtf8("电流为负向，电量下降，放到空或到截止电压为止"));
    m_radioCharge->setChecked(true);
    dirLayout->addWidget(m_radioCharge);
    dirLayout->addWidget(m_radioDischarge);
    dirLayout->addStretch(1);

    m_btnApply = new QPushButton(QString::fromUtf8("保存参数"), m_controls);
    m_btnApply->setObjectName(QStringLiteral("btnApply"));
    m_btnApply->setToolTip(QString::fromUtf8(
        "校验并保存设置；不会启动计算或回放。"));
    connect(m_btnApply, &QPushButton::clicked, this, [this]() {
        const int mode = (m_radioCharge && m_radioCharge->isChecked()) ? 1 : 2;
        emit applyRequested(m_spinVoltage->value(),
                            m_spinCurrent->value(),
                            mode,
                            m_spinSoc0->value() / 100.0);
    });

    form->addRow(QString::fromUtf8("当前电量"), m_lblSocPct);
    form->addRow(QString(), m_lblSocAh);
    form->addRow(QString::fromUtf8("方  向"), dirRow);
    form->addRow(QString::fromUtf8("截止电压 (2.50–3.45 V)"), m_spinVoltage);
    m_spinVoltage->setToolTip(QString::fromUtf8("恒流充电的电压上限，或恒流放电的电压下限；实际端电压由模型计算。"));
    connect(m_radioCharge,&QRadioButton::toggled,this,[this](bool checked){
        m_spinVoltage->setValue(checked?3.45:2.50);
    });
    form->addRow(QString::fromUtf8("电  流 (0–5 A)"), m_spinCurrent);
    form->addRow(QString(), m_lblCurrent);
    form->addRow(QString::fromUtf8("起始电量 (0–100%)"), m_spinSoc0);
    form->addRow(QString(), m_btnApply);

    root->addWidget(m_controls, 2);
    root->addStretch(3);
}

QRect BatteryView::batteryArea() const
{
    // 控件隐藏时（曲线页右侧的小电池）占满整个区域
    if (!m_controls || !m_controls->isVisible()) {
        return rect();
    }
    const int left = m_controls->geometry().right() + 1;
    if (left >= width() || left <= 0) {
        return rect();
    }
    return QRect(left, 0, width() - left, height());
}

void BatteryView::setControlsVisible(bool visible)
{
    if (!m_controls) return;
    m_controls->setVisible(visible);
    update();
}

bool BatteryView::controlsVisible() const
{
    return m_controls && m_controls->isVisible();
}

void BatteryView::setSoc(double soc)
{
    m_soc = qBound(0.0, soc, 1.0);
    m_lblSocPct->setText(QString::fromUtf8("%1 %").arg(m_soc * 100.0, 0, 'f', 1));
    m_lblSocPct->setObjectName(QStringLiteral("displaySoc"));
    m_lblSocAh->setText(QString::fromUtf8("剩余 %1 Ah").arg(m_soc * m_capacityAh, 0, 'f', 3));
    m_lblSocAh->setObjectName(QStringLiteral("displayAh"));
    update();
}

void BatteryView::setVoltage(double v)
{
    m_voltage = v;
    update();
}

void BatteryView::setCurrent(double i)
{
    m_current = i;

    // 模型约定：充电为正、放电为负。这里转成方向文字，避免负号被误读。
    QString direction;
    if (i > 1e-6) {
        direction = QString::fromUtf8("充电");
    } else if (i < -1e-6) {
        direction = QString::fromUtf8("放电");
    } else {
        direction = QString::fromUtf8("静置");
    }

    m_lblCurrent->setText(QString::fromUtf8("%1 %2 A")
                              .arg(direction)
                              .arg(qAbs(i), 0, 'f', 3));
}

void BatteryView::setCapacityAh(double ah)
{
    m_capacityAh = ah;
    setSoc(m_soc);
}

void BatteryView::setSavedPreview(double soc,double voltage,double current,int mode)
{
    setSoc(soc); setVoltage(voltage); setCurrent(0);
    m_lblCurrent->setText(QString::fromUtf8("设定%1 %2 A（待仿真）")
        .arg(mode==1?QString::fromUtf8("充电"):QString::fromUtf8("放电"))
        .arg(current,0,'f',3));
    m_lblCurrent->setObjectName(QStringLiteral("displayCurrent"));
}

void BatteryView::setApplyEnabled(bool enabled)
{
    if (m_btnApply) m_btnApply->setEnabled(enabled);
}
void BatteryView::setParametersEnabled(bool enabled) { m_controls->setEnabled(enabled); }

double BatteryView::voltage() const
{
    return m_spinVoltage ? m_spinVoltage->value() : 3.45;
}

double BatteryView::current() const
{
    return m_spinCurrent ? m_spinCurrent->value() : 2.5;
}

int BatteryView::mode() const
{
    return (m_radioCharge && m_radioCharge->isChecked()) ? 1 : 2;
}

double BatteryView::soc0() const
{
    return m_spinSoc0 ? m_spinSoc0->value() / 100.0 : 0.5;
}

void BatteryView::setInputs(double voltage, double current, int mode, double soc0)
{
    if (!m_spinVoltage || !m_spinCurrent) return;

    {
        const QSignalBlocker b1(m_spinVoltage);
        const QSignalBlocker b2(m_spinCurrent);
        m_spinVoltage->setValue(qBound(m_spinVoltage->minimum(), voltage,
                                       m_spinVoltage->maximum()));
        m_spinCurrent->setValue(qBound(m_spinCurrent->minimum(), qAbs(current),
                                       m_spinCurrent->maximum()));
    }

    if (m_spinSoc0) {
        const QSignalBlocker b(m_spinSoc0);
        m_spinSoc0->setValue(qBound(0.0, soc0 * 100.0, 100.0));
    }

    if (m_radioCharge && m_radioDischarge) {
        const QSignalBlocker b3(m_radioCharge);
        const QSignalBlocker b4(m_radioDischarge);
        m_radioCharge->setChecked(mode == 1);
        m_radioDischarge->setChecked(mode != 1);
    }
}

void BatteryView::setScenarioInfo(const QString& title,
                                  const QString& description,
                                  bool customizable)
{
    QString text = QString::fromUtf8("%1\n%2").arg(title, description);
    if (customizable) {
        text += QString::fromUtf8("\n可改电压/电流/方向/起始电量，改完可点「保存参数」。");
    } else {
        text += QString::fromUtf8("\n本场景电流固定，起始电量可改；应用参数后点击仿真计算。");
    }
    m_lblScenario->setText(title);
    m_lblScenario->setToolTip(text);
    setCustomEnabled(customizable);
}

void BatteryView::setCustomEnabled(bool enabled)
{
    // 电压/电流/方向按场景锁定；「当前电量」与「应用参数」对所有场景开放
    m_spinVoltage->setEnabled(enabled);
    m_spinCurrent->setEnabled(enabled);
    m_radioCharge->setEnabled(enabled);
    m_radioDischarge->setEnabled(enabled);

    m_btnApply->setToolTip(QString::fromUtf8("仅校验并保存设置；点击仿真后才计算。"));
}

void BatteryView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 背景
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, QColor(243, 248, 255));
    bg.setColorAt(1.0, QColor(226, 236, 248));
    p.fillRect(rect(), bg);

    const QRect area = batteryArea();
    if (area.width() < 60 || area.height() < 60) return;

    // ---- 电池主体尺寸：高度优先，宽高比 0.52 ----
    int bodyH = static_cast<int>(area.height() * 0.76);
    int bodyW = static_cast<int>(bodyH * 0.52);
    if (bodyW > area.width() * 0.62) {
        bodyW = static_cast<int>(area.width() * 0.62);
        bodyH = static_cast<int>(bodyW / 0.52);
    }
    if (bodyW < 40 || bodyH < 60) return;

    const QPoint center = area.center();
    const QRect body(center.x() - bodyW / 2, center.y() - bodyH / 2, bodyW, bodyH);

    const int capW = qMax(10, bodyW / 4);
    const int capH = qMax(4, bodyH / 16);
    const QRect cap(center.x() - capW / 2, body.top() - capH, capW, capH);

    // ---- 正极 ----
    p.setPen(QPen(QColor(96, 108, 124), 2));
    p.setBrush(QColor(132, 145, 162));
    p.drawRoundedRect(cap, 3, 3);

    // ---- 外壳 ----
    const int radius = qMax(6, bodyW / 10);
    p.setPen(QPen(QColor(70, 82, 98), 3));
    p.setBrush(QColor(251, 253, 255));
    p.drawRoundedRect(body, radius, radius);

    // ---- 电量填充 ----
    const int inset = qMax(5, bodyW / 16);
    const QRect inner = body.adjusted(inset, inset, -inset, -inset);
    const int fillH = static_cast<int>(inner.height() * qBound(0.0, m_soc, 1.0));
    if (fillH > 0) {
        const QRect fill(inner.left(), inner.bottom() - fillH + 1, inner.width(), fillH);

        QColor level = (m_soc > 0.5) ? QColor(64, 186, 106)
                     : (m_soc > 0.2) ? QColor(240, 178, 60)
                                     : QColor(224, 82, 82);
        QLinearGradient lg(fill.topLeft(), fill.bottomLeft());
        lg.setColorAt(0.0, level.lighter(118));
        lg.setColorAt(1.0, level.darker(106));

        p.setPen(Qt::NoPen);
        p.setBrush(lg);
        p.drawRoundedRect(fill, qMax(2, radius / 2), qMax(2, radius / 2));
    }

    // ---- 电量文字 ----
    QFont f = font();
    f.setPointSizeF(qMax(11.0, bodyW / 7.0));
    f.setBold(true);
    p.setFont(f);

    const QString text = QString::fromUtf8("%1%").arg(m_soc * 100.0, 0, 'f', 0);
    const QRect textRect = body.adjusted(0, 0, 0, -bodyH / 5);

    p.setPen(QColor(255, 255, 255, 200));
    p.drawText(textRect.translated(1, 1), Qt::AlignCenter, text);
    p.setPen(QColor(46, 58, 72));
    p.drawText(textRect, Qt::AlignCenter, text);
}


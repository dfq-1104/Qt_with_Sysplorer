#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QDoubleSpinBox;
class QPushButton;
class QRadioButton;

// 电池视图：上半部自绘卡通电池（含电量填充），下半部放电压/电流输入与电量显示
class BatteryView : public QWidget
{
    Q_OBJECT

public:
    explicit BatteryView(QWidget* parent = nullptr);

    void setSoc(double soc);          // 0..1
    void setVoltage(double v);
    void setCurrent(double i);
    void setSavedPreview(double soc, double voltage, double current, int mode);
    void setCapacityAh(double ah);

    // 回填输入框（切换场景时恢复该场景的电压/电流/方向/起始电量）
    void setInputs(double voltage, double current, int mode, double soc0); // mode: 1 充电, 2 放电

    // 当前输入框的值（供主窗口判断“是否需要重新仿真”）
    double voltage() const;
    double current() const;   // 幅值，正数
    int    mode() const;      // 1 充电，2 放电
    double soc0() const;      // 0..1

    // 场景简介 + 是否允许自定义输入
    void setScenarioInfo(const QString& title, const QString& description, bool customizable);
    void setCustomEnabled(bool enabled);
    void setApplyEnabled(bool enabled);   // Sysplorer 仿真期间置灰「应用参数」
    void setParametersEnabled(bool enabled);

    // 只显示电池图形（曲线页右侧用）
    void setControlsVisible(bool visible);
    bool controlsVisible() const;

signals:
    void applyRequested(double voltage, double current, int mode, double soc0);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect batteryArea() const;

    double m_soc = 0.5;
    double m_voltage = 3.235;
    double m_current = -2.5;
    double m_capacityAh = 2.5;

    QLabel*         m_lblSocPct = nullptr;
    QLabel*         m_lblSocAh = nullptr;
    QLabel*         m_lblCurrent = nullptr;
    QLabel*         m_lblScenario = nullptr;
    QWidget*        m_controls = nullptr;
    QDoubleSpinBox* m_spinVoltage = nullptr;
    QDoubleSpinBox* m_spinCurrent = nullptr;
    QDoubleSpinBox* m_spinSoc0 = nullptr;
    QRadioButton*   m_radioCharge = nullptr;
    QRadioButton*   m_radioDischarge = nullptr;
    QPushButton*    m_btnApply = nullptr;
};


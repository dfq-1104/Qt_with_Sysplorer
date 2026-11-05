#pragma once

#include <QMainWindow>
#include <QString>
#include <QVector>

#include "replay_data.h"

class QLabel;
class QPushButton;
class QComboBox;
class QTabWidget;
class QPlainTextEdit;
class QDoubleSpinBox;

class BatteryView;
class PlotWidget;
class ReplayPlayer;
class SysplorerRunner;

// 最近一次完整成功缓存与已应用参数。实时任务和部分结果单独管理。
struct ScenarioState
{
    QVector<ReplaySample> samples;   // 该场景当前生效的完整曲线
    bool   loaded = false;           // 是否已经加载 / 算过
    bool   customized = false;       // 是否被「应用参数」自定义覆盖
    double position = 0.0;           // 上次停住的仿真时间 s
    double drawn = 0.0;              // 曲线上已经画到的位置，停止后保留
    double voltage = 3.45;           // 当前曲线使用的电压
    double current = 2.50;           // 当前曲线使用的电流
    int    mode = 2;                 // 1 充电，2 放电
    double soc0 = 0.50;              // 当前曲线的起始电量 0..1

    // 界面上已保存、但还没用于仿真的参数（点「应用参数」写入）
    double uiVoltage = 3.45;
    double uiCurrent = 2.50;
    int    uiMode = 2;
    double uiSoc0 = 0.50;
    QString hint;                    // 该场景的提示文字
    QString errorText;               // 仅属于本场景的最近一次错误
    bool savedPreview = false;
    bool realSdk = false;
    QString modelHash;
    QString sdkJobDir;
};

// 主窗口：控制区 + 电池视图 + 曲线页
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onPrepare();
    void onReplay();
    void onPlay();
    void onPause();
    void onResume();
    void onStop();
    void onTick(double t, double soc, double current, double voltage);
    void onFinished();
    void onApplyCustom(double voltage, double current, int mode, double soc0);
    void onSysplorerFinished(bool ok, const QString& csvPath, const QString& message);
    void onHostStateChanged(const QString& phase);
    void onLiveSamples(const QVector<ReplaySample>& samples);
    void onSysplorerStopped(const QString& csvPath,const QString& message);
    void showStoredResult(int index);
    void onViewerFinished(const QString& jobDir, bool ok, const QString& message);

private:
    void buildUi();
    void refreshResultSelector();

    void applyScenario(int index);           // 把某个场景的快照装到界面上
    bool loadScenarioData(int index);        // 首次进入时读 CSV 并缓存
    void saveActiveState();                  // 记录当前场景的进度

    void setStateText(const QString& phase); // 自动带「自定义 ·」前缀
    void refreshButtons();
    void showSavedPreview();
    // 保存通过校验的完整结果，同时更新最近有效结果。
    void applyComputedCurve(QVector<ReplaySample> samples,
                            double voltage, double current, int mode, double soc0,
                            const QString& hint);

    void startSysplorerRun(double voltage, double current, int mode, double soc0);
    QString restoreStateText(const ScenarioState& st) const;

    QVector<ReplaySample> m_latestSamples;
    bool m_latestPartial=false;
    QPushButton* m_btnPrepare=nullptr;
    QPushButton* m_btnReplay=nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnPause = nullptr;

    QPushButton* m_btnStop = nullptr;
    QComboBox*   m_cmbSpeed = nullptr;
    QComboBox*   m_resultSelector = nullptr;
    QLabel*      m_lblTime = nullptr;
    QLabel*      m_lblState = nullptr;
    QLabel*      m_lblHint = nullptr;
    QPlainTextEdit* m_errorPanel = nullptr;
    QWidget* m_tuneControls = nullptr;
    QComboBox* m_tuneMode = nullptr;
    QDoubleSpinBox *m_tuneCurrent=nullptr,*m_tuneVoltage=nullptr,*m_tuneMin=nullptr,*m_tuneMax=nullptr,*m_tuneLimit=nullptr;
    QLabel* m_tuneStatus=nullptr;

    QTabWidget*   m_tabs = nullptr;
    BatteryView*  m_batteryView = nullptr;
    BatteryView*  m_sideBattery = nullptr;   // 曲线页右侧的小电池
    PlotWidget*   m_plotWidget = nullptr;

    ReplayPlayer* m_player = nullptr;
    SysplorerRunner* m_runner = nullptr;   // 宿主连接、准备及求解控制
    int m_requestIndex = -1;
    QString m_viewerJob, m_viewerMessage;
    bool m_viewerOk = false;
    bool m_stopped = false;

    // 正在跑的 Sysplorer 请求参数（成功时写回场景快照）
    double m_reqVoltage = 3.45;
    double m_reqCurrent = 2.50;
    int    m_reqMode = 2;
    double m_reqSoc0 = 0.50;

    struct PartialResult { QString taskId; QVector<ReplaySample> samples; QString csvPath; double voltage,current,soc0; int mode; };
    QVector<PartialResult> m_partials;
    QVector<ReplaySample> m_liveSamples;
    QString m_display = "preview";
    int m_partialIndex = -1;
    QVector<ScenarioState> m_states;   // 每个场景一份结果
    int m_activeIndex = -1;            // 当前场景
};


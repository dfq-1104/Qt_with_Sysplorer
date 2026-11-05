#include "main_window.h"

#include "battery_view.h"
#include "plot_widget.h"
#include "replay_player.h"
#include "sysplorer_runner.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QSignalBlocker>
#include <QTimer>
#include <QPlainTextEdit>
#include <cmath>

namespace {
void showWarning(QWidget* parent,const QString& title,const QString& text) {
    auto* box=new QMessageBox(QMessageBox::Warning,title,text,QMessageBox::Ok,parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    // FailedToStart can be emitted inside QProcess::start. Show the error after
    // that call unwinds instead of entering a nested event loop from its signal.
    QTimer::singleShot(0,box,[box]{box->open();});
}

struct Scenario
{
    const char* name;
    double capacityAh;
    const char* note;
    bool customizable;
    int    defMode;         // 1 充电，2 放电
    double defVoltage;      // 输入框回填
    double defCurrent;      // A
};

const Scenario kScenarios[] = {
    { "自定义充放电", 2.5,
      "按设定电流充电或放电，电压用作充电上限或放电下限。相同参数直接回放内存结果，参数变化后调用本地 Sysplorer 计算。",
      true,  1, 3.45, 2.5 },
};
const int kScenarioCount = static_cast<int>(sizeof(kScenarios) / sizeof(kScenarios[0]));

const double kSpeeds[] = { 0.5, 1.0, 5.0, 20.0, 60.0 };
const int kSpeedCount = static_cast<int>(sizeof(kSpeeds) / sizeof(kSpeeds[0]));

// 安全边界，来自模型定义：V_min=2.50、V_max=3.45、I_max=5.0
const double kVMin = 2.50;
const double kVMax = 3.45;
const double kIMax = 5.00;

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_player = new ReplayPlayer(this);
    m_runner = new SysplorerRunner(this);
    m_states.resize(kScenarioCount);
    for(int i=0;i<kScenarioCount;++i) {
        m_states[i].uiVoltage=kScenarios[i].defVoltage;
        m_states[i].uiCurrent=kScenarios[i].defCurrent;
        m_states[i].uiMode=kScenarios[i].defMode;
    }

    connect(m_player, &ReplayPlayer::tick, this, &MainWindow::onTick);
    connect(m_player, &ReplayPlayer::finished, this, &MainWindow::onFinished);
    connect(m_runner, &SysplorerRunner::finished, this, &MainWindow::onSysplorerFinished);
    connect(m_runner, &SysplorerRunner::viewerFinished, this, &MainWindow::onViewerFinished);

    connect(m_runner, &SysplorerRunner::stateChanged, this, &MainWindow::onHostStateChanged);
    connect(m_runner, &SysplorerRunner::samplesArrived, this, &MainWindow::onLiveSamples);
    connect(m_runner, &SysplorerRunner::stopped, this, &MainWindow::onSysplorerStopped);

    buildUi();
    m_activeIndex=0;
    applyScenario(0);

    setWindowTitle(QString::fromUtf8("电池充放电仿真操作台"));
    resize(1180, 780);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------------- 控制条 ----------------
    auto* bar = new QWidget(central);
    bar->setObjectName("controlBar");
    bar->setStyleSheet(
        "#controlBar { background: #f7f9fc; border-bottom: 1px solid #dde5ef; }"
        "QPushButton { min-width: 78px; min-height: 30px; }"
        "QLabel { color: #33404f; }");

    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 8, 12, 8);
    h->setSpacing(10);

    m_btnPlay = new QPushButton(QString::fromUtf8("仿真"), bar);
    m_btnPause = new QPushButton(QString::fromUtf8("暂停"), bar);
    m_btnResume = new QPushButton(QString::fromUtf8("继续"), bar);
    m_btnStop = new QPushButton(QString::fromUtf8("停止"), bar);
    m_btnPlay->setObjectName(QStringLiteral("btnPlay"));
    m_btnPause->setObjectName(QStringLiteral("btnPause"));
    m_btnResume->setObjectName(QStringLiteral("btnResume"));
    m_btnStop->setObjectName(QStringLiteral("btnStop"));

    m_cmbSpeed = new QComboBox(bar);
    m_cmbSpeed->setObjectName("cmbSpeed");
    for (int i = 0; i < kSpeedCount; ++i) {
        m_cmbSpeed->addItem(QString::fromUtf8("%1x").arg(kSpeeds[i], 0, 'g', 3));
    }
    m_cmbSpeed->setCurrentIndex(3);   // 默认 20x

    m_lblTime = new QLabel(QString::fromUtf8("0.0 / 0.0 s"), bar);
    m_lblTime->setMinimumWidth(150);
    m_lblTime->setObjectName(QStringLiteral("lblTime"));
    m_lblTime->setAlignment(Qt::AlignCenter);
    QFont tf = m_lblTime->font();
    tf.setBold(true);
    m_lblTime->setFont(tf);

    m_lblState = new QLabel(QString::fromUtf8("就绪"), bar);
    m_lblState->setMinimumWidth(70);
    m_lblState->setObjectName(QStringLiteral("lblState"));

    h->addWidget(m_btnPlay);
    h->addWidget(m_btnPause);
    h->addWidget(m_btnResume);
    h->addWidget(m_btnStop);
    h->addSpacing(14);
    h->addWidget(new QLabel(QString::fromUtf8("速度："), bar));
    h->addWidget(m_cmbSpeed);
    h->addSpacing(14);
    h->addWidget(m_lblTime);
    h->addWidget(m_lblState);
    h->addStretch(1);

    // ---------------- 选项卡 ----------------
    m_tabs = new QTabWidget(central);
    m_batteryView = new BatteryView(m_tabs);
    m_batteryView->setObjectName(QStringLiteral("batteryView"));
    m_plotWidget = new PlotWidget(m_tabs);
    m_plotWidget->setObjectName(QStringLiteral("plotWidget"));
    m_sideBattery = new BatteryView(m_tabs);
    m_sideBattery->setControlsVisible(false);   // 只保留电池图形

    m_tabs->addTab(m_batteryView, QString::fromUtf8("电池"));

    // 曲线页：左侧曲线，右侧电池图
    auto* plotPage = new QWidget(m_tabs);
    auto* plotRow = new QHBoxLayout(plotPage);
    plotRow->setContentsMargins(0, 0, 0, 0);
    plotRow->setSpacing(8);

    auto* plotColumn = new QWidget(plotPage);
    auto* plotLayout = new QVBoxLayout(plotColumn);
    plotLayout->setContentsMargins(0, 0, 0, 0);
    plotLayout->setSpacing(0);

    auto* plotCaption = new QLabel(
        QString::fromUtf8("曲线按仿真时间逐段绘制，虚线为当前进度；横轴为仿真时间（秒）"),
        plotColumn);
    plotCaption->setStyleSheet(QString::fromUtf8("color:#6b7a8c; padding:6px 14px 2px 14px;"));
    m_resultSelector = new QComboBox(plotColumn);
    m_resultSelector->setObjectName("cmbResults");
    m_resultSelector->addItem(QString::fromUtf8("尚无仿真结果"),"none");
    plotLayout->addWidget(m_resultSelector);
    connect(m_resultSelector,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&MainWindow::showStoredResult);
    plotLayout->addWidget(plotCaption);
    plotLayout->addWidget(m_plotWidget, 1);

    plotRow->addWidget(plotColumn, 3);
    plotRow->addWidget(m_sideBattery, 2);
    m_tabs->addTab(plotPage, QString::fromUtf8("结果曲线"));

    root->addWidget(bar);
    m_errorPanel=new QPlainTextEdit(central);
    m_errorPanel->setObjectName("errorPanel");
    m_errorPanel->setReadOnly(true);
    m_errorPanel->setMaximumHeight(160);
    m_errorPanel->setStyleSheet("background:#fff1f0;color:#972f29;border:1px solid #f0b5b0;padding:8px;");
    m_errorPanel->hide();
    root->addWidget(m_errorPanel);
    root->addWidget(m_tabs, 1);

    setCentralWidget(central);

    // ---------------- 提示行 ----------------
    m_lblHint = new QLabel(
        QString::fromUtf8("应用参数只校验并保存；新任务实时绘图，相同参数复用完整成功缓存。"),
        this);
    m_lblHint->setStyleSheet(QString::fromUtf8("color:#6b7a8c; padding:4px 12px;"));
    m_lblHint->setObjectName(QStringLiteral("lblHint"));
    m_lblHint->setWordWrap(true);
    m_lblHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_lblHint, 1);

    connect(m_btnPlay, &QPushButton::clicked, this, &MainWindow::onPlay);
    connect(m_btnPause, &QPushButton::clicked, this, &MainWindow::onPause);
    connect(m_btnResume, &QPushButton::clicked, this, &MainWindow::onResume);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_cmbSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index >= 0 && index < kSpeedCount) {
                    m_player->setSpeed(kSpeeds[index]);
                    m_runner->setSpeed(kSpeeds[index]);
                    refreshButtons();
                }
            });
    connect(m_batteryView, &BatteryView::applyRequested,
            this, &MainWindow::onApplyCustom);

    // 下拉框建好时已经选了默认速度，但那时还没连信号，这里补一次同步
    m_player->setSpeed(kSpeeds[m_cmbSpeed->currentIndex()]);
    m_runner->setSpeed(kSpeeds[m_cmbSpeed->currentIndex()]);
}

// 记录“当前场景停在哪一秒”，切换前调用
void MainWindow::saveActiveState()
{
    if (m_activeIndex < 0 || m_activeIndex >= m_states.size()) return;
    ScenarioState& st = m_states[m_activeIndex];
    if (!st.loaded || (m_display!="cache"&&m_display!="complete")) return;
    st.position = m_player->currentTime();
    st.drawn = m_plotWidget->drawnUntil();
}

// No preset data: only a successful SDK calculation populates the memory cache.
bool MainWindow::loadScenarioData(int index)
{
    return m_states[index].loaded;
}

// 把某个场景的快照（曲线 + 进度 + 输入 + 提示）装回界面
void MainWindow::applyScenario(int index)
{
    const Scenario& sc = kScenarios[index];
    ScenarioState& st = m_states[index];

    m_batteryView->setCapacityAh(sc.capacityAh);
    m_errorPanel->setPlainText(st.errorText);
    m_errorPanel->setVisible(!st.errorText.isEmpty());
    m_sideBattery->setCapacityAh(sc.capacityAh);

    const bool ok = loadScenarioData(index);

    QString desc = QString::fromUtf8(sc.note);
    if (st.customized && !st.samples.isEmpty()) {
        desc += QString::fromUtf8("\n当前曲线：起点 %1%，%2 A %3，共 %4 s")
                    .arg(st.soc0 * 100.0, 0, 'f', 0)
                    .arg(st.current, 0, 'f', 2)
                    .arg(st.mode == 1 ? QString::fromUtf8("充电")
                                      : QString::fromUtf8("放电"))
                    .arg(st.samples.last().t, 0, 'f', 0);
    }
    m_batteryView->setScenarioInfo(QString::fromUtf8(sc.name), desc, sc.customizable);

    if (!ok) {
        m_batteryView->setInputs(st.uiVoltage, st.uiCurrent, st.uiMode, st.uiSoc0);
        m_player->loadFromSamples({});
        m_plotWidget->clearData();
        m_batteryView->setSoc(st.uiSoc0); m_batteryView->setCurrent(0);
        m_sideBattery->setSoc(st.uiSoc0); m_sideBattery->setCurrent(0);
        m_lblState->setText(st.errorText.isEmpty()?QString::fromUtf8("就绪"):QString::fromUtf8("仿真失败"));
        m_lblTime->setText("0.0 / 0.0 s");
        m_lblHint->setText(st.hint.isEmpty()?QString::fromUtf8("尚无自定义仿真结果。设置参数后点击应用参数，再点击仿真。"):st.hint);
        if(st.savedPreview) showSavedPreview();
        refreshButtons();
        return;
    }

    m_player->loadFromSamples(st.samples);   // 装回该场景自己的曲线
    m_plotWidget->setDuration(m_player->duration());
    m_plotWidget->setSamples(st.samples, st.position, st.drawn);
    m_batteryView->setInputs(st.uiVoltage, st.uiCurrent, st.uiMode, st.uiSoc0);

    m_lblState->setText(st.errorText.isEmpty()?restoreStateText(st):QString::fromUtf8("仿真失败"));
    m_lblHint->setText(st.hint);
    m_lblTime->setText(QString::fromUtf8("%1 / %2 s")
                           .arg(st.position, 0, 'f', 1)
                           .arg(m_player->duration(), 0, 'f', 1));

    m_player->seek(st.position);   // 触发一次 tick，把电池图与时间标签对齐
    if(st.savedPreview) showSavedPreview();
    refreshButtons();
}

QString MainWindow::restoreStateText(const ScenarioState& st) const
{
    const double dur = st.samples.isEmpty() ? 0.0 : st.samples.last().t;

    if (st.customized) {
        if (st.position <= 0.0) return QString::fromUtf8("自定义曲线");
        if (st.position >= dur - 1e-6) return QString::fromUtf8("自定义 · 已结束");
        return QString::fromUtf8("自定义 · 已暂停");
    }

    if (st.position <= 0.0) return QString::fromUtf8("就绪");
    if (st.position >= dur - 1e-6) return QString::fromUtf8("已结束");
    return QString::fromUtf8("已暂停");
}

void MainWindow::setStateText(const QString& phase)
{
    if (m_activeIndex >= 0 && m_activeIndex < m_states.size()
        && m_states[m_activeIndex].customized) {
        m_lblState->setText(QString::fromUtf8("自定义 · %1").arg(phase));
    } else {
        m_lblState->setText(phase);
    }
}

void MainWindow::refreshButtons() {
    const bool busy=m_runner->isRunning();
    m_batteryView->setParametersEnabled(!busy);
    if(m_resultSelector)m_resultSelector->setEnabled(!busy);
    if(busy) {
        const auto p=m_runner->phase();const bool pending=m_runner->controlPending();
        m_btnPlay->setEnabled(false);m_btnPause->setEnabled(p=="Running"&&!pending);
        m_btnResume->setEnabled(p=="Paused"&&!pending);
        m_btnStop->setEnabled((p=="Running"||p=="Paused"||p=="Pausing"||p=="Resuming")&&!pending);
        m_cmbSpeed->setEnabled((p=="Running"||p=="Paused")&&!pending);return;
    }
    m_cmbSpeed->setEnabled(true);
    const bool playing=m_player->isPlaying();
    const bool atEnd=m_player->isLoaded()&&m_player->currentTime()>=m_player->duration()-1e-6;
    const bool paused=m_player->isLoaded()&&!playing&&!atEnd&&m_player->currentTime()>1e-9&&!m_stopped&&m_display=="cache";
    m_btnPlay->setEnabled(!playing);m_btnPause->setEnabled(playing);
    m_btnResume->setEnabled(paused);m_btnStop->setEnabled(playing||paused);
}

void MainWindow::showSavedPreview()
{
    const auto& st=m_states[m_activeIndex];
    m_batteryView->setSavedPreview(st.uiSoc0,st.uiVoltage,st.uiCurrent,st.uiMode);
    m_sideBattery->setSavedPreview(st.uiSoc0,st.uiVoltage,st.uiCurrent,st.uiMode);
    m_lblTime->setText(QString::fromUtf8("0.0 s · 待仿真"));
}


void MainWindow::onPlay()
{
    if (m_runner && m_runner->isRunning()) return;   // Sysplorer 未算完，点仿真无反应

    const int index = m_activeIndex;
    if (index < 0 || index >= kScenarioCount) return;

    // 参数与当前曲线不一致（改过没重算）→ 先算再播；一致 → 直接复用内存里的结果
    const auto& st=m_states[index];
    if(qAbs(m_batteryView->voltage()-st.uiVoltage)>1e-8
       ||qAbs(m_batteryView->current()-st.uiCurrent)>1e-8
       ||m_batteryView->mode()!=st.uiMode||qAbs(m_batteryView->soc0()-st.uiSoc0)>1e-8) {
        m_lblHint->setText(QString::fromUtf8("参数已编辑，请先点击应用参数，校验并保存后再仿真。"));return;
    }
    const double v=st.uiVoltage,i=st.uiCurrent,s0=st.uiSoc0;
    const int md=st.uiMode;
    if (needsRecompute(index, v, i, md, s0)) {
        startRecompute(index, v, i, md, s0);
        return;
    }

    m_display="cache";
    m_player->loadFromSamples(st.samples);
    m_plotWidget->setDuration(m_player->duration());
    m_plotWidget->setSamples(st.samples,0,0);
    refreshResultSelector();
    // Complete successful cache only.
    m_states[index].savedPreview=false;
    m_stopped=false;
    m_player->seek(0.0);
    m_plotWidget->clearDrawn();
    m_plotWidget->setProgress(0.0);

    m_player->play();
    setStateText(QString::fromUtf8("内存结果回放中"));
    m_lblHint->setText(QString::fromUtf8("正在回放上次仿真的内存结果。"));
    refreshButtons();
}

void MainWindow::onPause() {
    if(m_runner->isRunning()){m_runner->pause();setStateText(QString::fromUtf8("正在请求暂停"));refreshButtons();return;}
    if(!m_player->isPlaying())return;
    m_player->pause();saveActiveState();setStateText(QString::fromUtf8("回放已暂停"));refreshButtons();
}

void MainWindow::onResume() {
    if(m_runner->isRunning()){m_runner->resume();setStateText(QString::fromUtf8("正在请求继续"));refreshButtons();return;}
    if(m_stopped||m_display!="cache"||!m_player->isLoaded()||m_player->isPlaying())return;
    m_player->play();setStateText(QString::fromUtf8("缓存回放中"));refreshButtons();
}

void MainWindow::onStop() {
    if(m_runner->isRunning()){m_runner->stop();setStateText(QString::fromUtf8("正在停止"));refreshButtons();return;}
    m_player->pause();m_stopped=true;saveActiveState();setStateText(QString::fromUtf8("回放已停止"));refreshButtons();
}

void MainWindow::onTick(double t, double soc, double current, double voltage)
{
    m_batteryView->setSoc(soc);
    m_batteryView->setVoltage(voltage);
    m_batteryView->setCurrent(current);

    m_sideBattery->setSoc(soc);
    m_sideBattery->setVoltage(voltage);
    m_sideBattery->setCurrent(current);

    m_lblTime->setText(QString::fromUtf8("%1 / %2 s")
                           .arg(t, 0, 'f', 1)
                           .arg(m_display=="live"?m_runner->duration():m_player->duration(), 0, 'f', 1));

    m_plotWidget->setProgress(t);

    // 进度跟着当前场景一起记住
    if ((m_display=="cache"||m_display=="complete") && m_activeIndex >= 0 && m_activeIndex < m_states.size()) {
        m_states[m_activeIndex].position = t;
    }
}

void MainWindow::onFinished()
{
    saveActiveState();
    setStateText(QString::fromUtf8("已结束"));
    refreshButtons();
}

void MainWindow::onApplyCustom(double voltage, double current, int mode, double soc0)
{    if(m_runner->isRunning()) return;
    QStringList problems;

    if (!std::isfinite(voltage) || voltage < kVMin || voltage > kVMax) {
        problems << QString::fromUtf8("· 端电压 %1 V，超出 %2 – %3 V")
                        .arg(voltage, 0, 'f', 3)
                        .arg(kVMin, 0, 'f', 2)
                        .arg(kVMax, 0, 'f', 2);
    }
    if (!std::isfinite(current) || current < 0.0 || current > kIMax) {
        problems << QString::fromUtf8("· 电流 %1 A，超出 0 – %2 A")
                        .arg(current, 0, 'f', 2)
                        .arg(kIMax, 0, 'f', 1);
    }
    if(!std::isfinite(soc0)||soc0<0||soc0>1) problems<<QString::fromUtf8("· 起始电量必须在 0–100% 范围内");
    if(mode!=1&&mode!=2) problems<<QString::fromUtf8("· 请选择充电或放电");

    if (!problems.isEmpty()) {
        showWarning(
            this,
            QString::fromUtf8("参数超出安全边界"),
            QString::fromUtf8("以下参数不在电池模型允许的范围内：\n\n%1\n\n"
                              "模型允许的安全边界：\n"
                              "· 端电压：%2 – %3 V（放电下限 / 充电上限）\n"
                              "· 电流：0 – %4 A（对应 0 ~ 2C）")
                .arg(problems.join(QString::fromUtf8("\n")))
                .arg(kVMin, 0, 'f', 2)
                .arg(kVMax, 0, 'f', 2)
                .arg(kIMax, 0, 'f', 1));
        return;
    }

    const int index = m_activeIndex;
    if (index < 0 || index >= kScenarioCount) return;

    // 「应用参数」只保存设置：不启动仿真，也不改变当前曲线。
    // 曲线要不要重算，交给「仿真」按参数一致性判断。
    ScenarioState& st = m_states[index];
    st.uiVoltage = voltage;
    st.uiCurrent = current;
    st.uiMode = mode;
    st.uiSoc0 = soc0;
    st.hint = QString::fromUtf8("参数已保存：起点 %1%、%2 A %3。点「仿真」开始使用。")
                  .arg(soc0 * 100.0, 0, 'f', 0)
                  .arg(current, 0, 'f', 2)
                  .arg(mode == 1 ? QString::fromUtf8("充电") : QString::fromUtf8("放电"));
    st.errorText.clear();
    m_errorPanel->hide();

    m_display="preview";
    m_player->resetTime(); // Apply only saves settings and previews.
    m_stopped=true;
    if(index==0) {st.savedPreview=true; st.position=0; showSavedPreview();}

    m_lblHint->setText(st.hint);
    setStateText(QString::fromUtf8("参数已保存"));
    refreshButtons();
}

bool MainWindow::needsRecompute(int index, double voltage, double current,
                                int mode, double soc0) const
{
    if (index < 0 || index >= m_states.size()) return false;
    const ScenarioState& st = m_states[index];
    if (!st.loaded || st.samples.isEmpty()) return true;
    if(index==0&&(!st.realSdk||st.modelHash.isEmpty()||st.modelHash!=m_runner->modelFingerprint())) return true;

    return qAbs(st.voltage - voltage) > 1e-8
        || qAbs(st.current - current) > 1e-8
        || st.mode != mode
        || qAbs(st.soc0 - soc0) > 1e-8;
}

void MainWindow::startRecompute(int index, double voltage, double current,
                                int mode, double soc0)
{
    if(index!=0) return;
    startSysplorerRun(voltage, current, mode, soc0);
}

void MainWindow::startSysplorerRun(double voltage,double current,int mode,double soc0) {
    if(m_runner->isRunning())return;
    const double delta=mode==1?1-soc0:soc0;
    const double duration=current<=0?120.0:qBound(120.0,delta*9000/qMax(current,.05)*1.1+60,20000.0);
    m_reqVoltage=voltage;m_reqCurrent=current;m_reqMode=mode;m_reqSoc0=soc0;m_requestIndex=0;
    m_states[0].errorText.clear();m_states[0].savedPreview=false;m_errorPanel->hide();
    m_player->pause();m_stopped=true;m_display="live";m_liveSamples.clear();
    m_plotWidget->clearData();m_plotWidget->setDuration(duration);
    m_tabs->setCurrentIndex(1);
    m_batteryView->setApplyEnabled(false);
    m_lblHint->setText(QString::fromUtf8("新任务通过 UDP 调用本地 Sysplorer；运行中实时绘图，完成后更新成功缓存。"));
    m_lblTime->setText(QString::fromUtf8("等待连接…"));
    m_runner->start(voltage,current,mode,soc0,duration);
    refreshResultSelector();refreshButtons();
}

void MainWindow::onSysplorerFinished(bool ok,const QString&csvPath,const QString&message) {
    m_batteryView->setApplyEnabled(true);
    if(!ok) {
        m_stopped=true;m_player->pause();
        if(m_states[0].loaded){m_display="complete";applyScenario(0);}
        else if(!m_partials.isEmpty()){
            m_display="partial";m_partialIndex=m_partials.size()-1;refreshResultSelector();
            showStoredResult(m_resultSelector->findData("partial:"+QString::number(m_partialIndex)));
        }else {m_display="preview";applyScenario(0);}
        auto &st=m_states[0];st.errorText=QString::fromUtf8("本次仿真失败；上次完整成功缓存仍保留。\n%1").arg(message);
        st.hint=QString::fromUtf8("任务 %1 失败，请查看上方错误及任务日志。").arg(m_runner->taskId());
        m_errorPanel->setPlainText(st.errorText);m_errorPanel->show();m_lblHint->setText(st.hint);
        setStateText(QString::fromUtf8("仿真失败"));refreshResultSelector();refreshButtons();return;
    }
    ReplayData result;QString error;
    if(!result.load(csvPath,&error)){onSysplorerFinished(false,{},error);return;}
    m_states[0].realSdk=true;m_states[0].modelHash=m_runner->lastModelFingerprint();
    m_states[0].sdkJobDir=m_runner->lastRunDirectory();
    applyComputedCurve(result.samples(),m_reqVoltage,m_reqCurrent,m_reqMode,m_reqSoc0,
        QString::fromUtf8("完整结果 ·任务 %1 ·起点 %2% ·%3 A %4 ·有效范围 0–%5 s。%6")
        .arg(m_runner->taskId()).arg(m_reqSoc0*100,0,'f',1).arg(m_reqCurrent,0,'f',2)
        .arg(m_reqMode==1?QString::fromUtf8("充电"):QString::fromUtf8("放电")).arg(result.duration(),0,'f',2).arg(message));
}

void MainWindow::onViewerFinished(const QString& jobDir,bool ok,const QString& message)
{
    if(jobDir!=m_runner->lastRunDirectory()&&jobDir!=m_states[0].sdkJobDir) return;
    m_viewerJob=jobDir; m_viewerOk=ok; m_viewerMessage=message;
    if(m_runner->isRunning()) return;
    auto& st=m_states[0];
    if(!st.hint.contains(message)) st.hint+=QStringLiteral("\n")+message;
    if(!ok) {
        if(st.errorText.isEmpty()) st.errorText=QString::fromUtf8("Sysplorer 查看窗口未就绪；SDK 计算与客户端回放独立保留。\n")+message;
    }
    if(m_activeIndex==0) {
        m_lblHint->setText(st.hint);
        m_errorPanel->setPlainText(st.errorText); m_errorPanel->setVisible(!st.errorText.isEmpty());
    }
}

// 把一条新曲线装进“当前场景”：写快照、刷新曲线页、从头播放。
// Only verified SDK results are committed as the last successful memory result.
void MainWindow::applyComputedCurve(QVector<ReplaySample> samples,double voltage,double current,int mode,double soc0,const QString&hint) {
    if(samples.isEmpty())return;
    auto &st=m_states[0];st.samples=samples;st.loaded=true;st.customized=true;
    st.voltage=voltage;st.current=current;st.mode=mode;st.soc0=soc0;
    st.uiVoltage=voltage;st.uiCurrent=current;st.uiMode=mode;st.uiSoc0=soc0;
    st.hint=hint;st.savedPreview=false;st.errorText.clear();m_errorPanel->hide();
    m_display="complete";m_stopped=true;m_player->loadFromSamples(samples);
    st.position=samples.last().t;st.drawn=st.position;
    m_plotWidget->setDuration(samples.last().t);m_plotWidget->setSamples(samples,st.position,st.position);
    m_player->seek(st.position);m_lblHint->setText(hint);
    setStateText(QString::fromUtf8("仿真完成"));refreshResultSelector();refreshButtons();
}

void MainWindow::onHostStateChanged(const QString&phase) {
    if(!m_runner->isRunning())return;
    const QMap<QString,QString> names{
        {"Connecting",QString::fromUtf8("连接 Sysplorer")},{"Preparing",QString::fromUtf8("加载与设置参数")},
        {"Translating",QString::fromUtf8("翻译模型")},{"Starting",QString::fromUtf8("启动求解")},
        {"Running",QString::fromUtf8("实时仿真中")},{"Pausing",QString::fromUtf8("正在暂停")},
        {"Paused",QString::fromUtf8("仿真已暂停")},{"Resuming",QString::fromUtf8("正在继续")},
        {"Stopping",QString::fromUtf8("正在停止")},{"Finalizing",QString::fromUtf8("核验结果")},
        {"Completed",QString::fromUtf8("核验完整结果")},{"Stopped",QString::fromUtf8("保留部分结果")}};
    setStateText(names.value(phase,phase));
    if(phase=="Preparing"||phase=="Translating"||phase=="Starting"||phase=="Connecting")m_lblTime->setText(names.value(phase));
    m_lblHint->setText(QString::fromUtf8("当前任务 %1 ·%2 A %3 ·起点 %4% ·截止 %5 V")
        .arg(m_runner->taskId()).arg(m_reqCurrent,0,'f',2)
        .arg(m_reqMode==1?QString::fromUtf8("充电"):QString::fromUtf8("放电"))
        .arg(m_reqSoc0*100,0,'f',1).arg(m_reqVoltage,0,'f',3));refreshButtons();
}
void MainWindow::onLiveSamples(const QVector<ReplaySample>&samples) {
    if(m_display!="live"||samples.isEmpty())return;
    m_liveSamples+=samples;m_plotWidget->appendSamples(samples);
    const auto&s=samples.last();onTick(s.t,s.soc,s.current,s.voltage);refreshButtons();
}
void MainWindow::onSysplorerStopped(const QString&csvPath,const QString&message) {
    ReplayData result;QString error;
    if(!result.load(csvPath,&error)){onSysplorerFinished(false,{},error);return;}
    m_partials.append({m_runner->taskId(),result.samples(),csvPath,m_reqVoltage,m_reqCurrent,m_reqSoc0,m_reqMode});
    m_partialIndex=m_partials.size()-1;m_display="partial";m_stopped=true;
    m_batteryView->setApplyEnabled(true);m_errorPanel->hide();refreshResultSelector();
    showStoredResult(m_resultSelector->findData("partial:"+QString::number(m_partialIndex)));
    if(!message.isEmpty())m_lblHint->setText(m_lblHint->text()+"\n"+message);
}
void MainWindow::refreshResultSelector() {
    if(!m_resultSelector)return;
    QSignalBlocker blocker(m_resultSelector);m_resultSelector->clear();
    if(m_states[0].loaded)m_resultSelector->addItem(QString::fromUtf8("完整成功结果 ·任务 %1")
        .arg(QFileInfo(m_states[0].sdkJobDir).fileName()),"cache");
    for(int i=0;i<m_partials.size();++i){const auto&p=m_partials[i];
        m_resultSelector->addItem(QString::fromUtf8("已停止／部分结果 ·任务 %1 ·0–%2 s")
            .arg(p.taskId).arg(p.samples.last().t,0,'f',2),"partial:"+QString::number(i));}
    if(m_runner->isRunning())m_resultSelector->addItem(QString::fromUtf8("当前实时任务 ·%1").arg(m_runner->taskId()),"live");
    if(m_resultSelector->count()==0)m_resultSelector->addItem(QString::fromUtf8("尚无仿真结果"),"none");
    const QString key=m_runner->isRunning()?"live":m_display=="partial"?"partial:"+QString::number(m_partialIndex):"cache";
    const int index=m_resultSelector->findData(key);if(index>=0)m_resultSelector->setCurrentIndex(index);
}
void MainWindow::showStoredResult(int index) {
    if(m_runner->isRunning()||index<0)return;
    const auto key=m_resultSelector->itemData(index).toString();
    if(key=="cache"&&m_states[0].loaded){
        m_display="complete";m_stopped=true;auto&st=m_states[0];
        m_player->loadFromSamples(st.samples);m_plotWidget->setDuration(m_player->duration());
        m_plotWidget->setSamples(st.samples,m_player->duration(),m_player->duration());
        m_player->seek(m_player->duration());m_lblHint->setText(st.hint);
        setStateText(QString::fromUtf8("查看完整结果"));
    }else if(key.startsWith("partial:")){
        bool ok=false;int i=key.mid(8).toInt(&ok);if(!ok||i<0||i>=m_partials.size())return;
        m_display="partial";m_partialIndex=i;m_stopped=true;const auto&p=m_partials[i];
        m_player->loadFromSamples(p.samples);m_plotWidget->setDuration(m_player->duration());
        m_plotWidget->setSamples(p.samples,m_player->duration(),m_player->duration());m_player->seek(m_player->duration());
        m_lblHint->setText(QString::fromUtf8("已停止／部分结果 ·任务 %1 ·有效范围 %2–%3 s ·起点 %4% ·%5 A %6；不覆盖完整成功缓存。")
            .arg(p.taskId).arg(p.samples.first().t,0,'f',2).arg(p.samples.last().t,0,'f',2)
            .arg(p.soc0*100,0,'f',1).arg(p.current,0,'f',2)
            .arg(p.mode==1?QString::fromUtf8("充电"):QString::fromUtf8("放电")));
        setStateText(QString::fromUtf8("已停止／部分结果"));
    }refreshButtons();
}

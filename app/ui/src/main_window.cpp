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
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSettings>
#include <QToolButton>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QRadioButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QSpinBox>
#include <cmath>

namespace {
class ControlFlowLayout : public QLayout {
public:
    explicit ControlFlowLayout(QWidget* p):QLayout(p){setContentsMargins(12,6,12,6);setSpacing(8);}
    ~ControlFlowLayout() override {while(auto item=takeAt(0))delete item;}
    void addItem(QLayoutItem* i) override {items.append(i);}
    int count() const override {return items.size();}
    QLayoutItem* itemAt(int i) const override {return items.value(i);}
    QLayoutItem* takeAt(int i) override {return i>=0&&i<items.size()?items.takeAt(i):nullptr;}
    Qt::Orientations expandingDirections() const override {return {};}
    bool hasHeightForWidth() const override {return true;}
    int heightForWidth(int w) const override {return arrange(QRect(0,0,w,0),false);}
    QSize minimumSize() const override {QSize z;for(auto i:items)z=z.expandedTo(i->minimumSize());auto m=contentsMargins();return z+QSize(m.left()+m.right(),m.top()+m.bottom());}
    QSize sizeHint() const override {return minimumSize();}
    void setGeometry(const QRect& r) override {QLayout::setGeometry(r);arrange(r,true);}
private:
    int arrange(const QRect& r,bool apply) const {
        auto m=contentsMargins();auto a=r.adjusted(m.left(),m.top(),-m.right(),-m.bottom());int x=a.x(),y=a.y(),h=0;
        for(auto i:items){auto z=i->sizeHint();if(x>a.x()&&x+z.width()>a.right()+1){x=a.x();y+=h+spacing();h=0;}
            if(apply)i->setGeometry(QRect(QPoint(x,y),z));x+=z.width()+spacing();h=qMax(h,z.height());}
        return y+h-r.y()+m.bottom();
    }
    QList<QLayoutItem*> items;
};
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
      "按设定电流充电或放电，电压用作充电上限或放电下限。翻译模型后开始仿真；保存参数只保存设置，所选历史结果可独立回放。",
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

    QSettings saved(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","ButtonDetails");
    auto& initial=m_states[0];
    const double v=saved.value("parameters/voltage",initial.uiVoltage).toDouble();
    const double i=saved.value("parameters/current",initial.uiCurrent).toDouble();
    const double soc=saved.value("parameters/soc0",initial.uiSoc0).toDouble();
    const int mode=saved.value("parameters/mode",initial.uiMode).toInt();
    if(std::isfinite(v)&&v>=kVMin&&v<=kVMax&&std::isfinite(i)&&i>=0&&i<=kIMax&&std::isfinite(soc)&&soc>=0&&soc<=1&&(mode==1||mode==2)){
        initial.uiVoltage=v;initial.uiCurrent=i;initial.uiSoc0=soc;initial.uiMode=mode;
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
    resize(1280, 780);
    connect(m_runner,&SysplorerRunner::hostConnectionFailed,this,[this](const QString& message){
        m_hostConnectionFailed=true;
        setStateText(QString::fromUtf8("连接失败"));
        m_errorPanel->setPlainText(message);m_errorPanel->show();appendStageLog(QString::fromUtf8("错误"),message);
        showWarning(this,QString::fromUtf8("连接失败"),message);refreshButtons();
    });
    setStateText(m_runner->remoteMode()?QString::fromUtf8("尚未连接，请在连接设置中连接远端"):
        QString::fromUtf8("尚未连接；点击连接设置，或加载、翻译、开始时按需连接本机"));
    appendStageLog(QString::fromUtf8("连接"),QString::fromUtf8("界面已启动，仅读取配置；尚未连接或启动 Sysplorer。"));
    appendStageLog(QString::fromUtf8("环境"),QString::fromUtf8("客户端：%1；模型来源：客户端内置资源；本机宿主将在显式连接时检查。").arg(QCoreApplication::applicationFilePath()));
    refreshButtons();
}

MainWindow::~MainWindow() = default;

void MainWindow::showConnectionSettings(){
    if(m_runner->isRunning())return;
    QDialog dialog(this);dialog.setWindowTitle(QString::fromUtf8("连接设置"));dialog.setObjectName("connectionDialog");dialog.resize(480,0);
    auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;
    auto mode=new QComboBox(&dialog);mode->setObjectName("connectionMode");mode->addItems({QString::fromUtf8("本机 · 按需发现 / 启动"),QString::fromUtf8("远程 · IP 与端口")});mode->setCurrentIndex(m_runner->remoteMode()?1:0);
    auto address=new QLineEdit(m_runner->remoteAddress(),&dialog);address->setObjectName("remoteAddress");address->setPlaceholderText("192.168.1.20");
    auto port=new QSpinBox(&dialog);port->setObjectName("remotePort");port->setRange(1,65535);port->setValue(m_runner->remotePort());
    auto key=new QLineEdit(&dialog);key->setObjectName("remoteAccessKey");key->setEchoMode(QLineEdit::Password);key->setPlaceholderText(QString::fromUtf8("留空使用已保存凭据；首次连接至少 32 个字符"));
    key->setToolTip(QString::fromUtf8("填写该服务器原 access-key.txt 中的凭据。认证失败或切换服务器后请重新填写；留空会复用已保存的旧值。使用 Windows 当前用户保护后保存，不写入普通日志。"));
    form->addRow(QString::fromUtf8("连接方式"),mode);form->addRow(QString::fromUtf8("服务器地址"),address);form->addRow(QString::fromUtf8("TCP 端口"),port);form->addRow(QString::fromUtf8("访问凭据"),key);layout->addLayout(form);
    auto note=new QLabel(QString::fromUtf8("远程模式用于受信任的局域网或 VPN。仿真机需先安装配套插件并启用监听；Qt 所在电脑无需安装 Sysplorer。"),&dialog);note->setWordWrap(true);layout->addWidget(note);
    auto error=new QLabel(&dialog);error->setObjectName("connectionError");error->setWordWrap(true);error->setStyleSheet("color:#972f29");layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("保存并连接"));buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));layout->addWidget(buttons);
    auto update=[=]{const bool remote=mode->currentIndex()==1;address->setEnabled(remote);port->setEnabled(remote);key->setEnabled(remote);};
    connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[update]{update();});update();
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&,this]{
        if(!m_runner->configureConnection(mode->currentIndex()==1,address->text(),port->value(),key->text())){
            error->setText(QString::fromUtf8("请填写服务器地址和至少 32 个字符的有效凭据；也请检查用户设置目录是否可写。"));return;
        }
        dialog.accept();m_hostConnectionFailed=false;m_errorPanel->hide();m_runner->connectHost();refreshButtons();
    });dialog.exec();
}

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

    auto* h = new ControlFlowLayout(bar);
    h->setContentsMargins(12, 8, 12, 8);
    h->setSpacing(10);

    m_btnLoad=new QPushButton(QString::fromUtf8("加载模型"),bar);m_btnLoad->setObjectName("btnLoad");m_btnLoad->setEnabled(false);
    m_btnLoad->setToolTip(QString::fromUtf8("只加载并确认目标模型；不写参数、不翻译、不开始求解。"));
    m_btnPrepare = new QPushButton(QString::fromUtf8("翻译模型"),bar);
    m_btnPrepare->setToolTip(QString::fromUtf8("自动加载或复用有效模型，校验当前输入、写参回读并翻译；成功后等待开始仿真。"));
    m_btnPrepare->setObjectName("btnPrepare");
    m_btnReplay = new QPushButton(QString::fromUtf8("回放所选结果"),bar);
    m_btnReplay->setObjectName("btnReplay");
    m_btnPlay = new QPushButton(QString::fromUtf8("开始仿真"), bar);
    m_btnPause = new QPushButton(QString::fromUtf8("暂停仿真"), bar);

    m_btnStop = new QPushButton(QString::fromUtf8("停止仿真"), bar);
    m_btnPlay->setObjectName(QStringLiteral("btnPlay"));
    m_btnPlay->setToolTip(QString::fromUtf8("自动完成必要的加载、写参回读和翻译；复用有效准备实例，确认就绪后开始仿真。"));
    m_btnPause->setObjectName(QStringLiteral("btnPause"));

    m_btnStop->setObjectName(QStringLiteral("btnStop"));

    m_cmbSpeed = new QComboBox(bar);
    m_cmbSpeed->setObjectName("cmbSpeed");
    for (int i = 0; i < kSpeedCount; ++i) {
        m_cmbSpeed->addItem(QString::fromUtf8("%1x").arg(kSpeeds[i], 0, 'g', 3));
    }
    m_cmbSpeed->setCurrentIndex(3);   // 默认 20x

    m_lblTime = new QLabel(QString::fromUtf8("0.0 / 0.0 s"), bar);
    m_lblTime->setMinimumWidth(185);
    m_lblTime->setObjectName(QStringLiteral("lblTime"));
    m_lblTime->setAlignment(Qt::AlignCenter);
    QFont tf = m_lblTime->font();
    tf.setBold(true);
    m_lblTime->setFont(tf);

    m_lblState = new QLabel(QString::fromUtf8("就绪"), bar);
    m_lblState->setMinimumWidth(70);
    m_lblState->setObjectName(QStringLiteral("lblState"));

    h->addWidget(m_btnLoad);h->addWidget(m_btnPrepare);h->addWidget(m_btnPlay);h->addWidget(m_btnPause);h->addWidget(m_btnStop);
    auto speedGroup=new QWidget(bar);auto speedRow=new QHBoxLayout(speedGroup);speedRow->setContentsMargins(0,0,0,0);
    speedRow->addWidget(new QLabel(QString::fromUtf8("仿真倍速"),speedGroup));speedRow->addWidget(m_cmbSpeed);h->addWidget(speedGroup);h->addWidget(m_lblTime);
    root->addWidget(bar);
    auto connectionBar=new QWidget(central);auto connectionRow=new QHBoxLayout(connectionBar);connectionRow->setContentsMargins(12,4,12,4);
    m_connectionStatus=new QLabel(m_runner->connectionLabel(),connectionBar);m_connectionStatus->setObjectName("connectionStatus");
    m_connectionStatus->setWordWrap(true);m_connectionStatus->setMinimumWidth(0);m_connectionStatus->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    m_btnConnection=new QPushButton(QString::fromUtf8("连接设置"),connectionBar);m_btnConnection->setObjectName("btnConnection");
    m_btnReconnect=new QPushButton(QString::fromUtf8("重连并核对"),connectionBar);m_btnReconnect->setObjectName("btnReconnect");m_btnReconnect->hide();
    m_btnRetryDownload=new QPushButton(QString::fromUtf8("重试下载"),connectionBar);m_btnRetryDownload->setObjectName("btnRetryDownload");m_btnRetryDownload->hide();
    m_btnAbandon=new QPushButton(QString::fromUtf8("结束跟踪"),connectionBar);m_btnAbandon->setObjectName("btnAbandonRemote");m_btnAbandon->hide();
    connectionRow->addWidget(m_connectionStatus,1);connectionRow->addWidget(m_btnReconnect);connectionRow->addWidget(m_btnRetryDownload);connectionRow->addWidget(m_btnAbandon);connectionRow->addWidget(m_btnConnection);root->addWidget(connectionBar);
    connect(m_btnConnection,&QPushButton::clicked,this,&MainWindow::showConnectionSettings);
    connect(m_btnReconnect,&QPushButton::clicked,this,[this]{m_errorPanel->hide();m_runner->reconnectRemote();refreshButtons();});
    connect(m_btnRetryDownload,&QPushButton::clicked,this,[this]{m_errorPanel->hide();m_runner->retryDownload();refreshButtons();});
    connect(m_btnAbandon,&QPushButton::clicked,this,[this]{
        if(QMessageBox::question(this,QString::fromUtf8("结束远程任务跟踪"),QString::fromUtf8("结束跟踪不会强制停止服务器任务；运行任务失联超过 15 秒时服务器会请求暂停。结束后不能通过本次连接继续跟踪，是否继续？"))==QMessageBox::Yes)m_runner->abandonRemoteTask();
    });
    connect(m_runner,&SysplorerRunner::connectionInfoChanged,m_connectionStatus,&QLabel::setText);
    connect(m_runner,&SysplorerRunner::remoteNotice,this,[this](const QString& message){m_errorPanel->setPlainText(message);m_errorPanel->show();appendStageLog(QString::fromUtf8("连接"),message);refreshButtons();});
    connect(m_runner,&SysplorerRunner::downloadProgress,this,[this](qint64 received,qint64 total){setStateText(QString::fromUtf8("下载远程结果 %1 / %2 KB").arg(received/1024).arg(total/1024));});
    auto info=new QWidget(central);auto infoLayout=new QVBoxLayout(info);infoLayout->setContentsMargins(12,0,12,4);
    infoLayout->addWidget(m_lblState);m_lblState->setWordWrap(true);
    m_taskInfo=new QLabel(info);m_taskInfo->setObjectName("taskInfo");m_taskInfo->setWordWrap(true);infoLayout->addWidget(m_taskInfo);
    m_lblHint=new QLabel(info);m_lblHint->setObjectName("lblHint");m_lblHint->setWordWrap(true);m_lblHint->setTextInteractionFlags(Qt::TextSelectableByMouse);infoLayout->addWidget(m_lblHint);
    auto disclosure=[this,central,root](const QString& text,const char* name,QWidget* body,const QString& key){
        auto toggle=new QToolButton(central);toggle->setObjectName(name);toggle->setText(text);toggle->setCheckable(true);toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        QSettings config(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","ButtonDetails");bool open=config.value(key,false).toBool();toggle->setChecked(open);body->setVisible(open);toggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);
        connect(toggle,&QToolButton::toggled,this,[toggle,body,key](bool open){body->setVisible(open);toggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);QSettings config(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","ButtonDetails");config.setValue(key,open);});root->addWidget(toggle);root->addWidget(body);
    };
    disclosure(QString::fromUtf8("状态信息"),"toggleStatus",info,"statusExpanded");

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
    auto replayBar=new QWidget(plotColumn);auto replayRow=new ControlFlowLayout(replayBar);
    m_btnReplayPause=new QPushButton(QString::fromUtf8("暂停回放"),replayBar);m_btnReplayPause->setObjectName("btnReplayPause");
    m_btnReplayStop=new QPushButton(QString::fromUtf8("停止回放"),replayBar);m_btnReplayStop->setObjectName("btnReplayStop");
    m_replaySpeed=new QComboBox(replayBar);m_replaySpeed->setObjectName("cmbReplaySpeed");for(int i=0;i<kSpeedCount;++i)m_replaySpeed->addItem(QString::fromUtf8("回放 %1x").arg(kSpeeds[i]));m_replaySpeed->setCurrentIndex(1);
    m_replayTime=new QLabel("0.0 / 0.0 s",replayBar);m_replayTime->setObjectName("replayTime");m_replayTime->setMinimumWidth(185);
    m_replayState=new QLabel(QString::fromUtf8("尚无回放"),plotColumn);m_replayState->setObjectName("replayState");m_replayState->setWordWrap(true);
    replayRow->addWidget(m_btnReplay);replayRow->addWidget(m_btnReplayPause);replayRow->addWidget(m_btnReplayStop);replayRow->addWidget(m_replaySpeed);replayRow->addWidget(m_replayTime);
    plotLayout->addWidget(m_resultSelector);plotLayout->addWidget(replayBar);plotLayout->addWidget(m_replayState);
    connect(m_resultSelector,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&MainWindow::showStoredResult);
    plotLayout->addWidget(plotCaption);
    plotLayout->addWidget(m_plotWidget, 1);

    plotRow->addWidget(plotColumn, 3);
    plotRow->addWidget(m_sideBattery, 2);
    m_tabs->addTab(plotPage, QString::fromUtf8("结果曲线"));

    auto tunePage=new QWidget(m_tabs);auto tuneLayout=new QVBoxLayout(tunePage);
    auto note=new QLabel(QString::fromUtf8("运行中修改控制指令；提交时短暂停算并继续，仿真时间和 SOC 保留。恒流模式由电流控制，恒压模式由电压目标控制。"),tunePage);
    note->setWordWrap(true);tuneLayout->addWidget(note);
    m_tuneControls=new QWidget(tunePage);auto form=new QFormLayout(m_tuneControls);
    m_tuneMode=new QComboBox(m_tuneControls);m_tuneMode->setObjectName("tuneMode");
    m_tuneMode->addItems({QString::fromUtf8("静置"),QString::fromUtf8("恒流充电"),QString::fromUtf8("恒流放电"),QString::fromUtf8("恒压 / 限流充电")});
    form->addRow(QString::fromUtf8("工作模式"),m_tuneMode);
    auto spin=[this,form](const char *name,const QString& label,double lo,double hi,double value,const QString& unit){
        auto w=new QDoubleSpinBox(m_tuneControls);w->setObjectName(name);w->setRange(lo,hi);w->setDecimals(3);w->setSingleStep(.01);w->setSuffix(unit);w->setValue(value);form->addRow(label,w);return w;
    };
    m_tuneCurrent=spin("tuneCurrent",QString::fromUtf8("恒流电流指令"),0,5,2.5," A");
    m_tuneVoltage=spin("tuneVoltage",QString::fromUtf8("恒压目标"),2.5,3.45,3.30," V");
    m_tuneMin=spin("tuneVMin",QString::fromUtf8("放电截止下限"),2.5,3.45,2.5," V");
    m_tuneMax=spin("tuneVMax",QString::fromUtf8("充电截止上限"),2.5,3.45,3.45," V");
    m_tuneLimit=spin("tuneIMax",QString::fromUtf8("电流上限"),0,5,5," A");
    auto submit=new QPushButton(QString::fromUtf8("提交运行中修改"),m_tuneControls);submit->setObjectName("btnTune");form->addRow(submit);
    tuneLayout->addWidget(m_tuneControls);
    m_tuneStatus=new QLabel(QString::fromUtf8("启动真实仿真后可修改；初始 SOC 在本次运行中保持不变。"),tunePage);
    m_tuneStatus->setObjectName("tuneStatus");m_tuneStatus->setWordWrap(true);tuneLayout->addWidget(m_tuneStatus);tuneLayout->addStretch();
    connect(submit,&QPushButton::clicked,this,[this]{
        if(m_tuneMin->value()>=m_tuneMax->value()||m_tuneVoltage->value()>m_tuneMax->value()){
            m_tuneStatus->setText(QString::fromUtf8("未提交：下截止电压必须小于上截止电压，恒压目标不能超过上截止电压。"));return;
        }
        m_runner->tune({{"current",m_tuneCurrent->value()},{"voltage",m_tuneVoltage->value()},{"mode",m_tuneMode->currentIndex()},
            {"vMin",m_tuneMin->value()},{"vMax",m_tuneMax->value()},{"iMax",m_tuneLimit->value()}});
        m_tuneStatus->setText(m_runner->phase()=="Paused"?QString::fromUtf8("修改已暂存，点击「继续」后由下一求解步确认生效。"):
            QString::fromUtf8("修改已提交，等待模型下一求解步回读确认……"));refreshButtons();
    });
    connect(m_runner,&SysplorerRunner::tuneApplied,this,[this](double time,const QJsonObject&p){
        m_tuneStatus->setText(QString::fromUtf8("已在仿真 %1 s 由模型采样回读确认：模式 %2，电流指令 %3 A，恒压目标 %4 V，截止 %5–%6 V，电流上限 %7 A。")
            .arg(time,0,'f',3).arg(p["mode"].toInt()).arg(p["current"].toDouble()).arg(p["voltage"].toDouble())
            .arg(p["vMin"].toDouble()).arg(p["vMax"].toDouble()).arg(p["iMax"].toDouble()));
    });
    connect(m_runner,&SysplorerRunner::tuneRejected,this,[this](const QString&e){m_tuneStatus->setText(QString::fromUtf8("修改被拒绝：%1").arg(e));});
    m_tabs->addTab(tunePage,QString::fromUtf8("运行中调参"));

    m_errorPanel=new QPlainTextEdit(central);
    m_errorPanel->setObjectName("errorPanel");
    m_errorPanel->setReadOnly(true);
    m_errorPanel->setMaximumHeight(90);
    m_errorPanel->setStyleSheet("background:#fff1f0;color:#972f29;border:1px solid #f0b5b0;padding:8px;");
    m_errorPanel->hide();
    root->addWidget(m_errorPanel);
    root->addWidget(m_tabs, 1);

    auto logs=new QWidget(central);auto logLayout=new QVBoxLayout(logs);logLayout->setContentsMargins(12,0,12,6);auto actions=new QHBoxLayout;
    m_logFilter=new QComboBox(logs);m_logFilter->setObjectName("logFilter");m_logFilter->addItems({QString::fromUtf8("全部"),QString::fromUtf8("连接"),QString::fromUtf8("准备"),QString::fromUtf8("仿真"),QString::fromUtf8("结果"),QString::fromUtf8("回放"),QString::fromUtf8("参数"),QString::fromUtf8("错误")});
    auto copy=new QPushButton(QString::fromUtf8("复制日志"),logs);copy->setObjectName("btnCopyLog");m_btnOpenJob=new QPushButton(QString::fromUtf8("打开任务目录"),logs);m_btnOpenJob->setObjectName("btnOpenJob");
    actions->addWidget(m_logFilter);actions->addWidget(copy);actions->addWidget(m_btnOpenJob);actions->addStretch();logLayout->addLayout(actions);
    m_stageLog=new QPlainTextEdit(logs);m_stageLog->setObjectName("stageLog");m_stageLog->setReadOnly(true);m_stageLog->setMaximumHeight(140);logLayout->addWidget(m_stageLog);
    connect(m_logFilter,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{refreshLog();});
    connect(copy,&QPushButton::clicked,this,[this]{QApplication::clipboard()->setText(m_stageLog->toPlainText());});
    connect(m_btnOpenJob,&QPushButton::clicked,this,[this]{const auto path=m_runner->lastRunDirectory();if(QFileInfo(path).isDir())QDesktopServices::openUrl(QUrl::fromLocalFile(path));});
    disclosure(QString::fromUtf8("分阶段日志"),"toggleLog",logs,"logExpanded");setCentralWidget(central);

    connect(m_btnLoad,&QPushButton::clicked,this,[this]{
        if(!m_btnLoad->isEnabled())return;m_errorPanel->hide();
        appendStageLog(QString::fromUtf8("准备"),QString::fromUtf8("请求独立加载模型，不写参、不翻译、不求解。"));
        m_runner->loadModel();refreshButtons();
    });
    connect(m_btnPrepare,&QPushButton::clicked,this,[this]{m_startAfterPreparation=false;onPrepare();});
    connect(m_btnReplay,&QPushButton::clicked,this,&MainWindow::onReplay);
    connect(m_btnPlay, &QPushButton::clicked, this, &MainWindow::onPlay);
    connect(m_btnPause,&QPushButton::clicked,this,[this]{if(m_runner->phase()=="Paused")onResume();else onPause();});
    connect(m_btnReplayPause,&QPushButton::clicked,this,[this]{if(m_runner->isRunning())return;
        if(m_player->isPlaying()){m_player->pause();saveActiveState();m_replayState->setText(QString::fromUtf8("回放已暂停"));}
        else if(!m_stopped){m_player->play();m_replayState->setText(QString::fromUtf8("结果回放中"));}refreshButtons();});
    connect(m_btnReplayStop,&QPushButton::clicked,this,[this]{if(m_runner->isRunning())return;m_player->pause();m_stopped=true;saveActiveState();m_replayState->setText(QString::fromUtf8("回放已停止"));refreshButtons();});
    connect(m_replaySpeed,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i){if(i>=0&&i<kSpeedCount)m_player->setSpeed(kSpeeds[i]);});
    for(auto spin:m_batteryView->findChildren<QDoubleSpinBox*>())connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this]{refreshButtons();});
    for(auto radio:m_batteryView->findChildren<QRadioButton*>())connect(radio,&QRadioButton::toggled,this,[this]{refreshButtons();});

    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_cmbSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (index >= 0 && index < kSpeedCount) {
                    m_runner->setSpeed(kSpeeds[index]);
                    refreshButtons();
                }
            });
    connect(m_batteryView, &BatteryView::applyRequested,
            this, &MainWindow::onApplyCustom);

    // 下拉框建好时已经选了默认速度，但那时还没连信号，这里补一次同步
    m_player->setSpeed(kSpeeds[m_replaySpeed->currentIndex()]);
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
        m_lblHint->setText(st.hint.isEmpty()?QString::fromUtf8("尚无仿真结果。设置参数后，点击翻译模型，再开始仿真。"):st.hint);
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
    m_replayTime->setText(QString::fromUtf8("%1 / %2 s")
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

bool MainWindow::inputsMatchPreparation() const {
    return qAbs(m_batteryView->voltage()-m_reqVoltage)<1e-8&&qAbs(m_batteryView->current()-m_reqCurrent)<1e-8
        &&m_batteryView->mode()==m_reqMode&&qAbs(m_batteryView->soc0()-m_reqSoc0)<1e-8
        &&m_runner->modelFingerprint()==m_runner->lastModelFingerprint();
}
void MainWindow::appendStageLog(const QString& category,const QString& message){
    const auto line=QString::fromUtf8("%1 [%2] 任务 %3 · %4").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),category,m_runner->taskId().isEmpty()?QStringLiteral("-"):m_runner->taskId(),message);
    m_logEntries.append(qMakePair(category,line));refreshLog();
}
void MainWindow::refreshLog(){
    if(!m_stageLog)return;QStringList lines;
    for(const auto& entry:m_logEntries)if(m_logFilter->currentIndex()==0||entry.first==m_logFilter->currentText())lines<<entry.second;
    m_stageLog->setPlainText(lines.join("\n"));
}
void MainWindow::refreshButtons() {
    if(m_activeIndex<0)return;
    const bool busy=m_runner->isRunning(),prepared=m_runner->isPrepared();
    const auto phase=m_runner->phase();const bool pending=m_runner->controlPending();
    const bool replayActive=m_player->isPlaying()||(!m_stopped&&m_player->isLoaded());
    m_batteryView->setParametersEnabled((!busy||prepared)&&!pending&&!replayActive&&!m_startAfterPreparation&&!m_translateAfterDiscard);
    m_batteryView->setApplyEnabled((!busy||prepared)&&!pending&&!replayActive&&!m_startAfterPreparation&&!m_translateAfterDiscard);
    m_resultSelector->setEnabled(!busy&&!replayActive);
    const bool ready=!m_runner->isConnecting()&&!phase.isEmpty()&&(!m_runner->remoteMode()||(phase!="Idle"&&phase!="Disconnected"&&phase!="ConnectionFailed"&&phase!="ConnectionLost"&&phase!="Reconnecting"));
    m_btnConnection->setEnabled(!busy&&!replayActive);
    m_btnReconnect->setVisible(m_runner->canReconnect());m_btnRetryDownload->setVisible(m_runner->canRetryDownload());
    m_btnAbandon->setVisible(busy&&(m_runner->canReconnect()||m_runner->canRetryDownload()));
    m_connectionStatus->setText(m_runner->connectionLabel());
    m_btnLoad->setEnabled(ready&&!busy&&!pending&&!replayActive&&!m_runner->isModelLoaded());
    m_btnPrepare->setEnabled(ready&&(!busy||(prepared&&!inputsMatchPreparation()))&&!pending&&!replayActive&&!m_translateAfterDiscard&&!m_startAfterPreparation);
    m_btnPlay->setEnabled(ready&&(!busy||prepared)&&!pending&&!replayActive&&!m_startAfterPreparation&&!m_translateAfterDiscard);
    const auto key=m_resultSelector->currentData().toString();
    const bool validResult=(key=="cache"&&m_states[0].loaded)||key.startsWith("partial:");
    m_btnReplay->setEnabled(!busy&&validResult&&!replayActive);
    m_btnPause->setText(QString::fromUtf8(phase=="Paused"?"继续仿真":"暂停仿真"));
    const bool canControl=busy&&(phase=="Running"||phase=="Paused")&&!pending;
    m_btnPause->setEnabled(canControl);m_btnStop->setEnabled(canControl);
    m_tuneControls->setEnabled(canControl&&!m_runner->tunePending());
    m_cmbSpeed->setEnabled((!busy||prepared||canControl)&&!pending);
    m_btnReplayPause->setText(QString::fromUtf8((!m_player->isPlaying()&&replayActive)?"继续回放":"暂停回放"));
    m_btnReplayPause->setEnabled(!busy&&replayActive);m_btnReplayStop->setEnabled(!busy&&replayActive);m_replaySpeed->setEnabled(!busy);
    m_btnOpenJob->setEnabled(!m_runner->lastRunDirectory().isEmpty()&&QFileInfo(m_runner->lastRunDirectory()).isDir());
    m_taskInfo->setText(QString::fromUtf8("任务编号：%1").arg(m_runner->taskId().isEmpty()?QStringLiteral("-"):m_runner->taskId()));
    if(m_runner->remoteMode()){
        m_taskInfo->setText(m_taskInfo->text()+QString::fromUtf8("\n远端任务目录：%1\n本机任务目录：%2")
            .arg(m_runner->serverRunDirectory().isEmpty()?QStringLiteral("-"):m_runner->serverRunDirectory(),m_runner->lastRunDirectory()));
        m_taskInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
}
void MainWindow::showSavedPreview()
{
    const auto& st=m_states[m_activeIndex];
    m_batteryView->setSavedPreview(st.uiSoc0,st.uiVoltage,st.uiCurrent,st.uiMode);
    m_sideBattery->setSavedPreview(st.uiSoc0,st.uiVoltage,st.uiCurrent,st.uiMode);
    if(!m_runner->isPrepared())m_lblTime->setText(QString::fromUtf8("0.0 s / 待翻译"));
}


void MainWindow::onPrepare(){
    if((m_runner->isConnecting()&&!m_hostConnectionFailed)||m_runner->controlPending()||m_player->isPlaying())return;
    if(m_runner->isPrepared()){
        if(inputsMatchPreparation())return;
        m_translateAfterDiscard=true;m_runner->invalidatePreparation();refreshButtons();return;
    }
    if(m_runner->isRunning())return;
    const double v=m_batteryView->voltage(),i=m_batteryView->current(),soc=m_batteryView->soc0();const int mode=m_batteryView->mode();
    if(!std::isfinite(v)||v<kVMin||v>kVMax||!std::isfinite(i)||i<0||i>kIMax||!std::isfinite(soc)||soc<0||soc>1||(mode!=1&&mode!=2)){
        const auto error=QString::fromUtf8("参数检查失败：电压须为 2.50–3.45 V，电流须为 0–5 A，起始电量须为 0–100%。");
        m_startAfterPreparation=false;m_errorPanel->setPlainText(error);m_errorPanel->show();appendStageLog(QString::fromUtf8("错误"),error);return;
    }
    appendStageLog(QString::fromUtf8("准备"),QString::fromUtf8("翻译入口读取当前输入；自动加载或复用模型，再写参、回读、翻译。"));
    startSysplorerRun(v,i,mode,soc);
}
void MainWindow::onPlay(){
    if(m_runner->isConnecting()||m_runner->controlPending()||m_player->isPlaying()||m_startAfterPreparation)return;
    if(m_runner->isPrepared()&&inputsMatchPreparation()){
        appendStageLog(QString::fromUtf8("仿真"),QString::fromUtf8("开始入口复用有效翻译实例，提交启动请求。"));
        m_runner->beginSimulation();refreshButtons();return;
    }
    if(m_runner->isRunning()&&!m_runner->isPrepared())return;
    m_startAfterPreparation=true;
    appendStageLog(QString::fromUtf8("仿真"),QString::fromUtf8("开始入口自动准备：加载/复用模型 → 写参回读 → 翻译 → 启动；任一步失败即终止。"));
    onPrepare();refreshButtons();
}
void MainWindow::onReplay(){
    if(m_runner->isRunning()||m_player->isPlaying()||!m_stopped)return;
    const auto key=m_resultSelector->currentData().toString();QVector<ReplaySample> samples;
    if(key=="cache"&&m_states[0].loaded)samples=m_states[0].samples;
    else if(key.startsWith("partial:")){bool ok=false;const int i=key.mid(8).toInt(&ok);if(ok&&i>=0&&i<m_partials.size())samples=m_partials[i].samples;}
    if(samples.isEmpty())return;
    m_player->loadFromSamples(samples);m_display="replay";m_stopped=false;
    m_plotWidget->setDuration(m_player->duration());m_plotWidget->setSamples(samples,0,0);
    m_player->seek(0);m_player->play();
    m_replayState->setText(QString::fromUtf8("回放中 · %1").arg(m_resultSelector->currentText()));
    appendStageLog(QString::fromUtf8("回放"),m_replayState->text());refreshButtons();
}
void MainWindow::onPause(){
    if(m_runner->phase()!="Running"||m_runner->controlPending())return;
    m_runner->pause();setStateText(QString::fromUtf8("正在请求暂停"));refreshButtons();
}
void MainWindow::onResume(){
    if(m_runner->phase()!="Paused"||m_runner->controlPending())return;
    m_runner->resume();setStateText(QString::fromUtf8("正在请求继续"));refreshButtons();
}
void MainWindow::onStop(){
    if(!m_runner->isRunning()||m_runner->controlPending())return;
    m_runner->stop();setStateText(QString::fromUtf8("正在停止"));refreshButtons();
}

void MainWindow::onTick(double t, double soc, double current, double voltage)
{
    m_batteryView->setSoc(soc);
    m_batteryView->setVoltage(voltage);
    m_batteryView->setCurrent(current);

    m_sideBattery->setSoc(soc);
    m_sideBattery->setVoltage(voltage);
    m_sideBattery->setCurrent(current);

    if(m_display=="live")m_lblTime->setText(QString::fromUtf8("%1 / %2 s").arg(t,0,'f',1).arg(m_runner->duration(),0,'f',1));
    else m_replayTime->setText(QString::fromUtf8("%1 / %2 s").arg(t,0,'f',1).arg(m_player->duration(),0,'f',1));

    m_plotWidget->setProgress(t);

    // 进度跟着当前场景一起记住
    if ((m_display=="cache"||m_display=="complete") && m_activeIndex >= 0 && m_activeIndex < m_states.size()) {
        m_states[m_activeIndex].position = t;
    }
}

void MainWindow::onFinished()
{
    m_stopped=true;
    saveActiveState();
    m_replayState->setText(QString::fromUtf8("回放已结束"));
    refreshButtons();
}

void MainWindow::onApplyCustom(double voltage, double current, int mode, double soc0)
{    if(m_runner->isRunning()&&!m_runner->isPrepared()) return;
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
        m_errorPanel->setPlainText(problems.join("\n"));m_errorPanel->show();appendStageLog(QString::fromUtf8("错误"),problems.join("\n"));
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

    // Applying parameters invalidates the prepared instance; results remain available for replay.
    if(m_runner->isPrepared()&&!inputsMatchPreparation())m_runner->invalidatePreparation();
    appendStageLog(QString::fromUtf8("参数"),QString::fromUtf8("保存客户端参数；未提交宿主写参或求解。"));
    ScenarioState& st = m_states[index];
    st.uiVoltage = voltage;
    st.uiCurrent = current;
    st.uiMode = mode;
    st.uiSoc0 = soc0;
    QSettings saved(QSettings::IniFormat,QSettings::UserScope,"BatteryConsole","ButtonDetails");
    saved.setValue("parameters/voltage",voltage);saved.setValue("parameters/current",current);
    saved.setValue("parameters/mode",mode);saved.setValue("parameters/soc0",soc0);
    st.hint = QString::fromUtf8("参数已保存：起点 %1%、%2 A %3。请点击「翻译模型」。")
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

void MainWindow::startSysplorerRun(double voltage,double current,int mode,double soc0) {
    if(m_runner->isRunning())return;
    const double delta=mode==1?1-soc0:soc0;
    const double duration=current<=0?120.0:qBound(120.0,delta*9000/qMax(current,.05)*1.1+60,20000.0);
    const auto selected=m_resultSelector->currentData().toString();
    if(selected=="cache"||selected.startsWith("partial:"))m_resultBeforeTask=selected;
    m_reqVoltage=voltage;m_reqCurrent=current;m_reqMode=mode;m_reqSoc0=soc0;m_requestIndex=0;
    m_states[0].errorText.clear();m_states[0].savedPreview=false;m_errorPanel->hide();
    m_player->pause();m_stopped=true;m_display="live";m_liveSamples.clear();
    m_plotWidget->clearData();m_plotWidget->setDuration(duration);
    m_tabs->setCurrentIndex(1);
    m_batteryView->setApplyEnabled(false);
    m_lblHint->setText(m_startAfterPreparation?QString::fromUtf8("自动准备中；翻译确认成功后开始仿真。"):
        QString::fromUtf8("正在加载、写参回读并翻译模型；完成后请点击开始仿真。"));
    m_lblTime->setText(QString::fromUtf8("0.0 / %1 s").arg(duration,0,'f',1));
    m_tuneMode->setCurrentIndex(mode);m_tuneCurrent->setValue(current);m_tuneVoltage->setValue(voltage);
    m_tuneMin->setValue(mode==2?voltage:2.5);m_tuneMax->setValue(mode==1?voltage:3.45);m_tuneLimit->setValue(5);
    m_tuneStatus->setText(QString::fromUtf8("等待求解器开始运行。"));
    m_runner->prepareSimulation(voltage,current,mode,soc0,duration);
    refreshResultSelector();refreshButtons();
}

void MainWindow::onSysplorerFinished(bool ok,const QString&csvPath,const QString&message) {
    m_batteryView->setApplyEnabled(true);
    if(!ok) {
        m_translateAfterDiscard=false;m_startAfterPreparation=false;appendStageLog(QString::fromUtf8("错误"),message);
        const double editedV=m_batteryView->voltage(),editedI=m_batteryView->current(),editedSoc=m_batteryView->soc0();
        const int editedMode=m_batteryView->mode();
        m_stopped=true;m_player->pause();
        if(m_resultBeforeTask.startsWith("partial:")&&m_resultSelector->findData(m_resultBeforeTask)>=0){
            m_display="partial";showStoredResult(m_resultSelector->findData(m_resultBeforeTask));
        }else if(m_states[0].loaded){m_display="complete";applyScenario(0);}
        else if(!m_partials.isEmpty()){
            m_display="partial";m_partialIndex=m_partials.size()-1;refreshResultSelector();
            showStoredResult(m_resultSelector->findData("partial:"+QString::number(m_partialIndex)));
        }else {m_display="preview";applyScenario(0);}
        m_batteryView->setInputs(editedV,editedI,editedMode,editedSoc);
        const bool hasHistory=m_states[0].loaded||!m_partials.isEmpty();
        auto &st=m_states[0];st.errorText=(hasHistory?QString::fromUtf8("本次操作失败；最近有效结果仍保留。\n"):QString::fromUtf8("本次操作失败。\n"))+message;
        st.hint=QString::fromUtf8("任务 %1 失败，请查看上方错误及任务日志。").arg(m_runner->taskId());
        m_errorPanel->setPlainText(st.errorText);m_errorPanel->show();m_lblHint->setText(st.hint);
        setStateText(m_runner->phase()=="ConnectionFailed"?QString::fromUtf8("连接失败"):QString::fromUtf8("仿真失败"));refreshResultSelector();refreshButtons();return;
    }
    m_errorPanel->hide();
    ReplayData result;QString error;
    if(!result.load(csvPath,&error)){onSysplorerFinished(false,{},error);return;}
    m_states[0].realSdk=true;m_states[0].modelHash=m_runner->lastModelFingerprint();
    m_states[0].sdkJobDir=m_runner->lastRunDirectory();
    applyComputedCurve(result.samples(),m_reqVoltage,m_reqCurrent,m_reqMode,m_reqSoc0,
        QString::fromUtf8("完整结果 ·任务 %1 ·起点 %2% ·%3 A %4 ·有效范围 0–%5 s。%6")
        .arg(m_runner->taskId()).arg(m_reqSoc0*100,0,'f',1).arg(m_reqCurrent,0,'f',2)
        .arg(m_reqMode==1?QString::fromUtf8("充电"):QString::fromUtf8("放电")).arg(result.duration(),0,'f',2).arg(message));
    if(m_runner->hasRuntimeChanges()){
        // A trajectory with a command history is not a cache of static settings.
        m_states[0].realSdk=false;
        m_states[0].hint=QString::fromUtf8("动态调参完整结果 ·任务 %1；再次仿真将按起始参数重新计算。修改记录位于任务目录 result.json。").arg(m_runner->taskId());
        m_lblHint->setText(m_states[0].hint);
    }
}

void MainWindow::onViewerFinished(const QString& jobDir,bool ok,const QString& message)
{
    if(jobDir!=m_runner->lastRunDirectory()&&jobDir!=m_states[0].sdkJobDir) return;
    m_viewerJob=jobDir; m_viewerOk=ok; m_viewerMessage=message;
    if(m_runner->isRunning()) return;
    auto& st=m_states[0];
    if(!st.hint.contains(message)) st.hint+=QStringLiteral("\n")+message;
    if(!ok) {
        appendStageLog(QString::fromUtf8("错误"),message);
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
    m_lblTime->setText(QString::fromUtf8("%1 / %2 s").arg(samples.last().t,0,'f',1).arg(m_runner->duration(),0,'f',1));
    m_latestSamples=samples;m_latestPartial=false;
    auto &st=m_states[0];st.samples=samples;st.loaded=true;st.customized=true;
    st.voltage=voltage;st.current=current;st.mode=mode;st.soc0=soc0;
    st.uiVoltage=voltage;st.uiCurrent=current;st.uiMode=mode;st.uiSoc0=soc0;
    appendStageLog(QString::fromUtf8("结果"),QString::fromUtf8("完整结果已通过既有任务与文件核验。"));
    st.hint=hint;st.savedPreview=false;st.errorText.clear();m_errorPanel->hide();
    m_display="complete";m_stopped=true;m_player->loadFromSamples(samples);
    st.position=samples.last().t;st.drawn=st.position;
    m_plotWidget->setDuration(samples.last().t);m_plotWidget->setSamples(samples,st.position,st.position);
    m_player->seek(st.position);m_lblHint->setText(hint);
    setStateText(QString::fromUtf8("仿真完成"));refreshResultSelector();refreshButtons();
}

void MainWindow::onHostStateChanged(const QString&phase) {
    if(phase!=m_lastLoggedPhase){
        m_lastLoggedPhase=phase;
        const auto category=(phase=="Failed"||phase=="ConnectionFailed")?QString::fromUtf8("错误"):(phase=="Connected"||phase=="Connecting")?QString::fromUtf8("连接"):
            (phase=="Loading"||phase=="Loaded"||phase=="Preparing"||phase=="Prepared"||phase=="Translating")?QString::fromUtf8("准备"):QString::fromUtf8("仿真");
        appendStageLog(category,QString::fromUtf8("任务阶段：%1").arg(phase));
    }
    if(phase=="Connected"&&m_translateAfterDiscard){m_translateAfterDiscard=false;QTimer::singleShot(0,this,&MainWindow::onPrepare);}
    if(phase=="Prepared"&&m_startAfterPreparation){
        QTimer::singleShot(0,this,[this]{
            if(!m_startAfterPreparation||!m_runner->isPrepared()||m_runner->controlPending())return;
            if(!inputsMatchPreparation()){m_startAfterPreparation=false;refreshButtons();return;}
            m_startAfterPreparation=false;appendStageLog(QString::fromUtf8("仿真"),QString::fromUtf8("自动翻译已确认成功，提交启动请求。"));
            m_runner->beginSimulation();refreshButtons();
        });
    }
    if(phase=="Loaded"){m_hostConnectionFailed=false;setStateText(QString::fromUtf8("模型已加载，等待翻译"));refreshButtons();return;}
    if(phase=="Connected") {m_hostConnectionFailed=false;setStateText(QString::fromUtf8("Sysplorer 已连接"));refreshButtons();return;}
    if(phase=="ConnectionFailed"){m_hostConnectionFailed=true;setStateText(QString::fromUtf8("连接失败，请查看具体原因后重试"));refreshButtons();return;}
    if(phase=="Disconnected"){setStateText(QString::fromUtf8("远程未连接，请检查连接设置"));refreshButtons();return;}
    if(phase=="Connecting"&&!m_runner->isRunning()){setStateText(QString::fromUtf8("后台连接 Sysplorer"));refreshButtons();return;}
    if(!m_runner->isRunning()){refreshButtons();return;}
    const QMap<QString,QString> names{
        {"Loading",QString::fromUtf8("正在加载模型")},{"Connecting",QString::fromUtf8("连接 Sysplorer")},{"Preparing",QString::fromUtf8("加载与设置参数")},
        {"Prepared",QString::fromUtf8("准备就绪")},{"Translating",QString::fromUtf8("翻译模型")},{"Starting",QString::fromUtf8("启动求解")},
        {"Running",QString::fromUtf8("实时仿真中")},{"Pausing",QString::fromUtf8("正在暂停")},
        {"Paused",QString::fromUtf8("仿真已暂停")},{"Resuming",QString::fromUtf8("正在继续")},
        {"Stopping",QString::fromUtf8("正在停止")},{"Finalizing",QString::fromUtf8("核验结果")},
        {"Completed",QString::fromUtf8("核验完整结果")},{"Stopped",QString::fromUtf8("保留部分结果")},
        {"Downloading",QString::fromUtf8("下载并核验远程结果")},{"DownloadFailed",QString::fromUtf8("结果下载失败，可重试")},
        {"ConnectionLost",QString::fromUtf8("连接中断，任务状态待确认")},{"Reconnecting",QString::fromUtf8("重连并核对原任务")}};
    setStateText(names.value(phase,phase));

    m_lblHint->setText(QString::fromUtf8("当前任务 %1 ·%2 A %3 ·起点 %4% ·截止 %5 V")
        .arg(m_runner->taskId()).arg(m_reqCurrent,0,'f',2)
        .arg(m_reqMode==1?QString::fromUtf8("充电"):QString::fromUtf8("放电"))
        .arg(m_reqSoc0*100,0,'f',1).arg(m_reqVoltage,0,'f',3));refreshButtons();
    if(m_runner->hasRuntimeChanges()&&!m_liveSamples.isEmpty()){
        const auto &sample=m_liveSamples.last();
        m_lblHint->setText(QString::fromUtf8("当前任务 %1 ·动态调参 ·实际电流 %2 A ·端电压 %3 V ·当前 SOC %4%")
            .arg(m_runner->taskId()).arg(sample.current,0,'f',3).arg(sample.voltage,0,'f',3).arg(sample.soc*100,0,'f',2));
    }
}
void MainWindow::onLiveSamples(const QVector<ReplaySample>&samples) {
    if(m_display!="live"||samples.isEmpty())return;
    m_liveSamples+=samples;m_plotWidget->appendSamples(samples);
    const auto&s=samples.last();onTick(s.t,s.soc,s.current,s.voltage);refreshButtons();
}
void MainWindow::onSysplorerStopped(const QString&csvPath,const QString&message) {
    m_errorPanel->hide();
    appendStageLog(QString::fromUtf8("结果"),QString::fromUtf8("主动停止，读取已核验部分结果。"));
    ReplayData result;QString error;
    if(!result.load(csvPath,&error)){onSysplorerFinished(false,{},error);return;}
    m_lblTime->setText(QString::fromUtf8("%1 / %2 s").arg(result.duration(),0,'f',1).arg(m_runner->duration(),0,'f',1));
    m_latestSamples=result.samples();m_latestPartial=true;
    m_partials.append({m_runner->taskId(),result.samples(),csvPath,m_reqVoltage,m_reqCurrent,m_reqSoc0,m_reqMode});
    m_partialIndex=m_partials.size()-1;m_display="partial";m_stopped=true;
    m_batteryView->setApplyEnabled(true);m_errorPanel->hide();refreshResultSelector();
    showStoredResult(m_resultSelector->findData("partial:"+QString::number(m_partialIndex)));
    if(!message.isEmpty())m_lblHint->setText(m_lblHint->text()+"\n"+message);
    if(m_runner->hasRuntimeChanges())m_lblHint->setText(m_lblHint->text()+QString::fromUtf8("\n本轨迹包含运行期修改；各次输入和生效确认时刻见任务目录 result.json。"));
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
        m_replayState->setText(m_resultSelector->currentText());
    }else if(key.startsWith("partial:")){
        bool ok=false;int i=key.mid(8).toInt(&ok);if(!ok||i<0||i>=m_partials.size())return;
        m_display="partial";m_partialIndex=i;m_stopped=true;const auto&p=m_partials[i];
        m_player->loadFromSamples(p.samples);m_plotWidget->setDuration(m_player->duration());
        m_plotWidget->setSamples(p.samples,m_player->duration(),m_player->duration());m_player->seek(m_player->duration());
        m_lblHint->setText(QString::fromUtf8("已停止／部分结果 ·任务 %1 ·有效范围 %2–%3 s ·起点 %4% ·%5 A %6；不覆盖完整成功缓存。")
            .arg(p.taskId).arg(p.samples.first().t,0,'f',2).arg(p.samples.last().t,0,'f',2)
            .arg(p.soc0*100,0,'f',1).arg(p.current,0,'f',2)
            .arg(p.mode==1?QString::fromUtf8("充电"):QString::fromUtf8("放电")));
        m_replayState->setText(m_resultSelector->currentText());
    }refreshButtons();
}

# 项目级代理协作指南

适用于本仓库及所有子目录；更深层的 `AGENTS.md` 若存在，对其目录作补充或细化。用户当前明确要求优先。本文件提供开发基线，不授权关闭用户程序、丢弃未保存数据、发布或覆盖其他工作树。

维护日期：2026-11-08。当前基线为 be10 最终内置模型双模式版本。先核对当前源码，不要把历史对话或旧文档中的行为恢复到新版。

## 1. 项目定位与阅读顺序

这是 Windows 电池充放电仿真客户端：本机模式经 loopback UDP，远程模式经认证 TCP 与 Sysplorer 宿主插件交互。两种模式都上传客户端 EXE 内置模型；插件使用 SDK 访问宿主现有对象执行真实计算，远程结果下载到客户端核验。远程失败不得自动回退本机。

开始相关任务时按需阅读：

1. [README.md](README.md)：当前主目录架构、界面、配置、构建和已知限制。
2. [可移植部署说明](docs/可移植部署说明.md)、[模型上传协议](docs/模型上传协议.md)、[TCP远程任务协议](docs/TCP远程任务协议.md)、[UDP任务协议](docs/UDP任务协议.md)：现行部署、上传及两种传输契约。
3. [按钮细化界面开发交付记录](docs/按钮细化界面开发交付记录.md)：当前交互及实现背景。
4. [主目录合并测试结果](docs/主目录合并测试结果.md)：真实验收、首次失败和复测证据。
5. 修改相关模块前阅读其实现与对应测试。

当前交付证据见[内置模型交付测试报告](docs/内置模型交付测试报告.md)。早期双模式、R3/R4 外部模型和主目录合并报告均有历史适用范围，不得把旧结论当作 R5 当前状态。

`docs/交付文档/`、根目录部分 SDK 故障说明描述早期 helper 阶段；`docs/UDP正式接入开发记录.md` 和 `docs/UDP架构设计与开发经验及helper方案对比.md` 包含更早 UDP 阶段。按章节、日期及源码判断适用范围，不能把这些文档全部当作当前规范。

## 2. 工程地图

| 路径 | 职责 |
|---|---|
| `app/ui/src/main.cpp` | Qt 应用与事件循环入口 |
| `app/ui/src/main_window.*` | 页面、按钮流程、任务快照、历史结果和显示来源 |
| `app/ui/src/battery_view.*` | 参数表单、预览及自绘电池 |
| `app/ui/src/plot_widget.*` | QPainter 实时曲线与播放进度 |
| `app/ui/src/sysplorer_runner.*` | 宿主发现/启动、能力检查、UDP、状态和结果核验 |
| `common/remote_link.*`、`remote_server.*` | TCP 分帧、双向认证、消息完整性及服务端接入 |
| `common/model_transfer.h`、`model_file.h` | 模型分块上传、UTF-8/哈希校验及诊断 |
| `app/ui/src/runtime_environment.*` | 正式客户端启动环境处理 |
| `tools/deployment/` | 可移植客户端/服务器打包、配置、诊断与部署入口 |
| `app/ui/src/replay_data.*`、`replay_player.*` | 已核验结果的本地读取、插值与回放 |
| `app/ui/tests/udp_integration_test.cpp` | 当前真实 Qt 交互及宿主集成测试 |
| `tools/sysplorer_udp_plugin/control_plugin.*` | 正式宿主插件、任务控制、运行期输入、采集及结果导出 |
| `tools/sysplorer_udp_plugin/prepare_isolated_host.ps1` | 准备专用测试宿主副本并检查插件 |
| `model/BatterySim.mo` | 顶层 SingleRun、BatteryCell 及运行期输入 |
| `app/ui/models.qrc` | 客户端内嵌模型；插件不再内置固定模型 |
| `tools/sdk_probe/`、`tools/udp_host_probe/` | 历史 helper/探针，非正式客户端后端 |
| `docs/主目录合并验证/` | 已归档的精选日志、截图和结果摘要 |
| `logs/`、`runs/`、`runtime*`、构建目录 | 本地产物；通常被 Git 忽略 |

## 3. 当前行为与设计约束

除非用户明确要求改变，下列语义应保持：

- 两个主要应用为 Qt 与 Sysplorer；SDK 是进程内接口，不是独立网络进程。编译器、求解器可有子进程。
- 正式路径不重新接入 helper 或 Python 查看桥接，不以近似公式、模拟样本或旧 CSV 替代真实计算。
- 打开客户端只读设置，不自动连接或启动宿主；保存并连接或业务操作才发起连接。远程模式只访问指定服务器。
- 当前固定顶层模型为 `BatterySim.SingleRun`；不能宣称自动控制宿主任意打开的模型。
- 独立加载只加载模型，翻译完成停在 `Prepared`，开始仿真复用有效准备实例；未准备时开始入口自动准备。不要将不同按钮连接为同一完整流程。
- 保存参数只保存客户端设置并更新预览；翻译和开始读取当前输入，不强制先保存。输入变化必须按准备快照规则处理。
- 真实仿真控制与历史回放使用独立按钮、速度、时间和状态。回放使用当前选中结果，不创建求解任务；真实运行和准备/结果处理时限制回放。
- 暂停保留真实任务，继续恢复求解；主动停止保留有效部分结果，不能当成暂停任务继续。
- 完整和部分结果分别保留。失败任务不替换有效结果；新结果核验成功前不得提交。
- 动态调参轨迹不能当作静态初始参数缓存。结果索引目前仅在客户端进程内维护，落盘文件不等于已经实现重启恢复。
- 不用旧版“点击仿真即缓存回放”的交互覆盖当前独立开始/回放设计。

## 4. 通信、宿主与模型修改规则

- 本机 UDP 仅绑定 `127.0.0.1` 动态端口。发现记录位于 `%LOCALAPPDATA%/BatteryConsole/udp/<PID>.json`，含会话令牌，不提交或对外分发。远程 TCP 使用独立服务器设置和凭据，认证不等于 TLS 加密；不将凭据写入提交、日志或测试报告。
- 保留请求编号、会话、客户端、任务归属、模型哈希、游标和有限重试检查。收到命令确认不等于业务完成。
- 保留独立模型生命周期、模型上传能力和接口检查，不静默降级为旧插件行为。协议修改同步客户端、插件、common 模块、测试及对应文档。
- 复用宿主现有模型管理器、仿真面板与控制器；任务控制前检查实际绑定对象。不能仅凭任务 UUID 认定可控制。
- 已有宿主忙碌或模型不匹配时按发现逻辑跳过，不接管其他任务；新实例数量按当前保护逻辑限制。
- `tune` 提交六个运行期字段，按协议校验。在实际模型采样回读确认前不能显示已生效；即时 `GetReals` 可能还是旧步值。
- 在线调参保持同一任务、时间与 SOC，不重新初始化。运行中先暂停写入再恢复；已暂停任务保持暂停。
- 最终结果使用本地 CSV 和清单交接；保留任务、模型、参数、哈希、点数、时间和数值检查。部分结果按有效范围裁剪，不能以配置终点冒充有效时间。
- `nativeResult` 当前为结果根目录，不应直接当作具体 MSR 文件打开；原生绘图使用宿主结果对象。
- 正式模型来自 `:/models/BatterySim.mo`，不读取外部 `.mo` 或 `BATTERY_MODEL_FILE` 覆盖；测试专用覆盖宏不得进入正式 EXE。修改模型后重建客户端；插件按上传文本校验和加载，不再要求两端内置模型一致。修改接口或插件本身仍需配套构建并重启宿主，不绕过上传完整性与接口保护。
- 倍速映射 SDK 减速因子为 `1/speed`；仿真秒、现实耗时、采样间隔和渲染频率必须区分。
- SDK 与宿主部分接口有版本/ABI 依赖；保留现有导出符号适配和版本检查，不能仅凭年份相同认定兼容。

## 5. 构建与配置

当前脚本使用 Windows x64、C++11、VS 2017 Professional、Qt 5.14.2 `msvc2017_64` 与 Sysplorer SDK 2026a（vs2017）。默认路径写在各构建脚本、插件 `plugin.pro` 和 UI 部署脚本中，环境不同时先核对，不擅自升级工具链。

从仓库根目录的 PowerShell 构建；按修改范围选择必要项：

```powershell
.\tools\sysplorer_udp_plugin\build.cmd
if ($LASTEXITCODE -ne 0) { throw '插件构建失败' }
.\app\ui\build.cmd
if ($LASTEXITCODE -ne 0) { throw '客户端构建失败' }
.\app\ui\tests\build.cmd
if ($LASTEXITCODE -ne 0) { throw '测试构建失败' }
```

主要产物分别在 `tools/sysplorer_udp_plugin/release/`、`app/ui/build/release/`、`app/ui/tests/build/release/`。不手工修改生成的 Makefile。

优先使用纯英文、短路径的专用宿主和工作目录。参考部署流程如下，执行前确保目标是本任务的新目录、插件已构建、运行包未被占用：

```powershell
$ErrorActionPreference = 'Stop'
$batteryAgentHost = Join-Path $env:TEMP ('BQ' + (Get-Date -Format 'MMddHHmmss'))
if (Test-Path -LiteralPath $batteryAgentHost) { throw '请选择新的专用宿主目录' }
& .\tools\sysplorer_udp_plugin\prepare_isolated_host.ps1 `
    -SysplorerHome 'C:\Program Files\MWORKS\Sysplorer 2026a' -Destination $batteryAgentHost
& .\app\ui\deploy.ps1 -RuntimeDirectory app/ui/runtime_buttons -SysplorerHome $batteryAgentHost
Add-Content -LiteralPath .\app\ui\runtime_buttons\sysplorer.ini -Value 'TimeoutMs=180000' -Encoding utf8
```

可移植包主入口为客户端 `Start-Client.cmd`、服务器 `Setup-Server.cmd`；仓库入口及参数以当前 README、脚本为准。上面的隔离宿主命令是早期开发方式，使用前应核对与当前部署脚本兼容。新包不得携带开发工作树、私有宿主路径、凭据或测试配置。服务器设置和插件更新可能改安装目录及防火墙，不是纯只读操作；本任务未明确授权时不执行。

本机配置优先级：开发/测试的 `SYSPLORER_HOME` → EXE 旁 `sysplorer.ini` 的 `Host/Home` → 用户级 `local-runtime.json` 收据；未配置时应走设置或明确报错，不默认接管系统安装。`BATTERY_HOST_TIMEOUT_MS` 覆盖 `Host/TimeoutMs`，当前默认 180000 ms，有效范围 1000–600000 ms。`BATTERY_SIM_WORKSPACE` 指定任务工作目录基础路径，客户端与宿主配置应一致。

## 6. 验证与验收

- 文档修改只检查事实、链接和格式，不因此启动真实求解。
- UI/通信/模型变更运行相关现有测试；完成必要检查后，不无故重复全套求解。
- 使用真实可见 Qt 控件验证用户路径。诊断脚本、直接 UDP 或 SDK 调用不能替代最终窗口按钮验收。
- 测试会创建真实任务、启动宿主并占用许可，使用专用测试实例。先构建并部署配套两端，避免测试旧运行包。
- `tools/deployment/Build-Clean.ps1 -BuildName <新的英文名称>` 可在本树新建干净构建目录。模型校验、模型传输、TCP/界面模拟回归及打包自检优先使用此轮新产物；模拟通过不代表实际两机通过，跳过项不计通过。
- 模拟测试还须隔离用户级 `local-runtime.json`，不能只设置一个不存在的 `SYSPLORER_HOME`；“未配置”用例会清空该环境变量。可用本树测试目录的 `BATTERY_LOCAL_CONFIG` 指向空收据，测试 EXE 旁不要配置真实宿主。避免继承真实服务器凭据及实机测试开关。

专用宿主准备好后，从仓库根目录运行：

```powershell
& .\app\ui\tests\run_button_details.ps1 -TestHost $batteryAgentHost
```

日志、逐项结果和截图默认输出到 `logs/button_details/`，具体选项以脚本当前实现为准。按改动覆盖加载/翻译/开始、状态确认、真实暂停继续停止、结果选择与回放、调参、失败保护和忙碌实例隔离中的相关路径。

排障先查本次 `%TEMP%/BatteryConsoleUDP/<任务 UUID>`（或配置目录）的 `client.log`、`plugin.log`、`status.json`、`result.json` 和 CSV，再核对实际启动 EXE、宿主路径、加载插件版本和模型哈希。

2026-11-05 历史完整复测记录为 10 passed、0 failed，其中 8 项业务、初始化及清理。此前首次回归发生过 `TRANSLATE_FAILED`；相同二进制复测通过不能宣称根因已解决。不得把旧报告写成本轮新测试结果。

R5 已有普通双击本地实际计算和正式 EXE 到虚拟机计算的来源验收记录，但不自动代表本次合并后重新验收。远程认证超时与关闭窗口停止时序仍有已知边界；运行中先停止并等终态再退出。根因未确证不得标为修复。

## 7. 部署、进程与文件操作边界

- 优先使用本任务隔离宿主，勿覆盖其他工作树运行包或安装目录中的插件。
- 更新安装目录插件需要管理员权限；ExecutionPolicy Bypass 不授予该权限。使用现有部署脚本及其哈希检查，不替换原厂 DLL。
- 磁盘 DLL 更新不代表宿主已加载新版本。确认目标路径、PID、文件占用和加载版本；不要按名称批量终止所有 mworks.exe。
- 只管理当前任务已明确授权的专用测试进程。旧对话中对某次 PID 或测试轮次的授权，不自动涵盖未来所有实例；用户未保存工作不得擅自丢弃。
- 保留现有源码和用户改动；删除、移动或清理前核对绝对目标和范围。避免对临时目录、安装目录或整个仓库执行宽泛递归清理。
- SDK、许可、运行库、令牌、本机配置和大型生成物不提交。精选证据归档按现有文档目录惯例；不要为了展示通过而删除首次失败记录。

## 8. 代码与交付习惯

- 延续 Qt 信号槽、QTimer 和事件驱动方式，避免在 UI 或宿主事件线程中同步等待求解完成。
- 界面只展示数据，宿主适配集中于 Runner/插件，电池物理方程保留在 Modelica。避免把协议、缓存和数值算法散落进绘图函数。
- 保持 C++11、UTF-8 和现有命名风格；优先针对根因的小范围修改，不做无关重构。
- 先建立过渡状态再调用可能同步回调的接口；将宿主确认、命令已接受及最终成功分开。
- 文档以当前 README/协议为事实入口，历史内容标明日期与版本；错误根因未确证时写“待确认”，不编造兼容性、精度或性能结论。
- 完成后说明改了什么、为何修改、运行了哪些验证、哪些未验证。用户未要求时不自行发布、推送或进行无关部署。

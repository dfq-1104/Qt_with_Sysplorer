# 项目级代理协作指南

适用于本仓库及所有子目录；更深层的 `AGENTS.md` 若存在，对其目录作补充或细化。用户当前明确要求优先。本文件提供开发基线，不授权关闭用户程序、丢弃未保存数据、发布或覆盖其他工作树。

维护日期：2026-11-09。当前基线为 be10 最终内置模型双模式版本 `20261108-embedded-model-r5`。本文已按当前源码、构建和部署脚本核对；本次维护没有重新运行仿真。先核对当前源码，不要把历史对话或旧文档中的行为恢复到新版。

## 1. 项目定位与阅读顺序

这是 Windows 电池充放电仿真客户端：本机模式经 loopback UDP，远程模式经认证 TCP 与 Sysplorer 宿主插件交互。两种模式都上传客户端 EXE 内置模型；插件使用 SDK 访问宿主现有对象执行真实计算，远程结果下载到客户端核验。远程失败不得自动回退本机。

开始相关任务时按需阅读：

1. [README.md](README.md)：当前主目录架构、界面、配置、构建和已知限制。
2. [项目详解阅读说明](docs/项目详解/阅读说明.md) → [项目总览](docs/项目详解/01_项目总览.md) → [Qt 客户端详解](docs/项目详解/02_Qt客户端详解.md) → [Sysplorer 调用与部署详解](docs/项目详解/03_Sysplorer调用与部署详解.md)：先理解职责、状态、宿主对象和两端操作。离线 HTML 从 [index.html](docs/项目详解/index.html) 打开。
3. 按任务阅读[可移植部署说明](docs/可移植部署说明.md)、[模型上传协议](docs/模型上传协议.md)、[TCP远程任务协议](docs/TCP远程任务协议.md)、[UDP任务协议](docs/UDP任务协议.md)：部署步骤及具体通信契约。
4. [内置模型交付测试报告](docs/内置模型交付测试报告.md)与[最终双模式版本合并记录](docs/最终双模式版本合并记录_20261108.md)：分别核对来源版本验收和本工作树回归，不混用通过数量与时间。
5. 涉及界面演进时再读[按钮细化交付记录](docs/按钮细化界面开发交付记录.md)和[早期主目录合并测试](docs/主目录合并测试结果.md)；修改前始终阅读相关实现与对应测试。

当前交付证据见[内置模型交付测试报告](docs/内置模型交付测试报告.md)。早期双模式、R3/R4 外部模型和主目录合并报告均有历史适用范围，不得把旧结论当作 R5 当前状态。

`docs/项目详解` 是完整同步的 R5 文档快照：代码摘录行号、截图及 manifest 哈希对应其生成时点，不自动随代码更新。HTML 内嵌图片，Markdown 依赖同目录 assets。阅读说明提及的 `tools/documentation/` 生成工具未随本仓库同步，不能当作当前已有的可运行命令。`.gitattributes` 对该目录保留原始字节；不为消除源文档中的尾部空白而批量格式化，以免破坏清单和来源一致性。

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
| `docs/项目详解/` | 总览、Qt、Sysplorer 图文详解；含 Markdown、离线 HTML、assets 和来源清单 |
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
- 正常启动显示界面、读取设置，并按启动入口校验内置模型；不自动连接或启动宿主。保存并连接或业务操作才发起连接，远程模式只访问指定服务器。
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
- 已有宿主忙碌、能力不足或接口不兼容时按当前发现和错误处理逻辑处理，不接管其他任务；显式连接与准备任务的入口可能有不同策略，新实例数量按保护逻辑限制。模型内容以本次已提交的上传快照为准，不恢复“客户端模型哈希必须等于插件内置模型哈希”的旧约束。
- `tune` 提交六个运行期字段，按协议校验。在实际模型采样回读确认前不能显示已生效；即时 `GetReals` 可能还是旧步值。
- 在线调参保持同一任务、时间与 SOC，不重新初始化。运行中先暂停写入再恢复；已暂停任务保持暂停。
- 本机结果读取同机任务文件；远程结果下载到客户端后核验 CSV 与清单，不依赖共享目录或直接读取远端路径。保留任务、模型、参数、哈希、点数、时间和数值检查。部分结果按有效范围裁剪，不能以配置终点冒充有效时间；仅下载失败不重新启动求解。
- `nativeResult` 当前为结果根目录，不应直接当作具体 MSR 文件打开；原生绘图使用宿主结果对象。
- 正式模型来自 `:/models/BatterySim.mo`，不读取外部 `.mo` 或 `BATTERY_MODEL_FILE` 覆盖；测试专用覆盖宏不得进入正式 EXE。修改模型后重建客户端；插件按上传文本校验和加载，不再要求两端内置模型一致。修改接口或插件本身仍需配套构建并重启宿主，不绕过上传完整性与接口保护。
- 倍速映射 SDK 减速因子为 `1/speed`；仿真秒、现实耗时、采样间隔和渲染频率必须区分。
- SDK 与宿主部分接口有版本/ABI 依赖；保留现有导出符号适配和版本检查，不能仅凭年份相同认定兼容。

## 5. 构建与配置

当前脚本使用 Windows x64、C++11、VS 2017 Professional、Qt 5.14.2 `msvc2017_64` 与 Sysplorer SDK 2026a（vs2017）。默认路径写在各构建脚本、插件 `plugin.pro` 和 UI 部署脚本中，环境不同时先核对，不擅自升级工具链。

### 干净构建与可移植打包

需要完整验证或生成交付包时，优先从仓库根目录的 PowerShell 使用新的英文构建名称；此流程只生成本树产物，不安装服务器或执行仿真：

```powershell
$ErrorActionPreference = 'Stop'
$batteryBuildName = 'release-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
& .\tools\deployment\Build-Clean.ps1 -BuildName $batteryBuildName
$batteryBuildRoot = Join-Path (Get-Location).Path ('build/' + $batteryBuildName)
& .\tools\deployment\package.ps1 `
    -OutputDirectory (Join-Path (Get-Location).Path ('dist/' + $batteryBuildName)) `
    -ClientExe (Join-Path $batteryBuildRoot 'ui/release/battery_console_ui.exe') `
    -PluginDll (Join-Path $batteryBuildRoot 'plugin/release/battery_udp_control.dll')
```

Build-Clean 构建 `ui`、`plugin`、`validation`、`transfer`、`remote`、`build_startup_fixture`、`layout` 七个目标，产物在 `build/<名称>/<目标>/release/`。打包必须显式选择同次构建的 EXE 和 DLL，不误用旧 `release` 文件。输出客户端 ZIP、服务器 ZIP、文件清单和哈希；不把输出包、运行依赖或凭据提交 Git。

仅修改局部模块时，也可使用现有增量脚本，按需要选择，不要求每次全部执行：

```powershell
.\tools\sysplorer_udp_plugin\build.cmd
if ($LASTEXITCODE -ne 0) { throw '插件构建失败' }
.\app\ui\build.cmd
if ($LASTEXITCODE -ne 0) { throw '客户端构建失败' }
.\app\ui\tests\build.cmd
if ($LASTEXITCODE -ne 0) { throw '测试构建失败' }
```

增量脚本主要产物分别在 `tools/sysplorer_udp_plugin/release/`、`app/ui/build/release/`、`app/ui/tests/build/release/`，与干净构建目录不同。不手工修改生成的 Makefile。

### 启动与部署边界

- 可移植客户端：完整解压包，`Check-Model.cmd` 检查内置模型，`Start-Client.cmd` 启动。启动脚本清理继承的开发绑定并在实际 Qt 进程内执行资源自检，不自动启动宿主；只有后续连接或业务操作才按需准备。
- 本机首次设置：`Setup-Local.ps1` 调用统一 `Setup-Deployment.ps1 -Mode Local`，确认真实安装、配套插件、工作目录及用户收据。正常设置和测试隔离是不同流程，不把本机私有路径写入发布包。
- 远程服务器首次设置：`Setup-Server.cmd`／`Setup-Server.ps1` 调用统一脚本的 Server 模式。安装更新前保存并关闭对应宿主，必要时提权；可能改安装目录和防火墙，未获当前任务授权时不执行。沿用有效凭据和配置，不能为排除超时随意轮换密钥。
- 日常远程使用：用户从原图标启动服务器 Sysplorer，再用 `Check-Server.cmd` 确认监听、双向认证、上传能力及引擎 READY。服务器不需要同时运行 Qt；仅远程使用的客户端无需本地安装 Sysplorer。没有后台服务或远程 shell 启动宿主功能。
- 仓库开发入口：`app/ui/run_ui.bat → start_ui.ps1`。有本树 `.local-sysplorer/state.json` 时转专用开发入口，否则按显式 RuntimeDirectory 或候选包选择。它不等同正式包启动脚本；不要假定固定优先 runtime_buttons，或仅凭构建完成就认定启动的是新包。
- 测试宿主：优先采用本树授权的隔离副本及短英文工作路径，使用前阅读相应准备脚本参数和产物依赖；不把旧私有工作树脚本当作普通用户部署入口。

### 配置优先级

直接启动 EXE 时，本机宿主配置为开发/测试的 `SYSPLORER_HOME` → EXE 旁 `sysplorer.ini` 的 `Host/Home` → 用户级 `local-runtime.json` 收据；未配置时走设置或明确报错，不默认接管系统安装。正式 `Start-Client` 会清理 SYSPLORER_HOME 等继承变量，因此不能把直接 EXE 的优先级当作正式启动脚本允许的覆盖方式。

`BATTERY_HOST_TIMEOUT_MS` 覆盖 `Host/TimeoutMs`，Runner 当前默认 180000 ms，有效范围 1000–600000 ms。开发/测试的 `BATTERY_SIM_WORKSPACE` 指定任务基础路径；正式设置还会记录部署工作目录。默认远程 TCP 端口为 39001，本机 UDP 动态端口不可与其混淆。客户端访问凭据按当前 Windows 用户 DPAPI 保护保存，不复制到仓库或报告。

## 6. 验证与验收

- 文档修改只检查事实、链接和格式，不因此启动真实求解。
- UI/通信/模型变更运行相关现有测试；完成必要检查后，不无故重复全套求解。
- 使用真实可见 Qt 控件验证用户路径。诊断脚本、直接 UDP 或 SDK 调用不能替代最终窗口按钮验收。
- 测试会创建真实任务、启动宿主并占用许可，使用专用测试实例。先构建并部署配套两端，避免测试旧运行包。
- `tools/deployment/Build-Clean.ps1 -BuildName <新的英文名称>` 可在本树新建干净构建目录。模型校验、模型传输、TCP/界面模拟回归及打包自检优先使用此轮新产物；模拟通过不代表实际两机通过，跳过项不计通过。
- 模拟测试还须隔离用户级 `local-runtime.json`，不能只设置一个不存在的 `SYSPLORER_HOME`；“未配置”用例会清空该环境变量。可用本树测试目录的 `BATTERY_LOCAL_CONFIG` 指向空收据，测试 EXE 旁不要配置真实宿主。避免继承真实服务器凭据及实机测试开关。

专项测试应按实际产物路径执行。`run_remote.ps1` 读取 `app/ui/tests/build_remote/release/`，不能直接用于 Build-Clean 的 `remote/release/` 产物；后者应调用本次构建的测试 EXE，并准备模型、测试根目录、Qt 平台等环境。模型覆盖宏只属于测试工程。

需要执行已有真实本机按钮测试时，先核验 `$batteryAgentHost` 指向本任务已准备、配套的专用宿主，并确认增量测试产物已构建，再运行：

```powershell
& .\app\ui\tests\run_button_details.ps1 -TestHost $batteryAgentHost
```

该按钮脚本的日志、逐项结果和截图默认输出到 `logs/button_details/`，还会选择 TEMP 下的测试工作目录；其他测试输出以各脚本为准。按改动覆盖加载/翻译/开始、状态确认、真实暂停继续停止、结果选择与回放、调参、失败保护和忙碌实例隔离中的相关路径。

排障先查本次 `%TEMP%/BatteryConsoleUDP/<任务 UUID>`（或配置目录）的 `client.log`、`plugin.log`、`status.json`、`result.json` 和 CSV，再核对实际启动 EXE、宿主路径、加载插件版本和模型哈希。

2026-11-05 历史完整复测记录为 10 passed、0 failed，其中 8 项业务、初始化及清理。此前首次回归发生过 `TRANSLATE_FAILED`；相同二进制复测通过不能宣称根因已解决。不得把旧报告写成本轮新测试结果。

R5 已有普通双击本地实际计算和正式 EXE 到虚拟机计算的来源验收记录，但不自动代表本次合并后重新验收。远程认证超时与关闭窗口停止时序仍有已知边界；运行中先停止并等终态再退出。根因未确证不得标为修复。

本工作树 2026-11-08 最终合并回归为连接／界面 69 passed、1 failed、3 skipped，失败为 closingClientSendsStop。来源 R5 的 70 passed、0 failed、3 skipped 是另一份历史证据；项目详解中的数字不能覆盖合并后的失败记录。维护 AGENTS、README 或图文文档不需要因此重新构建、部署或运行真实求解。

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

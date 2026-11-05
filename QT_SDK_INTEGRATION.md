# Qt 界面接入 Sysplorer SDK

> 当前正式界面使用 `models.qrc` 打包的 `:/models/BatterySim.mo`，经进程管道校验传给 helper，并使用 SDK 公开 `NewModel(text)` 接口加载。修改工程模型后须重新构建和部署界面。此前外部 `.mo` 文件交接方案受本机桌面进程的透明加密环境影响；根因和实际按钮验收见 [SDK界面故障根因与按钮验证](SDK界面故障根因与按钮验证.md)。下文文件模式细节保留作独立 helper 的历史诊断说明。

当前工作区已接入独立 C++ SDK helper。历史界面源码已迁入 `app/ui`，历史工程未修改。

当前版本只保留自定义充放电；预设场景入口及本地近似计算分支已移除。点击“仿真”先比对上一次成功计算的模型指纹、方向、截止电压、电流和起始 SOC：匹配时直接回放内存结果，不启动 SDK 或查看桥接，不重复打开 Sysplorer。未命中才进行真实计算并打开原生结果。内存缓存只在本次客户端运行期间保留。

当前版本实际按钮验收使用 0.10 A 充电、起始 SOC 53%、截止 3.45 V：任务 `20261104_031826_d68460d3-800a-41d2-9ec3-a2d1d4c1ca7b` 完成本地 SDK 计算，得到 20002 个采样点，Sysplorer 三幅原生曲线均显示成功。暂停后为 1453 s、54.6%、剩余 1.365 Ah；点击停止后三项保持一致，进度标记和曲线保留。再次点击仿真显示“内存结果回放中”，helper 日志、查看桥接日志/清单及原生 MSR 的内容与修改时间全部保持不变，见 `logs/single_custom_cache_verified.json`。最终 Qt 回归 3 passed、0 failed，89188 ms，覆盖移除预设入口、参数变更重算、缓存复用、暂停停止冻结结果与失败保留曲线。

## 启动和使用

双击 [启动界面](app/ui/run_ui.bat)，或运行 `app/ui/runtime/battery_console_ui.exe`。本机需要保留已安装的 Qt 编译环境、Sysplorer SDK 2026a 和相应许可。

1. 设置自定义充放电的方向、起始电量、电流及截止电压。
2. 点击应用参数，进行校验和保存，立即更新自定义场景的电池图、当前电量、剩余 Ah 和设定电流显示，并标记“待仿真”；不计算、不回放。切换场景再回来仍保留该预览。
3. 点击仿真。参数变化或没有对应真实结果时，启动独立 C++ helper，设置模型参数、重新编译并由本地求解器计算。同时连接本机已打开的 Sysplorer 2026a；没有可用 GUI 实例时自动打开。
4. 计算成功并核验结果后，客户端自动进入曲线页回放；Sysplorer 同时加载该任务的原生 `solver/Result.msr`，生成 SOC、电压和电流曲线。两处查看的是同一次计算，Sysplorer 查看过程不重复仿真。
5. 暂停与停止都冻结当前时间、进度标记、电池读数和曲线，缓存不被清除。暂停后“继续”从当前位置播放；再次点击“仿真”按当前已保存参数比对缓存，匹配时从头回放。

电压输入是恒流充电的上限或恒流放电的下限，实际端电压由模型计算。切换充放电方向时分别回填 3.45 V、2.50 V，可在安全范围内调整。

首次打开不加载历史曲线。只有成功计算的结果才能进入缓存。相同参数且模型 SHA256 一致时复用内存结果；参数或模型改变后重新计算。失败不会覆盖上一次成功结果。

## 本地 Sysplorer 查看

查看桥接使用已安装 Sysplorer 自带的 `External/python64/python.exe` 和官方 `mworks.sysplorer` API。`FindSysplorer`/`ConnectSysplorer` 复用同一安装路径的 GUI 实例；仅找到控制台实例或其他版本时不误用，使用 `StartSysplorer` 打开配置的 2026a。`CreatePlot(resultFile=...)` 直接加载或复用指定原生结果并生成三幅曲线，不再调用会弹出“已经打开”的 `OpenResult`。不清除用户已有结果。C++ SDK 仍是唯一的计算路径，Python 只负责打开 GUI 和查看。

默认安装目录为 `C:/Program Files/MWORKS/Sysplorer 2026a`，可通过 `SYSPLORER_HOME` 指定。部署脚本将 `sysplorer_view.py` 放入客户端 `helper` 目录。每个任务的 `sysplorer_view.log` 记录查看接口日志，`sysplorer_view.json` 记录连接/打开阶段、是否复用窗口和原生结果路径。若查看接口失败，客户端已计算结果继续可用，并在对应场景显示独立查看错误；关闭客户端不会关闭用户的 Sysplorer。

离屏 Qt 回归通过 `BATTERY_SYSPLORER_GUI=0` 仅关闭查看桥接，避免测试批量打开窗口；真实计算仍由 SDK 执行。实际桌面按钮验收另验证自动启动和复用现有 Sysplorer。

以下为早期查看桥接版本的历史验收；其中“缓存时重新打开 Sysplorer”已由当前版本的“缓存只回放、不发起打开请求”替代：

- 修改起始 SOC 为 65%，点击应用参数，现场显示 65%、剩余 1.625 Ah、设定充电 2.500 A（待仿真），没有启动计算。
- Sysplorer 未打开时点击仿真：任务 `20261104_024349_85dc0a15-34b0-4feb-aa00-25c17703a7f0` 自动启动 GUI，`sysplorer_view.json` 为 `stage=opened,reused=false`，三幅原生曲线均成功；结果查看器实际显示 runCurrent=2.5、socStart=0.65。
- 将起始 SOC 改为 40% 并应用，显示 40%、剩余 1.000 Ah。保持 Sysplorer 打开后再仿真：任务 `20261104_024834_bb10f656-1811-4530-9415-828f5c3b18ab` 使用现有窗口载入新结果，`reused=true`，三幅曲线均成功；记录见 `logs/sysplorer_reuse_verified.json`。
- 关闭 Sysplorer 后，在客户端停止并点击同参数仿真，直接播放缓存并重新启动 Sysplorer，加载同一 MSR，`reused=false`。helper 日志哈希和修改时间均未变化，证明没有重复 SDK 计算；记录见 `logs/sysplorer_cache_verified.json`。
- 最终 Qt 回归 3 passed、0 failed，114892 ms；覆盖应用参数即时预览、场景切换恢复、真实充放电、保护、零电流、缓存和错误隔离，见 `logs/ui_integration.txt`。

## 调用和结果校验

```text
Qt MainWindow
  → SysplorerRunner 使用 QProcess 异步启动 sdk_probe.exe
  → BatterySim.SingleRun 写入方向、电流、截止电压及初始 SOC
  → CompileModel 生成 64 位求解器
  → 64 位 MWSolver 计算
  → SDK GetVarData 读取 SOC、电流、电压
  → result.csv 和 result.json
  → 界面验证成功后加载 ReplayPlayer
```

界面不链接 SDK DLL，不启动 Python/MCP，不在自定义计算失败时替换为本地积分结果。helper 使用独立英文任务目录保存模型副本、编译文件、原始 MSR、CSV、参数清单和日志。

界面核对 helper 正常退出码、结果来源、模型指纹、方向、电流、电压、起始 SOC、时长、采样点数量、时间顺序及数值范围。计算期间锁定参数和场景选择，防止重复提交或结果归属混乱。超时和关闭时处理本次 helper 的进程树。

失败时保持上一条成功曲线，显示可复制、可滚动的错误详情及日志路径，恢复可操作状态。

## 已完成的验证

端到端测试通过 Qt 按钮实际执行了三个本地 SDK 任务：

| 任务 | 参数 | 验证结果 |
|---|---|---|
| 恒流放电 | 初始 SOC 30%，1 A，截止 2.50 V | 初始电流 −1 A；60 秒 SOC 与安时积分一致；放电末段电量低于 1%；完成后自动回放 |
| 恒流充电 | 初始 SOC 70%，2 A，截止 3.45 V | 初始电流 +2 A；60 秒 SOC 包含 0.995 充电电荷效率；末段电量高于 99% |
| 电压保护 | 初始 SOC 70%，2 A，截止改为 3.30 V | 起点已达到电压保护条件，全程实际电流为 0，SOC 保持 70%，证明电压设置参与模型计算 |

同一测试还检查了应用参数不启动任务、计算期间按钮锁定、暂停时间冻结、继续接续、停止保留结果、场景切换恢复、相同参数缓存复用，以及 helper 不存在时只报告一次失败并保留旧曲线。

[端到端测试日志](logs/ui_integration.txt) 显示 `3 passed, 0 failed`，其中包含初始化、完整功能流程和清理三个测试项。完整流程内部包含上述三次真实 SDK 计算及故障注入。已部署的实际界面程序另通过启动自检，退出码为 0。

截图：[初始界面](logs/ui_startup.png)、[真实充电结果回放](logs/sdk_charge_replay.png)。

## 构建和复跑

在工作区根目录的 PowerShell 中运行：

```powershell
.\tools\sdk_probe\build.cmd
.\app\ui\build.cmd
.\app\ui\deploy.ps1
.\app\ui\tests\build.cmd
.\app\ui\tests\run.ps1
```

部署脚本从指定 Qt 5.14.2 MSVC2017 x64 Kit 按二进制复制 DLL 和插件，并逐个核对 SHA256。helper 独立配置 SDK DLL 搜索路径，避免界面与 SDK 的 Qt 依赖混入同一进程。

可选环境变量：

| 变量 | 用途 |
|---|---|
| BATTERY_PROJECT_DIR | 工程根目录，包含 model 和 data |
| BATTERY_SIM_WORKSPACE | 英文仿真任务目录，默认是系统临时目录下的 BatteryConsole |
| BATTERY_HELPER_EXE | helper 可执行文件位置，默认是界面程序同级 helper/sdk_probe.exe |
| SYSPLORER_SDK_BIN | SDK bin/win_msvc2017x64 目录 |

完整运行日志位于每个任务目录的 `helper.log`，原始结果及参数清单位于其下 `solver` 目录。

## 模型加载非法字符问题的处理

现场曾在初始电量 65%、充电 2.3 A、截止电压 3.43 V 时出现 helper 退出码 11、SDK 1102 非法字符错误，失败位置为 `OpenFile`。失败任务副本、工程源文件和先前成功任务副本的 SHA256 相同，且均通过严格 UTF-8 检查；源文件没有损坏。相同失败副本独立重跑成功，实际 Windows 界面重跑也成功，因此尚不能将原始失败唯一归因于编码或某个环境变量。

helper 现增加确定的输入处理：读取原始字节并保留其指纹，严格检查编码，生成带 UTF-8 BOM 的独立 SDK 输入文件，通过 `QSaveFile` 提交后重新读取逐字节核对，再调用 `OpenFile`。文件位于本次任务的 `solver/sdk_input/BatterySim.mo`；原始模型不改动，缓存和参数清单继续使用原始源文件指纹。结果清单另记录 `sdkInputSha256` 和 `sdkInputEncoding`，stdout 明确使用 UTF-8，避免日志中的中文再次被错误解码。

补充验证包括实际 Windows 平台和默认临时目录，不局限于此前的离屏测试：

- 按上述现场参数，两个初始回归实例和最终构建实例均计算成功、退出码 0；[最终 Windows 界面记录](logs/gui_custom_probe_final.json)。
- 充电、放电、电压保护、缓存及失败恢复的完整 Qt 回归通过；[测试日志](logs/ui_integration.txt)。
- 截断 UTF-8 输入在调用 SDK 前被拒绝，退出码 11，并报告具体的编码错误；[编码保护日志](logs/invalid_encoding_guard.log)。

修正后的 helper 已直接更新到 `app/ui/runtime/helper/sdk_probe.exe`，现有窗口的下一次真实计算会使用新版本。诊断参数 `--verify-custom` 仅供回归，使用现场参数自动完成应用、仿真并输出 `gui_custom_probe.json`；正常启动不执行该诊断。

SDK 首次探测尚未生成的 MSR 时仍可能输出 HDF5 文件不存在诊断；退出时仍有许可计时器的非致命线程警告。最终任务按求解器错误码、helper 退出码和实际结果校验判断成功，不以这些诊断文字单独判失败。持续驻留、多任务并发和其他机器部署不在本轮验收范围。

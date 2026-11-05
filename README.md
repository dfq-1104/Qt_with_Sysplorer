# 基于 Qt 与 Sysplorer SDK 的电池充放电仿真交互系统

本项目使用 Qt 构建电池充放电操作界面，通过本机 UDP 与运行在 Sysplorer 内的 C++ 插件通信，由 Sysplorer SDK 驱动 Modelica 模型 `BatterySim.SingleRun` 完成真实仿真，并将状态与结果返回界面。

## 项目功能

- 设置充电／放电模式、电流、截止电压和初始 SOC，预览电池状态。
- 实时展示 SOC、电流、端电压曲线及任务进度。
- 支持真实仿真的暂停、继续、停止，以及 0.5、1、5、20、60 倍速度控制。
- 相同仿真条件命中当前进程的完整结果缓存时，直接回放；回放按钮控制播放器。
- 主动停止后保留部分结果及有效时间范围，部分结果不覆盖完整缓存，不能继续求解。
- 显示参数、通信、翻译及求解错误，生成任务日志和 CSV 结果。

“应用参数”只校验、保存输入并更新预览；点击“仿真”才启动任务或缓存回放。缓存随客户端进程结束而失效。倍速控制仿真时间相对现实时间的推进节奏，不改变电流等模型参数，也不保证求解器能达到指定速度。

## Qt 与 Sysplorer 如何通信

```text
Qt 界面（battery_console_ui.exe）
        ⇅ 本机 UDP / JSON（battery-udp/1）
Sysplorer 内的 BatteryUdpControl 插件
        ⇅ Sysplorer C++ SDK
模型加载 → 参数写入与回读 → 翻译 → 求解 → 结果导出
        ⇅
Modelica 模型 BatterySim.SingleRun
```

1. 插件监听 `127.0.0.1` 动态 UDP 端口，在 `%LOCALAPPDATA%\BatteryConsole\udp\<PID>.json` 发布宿主发现信息。
2. 客户端发现并校验目标宿主、会话和模型哈希；没有可用宿主时自动启动本机 Sysplorer。已有宿主忙碌时不会接管其他任务。
3. 客户端发送 `begin`，插件写入并回读 `runMode`、`runCurrent`、`runVoltage`、`socStart`，然后调用 SDK 翻译并运行模型。
4. 客户端通过 `poll` 查询确认状态和增量样本 `[time, SOC, I_act, V_term]`，绘制实时曲线；通过 `pause`、`resume`、`stop`、`speed` 控制本任务。
5. 任务结束后导出 `result.csv` 和 `result.json`，客户端校验结果后展示或缓存。

请求带有请求编号、会话、令牌、客户端和任务标识；超时会有限重试。按钮依据宿主确认状态更新，命令被接受不等于操作已完成。发现记录包含令牌，请勿提交或分发。该协议用于同机通信，不提供远程服务。

真实仿真使用 SDK 实时模式，减速因子为 `1 / speed`，例如 `20x` 对应 `0.05`。正式运行不依赖 Python 桥接或独立 SDK helper。

## 环境配置

当前脚本及已验证环境为 Windows x64：

| 组件 | 版本／要求 |
| --- | --- |
| Qt | 5.14.2，`msvc2017_64` 套件，包含 Core、Gui、Widgets、Network、Svg |
| C++ 工具链 | Visual Studio 2017 Professional，安装 C++ 桌面开发工具及 Windows SDK，使用 x64 工具链 |
| Sysplorer | MWORKS Sysplorer 2026a，安装并配置可用的仿真许可 |
| Sysplorer SDK | 2026a（vs2017），插件构建需要 SDK 头文件和导入库 |
| 脚本环境 | Windows PowerShell、Git |

默认路径如下：

```text
Qt:             C:\Qt\Qt5.14.2\5.14.2\msvc2017_64
Visual Studio:  C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional
Sysplorer:      C:\Program Files\MWORKS\Sysplorer 2026a
Sysplorer SDK:  C:\Program Files\MWORKS\Sysplorer SDK 2026a(vs2017)
```

若安装位置不同，构建前修改以下位置：

- `app/ui/build.cmd`、`tools/sysplorer_udp_plugin/build.cmd`：Visual Studio 与 Qt 路径。
- `app/ui/deploy.ps1`：Qt 路径。
- `tools/sysplorer_udp_plugin/plugin.pro`：`SDK` 路径。
- 插件部署时用 `-SysplorerHome` 指定安装目录；客户端用环境变量 `SYSPLORER_HOME` 指向同一目录。

宿主与 SDK 的已验证小版本存在差异，插件使用了与目标 DLL 导出相关的接口，不能保证其他版本直接兼容。更换版本或机器后应重新验证。

## 构建与运行

以下命令在 **PowerShell** 中执行。仓库不包含编译产物和 Qt 运行库，首次使用需要自行构建、部署。

### 1. 获取源码

```powershell
git clone https://github.com/dfq-1104/Qt_with_Sysplorer.git
Set-Location .\Qt_with_Sysplorer
```

后续命令均以仓库根目录为当前目录。

### 2. 构建并安装 Sysplorer 插件

```powershell
.\tools\sysplorer_udp_plugin\build.cmd
if ($LASTEXITCODE -ne 0) { throw '插件构建失败' }
```

关闭需要更新的 Sysplorer 实例，避免插件 DLL 被占用，然后执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\sysplorer_udp_plugin\deploy.ps1 -Elevate
if ($LASTEXITCODE -ne 0) { throw '插件部署失败' }
```

接受 Windows UAC 授权后，脚本将插件安装到：

```text
<Sysplorer 安装目录>\Bin64\addins\BatteryUdpControl\battery_udp_control.dll
```

部署脚本校验文件哈希，并备份被替换的插件版本。安装到 `Program Files` 需要管理员权限；`ExecutionPolicy Bypass` 本身不提供文件写入权限。

自定义安装路径示例：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\sysplorer_udp_plugin\deploy.ps1 -SysplorerHome 'D:\MWORKS\Sysplorer 2026a' -Elevate
$env:SYSPLORER_HOME = 'D:\MWORKS\Sysplorer 2026a'
```

### 3. 构建并部署 Qt 界面

```powershell
.\app\ui\build.cmd
if ($LASTEXITCODE -ne 0) { throw '界面构建失败' }
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\ui\deploy.ps1
if ($LASTEXITCODE -ne 0) { throw '界面部署失败' }
```

部署脚本将程序和所需 Qt 库复制到 `app/ui/runtime/`。

### 4. 启动并进行仿真

```powershell
.\app\ui\run_ui.bat
```

1. 选择充电或放电，设置电流、截止电压和初始 SOC。
2. 点击“应用参数”，检查预览和输入提示。
3. 点击“仿真”。首次运行需要等待宿主启动、模型加载和翻译。
4. 在电池页与结果曲线页查看变化，按需暂停、继续、停止或调整速度。
5. 停止后可查看部分结果；再次点击“仿真”将重新判断缓存，未命中则从设置的初始状态计算。

当前允许电流 `0–5 A`、截止电压 `2.5–3.45 V`、初始 SOC `0–100%`。界面按容量估算时长并限制为 `120–20000 s`，零电流执行真实 `120 s` 静置仿真。

模型通过 Qt 资源文件分别嵌入客户端和插件。修改 `model/BatterySim.mo` 后，需要重新构建并部署两端，重启加载旧插件的宿主，避免模型哈希不一致。

## 结果与常见问题

默认任务目录为 `%TEMP%\BatteryConsoleUDP\<任务编号>`，包含 `client.log`、`plugin.log`、`status.json`、`result.csv`、`result.json` 等文件。可使用环境变量 `BATTERY_SIM_WORKSPACE` 指定工作目录；如手动启动宿主，应让宿主和客户端继承一致的配置。

| 问题 | 检查方式 |
| --- | --- |
| 找不到 Sysplorer | 检查安装目录和客户端的 `SYSPLORER_HOME` |
| 未安装插件或无法发现宿主 | 确认插件部署成功，并重启目标 Sysplorer；检查当前用户的发现记录 |
| `MODEL_MISMATCH` | 从同一份模型重新构建并部署客户端和插件 |
| `HOST_BUSY` | 等待宿主现有任务结束，不接管其他仿真 |
| 插件部署失败 | 关闭占用 DLL 的宿主，检查 UAC 授权及部署脚本输出 |
| Qt 平台插件加载失败 | 重新部署，确认 `runtime/platforms/qwindows.dll` 和 Qt 依赖完整 |
| 翻译、求解或结果校验失败 | 查看界面错误和本次任务目录日志，检查模型、许可及 SDK 兼容性 |

## 目录与文档

| 路径 | 内容 |
| --- | --- |
| `app/ui/` | Qt 界面、构建部署脚本及按钮集成测试 |
| `model/BatterySim.mo` | 电池 Modelica 模型 |
| `tools/sysplorer_udp_plugin/` | 正式 UDP 宿主插件、部署脚本及验收脚本 |
| `tools/sdk_probe/`、`tools/udp_host_probe/` | 历史探针和诊断工具，正式启动流程不依赖它们 |
| `data/simulation/` | 示例仿真数据 |
| `docs/` | 通信协议、开发记录及交付文档 |
| `logs/` | 保留的验证记录、截图和文档检查工具；新生成日志不提交 |

- [UDP 任务协议](docs/UDP任务协议.md)
- [UDP 正式接入开发记录](docs/UDP正式接入开发记录.md)
- [插件构建与部署说明](tools/sysplorer_udp_plugin/README.md)
- [界面操作说明](app/ui/UDP运行说明.md)
- [宿主验证记录](docs/UDP插件宿主验证记录.md)

`docs/交付文档/` 中的历史文档部分描述早期 helper 架构；当前运行方式以正式 UDP 协议、开发记录和源码为准。

根目录 `.gitignore` 排除了构建目录、Qt 运行包、IDE 缓存、原始日志、临时文件及本机配置。保留源码、模型、示例数据和必要的验证材料。

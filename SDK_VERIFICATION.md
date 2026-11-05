# Sysplorer 2026a SDK 最小闭环验证

验证日期：2026-11-04。本轮未开发完整界面。

## 结论

已通过独立 C++ helper，在本机完成：加载电池模型 → 写入新参数并回读 → 重新编译 → 启动本地 64 位求解器 → 仿真 600 秒 → SDK 读取 SOC、电流、电压 → 导出 SOC CSV → 清理资源并以退出码 0 退出。

这是 SDK 自带本地引擎的真实求解，不要求打开 Sysplorer GUI，也没有经过 MCP 或本地替代积分器。解析积分仅用于独立核验计算结果。

## 已验证的输入和结果

| 组别 | 初始 SOC | 放电设定 | SDK 读回初始电流 | 600 秒 SOC | 全曲线最大 SOC 绝对误差 |
|---|---:|---:|---:|---:|---:|
| A | 0.3 | 1 A | -1 A | 0.233333334326744 | 1.6186e-8 |
| B | 0.7 | 2 A | -2 A | 0.566666662693024 | 3.2955e-8 |

两组分别在全新目录中编译，没有复用旧求解器或旧结果。每组读取到 502 个 SOC 采样点，求解器结束时间 600 秒、错误码 0，helper 退出码 0。模型为 `BatterySim.BatteryDemo`，容量 9000 C，放电库仑效率 1；核验公式为 `SOC(t)=SOC0-I*t/9000`。电流符号沿用模型的“充电为正”。

原模型已复制到当前工程 `model/BatterySim.mo`，不修改历史工程。正式验证使用该文件的英文路径副本。

## 故障定位与处理

1. **编译配置未完整初始化。** 未配置的独立进程选择了默认 32 位 GCC，其预期目录不存在。仅改 `MwSetting` 的类型、路径不足以完整配置内核。helper 显式构造 `MwCoreOption`，调用 `SetCompileInfo` 指定本机 VC，并调用 `SetCompilePlatform(PF_WIN64)`。本轮没有调用 `MwSetting::Save` 或修改安装目录配置。
2. **中文输出路径被 MSVC 响应文件错误解码。** `build.log` 出现 `C1083 Cannot open source file`，错误文件名中的中文已乱码。采用纯英文的模型和求解输出目录后，64 位 `MWSolver.dll` 编译成功。
3. **编译平台和运行平台要分别设置。** 运行阶段默认使用 `Simulator/Bin/MWSolver.exe`，加载 64 位模型失败，求解器记录 `Loading libraries failed`、退出码 102。创建仿真控制对象后调用 `SetTargetPlatform(PF_WIN64)`，确认启动 `Simulator/Bin64/MWSolver.exe`。
4. **沙箱影响本地进程通信。** 沙箱内出现 ZeroMQ socket 错误；完整验证在普通本机进程环境（沙箱外）完成。不能将沙箱内错误直接认定为 SDK 或许可不可用。
5. **退出时许可后台线程尚未停止。** 调试器记录 `0xC0000409`，子码 7（FAST_FAIL_FATAL_APP_EXIT）；Qt 致命日志明确为 `QThread: Destroyed while thread is still running`，退出栈指向 `mw_license_ui.dll`。计算、结果对象清理完成后，对当前 helper 的 `MwLicenseActivityCollector` 及运行中的激活队列线程调用 `quit`、`wait`，再销毁应用，恢复正常退出。最终版本使用普通 `return`，没有强杀自身、绕过许可或忽略非零退出码。

## 文件和复跑方式

- `tools/sdk_probe/main.cpp`：独立 helper，参数写入、求解、结果核验和资源清理。
- `tools/sdk_probe/sdk_probe.pro`、`build.cmd`：Qt 5.14.2 / MSVC2017 x64 构建。
- `tools/sdk_probe/verify.ps1`：自动跑 A/B 两组，每次用全新目录；任一 helper 退出码非零即判失败。
- `tools/sdk_probe/debug_worker.py`：仅供诊断自身进程退出异常的 Windows 调试脚本，正式验证不依赖它。
- `logs/full_20261104_4_a.log`、`logs/full_20261104_4_b.log`：最终成功日志。
- `logs/verified_results.json`：独立读取全部 CSV 点后的核验统计。

在当前工程的普通 PowerShell 中运行：

```powershell
.\tools\sdk_probe\build.cmd
.\tools\sdk_probe\verify.ps1
```

脚本为本机路径配置，自动把 SDK Release 加入当前进程 PATH，使用 offscreen Qt 平台，不显示窗口。运行目录位于：

`C:/Users/TR/.codex/visualizations/2026/11/03/01a1a14e-d533-7a73-8c71-2fa02f587ae3/sdk_probe/full_20261104_4/`

其下 `a/`、`b/` 分别保留生成代码、编译日志、`MWSolver.dll`、原始 `Result.msr` 和 `result.csv`。

## 适用范围和剩余事项

当前证实的是本机、本模型的两组恒流放电参数，并非所有模型、所有场景和其他机器均已验收。后续 Qt 可用 `QProcess` 启动同类单任务 helper，等待退出码 0 和核验成功后加载真实结果回放。持续驻留、多任务并发、充电/恒压工况和部署授权仍需在对应开发阶段分别验证。

首次创建结果对象时，SDK 会探测尚未存在的 MSR，输出 HDF5 文件不存在诊断；之后真实创建结果并读回成功。许可线程停止后仍有 `QObject::~QObject: Timers cannot be stopped from another thread` 的非致命警告；两次最终测试均正常退出，但 SDK 的线程/计时器生命周期警告仍建议向厂商核实。该警告没有被隐藏。

# Sysplorer 宿主内 UDP 控制插件

本插件使用目标 Sysplorer 工作台现有的模型管理器、仿真面板和控制器。Qt 通过本地 UDP 发送任务命令，不启动 SDK helper 或常驻 Python 桥接。

## 构建与部署

1. 在项目根目录运行 tools/sysplorer_udp_plugin/build.cmd。
2. 关闭需要更新的测试 Sysplorer 实例，避免 DLL 被占用。
3. 在 PowerShell 执行下列命令。非管理员终端会通过 -Elevate 申请 UAC：

    powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\TR\Documents\ChatGPT\qt界面开发\tools\sysplorer_udp_plugin\deploy.ps1" -Elevate

脚本只更新 Bin64/addins/BatteryUdpControl/battery_udp_control.dll，校验哈希并备份该文件旧版本。成功打印 SUCCESS；失败打印原因并返回非零退出码。ExecutionPolicy Bypass 仅对该 PowerShell 进程生效，不赋予文件权限。

## 验证与文档

- verify_stage1.py：真实充电暂停/继续/停止、完整放电、实际 60 倍速验收，会创建专用可见 Sysplorer 实例。
- verify_runtime_tuning.py：专用真实宿主验证电流、限流、充放电/静置/恒压模式、恒压目标和上下截止电压；检查输入回读、同一控制器/任务、SOC 连续性，并与原生结果交叉核验。测试结束会清理自己创建的宿主。
- artifacts/<时间>/verification.json：诊断测试结果。
- app/ui/tests/udp_integration_test.cpp：真实 Qt 按钮集成测试。
- Qt 界面第三个页签“运行中调参”可在 Running/Paused 状态提交六项运行期输入。修改过的完整轨迹不会作为静态参数缓存再次使用。
- docs/UDP任务协议.md：正式消息与生命周期。
- docs/UDP正式接入开发记录.md：阶段进展与证据。

Sysplorer 正常启动时，插件在当前用户 LocalAppData/BatteryConsole/udp/<PID>.json 注册动态端口。记录含令牌，不应分发。

### 无管理员权限的隔离验证

运行 `prepare_isolated_host.ps1`，在英文临时目录 `%TEMP%/BatteryConsoleRuntime` 创建真实宿主副本，复制本插件并核对哈希；依赖目录链接到原安装，不覆盖安装目录。
将 `SYSPLORER_HOME` 设置为该目录后运行 `verify_runtime_tuning.py` 或 Qt 测试。
宿主根目录必须为英文路径：本机模型编译脚本会错误解码含中文的宿主路径。

界面倍速与原生 SDK 减速因子互为倒数，例如 60x 对应 1/60。SDK 和目标宿主小版本不同，所用接口需要实测，不承诺其他安装版直接兼容。

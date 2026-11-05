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
- artifacts/<时间>/verification.json：诊断测试结果。
- app/ui/tests/udp_integration_test.cpp：真实 Qt 按钮集成测试。
- docs/UDP任务协议.md：正式消息与生命周期。
- docs/UDP正式接入开发记录.md：阶段进展与证据。

Sysplorer 正常启动时，插件在当前用户 LocalAppData/BatteryConsole/udp/<PID>.json 注册动态端口。记录含令牌，不应分发。

界面倍速与原生 SDK 减速因子互为倒数，例如 60x 对应 1/60。SDK 和目标宿主小版本不同，所用接口需要实测，不承诺其他安装版直接兼容。

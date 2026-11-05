# SDK 界面故障根因与实际按钮验证

日期：2026-11-04。本报告更新《SDK现场检错_20261104.md》中“触发原因尚未复现”的结论。

## 到底哪一步出错

失败在 **Qt 桌面进程读取外部 `model/BatterySim.mo`**，早于参数写入、模型编译和求解。

实际桌面进程加载了 `C:/Windows/System32/TyEnc64.dll`（TianYu Software，EncryptA）和 `intefsctrl64.dll`（Wuhan TianYu Soft，InteKEY）透明文件加密模块。终端工具读取文件时能得到 13196 字节 UTF-8 明文，实际桌面程序读取同一模型却得到 17292 字节带加密封装的二进制内容。多出 4096 字节；文件头按 GBK 解码为“苏州同元软控信息技术有限公司”。

这不是应该用 GBK 解码模型文本的意思：厂商头之后是二进制负载，不能靠切换文本编码或删除头部解决。没有尝试私有解密、关闭防护或调整安全策略。

旧链路把读到的二进制内容当作 UTF-8 模型复制给 helper。最初 SDK 报非法字符；加入严格编码校验后变为 helper 自己提前返回 11。BOM 处理不能把加密封装变成 Modelica 文本，所以前面的处理没有解决这次故障。

## 实际窗口复现证据

使用 computer-use 技能，在用户原窗口点击“仿真”，得到新任务：

`C:/Users/TR/AppData/Local/Temp/BatteryConsole/20261104_021015_ee263172-cb33-49e9-989b-9cefba346b94`

新日志与现场一致：17292 字节，SHA-256 `fd22d55a4aa1fccb48ec3325f7be9c9c4a333388d5c1c1dae76f1343742e1f63`，invalid=5769，null=1，helper 返回 11。

中间诊断版本仅将文件交接改为管道，再次点击实际窗口后得到：

`C:/Users/TR/AppData/Local/Temp/BatteryConsole/20261104_021336_2c9e84ec-7f4c-4b73-b24b-4517184e05a0`

`GUI_MODEL` 已记录 17292 字节、同一二进制哈希，helper 管道完整性校验通过但解码拒绝。这把错误进一步定位到 **GUI 源文件读取**，排除了“只有 helper 读取副本时内容才变化”的早期猜测。因此只改管道也不足以修复。

此前终端直接调用 helper、自动启动的离屏测试，以及少量 Windows 诊断成功，没有覆盖用户实际桌面进程的文件读取环境。不能拿那些成功代替实际按钮验收；这是先前验证的缺口。

## 最终实现

1. 将项目 `model/BatterySim.mo` 通过 `app/ui/models.qrc` 构建为应用资源 `:/models/BatterySim.mo`。正式界面和回归测试使用同一资源。模型仍是本项目的 `BatterySim.SingleRun`，未替换为本地近似计算。
2. “应用参数”继续仅校验和保存设置。“仿真”读取资源，经 `QProcess` 标准输入管道传给独立 helper；包含协议版本、Base64 模型字节和 SHA-256。
3. helper 对实际收到的模型字节检查大小、协议、SHA-256 和编码；命令行期望哈希与管道清单哈希均须匹配。
4. 使用 SDK 已公开的 `MwMoHandler::NewModel(const std::string& txt, MoKey owner_key)` 加载 UTF-8 模型文本，随后照常参数设置及回读、重新编译、启动本机求解器、读取结果并校验。该 API 的本地接口说明见 SDK `interface/mw_class_manager/mw_mo_handler.h`。
5. 界面校验本次参数、模型指纹及 CSV，再自动回放；失败保留之前成功结果。各场景独立保存错误。

该方案改变了模型来源：**运行时使用构建时打包的模型，修改外部 `.mo` 后必须重新构建并部署界面**。不会在运行中悄悄切回受进程读取环境影响的外部文件。更新后的缓存指纹取自实际使用的资源字节。

## 实际桌面按钮验收

使用发布目录 `app/ui/runtime/battery_console_ui.exe`，通过真实窗口的按钮、单选框和输入框操作；未以直接调用 helper 代替验收。

| 项目 | 充电 | 修改参数后放电 |
|---|---|---|
| 交互 | 应用参数 → 仿真 | 停止 → 电池页 → 修改方向、电流、SOC → 应用参数 → 仿真 |
| 初始电量 | 50% | 30% |
| 电流设置 | 2.5 A | 1 A |
| 截止电压 | 3.45 V | 2.5 V |
| 结果初始电流 | +2.5 A | −1 A |
| 结果最终 SOC | 1 | 0.00163228425662965 |
| 仿真时长 | 2040 s | 3030 s |
| 采样点 | 2044 | 3032 |
| SDK 编译 | 成功 | 成功 |
| 求解错误码 | 0 | 0 |

充电任务：`C:/Users/TR/AppData/Local/Temp/BatteryConsole/20261104_021743_54a4577a-5b99-4018-9770-e960d9f83134`。

放电任务：`C:/Users/TR/AppData/Local/Temp/BatteryConsole/20261104_022152_c6ca2583-d85f-4d53-8e1e-3819de1668de`。

两次模型均为 13196 字节，界面、helper、SDK 输入 SHA-256 均为 `4670d908273243caa2996f708ce1e2692b4012eac5c3107a4209eeea16f6feb9`；`MODEL load=1 method=NewModel(text)`，结果清单记录 `modelTransport=stdin`、`sdkLoadMethod=NewModel(text)`。充电计算完成后已现场观察到曲线页“自定义 · 回放中”，SOC、电压、电流曲线和电池同步更新。

放电同样现场观察到曲线页“自定义 · 回放中”，约 1069/3030 s 时电池显示 18%，SOC 下降、电流为负。两组结果摘要保存在 `logs/desktop_button_verification.json`，原始任务目录保留完整 helper 日志、求解器产物、CSV 和结果清单。

最终资源模型版本的 Qt 回归为 **3 passed、0 failed，115929 ms**，见 `logs/ui_integration.txt`。主流程中执行四次真实 SDK 计算，覆盖放电、充电、电压保护及 0 A/0% 边界，同时验证“应用参数”不启动计算、失败保留成功曲线、错误切换场景隐藏且返回时恢复。该回归是实际桌面按钮验收之外的补充，不替代上述两次现场验证。

修复后实际桌面进程仍加载 TyEnc64/intefsctrl64，说明成功不是通过关闭加密模块取得。

发布界面 SHA-256：`9368764791D6D11E4C8F001464E70376452769B65E727B55D53F9C3EA820C0C9`。

发布 helper SHA-256：`B6150DC3BE622479F01A3E1FF1D6BD53F344A52A6712FDE59348D07C8CAD78CC`。

有限用例不能证明所有参数组合均已穷举；本轮已经针对现场故障环境完成复现和修复前后对照。许可计时器清理警告仍单独保留，不作为这次模型读取故障的原因。

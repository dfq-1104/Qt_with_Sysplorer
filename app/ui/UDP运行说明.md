# 电池客户端 UDP 版运行说明

本机推荐入口为 `app/ui/run_local.bat`。当前工作树已准备配套 Qt 和私有 Sysplorer 副本，点击本机连接或仿真操作时使用该副本。只打开 Qt 不会启动 Sysplorer。

新运行包只部署界面和 Qt 依赖。旧 sdk_probe.exe 和 sysplorer_view.py 不再参与正式运行。

## 本次本机修复：直接使用工作树入口

2026-11-07 再次核对确认：旧入口运行的 Qt 已是新版，但 `dist/connection-fix` 中原配置仍指向安装目录，那里实际加载的 DLL 仍为 `6AABFDBE…`。之前在工作树内生成新包并没有替换安装目录 DLL，所以该入口仍被正确拒绝。

本次将实际本机目标配置为工作树内的配套宿主副本，保持能力与模型校验，不需要修改 `Program Files`。具体操作：

1. 自己关闭原来显示旧插件错误的 Qt 窗口。
2. 双击本工作树的 **`app\ui\run_local.bat`**。不要继续双击原 `dist\connection-fix` 中绑定安装目录的 EXE。
3. 新 Qt 打开后显示未连接，界面布局不变。点击“连接设置 → 保存并连接”，或直接点击加载、翻译、开始，才按需启动工作树宿主。
4. 首次宿主初始化可能较慢，默认等待 180 秒。连接成功后按原流程加载、翻译、开始。本机真实求解已经用此入口验证通过。

通用 `run_ui.bat` 在存在工作树运行配置时也使用新客户端，继续尊重原有连接模式；`run_local.bat` 明确选择本机。安装版 Sysplorer 可保留，新客户端按私有可执行文件身份筛选目标，不会接管安装版任务。

私有宿主位于 `.local-sysplorer/host-*`，客户端位于 `dist/local-worktree/*/BatteryConsole-Client-win64`。`.local-sysplorer/state.json` 记录配对信息，`Start-Client.ps1` 也可启动这份新客户端。这些本机目录属于生成产物，不纳入 Git；宿主使用现有安装的库、工具和许可，原 Sysplorer 安装仍需保留。

在其他尚未准备的工作树，先在根目录执行一次：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\deployment\Prepare-LocalWorktree.ps1 -SysplorerHome 'C:\Program Files\MWORKS\Sysplorer 2026a'
```

脚本复制私有程序和设置、同步配套 DLL，校验文件哈希，不启动 Qt 或 Sysplorer。重复执行会复用正确的副本；正在运行的程序不会被自动结束。已有完整副本的二进制若不符则报错，不自动覆盖。

由于原生模型编译器要求英文路径，启动器会从 T–Z 选择空闲盘符，将它映射到**当前工作树**，例如 `Z:`。所有任务与临时文件仍在工作树内；已占用盘符不会被覆盖。关闭 Qt 后仍可能保留宿主窗口，因此启动器不会自动删除盘符或关闭宿主。需要移除映射时，先自己关闭这份 Qt 和私有 Sysplorer，再执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\deployment\Remove-LocalWorktreeDrive.ps1
```

若相关程序还在运行，脚本会拒绝移除。下次通过本机入口启动时会重新建立映射。直接复制生成的 EXE 到其他路径无法代替准备步骤；移动工作树后需重新准备绑定。

## 操作

| 操作 | 行为 |
|---|---|
| 界面打开 | 只读取配置、显示未连接状态，不自动连接或启动 Sysplorer |
| 连接设置 → 保存并连接 | 本机模式按需发现或启动宿主；远程模式只连接指定地址，失败不转本机 |
| 保存参数 | 只校验并保存客户端设置；相同数值不使准备实例失效。翻译和开始均读取当前输入，不要求先保存 |
| 加载模型 | 本机未连接时先按需发现/启动，再加载并确认模型；不写参、不翻译、不求解 |
| 翻译模型 | 自动加载或复用模型，然后校验、写参、逐项回读、翻译；成功后等待用户开始 |
| 开始仿真 | 自动完成必要的加载和翻译，再请求启动；有效准备实例可复用。失败即停止后续步骤 |
| 暂停仿真 / 继续仿真 | 只控制本客户端的真实求解；请求待确认时禁用相关按钮 |
| 停止仿真 | 等待停止、导出和核验，保留真实部分结果及停止时间，不归零 |
| 回放所选结果 | 只播放结果下拉框中选定的有效结果，不调用宿主或创建新任务 |
| 暂停回放 / 继续回放 / 停止回放 | 只控制本地播放器，与仿真按钮、倍速、时间和状态独立 |
| 状态信息 / 分阶段日志 | 分别展开或收起，默认收起并保存选择；错误摘要仍直接显示，收起不停止记录日志 |
| 运行中调参 | 保留第三个页签及原有功能；以模型采样回读确认生效 |

## 使用原安装目录的可选流程

没有工作树运行配置时，`run_ui.bat` 才依次寻找 `dist/connection-fix`、`dist/remote` 和旧运行目录；显式 `-RuntimeDirectory` 可以选择原运行包。`run_button_details.bat`、`run_runtime_tuning.bat` 是历史入口。桌面副本不会随工作树构建自动更新。若选择继续绑定安装目录，则必须实际同步该目录的 DLL，流程如下。

本机宿主由当前客户端旁 `sysplorer.ini` 的 `[Host] Home` 指定，默认 `C:/Program Files/MWORKS/Sysplorer 2026a`；进程环境变量 `SYSPLORER_HOME` 可覆盖它。生命周期能力和模型不匹配时立即失败，不跳过该旧插件再启动新实例。本机修复后可以从连接、加载、翻译、开始四个入口显式重试。繁忙实例隔离仍保留，每次新的连接尝试重新扫描实例。

截图中 Qt 期望模型哈希为 `e03bd363…`、宿主实际为 `4670d908…`，并缺少 `modelLifecycle`。检查确认已运行 Qt 与上轮交付一致，但安装目录 DLL 仍为 `6AABFDBE…`；配套 2026a DLL 应为 `9C187D822403DFEDAAC606EC97CBB06346027E846FA6C8B86D1DE80271E85F9A`。这需要同步实际宿主插件，单独更新 Qt 不会同步安装目录 DLL。

1. 保存结果并手动退出 Qt 和目标安装目录下全部 Sysplorer。
2. 在新服务器包目录用管理员 PowerShell 执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Update-Plugin.ps1 -SysplorerHome 'C:\Program Files\MWORKS\Sysplorer 2026a'`。脚本备份和更新 DLL，保留 `remote.ini`、凭据、端口和防火墙；纯本机使用也不需要重新配置这些项目。
3. 启动一个 Sysplorer，再从新版 Qt 选择本机并“保存并连接”；也可以直接通过本机加载、翻译、开始按需启动。不要继续使用仍占有旧 DLL 的进程。
4. 连接成功后验证加载、翻译和开始。首次就绪等待默认 180 秒，可通过 `[Host] TimeoutMs=180000` 或 `BATTERY_HOST_TIMEOUT_MS` 调整。

新客户端包提供只读诊断 `Check-Local.ps1`。在客户端目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Check-Local.ps1 -SysplorerHome 'C:\Program Files\MWORKS\Sysplorer 2026a' -OutputFile .\local-diagnostic.json
```

它核对运行进程、实际加载插件路径、磁盘 DLL 哈希，并向目标 PID 对应的回环 UDP 端口发送一次 `hello`，记录能力和模型哈希。不会启动程序、加载模型或修改安装配置；报告不包含发现令牌或访问密钥。`INCOMPATIBLE` 表示插件能力或模型不匹配，`COMPATIBLE` 表示本次握手兼容；`NOT_READY`、`BUSY`、`TARGET_NOT_RUNNING` 需要分别等待初始化、释放目标任务或启动宿主。诊断和新版错误提示均明确目标路径，避免更新错安装目录。初次读取进程模块受限时可使用管理员终端。

## 模型文本实际在哪里

源文件为 `model/BatterySim.mo`，构建时分别通过 `app/ui/models.qrc` 和 `tools/sysplorer_udp_plugin/models.qrc` 编入 Qt EXE 与插件 DLL。

1. Qt 从自身资源 `:/models/BatterySim.mo` 读取模型字节，计算预期 SHA-256。
2. 插件从自身资源 `:/battery/BatterySim.mo` 读取模型字节，保存在插件的 `modelText` 中，并在 `hello` 返回其模型哈希。
3. Qt 检查能力和哈希一致后，发送 `load` / `translate`、模型哈希及仿真参数。**这些请求没有模型文本字段，不会发送整份 `.mo` 内容。**
4. 插件在 Sysplorer 进程内调用 `NewModel(modelText, ...)` 加载它自己内置的模型，再完成参数回读、翻译和求解。

所以模型并非只存在 Qt 再传给宿主；当前两端各自携带一份，哈希检查保证使用同一版本。修改源 `.mo` 后需重新构建并同步 Qt 和 DLL；单独替换磁盘源文件、只换 Qt，或在 Sysplorer 中手动打开其他模型，都不会更新旧 DLL 内置的文本。

运行期模式：0 静置、1 恒流充电、2 恒流放电、3 恒压/限流充电。恒压目标与截止电压分开。
运行中提交会短暂停算、批量暂存输入并继续，时间和 SOC 不重置；已暂停时提交保持暂停，点击继续后确认。
界面显示首次模型采样确认的仿真时刻。带动态修改的结果保留轨迹与 result.json 中的输入历史，不复用为静态参数缓存。


运行、暂停、停止确认及准备期间禁止回放。完整结果与停止结果分别保留；停止结果在下拉框中自动选中；回放始终使用当前所选结果，失败任务不替换有效结果。结果仅属于当前客户端进程，关闭后不自动恢复。零电流执行真实 120 秒静置仿真，其他时长按容量估算并限制在 120–20000 秒。

Qt 界面可独立显示、回放已有结果；新计算依赖 Sysplorer。新版界面与插件需配套部署；替换插件前须保存工作并退出已加载旧插件的 Sysplorer。旧插件会被新客户端明确拒绝；加载、翻译和启动不会混为同一个协议命令。
任务目录通常位于 Temp/BatteryConsoleUDP/<任务编号>，包含客户端、插件日志、状态及结果文件。错误显示在窗口上方。未发现插件时检查部署并重启宿主；模型不一致时重新构建两端资源；宿主忙碌时等待，不接管其他任务。Program Files 写入需管理员权限，仅更改脚本执行策略无效。

本轮本机专项验证见 `docs/本机连接验证记录.md`，此前连接功能记录见 `docs/远程连接验证记录.md`；正式本机协议见 `docs/UDP任务协议.md`。按钮细化与早期正式接入记录属于历史验收材料，历史 helper 文档不代表当前运行方式。

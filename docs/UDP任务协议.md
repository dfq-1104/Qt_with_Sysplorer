# 本地 UDP 电池仿真任务协议

协议：battery-udp/1，schema=1。当前实现见 tools/sysplorer_udp_plugin 和 app/ui/src/sysplorer_runner.*。目标为同机通信，不提供远程服务。

## 通道与发现

插件绑定 127.0.0.1 的动态 UDP 端口。发现记录位于 Windows 当前用户 LocalAppData/BatteryConsole/udp/<PID>.json，每 2 秒原子更新。记录包含 PID、可执行文件路径、协议、端口、会话编号、令牌、模型哈希及更新时间。

客户端确认 PID 存活且可执行文件为配置的目标 Sysplorer，连接后核对模型哈希。发现已有繁忙或暂停任务时跳过该实例，不接管其他任务；没有空闲宿主时启动一个本任务专用的 mworks.exe。新启动实例也被占用时才报错，避免无限启动。

令牌不进入普通客户端日志，也不对外分发发现记录。

## 请求

| 字段 | 类型 | 说明 |
|---|---|---|
| schema | integer | 固定 1 |
| id | string | 每次请求 UUID；超时重发使用同一个 ID 与相同报文 |
| session | string | 目标宿主本次会话 ID |
| token | string | 发现记录中的当前会话令牌 |
| client | string | 客户端进程内稳定标识 |
| taskId | string | 仿真任务 UUID；连接查询时可为空 |
| command | string | 命令名 |
| args | object | 命令参数 |

## 命令

| 命令 | 参数 | 作用 |
|---|---|---|
| hello | 无 | 查询宿主就绪、控制器状态、模型哈希及 splitLifecycle=true 能力 |
| begin | modelHash、mode、voltage、current、soc0、duration、speed | 接受新准备任务，异步加载、设参、翻译，完成后停在 Prepared |
| start | 无 | 从 Prepared 启动本任务求解 |
| discard | 无 | 放弃本客户端的 Prepared 状态，重新应用参数时使用 |
| poll | cursor | 获取任务状态及一批实时数据 |
| pause | 无 | 请求暂停本任务求解 |
| resume | 无 | 请求继续本任务求解 |
| stop | 无 | 请求停止本任务，导出部分结果 |
| speed | speed | 调整运行期速度 |
| tune | current、voltage、mode、vMin、vMax、iMax | 同一任务在线修改运行期输入；自动暂停、批量写入、回读、恢复 |

### 运行期输入

`tune` 必须一次提交上述六个数值字段，不接受 SOC、时间或任意变量名。
mode 为 0 静置、1 恒流充电、2 恒流放电、3 恒压/限流充电；current 为 0–5 A；
2.5 ≤ vMin < vMax ≤ 3.45 V；2.5 ≤ voltage ≤ vMax；iMax 为 0–5 A。
voltage 是恒压目标，vMin/vMax 是独立的上下截止电压。恒压模式的实际电流由模型计算并受 iMax 限制。

仅在 Running/Paused 接受。Running 请求先暂停求解器，再通过 MwSimData::SetInputReals 写入顶层 live 输入；
Paused 请求暂存输入并保持暂停，点击继续后在下一求解步确认。写入前后检查求解器时间与 SOC，不重新编译、不重启任务、不修改初值。
本机 SDK 的即时 GetReals 仍返回旧步值，不能用它判断修改失败。插件额外订阅六个模型输入，在后续实际模型采样与请求完全一致后才写入确认记录。
`ok` 仅表示命令已接受；以状态 tuneHistory 中出现新的 sequence 为模型采样回读完成依据；time 是暂停写入边界，confirmedAt 是首次确认采样时刻。
状态同时提供 tunePending、runtimeParams。tuneHistory 保存生效时间、写入前后 SOC 和输入回读值，最终也写入 result.json。
同一请求 ID 的重发不重复执行；任务归属检查与其他控制命令一致。

UI 使用独立“运行中调参”页；动态修改结果不能作为静态起始参数的缓存复用。
验证脚本为 tools/sysplorer_udp_plugin/verify_runtime_tuning.py，另有 Qt 真实按钮测试 runtimeTuningButtons。

mode 只允许 1 充电或 2 放电；voltage 2.5–3.45 V；current 0–5 A；soc0 0–1；duration 0.1–20000 秒；speed 0.5–60。界面时长按容量规则估算并限制为 120–20000 秒；零电流固定执行真实 120 秒静置任务。UI 倍速与 SDK 减速因子的映射为 slowdownFactor=1/speed，例如 60x 对应 1/60；倍速不是实时性能保证。

begin 只负责准备，不启动求解；客户端必须在 Prepared 后单独发送 start。客户端拒绝缺少 splitLifecycle 能力的旧插件，防止准备动作意外启动仿真。discard 保留已有结果，仅释放准备状态。当前不提供任意模型加载或任意代码执行命令。

## 响应与实时数据

响应包含 schema、id、session、taskId、command、ok；失败包含 code/message。任务响应的 status 包含 phase、任务与客户端标识、参数回读、模型哈希、宿主 PID、控制器/数据地址诊断值、持续时间、有效样本时间、任务目录、结果哈希与查看信息。

poll 返回 rows，每行固定为 [time, SOC, I_act, V_term]。cursor 为本批起点，next 为下批位置，total 为当前已经产生的实时样本数量；每包最多 8 行。客户端只接纳严格对应本次请求、会话和任务的响应，并按连续序号追加。网络序号与原生求解结果点号不同，最终 CSV 单独核验。

重复请求保留最多 512 条响应；同一 ID 配不同报文不执行。客户端超时重发相同请求，最多 6 次；不是无限重试。该范围尚不等于长期丢包、乱序、多客户端压力验收完成。

## 状态

Ready → Preparing → Translating → Prepared →（start）Starting → Running。Prepared 可通过 discard 返回 Ready。

运行期间可能进入 Pausing → Paused → Resuming → Running；停止经过 Stopping → Finalizing → Stopped。自然结束经过 Finalizing → Completed。错误进入 Failed。

按钮以宿主确认状态驱动；命令 ok 只表示该操作被接受，不能代替完成状态。

模型和控制器对象必须仍属于本任务。任务编号、客户端归属或宿主绑定对象不一致时拒绝控制。超过 15 秒未收到拥有者查询且任务在运行时，请求暂停以避免客户端失联后继续无人查看的计算。

## 结果交接

任务目录包含 plugin.log、status.json、result.csv、result.json，以及宿主编译/原生结果。客户端另写 client.log。

完整结果必须满足退出成功、实际结果末时间达到请求终点、初始 SOC 与参数一致、变量有限且满足电流方向/范围等要求，才产生 complete=true 清单。客户端再次核对任务、模型、参数、CSV 哈希、点数与数据范围。

主动停止生成 complete=false，限制有效范围至停止时刻，保留真实已产生的样本；不使用停止后控制器回填的配置终止时间冒充有效数据时间。部分结果不写入完整成功缓存，但通过客户端校验后成为最近有效结果，作为默认回放对象。失败任务不替换最近有效结果。

原生查看通过宿主绘图对象进行，不启动额外常驻 Python 服务；缓存回放不重新触发原生查看。

nativeResult 当前来自宿主 GetResultPath，表示配置的结果根目录，不是具体 .msr 文件路径。它属于诊断信息；客户端核验交接依赖任务目录中的 result.csv/result.json。原生绘图直接使用宿主结果对象。

## 错误与部署边界

常见代码：HOST_BUSY、HOST_STARTING、MODEL_MISMATCH、INVALID_PARAMS、INVALID_DURATION_OR_SPEED、TASK_OWNERSHIP_MISMATCH、HOST_TASK_CHANGED、INVALID_STATE、INVALID_SPEED、SPEED_REJECTED、PREPARE_FAILED、TRANSLATE_FAILED、TRANSLATE_REJECTED、START_FAILED、STREAM_FAILED、RESULT_FAILED。部分内部接口错误仍含诊断文本，需结合插件日志定位。

SDK 与安装版宿主小版本不同，当前使用的导出接口逐项验证；不能据此承诺所有 ABI 或其他安装版本兼容。跨机器部署、并发压力及长期运行需另外验收。

# TCP 远程任务协议

协议名称 `battery-tcp/1`。客户端为 `SysplorerRunner`，服务器为 Sysplorer 插件内的 `RemoteServer`；帧与认证实现集中于 `common/remote_link.*`。既有 `battery-udp/1` 继续仅用于本机，不接收远端报文。

## 帧与边界

每帧由 4 字节无符号大端长度和对应长度的 UTF-8 JSON 对象组成。长度不得为零或超过 1 MiB。接收器保留半包，循环拆分连包；输入缓冲上限约 2 MiB，超限关闭连接。输出积压超过约 2 MiB 也拒绝继续排队。服务最多允许 8 个同时连接。

## 认证

共享访问密钥至少 32 个字符，正式安装脚本使用随机 32 字节生成 64 个十六进制字符。握手限时约 10 秒：

1. 服务器发送 `type=challenge, protocol, nonce, session`。nonce 是本次连接随机挑战，session 是本次宿主 UUID。
2. 客户端发送 `type=authenticate, protocol, client, nonce, proof`。client 是客户端进程内稳定 UUID；此处 nonce 为客户端另行生成的随机挑战。
3. 服务器校验客户端证明，回复 `type=authenticated, proof`；客户端校验服务器证明。

证明使用 HMAC-SHA256，输入为以下 UTF-8 字段，以单个 LF 分隔，无尾换行：

```text
purpose
serverNonce
clientNonce
clientUUID
sessionUUID
```

purpose 分别为 `client`、`server`、`channel`。前两者用于双向证明。`channel` 的 HMAC 小写十六进制 ASCII 结果作为本连接帧签名密钥。证明比较使用固定长度的逐字节差异累积。

客户端不读取或发送本机 UDP 的 token。服务器在校验 TCP 连接身份后才接入内部命令执行器。

## 已认证消息

握手之后，消息被封装为：

```json
{"n":"1","payload":"<base64 JSON bytes>","mac":"<HMAC hex>"}
```

每个方向的 n 独立从 1 严格递增，接收端只接受下一个序号。mac 输入为：

```text
direction\nn\npayload
```

direction 为 `client` 或 `server`。重放、跳号、签名错误、无效 JSON 或超限消息会关闭连接。内容经过认证但没有加密，应运行在受信任局域网或 VPN。

payload 内沿用任务消息：`schema=1,id,session,client,taskId,command,args`。TCP 接口检查 client 与认证身份一致、session 与宿主会话一致、请求 ID 为 UUID。服务器响应匹配 `schema,id,session,taskId,command,ok`。

## 命令和去重

任务命令继续复用 [UDP 协议](UDP任务协议.md) 的 `hello/load/translate/start/poll/pause/resume/stop/speed/tune/discard` 及现有状态机。每个宿主只允许一个活动任务；操作检查 taskId 和 owner。实时样本继续使用连续游标，每批当前最多 8 行。

新增 TCP 专用 `download` 命令：

```json
{"file":"result.csv","offset":0}
```

- 仅允许该客户端拥有的当前 `Completed/Stopped` 任务。
- 文件名仅允许 `result.csv` 和 `result.json`，不接受路径。
- offset 必须是非负整数，且不超过文件大小；文件必须非空且不超过 128 MiB。
- 每次读取最多 48 KiB，响应字段为 `file,offset,size,data,eof,chunkHash`。
- data 为分块字节的 Base64；chunkHash 为该块 SHA-256 的小写十六进制摘要。

客户端先下载清单，再下载 CSV，使用 `QSaveFile` 提交到本地任务目录。分块检查文件名、连续偏移、大小一致性、编码及哈希，完整下载后继续复用原结果任务/模型/参数/CSV 哈希/点数/取值范围校验。

请求 ID 去重缓存在当前宿主内最多保存 512 条。相同 ID、相同规范化 JSON 返回原响应；相同 ID、不同请求返回 `REQUEST_ID_REUSED`，不执行。断线重试保留待确认请求的原始 ID 与内容。新连接使用新的挑战、帧序号和通道密钥。

## 客户端恢复和错误

请求超过 10 秒未确认时最多尝试 3 次，然后进入 `ConnectionLost`，不自动重新提交新任务。重连认证后先比较原宿主会话；发生变化则报告原任务无法恢复，不发送旧控制。会话未变则重发原待确认请求或按原任务及游标继续查询。

结果下载错误进入 `DownloadFailed`，不触发仿真重新开始。用户可重试下载，或结束跟踪。服务器原有失联约 15 秒请求暂停的逻辑保持不变。客户端进程退出后的接管、宿主重启后的恢复和历史任务服务接口不属于当前范围。

服务器监听状态、配置错误和最近认证错误写入不含密钥的状态文件。客户端记录命令与阶段，不记录认证帧、共享密钥或带密钥配置。

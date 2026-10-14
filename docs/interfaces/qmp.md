# QMP 接口

## 模块

`vm/qmp`（SKILL-03）

## 职责

通过 QEMU Monitor Protocol（QMP）控制 Windows 虚拟机：状态查询、关机、热插拔等。

## 公开接口

```cpp
class QmpClient {
public:
  Result<void> connect(const std::filesystem::path& sock);
  Result<json> execute(const std::string& command, const json& args = {});
  Result<void> system_powerdown();
  Result<bool> query_status();
};
```

## Socket 与协议

| 项目 | 值 |
| --- | --- |
| 路径 | `$RUNTIME/lsw/qmp.sock`（默认 `$XDG_RUNTIME_DIR/lsw/qmp.sock`） |
| 传输 | Unix Domain Socket，行分隔 JSON（每条消息一行，以 `\n` 结束） |
| 握手 | 连接后必须先执行 `qmp_capabilities` |
| 消息格式 | 请求 `{ "execute": "...", "arguments": { ... } }`；响应 `{ "return": ... }` 或 `{ "error": { "class": "...", "desc": "..." } }` |

示例：

```json
{ "execute": "qmp_capabilities" }
{ "execute": "query-status" }
{ "execute": "system_powerdown" }
```

## 输入

- `connect`：QMP socket 路径，由 `lswd` 在启动 QEMU 时创建。
- `execute`：QMP 命令名与可选参数对象。

## 输出

- `execute`：QMP 的 `return` 字段（JSON）。
- `query_status`：VM 是否处于 `running`。
- `system_powerdown`：成功发送电源事件。

## 错误码

| 错误 | 说明 |
| --- | --- |
| `ConnectFailed` | socket 不存在或连接被拒绝 |
| `HandshakeFailed` | `qmp_capabilities` 未成功 |
| `ProtocolError` | 响应不是合法 JSON 或缺少 `return`/`error` |
| `CommandFailed` | QMP 返回 `error` 对象 |

## 依赖

`nlohmann/json`、Unix Domain Socket、`Result<T>` 错误类型（`lsw_core`）。

## 调用关系

`lswd` → `QmpClient` → QEMU；关机流程见 `docs/diagrams/sequence-shutdown.puml`。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 建立接口文档骨架，定义 socket 路径与协议格式 |

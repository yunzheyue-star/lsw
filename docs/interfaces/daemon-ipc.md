# lswd IPC 接口

| 项目 | 值 |
| --- | --- |
| 传输 | Unix Domain Socket |
| 路径 | `$RUNTIME/lsw/lswd.sock`（默认 `$XDG_RUNTIME_DIR/lsw/lswd.sock`） |
| 格式 | JSON Lines（每个请求/响应占一行） |
| 模型 | 请求-响应，`id` 一一对应 |

> 实现状态：P5 已将 `start`/`shutdown` 接入 `DaemonService`（拉起 QEMU、等待 SSH banner、SSH/QMP 关机与资源清理）；P6 已把 `mount`/`unmount` 接入 CIFS，并在 `start`/`shutdown` 中启动/停止 host Samba 与挂载/卸载 CIFS；`Z:` 因按登录会话隔离，延迟到实际使用会话（`lsw shell`/验收）映射；`shell`/`exec` 返回 `NOT_IMPLEMENTED`，P7/P8 实现。

## 报文结构

请求：

```json
{ "id": "1", "cmd": "start", "args": {} }
```

成功响应：

```json
{ "id": "1", "ok": true, "data": {} }
```

失败响应：

```json
{ "id": "1", "ok": false, "error": { "code": "VM_NOT_RUNNING", "message": "虚拟机未启动" } }
```

## 命令

| `cmd` | `args` | 成功 `data` | 说明 |
| --- | --- | --- | --- |
| `start` | `{ "password": "..." }`（可选，一次性凭据，禁止日志） | `{ "pid": 1234, "vm_status": "running", "already_running": false, "mounts": ["/mnt/Windows/C", "/mnt/Windows/D"], "z_drive": "Z:" }` | 幂等；并发调用串行化；等待 SSH banner 失败返回 `SSH_FAILED`；挂载失败返回 `MOUNT_FAILED`/`SHARE_SETUP_FAILED` |
| `shutdown` | `{}` | `{ "stopped": true, "already_stopped": false }` | 优先 SSH 关机，失败走 QMP/信号；完成后 lswd 退出 |
| `shell` | `{ "cwd": "/home/u/p" }` | `{ "ssh_args": ["ssh", "-t", ...] }` | 由 CLI `execvp` 执行；cwd 不在 `$HOME` 下返回错误 |
| `exec` | `{ "argv": ["test.exe", "a"], "cwd": "/home/u/p" }` | `{ "stdout": "...", "stderr": "...", "exit_code": 0 }` | 退出码为 Windows 程序退出码 |
| `status` | `{}` | `{ "daemon": "running", "vm_status": "running", "mounts": [...] }` | 查询守护进程与 VM 状态 |
| `mount` | `{ "password": "..." }`（可选） | `{ "mounts": ["/mnt/Windows/C", "/mnt/Windows/D"] }` | 幂等；无凭据/helper 权限返回 `MOUNT_FAILED`/`PERMISSION_DENIED` |
| `unmount` | `{}` | `{ "mounts": [] }` | 幂等 |

## 错误码

| `error.code` | 含义 |
| --- | --- |
| `BAD_REQUEST` | JSON 非法或缺少 `id`/`cmd` |
| `UNKNOWN_COMMAND` | 未识别的 `cmd` |
| `NOT_IMPLEMENTED` | 命令已定义、功能待后续阶段实现（P2 的 `shell`/`exec`） |
| `VM_NOT_RUNNING` | VM 未启动，无法执行 `shell`/`exec` |
| `PATH_OUTSIDE_HOME` | `cwd` 不在 `$HOME` 下且无法映射 |
| `SSH_FAILED` | SSH 连接或执行失败 |
| `MOUNT_FAILED` | CIFS 挂载失败（凭据、helper、端口不通或残留） |
| `UNMOUNT_FAILED` | CIFS 卸载失败（占用或 helper 失败） |
| `SHARE_SETUP_FAILED` | 使用会话内 Windows `net use Z:` 失败 |
| `PERMISSION_DENIED` | helper 提权失败或 secret-tool/helper 不可执行 |
| `MISSING_DEPENDENCY` | helper / secret-tool / ssh 等依赖缺失 |
| `QMP_ERROR` | QMP 命令失败 |
| `INTERNAL` | 未归类的内部错误 |

## 生命周期与幂等

- `lswd` 启动后创建 `$RUNTIME/lsw/lswd.sock`，退出时清理 socket 与 PID 文件。
- `start`、`mount`、`unmount` 必须幂等，重复调用返回成功与当前状态。
- 同一时刻只允许一个 `lswd` 实例（`$RUNTIME/lsw/lswd.pid` 加锁）。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 补全报文结构、命令参数/返回、错误码与幂等约定 |
| 未发布 | P2 实现：JSON Lines 编解码、Client/Server、lswd 骨架与幂等状态机；新增 `NOT_IMPLEMENTED` |
| 未发布 | P5 实现：`DaemonService` 接管 start/shutdown，等待 SSH、资源清理与进程退出 |
| 未发布 | P6 实现：start 检查真实 SSH banner、启动 host Samba 并挂载 C$/D$；`Z:` 映射由使用会话执行；shutdown 卸载并停止 host Samba；mount/unmount 接入 CIFS 与 keyring 凭据 |

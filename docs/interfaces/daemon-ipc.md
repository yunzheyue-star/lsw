# lswd IPC 接口

| 项目 | 值 |
| --- | --- |
| 传输 | Unix Domain Socket |
| 路径 | `$RUNTIME/lsw/lswd.sock`（默认 `$XDG_RUNTIME_DIR/lsw/lswd.sock`） |
| 格式 | JSON Lines（每个请求/响应占一行） |
| 模型 | 请求-响应，`id` 一一对应 |

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
| `start` | `{}` | `{ "pid": 1234, "vm_status": "running", "mounts": [...] }` | 幂等；已运行直接返回当前状态 |
| `shutdown` | `{}` | `{ "stopped": true }` | SSH 关机失败时用 QMP 兜底 |
| `shell` | `{ "cwd": "/home/u/p" }` | `{ "ssh_args": ["ssh", "-t", ...] }` | 由 CLI `execvp` 执行；cwd 不在 `$HOME` 下返回错误 |
| `exec` | `{ "argv": ["test.exe", "a"], "cwd": "/home/u/p" }` | `{ "stdout": "...", "stderr": "...", "exit_code": 0 }` | 退出码为 Windows 程序退出码 |
| `status` | `{}` | `{ "daemon": "running", "vm_status": "running", "mounts": [...] }` | 查询守护进程与 VM 状态 |
| `mount` | `{}` | `{ "mounts": ["/mnt/Windows/C", "/mnt/Windows/D"] }` | 幂等 |
| `unmount` | `{}` | `{ "mounts": [] }` | 幂等 |

## 错误码

| `error.code` | 含义 |
| --- | --- |
| `BAD_REQUEST` | JSON 非法或缺少 `id`/`cmd` |
| `UNKNOWN_COMMAND` | 未识别的 `cmd` |
| `VM_NOT_RUNNING` | VM 未启动，无法执行 `shell`/`exec` |
| `PATH_OUTSIDE_HOME` | `cwd` 不在 `$HOME` 下且无法映射 |
| `SSH_FAILED` | SSH 连接或执行失败 |
| `MOUNT_FAILED` | CIFS 挂载或卸载失败 |
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

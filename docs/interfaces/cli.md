# CLI 接口

可执行文件：`lsw`。裸命令 `lsw` 等价于 `lsw shell`。

## 全局退出码

| 退出码 | 含义 |
| --- | --- |
| `0` | 成功 |
| `2` | 参数错误 |
| `3` | 依赖缺失（`/dev/kvm`、QEMU、OVMF、swtpm 等） |
| `5` | 路径映射失败（目录不在 `$HOME` 下等） |
| `6` | SSH 执行失败（守护进程未启动、端口 2222 不可用、认证失败） |
| `7` | 被调用程序返回非零 |
| `8` | QEMU 错误 |

## 命令

### `lsw --install`

引导用户完成 Windows 安装。**不提供、不下载 Windows ISO**。

参数：

| 参数 | 说明 |
| --- | --- |
| 无参数 | 交互式询问 ISO 路径 |
| `--iso <path>` | 用户自备的合法 Windows ISO |
| `--virtio-iso <path>` | virtio-win ISO |
| `--disk <path>` | qcow2 路径，默认 `~/.local/share/lsw/vm/disk.qcow2` |

行为：检查 `/dev/kvm`、QEMU、OVMF、swtpm → 创建磁盘与 `$DATA/vm/OVMF_VARS.fd` → 启动 QEMU 安装模式（GUI）→ 安装完成后提示启用 OpenSSH Server、QEMU Guest Agent、SMB 并映射 `Z:`。

退出码：`0`、`2`、`3`、`8`。

### `lsw start`

启动虚拟机服务：拉起 `lswd` → QMP 启动 VM → 等待 `127.0.0.1:2222` 可用 → 挂载 `C$`/`D$` → 确保 `Z:` 映射。

幂等：已启动时直接返回成功。

### `lsw` / `lsw shell`

直接在当前终端进入 Windows PowerShell，不新开窗口。

| 当前 Linux 目录 | 行为 |
| --- | --- |
| `$HOME` | 进入 PowerShell 默认路径 |
| `$HOME` 之下（如 `~/Project/demo`） | 映射为 `Z:\Project\demo` 并 `Set-Location` |
| 不在 `$HOME` 下 | 报错并提示进入 `$HOME` 下的目录，或使用临时复制策略 |

执行方式：`execvp` 替换当前进程，运行 `ssh -t -p 2222 "$WINDOWS_USER"@127.0.0.1 -- powershell.exe -NoLogo -NoExit`。`$WINDOWS_USER` 取 `config.user.windows_user`，默认 `lsw`（`PROMPT.md` 中的 `user` 即此字段）。

退出码：`0`、`5`、`6`。

### `lsw test.exe [args...]`

在 Windows 环境中运行指定 exe，暂不支持 GUI 程序（需要 GUI 时明确提示用户）。

1. 相对路径解析为绝对路径。
2. 位于 `$HOME` 下 → 映射为 `Z:\...`。
3. 不在 `$HOME` 下 → 复制到 `$HOME/.lsw/tmp/<hash>/` 后再映射。
4. 经 SSH 执行 `& 'Z:\path\test.exe' args; exit $LASTEXITCODE`。
5. 透传 stdout、stderr 与退出码。

退出码：`0` 成功、`5` 路径映射失败、`6` SSH 执行失败、`7` 程序返回非零。

### `lsw shutdown`

优先通过 SSH 执行 Windows 关机；失败时通过 QMP 发送 `system_powerdown`；等待 QEMU 退出；卸载 `/mnt/Windows/C`、`/mnt/Windows/D`；停止 `lswd`。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 补全全局退出码、`lsw shell` 目录映射规则与 SSH 用户名说明 |

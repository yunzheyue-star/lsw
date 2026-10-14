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
| `--firmware <bios\|ovmf>` | 安装固件，默认 `bios`（SeaBIOS）；`ovmf` 为 UEFI。Windows 10 建议 BIOS |
| `--allow-existing-disk` | 允许安装模式挂载 LSW 数据目录之外已存在的磁盘；**默认拒绝**，防止覆盖 libvirt 等其他虚拟机的磁盘 |

> 不支持位置参数：ISO / 磁盘路径必须用 `--iso`、`--virtio-iso`、`--disk` 指定；只运行 `lsw --install`（不带任何路径）时，会在交互终端中提示输入。

行为（P4 已实现）：

1. 依赖检查：`/dev/kvm`、`qemu-system-x86_64`、`qemu-img`、`OVMF_CODE.fd`、`OVMF_VARS.fd` 模板、可选 `swtpm`；缺失时按发行版列出修复建议（Arch：`sudo pacman -S edk2-ovmf`；Debian/Ubuntu：`sudo apt install ovmf`；Fedora：`sudo dnf install edk2-ovmf`）。
2. ISO 输入：优先使用 `--iso` / `--virtio-iso`；未提供且为交互终端时提示输入；非 TTY 环境必须显式传参。只做存在性、可读性、`.iso` 扩展名校验，**不下载、不内置**。
3. 存储准备：`qemu-img create -f qcow2` 创建 `$DATA/vm/disk.qcow2`；复制 OVMF VARS 模板到 `$DATA/vm/OVMF_VARS.fd`；重复执行不覆盖已有文件。
4. 启动安装模式：`QemuVm::start_install()`（`-display gtk` + Windows ISO + virtio-win ISO + OVMF pflash），等待用户完成安装并关闭 QEMU。未安装 Samba（`smbd`）时跳过 `smb=$HOME` 并警告，安装仍可继续，但 Windows 内 `Z:` 共享将在安装 Samba 后才可用。
5. 安装后输出 `scripts/post-install.ps1` 指引：检查 OpenSSH Server、QEMU Guest Agent、SMB、`Z:` 映射。

退出码：`0` 成功、`2` 参数/ISO 错误、`3` 依赖缺失、`8` QEMU 异常退出。

### `lsw start`

启动流程（P5 已实现）：`lswd` 未运行时自动拉起 → 通过 IPC 调用 `start` → `lswd` 内 `QemuVm` 以运行模式启动 QEMU（`-display none`）→ 等待 `127.0.0.1:2222` 就绪（默认 120s）→ 返回 `{pid, vm_status:"running"}`。

- 幂等：已在运行且端口就绪时直接成功；并发 `start` 由互斥锁串行化。
- 退出码：`0`、`6`（等待 SSH 超时）、`8`（QEMU 错误）。
- 启动期间会输出等待提示；默认 120s 内必须读到 Windows SSH banner（`LSW_SSH_READY_TIMEOUT_MS` 可调），仅 hostfwd 端口被监听不算就绪，未装/未启动 sshd 会超时返回 6。
- 说明：CIFS 挂载与 `Z:` 映射属于 P6，SSH 交互式 shell 属于 P7。

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

关闭流程（P5 已实现）：优先 `ssh ... shutdown /s /t 0`；失败时 `QemuVm::shutdown()` 走 QMP `system_powerdown` → SIGTERM → SIGKILL；清理 `qmp.sock`/`qga.sock` 并停止 `lswd`。

- 幂等：`lswd` 未运行或 QEMU 已停止时返回成功。
- 默认给 guest 15 秒优雅关机窗口（`LSW_SHUTDOWN_TIMEOUT_MS` 可调），超时后 SIGTERM → SIGKILL。
- 挂载点卸载在 P6 接入 CIFS 后生效。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 补全全局退出码、`lsw shell` 目录映射规则与 SSH 用户名说明 |
| 未发布 | P5 实现 `lsw start` / `lsw shutdown` 编排：守护进程拉起、等待 SSH、SSH/QMP 关机与资源清理 |

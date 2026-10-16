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
| `8` | QEMU / 文件系统错误（QEMU、CIFS 挂载、`Z:` 映射） |

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
4. 启动安装模式：`QemuVm::start_install()`（`-display gtk` + Windows ISO + virtio-win ISO + OVMF pflash），等待用户完成安装并关闭 QEMU。安装模式只负责装系统，`Z:` 共享在运行模式由宿主 root Samba（`127.0.0.1:445`）提供。
5. 安装后输出 `scripts/post-install.ps1` 指引：检查 OpenSSH Server、QEMU Guest Agent、SMB、`Z:` 映射。

退出码：`0` 成功、`2` 参数/ISO 错误、`3` 依赖缺失、`8` QEMU 异常退出。

### `lsw start`

启动流程（P5/P6 已实现）：`lswd` 未运行时自动拉起 → 通过 IPC 调用 `start` → `QemuVm` 以运行模式启动（`-display none`）→ 等待真实 SSH banner（默认 120s）→ 启动 host Samba（`127.0.0.1:445`，guest 经 `10.0.2.2` 访问）→ 挂载 `//127.0.0.1/C$`、`//127.0.0.1/D$` 到 `/mnt/Windows/{C,D}` → 返回 `{pid, vm_status:"running", mounts:[...], z_drive:"Z:"}`。`Z:` 映射按登录会话隔离，由真正使用它的 SSH 会话（`lsw shell`/P6 验收）在会话内执行。

- 幂等：已在运行且挂载就绪时直接成功；并发 `start` 由互斥锁串行化。
- 凭据：优先 keyring（`secret-tool`）；TTY 中隐藏回显提示输入密码，密码仅经 IPC 内存传递；`--save-password` 可写入 keyring；自动化可用 `LSW_WINDOWS_PASSWORD`（仅本次环境）。
- 退出码：`0`、`3`（helper/secret-tool/ssh 缺失）、`6`（等待 SSH 超时）、`8`（QEMU 或 CIFS/`Z:` 失败）。
- 启动期间会输出等待提示；默认 120s 内必须读到 Windows SSH banner（`LSW_SSH_READY_TIMEOUT_MS` 可调），仅 hostfwd 端口被监听不算就绪，未装/未启动 sshd 会超时返回 6。
- `LSW_DISABLE_MOUNTS=1` / `LSW_DISABLE_Z_MAPPING=1` 可分别跳过文件系统步骤（测试/无权限环境）。

### `lsw mount` / `lsw unmount`

- `lsw mount`：确保 Windows `C$`/`D$` 挂载到 `filesystem.mount_root`（默认 `/mnt/Windows`），幂等；需要绑定的凭据与 helper 权限，失败返回 `8`。
- `lsw unmount`：卸载 `/mnt/Windows` 下全部 CIFS 挂载点，幂等。
- 退出码：`0`、`3`、`8`。

### `lsw` / `lsw shell`

直接在当前终端进入 Windows PowerShell，不新开窗口。

| 当前 Linux 目录 | 行为 |
| --- | --- |
| `$HOME` | 进入 PowerShell 默认路径 |
| `$HOME` 之下（如 `~/Project/demo`） | 映射为 `Z:\Project\demo` 并 `Set-Location` |
| 不在 `$HOME` 下 | 返回 `PATH_OUTSIDE_HOME`（退出码 5），提示进入 `$HOME` 下的目录或使用 P8 临时复制策略 |

流程（P7 已实现）：

1. CLI 读取当前 Linux 工作目录，通过 IPC `shell { cwd }` 询问 `lswd`；
2. `lswd` 检查 VM 正在运行（否则 `VM_NOT_RUNNING`，退出码 6），把 `cwd` 映射为 `Z:`；`$HOME` 本身返回默认路径；
3. `lswd` 返回 `ssh_args`：`ssh -t -p 2222 -o StrictHostKeyChecking=accept-new "$WINDOWS_USER"@127.0.0.1 -- powershell.exe -NoLogo -NoExit -EncodedCommand <base64>`；编码脚本在同一个 SSH/PowerShell 会话内先 `net use Z: \\10.0.2.2\qemu`，再按需 `Set-Location`；
4. CLI 用 `execvp` 替换当前进程执行 `ssh`，退出后回到原 shell，退出码透传；`PATH` 与 argv 均不经过 shell 拼接；
5. `execvp` 前 CLI 会向本地 TTY 发送 `ESC[2J/3J/H`，清掉启动命令行与滚动回退，让 PowerShell 从干净屏幕开始；
6. 远端 shell 会把 PowerShell `RawUI.BufferSize` 收敛到窗口尺寸，并覆盖 `Clear-Host` 发送 `ESC[2J/3J/H`，使 `clear`/`cls` 同时清可视区与滚动回退。

> `$WINDOWS_USER` 取 `config.user.windows_user`，默认 `lsw`。

退出码：`0`、`5`（路径越界）、`6`（VM 未运行、SSH/认证失败）。

### `lsw <program> [args...]`

在 Windows 环境中运行指定程序；支持 `.exe`、`.bat`、`.cmd`（.bat 为一等公民，适合没有 exe 时的脚本验收）。GUI 程序暂不支持，会明确提示。

流程（P8 已实现，daemon 侧完成路径处理）：

1. CLI 把 `argv`（程序 + 参数）与当前 `cwd` 通过 IPC `exec` 交给 `lswd`；
2. 相对路径按 `cwd` 绝对化；位于 `$HOME` 下 → 映射为 `Z:\...`；
3. 不在 `$HOME` 下 → 复制到 `$HOME/.lsw/tmp/<hash>/<文件名>` 后再映射（超过 7 天的旧子目录会在下次执行时清理）；
4. 若为 `.exe` 且 PE Subsystem 为 GUI（2），返回 `PROGRAM_FAILED`，提示暂不支持 GUI；
5. daemon 经非交互 SSH 执行 `powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand <base64>`；远端脚本先把 `Z:` 上的程序复制到 Windows 本地 `%TEMP%\lsw-exec-<guid>\` 并 `Unblock-File`，再从本地路径执行，结束后删除临时目录（避免 SMB 租约/文件锁或 Defender 扫描导致 `.bat` 卡住）；
6. 透传 stdout、stderr；程序退出码原样透传（0–255，超出按低 8 位）。

退出码：`0` 成功、`5` 路径映射/文件不存在、`6` VM 未运行/SSH 失败/执行超时、`7` GUI 程序（`PROGRAM_FAILED`）或未归类执行错误。

示例：

```bash
lsw test.exe arg1 arg2
lsw hello.bat
lsw build.cmd --release
```

### `lsw shutdown`

关闭流程（P5 已实现）：优先 `ssh ... shutdown /s /t 0`；失败时 `QemuVm::shutdown()` 走 QMP `system_powerdown` → SIGTERM → SIGKILL；清理 `qmp.sock`/`qga.sock` 并停止 `lswd`。

- 幂等：`lswd` 未运行或 QEMU 已停止时返回成功。
- 默认给 guest 15 秒优雅关机窗口（`LSW_SHUTDOWN_TIMEOUT_MS` 可调），超时后 SIGTERM → SIGKILL。
- P6 起先卸载 `/mnt/Windows/{C,D}`，再关闭 Windows；卸载失败只记录 warning，不阻塞关机。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 补全全局退出码、`lsw shell` 目录映射规则与 SSH 用户名说明 |
| 未发布 | P5 实现 `lsw start` / `lsw shutdown` 编排：守护进程拉起、等待 SSH、SSH/QMP 关机与资源清理 |
| 未发布 | P6 实现 `lsw mount` / `lsw unmount` 与 start/shutdown 文件系统集成：keyring 凭据、CIFS 挂载/卸载/重挂、Windows `Z:` 映射 |

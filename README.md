# LSW · Linux Subsystem for Windows

> **专注反向输出，给 Linux 喂 Windows 的 💩**
>
> *"既然微软能给 Linux 喂巧克力（WSL），那我们凭什么不能给 Linux 喂点硬核的💩？"*

![C++26](https://img.shields.io/badge/C%2B%2B-26-blue)
![CMake](https://img.shields.io/badge/CMake-%E2%89%A5%203.25-blue)
![Platform](https://img.shields.io/badge/platform-Linux%20%2B%20KVM-lightgrey)
![Status](https://img.shields.io/badge/status-1.0.0-brightgreen)
![License](https://img.shields.io/badge/license-MIT-green)
![Tests](https://img.shields.io/badge/tests-131%2F131%20passed-brightgreen)

---

## 🤔 这到底是什么玩意儿？

欢迎来到 **LSW (Linux Subsystem for Windows)**。
这不是微软的恩赐，这是 Linux 原住民的绝地反击。

WSL 是 Windows 试图在内部消解 Linux；而 LSW，是 Linux 对 Windows 的降维包容。我们用极其严谨的 C++26 标准、非阻塞 Socket、QMP 协议交互，并跑通了 131 个单元/集成测试，只为了完成一个极其抽象的终极目标：**在 Linux 上拉起 QEMU/KVM，跑一个 Windows 虚拟机，并强行管它叫“子系统”。**

你问我这东西有什么用？没关系，问就是**俄罗斯套娃**：
> 你在 LSW 里跑 Windows，Windows 里跑 WSL，套到最后你得到了一个 Linux。
> 恭喜你，你完成了计算机科学史上的《盗梦空间》，CPU 直呼内行，内存条高呼救命。

---

## 💩 免责声明（必读）

*   **关于性能**：我们保证了 IPC 通信的绝对优雅和并发状态的一致性（通过 131 个测试），但 Windows 虚拟机卡不卡？我不到啊。毕竟我只负责喂💩，不负责消化。
*   **关于生态**：目前是 1.0.0 初版，没有 WSLg 那种高级图形功能。想用图形界面？建议自己看着 Looking Glass 的 GPLv2 协议发呆。
*   **关于协议**：本项目采用 MIT 协议，主打一个自由。如果自由软件基金会（FSF）觉得我们和 GPL 代码隔着 Unix Socket 说话也算链接，那我们就多开几个 Socket，套死他们。

> **⚠️ 警告**：本项目包含大量乐子人特有的脑干缺失设计。如果你在体验过程中感到智商被侮辱，或者遭遇系统崩溃、蓝屏甚至宇宙重启，纯属正常现象。
> **“用最顶级的系统编程，写最抽象的代码。”**

---

> **在 Linux 终端里运行 Windows。**
> LSW 用 KVM/QEMU 启动 Windows，用 OpenSSH 把 PowerShell 接进 Linux 终端，用 Samba/CIFS 打通文件系统，提供接近 WSL2 的使用体验。

`lsw` 是命令行入口：初始化依赖、安装 Windows、启动虚拟机、进入 PowerShell、执行 Windows 程序、查看状态、卸载与重装。项目自带完整的接口文档、PlantUML 时序图和分阶段验收脚本，适合直接使用，也适合作为 C++26 / CMake 工程参考。

---

## 目录

- [这到底是什么玩意儿？](#这到底是什么玩意儿)
- [免责声明（必读）](#免责声明必读)
- [特性](#特性)
- [系统要求](#系统要求)
- [普通用户：快速开始](#普通用户快速开始)
- [Windows 内首次配置](#windows-内首次配置)
- [命令详解](#命令详解)
- [配置参考](#配置参考)
- [数据与状态路径](#数据与状态路径)
- [常见问题](#常见问题)
- [开发者：从源码构建](#开发者从源码构建)
- [项目结构](#项目结构)
- [贡献指南](#贡献指南)
- [架构与文档](#架构与文档)
- [路线图](#路线图)
- [作者与许可](#作者与许可)

---

## 特性

| 能力 | 说明 |
| --- | --- |
| `lsw init` | 识别发行版/包管理器，检查并安装缺失依赖（KVM、QEMU、OVMF、Samba、cifs-utils、OpenSSH、swtpm、libsecret），初始化配置与 helper |
| `lsw --install` / `lsw install` | 用自备 Windows ISO + virtio-win ISO 启动 GUI 安装模式 |
| `lsw start` | 启动 Windows VM，等待 SSH 就绪，启动 host Samba，挂载 Windows `C$`/`D$` |
| `lsw` / `lsw shell` | 直接进入 Windows PowerShell；按 Linux cwd 自动 `Set-Location Z:\...`；`execvp` 接管终端 |
| `lsw <program>` | 在 Windows 中执行 `.exe` / `.bat` / `.cmd`；透传 stdout/stderr/退出码；GUI 程序明确拒绝 |
| `lsw status [--json]` | 查看 daemon/VM 是否运行、磁盘是否已安装、Windows 主机名、virtio-win 驱动状态 |
| `lsw mount` / `lsw unmount` | 手动挂载/卸载 `/mnt/Windows/C`、`/mnt/Windows/D` |
| `lsw shutdown` | 优先 SSH 关机，QMP/信号兜底，清理挂载与守护进程 |
| `lsw uninstall` / `lsw --uninstall` | 默认只删除 VM，保留配置与依赖方便重装；可选 `--purge` 彻底清理 |
| 无明文密码 | Windows 密码优先存入系统 keyring；helper 仅通过 stdin 接收一次性密码 |
| 无 Windows 组件下载 | 不下载、不内置 Windows ISO；由用户提供合法授权与镜像 |
| 标准 C/C++ 打包 | `cmake --build --target package_source` 生成 `.tar.xz`；`make install` / `make uninstall` 干净安装卸载；GitHub Actions 自动发布 |

---

## 系统要求

### 宿主机

- x86_64 Linux，内核支持 KVM，当前用户可读写 `/dev/kvm`
- 支持的系统家族：Arch Linux、Debian/Ubuntu、Fedora/RHEL/CentOS（`lsw init` 自动识别）
- 运行时依赖（`lsw init` 可自动安装）：
  - `qemu-system-x86_64`、`qemu-img`
  - `edk2-ovmf` / `ovmf`（UEFI 可选；Windows 10 默认 BIOS 不需要）
  - `samba`（提供 `smbd`，host Samba）
  - `cifs-utils`（提供 `mount.cifs`）
  - `openssh` 客户端（提供 `ssh`）
  - 可选：`swtpm`（Windows 11 TPM）、`libsecret`（提供 `secret-tool`）
- 需要 `sudo` 或 `pkexec` 来安装系统包、启动 Samba 与安装 helper

### guest（Windows）

- Windows 10 x64（推荐）；Windows 11 需要 `--firmware ovmf` 与 `swtpm`
- 自备 Windows ISO 与合法授权
- 建议同时准备 [virtio-win ISO](https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso)（提供磁盘/网卡驱动与 QEMU Guest Agent）
- Windows 内需启用：
  - OpenSSH Server（`sshd`）
  - QEMU Guest Agent（QGA）
  - SMB / 管理共享 `C$`、`D$`
  - **Windows 账户名建议直接设为 `lsw`**（与 `config.user.windows_user` 默认值一致）；如果使用其他用户名，必须创建同名用户或修改配置，否则 SSH/CIFS 都会连不上

> LSW 不会下载 Windows ISO，也不会绕过授权；请确认你拥有合法的 Windows 许可证。

---

## 普通用户：快速开始

以下命令默认使用 Bash；Fish 用户把 `$(nproc)` 换成 `(nproc)` 即可。

> 如果尚未把二进制安装到 PATH，请把下文中的 `lsw` / `lswd` 替换为 `./build/lsw` / `./build/lswd`。

### 1. 获取 LSW

#### 方式 A：从源码构建（当前主要方式）

先安装编译环境（任选对应发行版的包名）：

```bash
# Arch Linux
sudo pacman -S --needed base-devel cmake cli11 nlohmann-json spdlog fmt catch2 tomlplusplus

# Debian / Ubuntu
sudo apt install build-essential cmake libcli11-dev nlohmann-json3-dev libspdlog-dev libfmt-dev catch2 libtomlplusplus-dev

# Fedora / RHEL
sudo dnf install gcc-c++ cmake cli11-devel json-devel spdlog-devel fmt-devel catch2-devel toml++-devel
```

构建：

```bash
git clone <your-repo-url> lsw
cd lsw

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 可选：跑测试
ctest --test-dir build --output-on-failure
```

> 离线环境且系统包齐全时，可加 `-DLSW_FETCH_DEPS=OFF`，禁止 CMake 联网下载依赖。

#### 方式 B：构建后安装到系统（`make install` / `make uninstall`）

CMake 已提供 `install` / `uninstall` 目标：

```bash
cmake -S . -B build -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build build -j"$(nproc)"

sudo make -C build install      # 安装到 /usr/local
sudo make -C build uninstall    # 干净卸载
```

默认安装内容：

| 内容 | 路径 |
| --- | --- |
| CLI | `/usr/local/bin/lsw` |
| daemon | `/usr/local/bin/lswd` |
| mount helper | `/usr/local/lib/lsw/lsw-mount-helper` |
| Windows 检查脚本 | `/usr/local/share/lsw/post-install.ps1` |
| 文档 | `/usr/local/share/doc/lsw/` |

> 想安装到用户目录：`-DCMAKE_INSTALL_PREFIX="$HOME/.local"`。
> 使用 Ninja 生成器时，用 `cmake --install build` / `cmake --build build --target uninstall` 代替 `make -C build ...`。

#### 方式 C：下载 GitHub Actions 生成的 `.tar.xz`

在 GitHub 的 Releases 或 Actions artifacts 中下载 `lsw-<version>.tar.xz`：

```bash
tar -xJf lsw-1.0.0.tar.xz
cd lsw-0.1.0

cmake -S . -B build -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DLSW_FETCH_DEPS=OFF
make -C build -j"$(nproc)"

sudo make -C build install
sudo make -C build uninstall
```

该源码包内置 CMake、源码、脚本与文档，支持离线构建（前提是系统已装齐依赖）。

### 2. 初始化宿主环境

```bash
lsw init
```

它会：

- 识别发行版与包管理器；
- 检查 KVM 权限、QEMU、OVMF、Samba、cifs-utils、OpenSSH、swtpm、libsecret 与 `lsw-mount-helper`；
- 已有的组件直接跳过；缺失项展示 `sudo pacman/apt/dnf ...` 计划，确认后自动安装；
- 生成 `~/.config/lsw/config.toml`（已存在则原样保留）；
- 缺失 helper 时安装 `/usr/local/lib/lsw/lsw-mount-helper` 与 `/etc/sudoers.d/lsw`；
- 提示 Windows 侧需要执行的组件检查。

只想看看会做什么、不产生变更：

```bash
lsw init --dry-run
```

自动化静默执行：

```bash
lsw init --yes
```

### 3. 准备 ISO

- Windows ISO：自备；
- virtio-win ISO：可从 Fedora 官方下载：
  <https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/stable-virtio/virtio-win.iso>

### 4. 安装 Windows

```bash
lsw install \
  --iso /path/to/Windows.iso \
  --virtio-iso /path/to/virtio-win.iso
```

等价写法：

```bash
lsw --install --iso /path/to/Windows.iso --virtio-iso /path/to/virtio-win.iso
```

> [!IMPORTANT]
> **Windows 用户名请优先设置为 `lsw`**。
> LSW 的 `lsw start` / `lsw shell` / `lsw <program>` 全部通过 `config.user.windows_user` 登录 Windows，默认值是 `lsw`。
> 如果 Windows 用户不叫 `lsw`，请在安装完成后修改：
>
> ```toml
> [user]
> windows_user = "你的 Windows 用户名"
> ```
>
> 并确保 SSH 登录名、`C$`/`D$` 凭据都使用同一个用户；否则 `lsw start` 会等待 SSH 超时。

- 安装模式会打开 QEMU GUI；
- 如果磁盘选择页看不到 virtio 磁盘，加载 virtio CD 里的 `viostor` / `vioscsi` 驱动；
- **强烈建议创建 Windows 用户 `lsw`**；若不叫 `lsw`，安装后必须修改 `~/.config/lsw/config.toml` 的 `[user] windows_user`，并确保 SSH 登录名与之一致；
- 安装完成后进入 Windows 桌面，按下节完成一次配置。

### 5. 启动与进入

```bash
lsw start          # 启动 Windows、host Samba、挂载 C$/D$
lsw status         # 查看运行状态、主机名、virtio-win
lsw                # 等价于 lsw shell
lsw shell          # 进入 PowerShell（会按 Linux cwd 映射 Z:）
```

`lsw start` 首次会提示输入 Windows 用户密码用于 CIFS 挂载 `C$`/`D$`；可用全局选项 `lsw --save-password start` 存入 keyring。

### 6. 退出与关机

```bash
lsw shutdown
```

---

## Windows 内首次配置

LSW 依赖 guest 内的 OpenSSH 连接；请在新 Windows 里至少完成以下配置。

### 1. 启用 OpenSSH Server

Windows 10/11 管理员 PowerShell：

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic

New-NetFirewallRule -Name sshd -DisplayName 'OpenSSH Server' `
  -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22
```

如果 Windows Update 卡住或安装盘不带该按需功能，也可以使用便携版 OpenSSH：

```powershell
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest `
  -Uri https://github.com/PowerShell/Win32-OpenSSH/releases/latest/download/OpenSSH-Win64.zip `
  -OutFile $env:TEMP\OpenSSH-Win64.zip
Expand-Archive $env:TEMP\OpenSSH-Win64.zip -DestinationPath 'C:\Program Files\OpenSSH' -Force
Set-ExecutionPolicy Bypass -Scope Process -Force
cd 'C:\Program Files\OpenSSH'
.\install-sshd.ps1
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

### 2. 安装 QEMU Guest Agent

打开 virtio-win ISO（盘符以 `Get-Volume` 为准），运行：

```powershell
E:\virtio-win-guest-tools.exe
```

或只安装 QGA：

```powershell
msiexec /i E:\guest-agent\qemu-ga-x86_64.msi /qn
Get-Service QEMU-GA
```

### 3. 启用 SMB / 管理共享

管理员 PowerShell：

```powershell
Set-Service LanmanServer -StartupType Automatic
Start-Service LanmanServer

New-NetFirewallRule -Name smb -DisplayName 'SMB' `
  -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 445
```

如果 `mount.cifs` 报 `mount error(13): Permission denied`，需要允许本地管理员通过网络访问管理共享：

```powershell
New-ItemProperty `
  -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' `
  -Name LocalAccountTokenFilterPolicy -Value 1 -PropertyType DWord -Force

New-ItemProperty `
  -Path 'HKLM:\SYSTEM\CurrentControlSet\Services\LanmanServer\Parameters' `
  -Name AutoShareWks -Value 1 -PropertyType DWord -Force

Restart-Computer -Force
```

### 4. 写入 Linux SSH 公钥

Windows 用户建议与 `config.user.windows_user` 一致（默认 `lsw`）。

在 Linux 查看公钥：

```bash
cat ~/.ssh/id_ed25519.pub
```

Windows 管理员 PowerShell 中写入：

- 普通用户：`C:\Users\lsw\.ssh\authorized_keys`
- 管理员用户：`C:\ProgramData\ssh\administrators_authorized_keys`

管理员文件权限：

```powershell
New-Item -ItemType Directory -Force -Path C:\ProgramData\ssh | Out-Null
Add-Content C:\ProgramData\ssh\administrators_authorized_keys "<粘贴 Linux 公钥>"
icacls C:\ProgramData\ssh\administrators_authorized_keys /inheritance:r `
  /grant 'SYSTEM:F' /grant 'Administrators:F'
Restart-Service sshd
```

Linux 侧验证免密：

```bash
ssh-keygen -R "[127.0.0.1]:2222" 2>/dev/null
ssh -p 2222 -o StrictHostKeyChecking=accept-new lsw@127.0.0.1 hostname
ssh -p 2222 -o BatchMode=yes lsw@127.0.0.1 hostname
```

### 5. 检查 Windows 侧组件

仓库内提供了 `scripts/post-install.ps1`，可在 Windows 中运行，检查：

- OpenSSH Server (`sshd`)
- QEMU Guest Agent (`QEMU-GA`)
- SMB / `C$`、`D$`
- `Z:` 映射

---

## 命令详解

### `lsw init`

```text
lsw init [--yes] [--dry-run]
```

- 默认交互确认后安装缺失包；
- `--yes` 跳过确认，适合自动化；
- `--dry-run` 只检测和打印，不写配置、不安装；
- 不替代 `lsw --install`：init 只准备宿主环境，不安装 Windows。

### `lsw install` / `lsw --install`

```text
lsw install [--iso PATH] [--virtio-iso PATH] [--disk PATH]
            [--firmware bios|ovmf] [--allow-existing-disk]
lsw --install [同样的选项]
```

- 两条命令完全等价；
- `--disk` 默认 `~/.local/share/lsw/vm/disk.qcow2`；
- 默认 BIOS/SeaBIOS；Windows 11 使用 `--firmware ovmf`；
- 默认拒绝复用 LSW 数据目录外的已存在磁盘。

### `lsw start`

```text
lsw start [--save-password]
```

- 拉起 `lswd`、启动 QEMU（无显示模式）；
- 等待 Windows SSH banner（默认 120s）；
- 启动 host Samba（`127.0.0.1:445`）；
- 通过 `mount.cifs` 挂载 `C$`/`D$` 到 `/mnt/Windows`；
- 幂等：已运行时直接返回当前状态。

### `lsw` / `lsw shell`

```text
lsw
lsw shell
```

- 读取当前 Linux cwd；
- `$HOME` → PowerShell 默认路径；
- `$HOME` 下 → 映射为 `Z:\relative\path` 并 `Set-Location`；
- `$HOME` 外 → 返回 `PATH_OUTSIDE_HOME`（退出码 5）；
- 用 `execvp` 替换当前进程，退出后回到原 shell，不新开窗口。

### `lsw <program>`

```text
lsw test.exe [args...]
lsw test.bat [args...]
lsw build.cmd --release
```

- 支持 `.exe` / `.bat` / `.cmd`；相对路径按 cwd 解析；
- 程序位于 `$HOME` 下 → 映射为 `Z:\...`；
- 不在 `$HOME` 下 → 复制到 `~/.lsw/tmp/<hash>/` 后再执行；
- 远端会再把程序复制到 Windows `%TEMP%\lsw-exec-*` 执行，规避 SMB 文件锁；
- 透传 stdout/stderr 与退出码（0–255）；
- `.exe` PE Subsystem 为 GUI 时明确拒绝，返回 `PROGRAM_FAILED`（退出码 7）。

### `lsw status`

```text
lsw status [--json]
```

查看：

- `lswd` 是否运行；
- Windows 虚拟机是否已安装（磁盘是否存在）；
- 虚拟机运行状态（QMP）；
- Windows 主机名（运行时 SSH 探测后缓存）；
- virtio-win 驱动是否存在；
- 安装标记、磁盘路径等。

daemon 未运行时会离线读取磁盘与缓存，不会自动拉起 `lswd`。

### `lsw mount` / `lsw unmount`

手动挂载/卸载 Windows `C$`、`D$` 到 `/mnt/Windows`，幂等。

### `lsw shutdown`

优先通过 SSH 执行 Windows 关机；失败时走 QMP `system_powerdown` → SIGTERM → SIGKILL；随后卸载 CIFS 并停止 `lswd`。

### `lsw uninstall` / `lsw --uninstall`

```text
lsw uninstall [--yes] [--keep | --purge] [--force] [--dry-run]
lsw --uninstall [同样的选项]
```

- 默认只删除虚拟机文件：磁盘、`OVMF_VARS.fd`、安装/guest 标记；
- 保留 `config.toml`、helper、依赖，方便直接重装；
- `--purge`：额外删除配置、数据/状态目录、helper/sudoers，并卸载 `lsw init` 记录过的受管包；
- `--dry-run`：只打印计划；
- `--force`：允许删除 LSW 数据目录之外的磁盘；
- 运行中的 VM 会先通过 IPC `shutdown` 正常关机。

---

## 配置参考

配置文件：`~/.config/lsw/config.toml`（遵循 `XDG_CONFIG_HOME`）。

```toml
[vm]
name = "lsw-windows"
memory_mb = 8192
cpus = 4
disk = "~/.local/share/lsw/vm/disk.qcow2"
windows_iso = ""
virtio_iso = ""

[network]
ssh_host_port = 2222
smb_host_port = 1445
guest_smb_host = "10.0.2.2"
guest_smb_share = "qemu"

[filesystem]
mount_root = "/mnt/Windows"
linux_home_share = "~"
windows_drive_z = "Z:"

[user]
windows_user = "lsw"
```

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `vm.memory_mb` | `8192` | VM 内存（MB） |
| `vm.cpus` | `4` | vCPU 数量 |
| `vm.disk` | `~/.local/share/lsw/vm/disk.qcow2` | Windows 系统盘 |
| `vm.windows_iso` | `""` | Windows ISO（安装时可用 `--iso` 覆盖） |
| `vm.virtio_iso` | `""` | virtio-win ISO |
| `network.ssh_host_port` | `2222` | 宿主机 SSH 转发端口 |
| `network.smb_host_port` | `1445` | 宿主机 SMB 转发端口 |
| `network.guest_smb_host` | `10.0.2.2` | QEMU user-net 宿主网关 |
| `network.guest_smb_share` | `qemu` | host Samba 共享名 |
| `filesystem.mount_root` | `/mnt/Windows` | C$/D$ 挂载根 |
| `filesystem.linux_home_share` | `~` | 共享给 Windows 的 Linux 目录 |
| `filesystem.windows_drive_z` | `Z:` | Windows 内映射盘符 |
| `user.windows_user` | `lsw` | Windows SSH 用户名；**建议 Windows 安装时创建同名用户 `lsw`**，否则必须修改此处 |

修改后无需重启 daemon 的字段：下次命令读取配置即生效；涉及 QEMU 参数的字段需要 `lsw shutdown` → `lsw start` 重新启动 VM。

---

## 数据与状态路径

| 用途 | 路径 |
| --- | --- |
| 配置 | `${XDG_CONFIG_HOME:-$HOME/.config}/lsw/config.toml` |
| 数据（磁盘、OVMF_VARS） | `${XDG_DATA_HOME:-$HOME/.local/share}/lsw` |
| 状态（日志、安装标记、guest 缓存） | `${XDG_STATE_HOME:-$HOME/.local/state}/lsw` |
| 运行时（socket、pid） | `${XDG_RUNTIME_DIR}/lsw` |
| 宿主 helper | `/usr/local/lib/lsw/lsw-mount-helper` |
| helper sudoers | `/etc/sudoers.d/lsw` |
| Windows SSH | `127.0.0.1:2222` → guest `22` |
| host Samba | `127.0.0.1:445`，guest 通过 `10.0.2.2` 访问 |
| C$/D$ 挂载 | `/mnt/Windows/C`、`/mnt/Windows/D` |

`lsw status --json` 可以查看磁盘、主机名、virtio 状态、安装标记等完整字段。

---

## 常见问题

### `/dev/kvm` 不存在或没有权限

```bash
ls -l /dev/kvm
sudo usermod -aG kvm "$USER"
# 重新登录后生效
```

### `lsw start` 等待 SSH 超时（退出码 6）

检查：

1. Windows 内 `sshd` 是否 Running；
2. 防火墙 22 是否放行；
3. Windows 用户是否与 `config.user.windows_user` 一致；
4. SSH 公钥是否写入正确的 `authorized_keys`；
5. 先手动验证：

```bash
ssh -p 2222 -o BatchMode=yes lsw@127.0.0.1 hostname
```

### `mount error(13): Permission denied`

Windows 管理员 PowerShell：

```powershell
New-ItemProperty `
  -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' `
  -Name LocalAccountTokenFilterPolicy -Value 1 -PropertyType DWord -Force

New-ItemProperty `
  -Path 'HKLM:\SYSTEM\CurrentControlSet\Services\LanmanServer\Parameters' `
  -Name AutoShareWks -Value 1 -PropertyType DWord -Force

Restart-Computer -Force
```

### `Z:` 不存在

`Z:` 按登录会话隔离；请在 Linux 侧用 `lsw shell` 进入，而不是手动 `ssh`：

```bash
cd ~
lsw shell
```

手动 SSH 会话内可以临时：

```powershell
net use Z: \\10.0.2.2\qemu /persistent:yes
```

### QEMU 报 `Failed to get "write" lock`

有残留 QEMU 或 `lswd` 占用磁盘：

```bash
pkill -x lswd
pgrep -af qemu-system-x86_64
# 只结束命令行包含 lsw/vm/disk.qcow2 的 QEMU
```

### `lsw test.bat` 从 SMB 盘执行卡住

LSW 已内置规避：远端先复制到 Windows `%TEMP%` 再执行。若仍遇到，确认 Windows 内没有旧 `.bat` 进程残留，并查看 `lsw status` 是否正常运行。

### Windows 用户名不是 lsw 怎么办

默认 `config.user.windows_user = "lsw"`。两种处理方式：

1. 在 Windows 里新建一个名为 `lsw` 的账户（推荐）；
2. 或修改 Linux 配置：

```toml
[user]
windows_user = "你的 Windows 用户名"
```

同时确保：

- `ssh -p 2222 <用户名>@127.0.0.1 hostname` 能成功；
- `lsw start` 提示输入的密码是该 Windows 用户的密码；
- `lsw status` 的主机名探测正常。

### 重装 Windows 后 SSH 报 host key 变化

```bash
ssh-keygen -R "[127.0.0.1]:2222"
```

然后重新连接并确认新指纹。

### 想彻底卸载 LSW

```bash
lsw uninstall --purge
```

会删除 VM、配置、helper 和 `lsw init` 记录过的受管包；未记录的包只打印手动命令。

---

## 开发者：从源码构建

### 编译依赖

- CMake ≥ 3.25
- C++26 编译器（GCC 14+ 或 Clang 18+，以实际支持为准）
- CLI11、nlohmann/json、spdlog、fmt、tomlplusplus
- 测试需要 Catch2 v3

CMake 默认“系统包优先，缺失时 FetchContent 下载”。完全离线时：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLSW_FETCH_DEPS=OFF
```

### 构建与测试

```bash
cmake -S . -B build -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

关闭测试可加 `-DBUILD_TESTING=OFF`。

覆盖率：

```bash
bash scripts/coverage.sh
```

该脚本会构建 `build-coverage`、运行 ctest、用 gcovr（未安装时用 gcov JSON 回退）输出行覆盖率，
默认阈值：`src/core ≥ 50%`、`src/ipc ≥ 70%`、`src/vm ≥ 70%`、总体 ≥ 55%。

### 打包 `.tar.xz`

GitHub Actions 工作流 `.github/workflows/package-tar-xz.yml` 会在推送 `v*` tag 或手动触发时：

1. 在 Arch Linux 容器中安装构建依赖；
2. `cmake` 配置、编译、`ctest`；
3. 生成源码包 `lsw-<version>.tar.xz`；
4. 解压后验证 `make install` 与 `make uninstall`；
5. tag 触发时自动创建 GitHub Release 并附带 `.tar.xz`。

本地也可以生成同样的包：

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target package_source
ls -lh build/lsw-*.tar.xz
```

### 验收脚本

P9 最终验收：

```bash
bash scripts/verify-p9.sh                  # 自动：构建/ctest/覆盖率/文档/审计/回归
LSW_P9_REAL=1 bash scripts/verify-p9.sh    # 追加真机 5 命令 E2E
```

仓库为每个阶段提供了一键验收脚本：

| 脚本 | 内容 |
| --- | --- |
| `scripts/verify-p0.sh` | 工程基线、干净构建、文档/目录完整性 |
| `scripts/verify-p1.sh` … `verify-p8.sh` | P1~P8 分阶段功能验收 |
| `scripts/verify-status.sh` | `lsw status` 离线/运行时验收 |
| `scripts/verify-init.sh` | `lsw init` dry-run 与依赖检测验收 |
| `scripts/verify-uninstall.sh` | `lsw uninstall` keep/purge 计划验收 |
| `scripts/verify-docs.sh` | P9 文档/命令/端口/图一致性检查 |
| `scripts/audit-security.sh` | P9 安全与合规审计 |
| `scripts/coverage.sh` | P9 行覆盖率报告与阈值检查 |
| `scripts/verify-p9.sh` | P9 自动验收汇总（LSW_P9_REAL=1 时追加真机 E2E） |
| `scripts/lsw-p9-real-test.sh` | P9 真机 5 命令验收（start/status/shell/exec/shutdown） |
| `scripts/post-install.ps1` | Windows 内组件检查脚本 |

运行示例：

```bash
bash scripts/verify-p8.sh
bash scripts/verify-status.sh
bash scripts/verify-init.sh
bash scripts/verify-uninstall.sh
```

### 代码规范

- 语言标准：C++26；
- 所有 `.hpp` / `.cpp` 必须带 Doxygen 文件头（`@file` / `@brief` / `@details` / `@author` / `@date`）；
- 公共接口使用 Doxygen 注释；
- 提交前格式化检查：

```bash
find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 clang-format --dry-run --Werror
```

### 文档同步

任何接口、命令行为、端口、挂载点、模块依赖变化，必须同步更新：

- `docs/interfaces/*.md`
- `docs/diagrams/*.puml`
- `docs/CHANGELOG.md`
- `docs/TASKS.md`

---

## 项目结构

```text
lsw/
├── .github/workflows/         # GitHub Actions：构建、测试、打包、Release
├── CMakeLists.txt            # C++26 工程定义
├── cmake/Dependencies.cmake  # 依赖解析（系统包 + FetchContent）
├── include/lsw/              # 公共头文件
│   ├── core/                 # 配置、路径、进程、状态、init/uninstall
│   ├── vm/                   # QEMU/QMP、安装、guest status
│   ├── fs/                   # CIFS 挂载、host Samba、Z: 映射、凭据
│   ├── ipc/                  # Unix Socket JSON Lines 协议
│   ├── shell/                # PowerShell SSH 桥接
│   ├── exec/                 # Windows 程序执行
│   └── util/                 # 日志、网络、路径工具
├── src/
│   ├── cli/main.cpp          # lsw CLI
│   ├── daemon/lswd.cpp       # lswd 守护进程入口
│   ├── core/                 # 核心实现
│   ├── vm/                   # QEMU/QMP/安装/status
│   ├── fs/                   # 挂载与 Samba
│   ├── ipc/                  # Client/Server/协议
│   ├── shell/                # shell bridge
│   ├── exec/                 # 程序执行
│   └── util/
├── tests/
│   ├── unit/                 # Catch2 单元测试
│   └── integration/          # 集成探针、假 QEMU
├── scripts/                  # 验收脚本、helper、Windows 检查脚本
└── docs/
    ├── interfaces/           # CLI/IPC/QMP/配置/文件系统等接口
    ├── diagrams/             # PlantUML 组件图与时序图
    ├── TASKS.md              # 阶段任务与验收状态
    └── CHANGELOG.md
```

---

## 贡献指南

欢迎提交 Issue 与 Pull Request。

1. Fork 仓库并从 `main` 创建分支；
2. 修改代码后运行：

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror <files>
```

3. 同步更新对应 `docs/interfaces`、`docs/diagrams`、`docs/CHANGELOG.md`；
4. 提交信息建议格式：

```text
<type>: <简短说明>

- 变更点 1
- 变更点 2
```

`type` 可用 `feat` / `fix` / `docs` / `test` / `refactor` / `chore`。

### 开发环境快速验证

```bash
bash scripts/verify-p0.sh
bash scripts/verify-p8.sh
```

真实 QEMU/KVM 环境运行 `scripts/lsw-p9-real-test.sh`，或 `LSW_P9_REAL=1 bash scripts/verify-p9.sh` 完成 5 命令真机验收。

---

## 架构与文档

### 核心组件

```text
User
  │
  ▼
lsw CLI ── Unix Socket JSON ──► lswd
  │                              │
  │                              ├─ QEMU/KVM + QMP
  │                              ├─ host Samba (127.0.0.1:445)
  │                              ├─ mount.cifs → /mnt/Windows/{C,D}
  │                              └─ ssh / PowerShell / exec
  │
  └─ execvp ssh -t（lsw shell 接管终端）
```

### 关键设计

- `lsw` 与 `lswd` 通过 `$RUNTIME/lsw/lswd.sock` 上的 JSON Lines 协议通信；
- 运行模式 QEMU 无显示，安装模式显示 GUI；
- host Samba 共享 `$HOME`，guest 通过 `10.0.2.2` 访问并映射 `Z:`；
- `Z:` 按登录会话隔离，由真正使用它的 SSH 会话延迟执行 `net use`；
- Windows 程序执行时复制到 Windows `%TEMP%` 再运行，规避 SMB 文件锁；
- 密码仅通过 stdin / IPC 内存传递，不写日志。

详细文档：

- 架构：`docs/architecture.md`
- CLI：`docs/interfaces/cli.md`
- IPC：`docs/interfaces/daemon-ipc.md`
- 配置：`docs/interfaces/config.md`
- 文件系统：`docs/interfaces/filesystem.md`
- QMP：`docs/interfaces/qmp.md`
- init / status / uninstall：`docs/interfaces/{init,uninstall}.md`
- 时序图：`docs/diagrams/*.puml`
- 阶段回顾：`docs/P6-回顾.md`、`docs/P8-回顾.md`

---

## 路线图

- [x] P0 工程基线
- [x] P1 核心库
- [x] P2 IPC / lswd
- [x] P3 QEMU / QMP
- [x] P4 `lsw --install`
- [x] P5 `lsw start` / `lsw shutdown`
- [x] P6 文件系统（Z: / CIFS）
- [x] P7 `lsw shell`
- [x] P8 `lsw <program>`（.exe/.bat/.cmd）
- [x] 扩展：`lsw status`
- [x] 扩展：`lsw install` 子命令别名
- [x] 扩展：`lsw init`
- [x] 扩展：`lsw uninstall`
- [x] P9 系统测试与 1.0 收口（真机 6/6、覆盖率/文档/安全审计全绿）

---

## 作者与许可

**作者 / 维护者**：琳筱筱 `<341178706@qq.com>`

本项目使用 [MIT License](LICENSE) 开源。

```text
MIT License
Copyright (c) 2026 琳筱筱 <341178706@qq.com>
```

> LSW 不提供、不下载 Windows ISO，也不包含任何 Microsoft 授权文件；使用者需自备合法 Windows 许可证与镜像。

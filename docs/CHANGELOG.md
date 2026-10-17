# 变更记录

本文件记录 LSW 的所有重要变更。格式参考 Keep a Changelog，版本号遵循 Semantic Versioning，当前尚未发布。

## [1.0.0] - 2026-10-17

首个正式版本：`lsw` / `lswd` / KVM/QEMU / SSH / Samba 全链路可用；
支持 init / install / start / shell / exec / status / mount / shutdown / uninstall 与 `.tar.xz` 打包。

P9 收口：真机 5 命令 6/6 通过；覆盖率、文档对齐、安全审计全绿；`ctest` 131/131。

### 新增

- 新增 MIT `LICENSE`，README 同步许可证信息。
- 打包与安装：CMake 新增 `install` / `uninstall` 目标，可 `make install` 安装 `lsw`/`lswd`/helper/Windows 脚本/文档，`make uninstall` 清理 LSW 专属文件与空目录；`cmake --build --target package_source` 生成 `lsw-<version>.tar.xz` 源码包；新增 GitHub Actions `.github/workflows/package-tar-xz.yml`，tag 自动构建、测试、验证安装卸载并创建 Release。
- 新增 `lsw uninstall` / `lsw --uninstall`（P13 扩展）：默认只删除 VM 磁盘、OVMF_VARS 与 install/guest 元数据，保留配置/helper/依赖便于重装；可交互选择彻底清理，`--purge` 只卸载 `lsw init` 记录的受管包，`--keep`/`--force`/`--dry-run` 可控；运行中 VM 先经 IPC shutdown。
- 新增 `lsw init [--yes] [--dry-run]`（P12 扩展）：识别 Arch/Debian/Fedora 与包管理器，检查 KVM/QEMU/OVMF/Samba/cifs-utils/OpenSSH/swtpm/libsecret/helper，缺包经确认后自动安装；生成默认 `config.toml`（已有保留）；Windows 侧只提示 `post-install.ps1`。
- 新增 `lsw install` 子命令，与 `lsw --install` 完全等价，支持相同的 ISO/磁盘/固件选项，便于把 install 与 start/status 等子命令保持统一形式。
- `lsw status [--json]`（新增功能）：查看 daemon/VM 状态、磁盘是否存在、安装标记、Windows 主机名与 virtio-win 驱动；VM 运行时 SSH 探测并缓存到 `$STATE/lsw/guest.json`，停止时读缓存；`lsw --install` 成功后写 `$STATE/lsw/install.json`。
- P8 `lsw <program>`：支持 `.exe`/`.bat`/`.cmd`；daemon 解析 argv/cwd、`$HOME` 外复制到 `~/.lsw/tmp/<hash>/`、PE GUI 检测、非交互 SSH、Windows `%TEMP%` 本地副本执行（规避 SMB 文件锁）、stdout/stderr/exit_code 透传；新增 `PROGRAM_FAILED` 与 `scripts/verify-p8.sh`。
- P7 `lsw shell`：CLI 上报 `cwd`，daemon 经 `map_cwd_to_z` 生成含 `net use Z:` + `Set-Location` 的 EncodedCommand `ssh_args`；CLI `execvp` 接管终端，`$HOME` 外返回 `PATH_OUTSIDE_HOME`，VM 未运行返回 `VM_NOT_RUNNING`；新增 `scripts/verify-p7.sh`；终端体验：启动清屏 + RawUI 缓冲区归一化 + `Clear-Host` 清滚动回退。
- P6 复盘文档：[`docs/P6-回顾.md`](P6-回顾.md)（QEMU user SMB → host root Samba 的演进、真机 30/30 验收与经验教训）。
- P8 复盘文档：[`docs/P8-回顾.md`](P8-回顾.md)（SMB 直执行卡死的定位、Windows 本地副本执行、终端体验修复与真机验收记录）。

- 工程基线：CMake 目标 `lsw`、CTest 冒烟测试 `lsw_smoke`。
- 目录骨架：`include/lsw/{cli,core,vm,fs,shell,exec,ipc,util}`、`tests/`、`scripts/`、`cmake/`。
- 文档骨架：`docs/architecture.md`、`docs/interfaces/{qmp,filesystem,config}.md`、`docs/diagrams/sequence-{install,start,exec,shutdown}.puml`。
- 任务清单：`docs/TASKS.md`（P0~P9 共 47 项任务，权重合计 100%，含关键路径、里程碑与执行规约）。
- P0 验收辅助：`scripts/verify-p0.sh`（一键核验 C++26 基线、目录/文档完整性、源码自动 glob、干净构建与 CTest）。
- `lsw_core` 静态库：`Result<T>`/统一错误码、XDG 路径、日志、配置加载/校验、`linux_to_z` 路径映射、进程 RAII 执行。
- 第三方依赖接入：`cmake/Dependencies.cmake`（CLI11、nlohmann/json、spdlog、fmt、toml++、Catch2；系统包优先 + FetchContent 兜底，`LSW_FETCH_DEPS=OFF` 可强制离线）。
- 单元测试 32 例：`tests/unit/*.cpp`，`ctest` 共 33 项全部通过。
- 接口文档：`docs/interfaces/core.md`，记录 `lsw_core` 公开接口。
- IPC：JSON Lines 协议（`ipc/protocol`）、Unix Socket `Client`/`Server`（多连接、0700 权限、优雅关闭）。
- `lswd` 骨架：`lswd.pid` + `flock` 单实例锁、SIGINT/SIGTERM 优雅退出、`status` 命令、socket/PID 清理；`DaemonState` 幂等状态机。
- 集成探针 `tests/integration/ipc_probe.cpp` 与 P2 验收脚本 `scripts/verify-p2.sh`。
- `QmpClient`：QMP greeting/`qmp_capabilities` 握手、按 id 匹配、异步事件跳过、错误对象分类、读取超时。
- `QemuVm`：运行/安装参数构建、后台进程 RAII、`query-status` 状态映射、`system_powerdown` → SIGTERM → SIGKILL 关闭流程。
- `ChildProcess`（`core/process`）：后台子进程、输出重定向文件、`wait_for`、析构回收。
- swtpm 可选检测与 TPM 设备参数；OVMF/KVM 依赖校验。
- 测试替身 `tests/integration/fake_qemu.cpp`、真实 QMP 探针 `qmp_probe.cpp` 与 P3 验收脚本 `scripts/verify-p3.sh`；单元/集成测试增至 65 例。
- `Installer`（P4）：依赖检查与修复建议、ISO 交互/校验（不下载不内置）、`qemu-img` 创建 qcow2、OVMF_VARS 模板复制、安装模式执行与安装后指引。
- CLI：CLI11 接入 `lsw --install` / `--iso` / `--virtio-iso` / `--disk` / `--version` / `--help`；P7 起裸 `lsw` 与 `lsw shell` 由 `run_shell_command` 经 `execvp ssh -t` 接管终端。
- `scripts/post-install.ps1`：Windows 内 OpenSSH / QEMU Guest Agent / SMB / `Z:` 检查。
- 测试替身支持 `LSW_FAKE_QEMU_AUTO_QUIT_MS`，安装流程集成探针 `lsw_install_probe` 与 P4 验收脚本 `scripts/verify-p4.sh`；测试增至 72 例。
- `DaemonService`（P5）：`lsw start` 拉起 lswd/QEMU 并等待 SSH 2222，`lsw shutdown` 优先 SSH 关机、QMP/信号兜底，清理 `qmp.sock`/`qga.sock` 并让 lswd 退出；start/shutdown 幂等且并发安全。
- `wait_for_tcp_port`（`util/net`）与 `spawn_detached`（双重 fork）工具。
- CLI 新增 `lsw start` / `lsw shutdown` 子命令；P5 验收脚本 `scripts/verify-p5.sh`。

### 变更

- `Z:` 映射改为按登录会话延迟：`lsw start` 只启动 host Samba 并报告 `z_drive`，由第一个实际使用 `Z:` 的 SSH 会话（`lsw shell`/验收/exec）在会话内执行 `net use`，避免 daemon 提前占用 QEMU 网关首次会话导致后续会话 53。
- `Z:` 数据源从 QEMU user-mode SMB1 改为宿主 root Samba（`127.0.0.1:445`）+ guest `10.0.2.2` 网关：`lswd` 经 helper `smb_start`/`smb_stop` 启停 smbd，SMB2/3 支持多会话，P7 `lsw shell` 可反复映射。
- C++ 标准统一为 **C++26**（对齐 `PROMPT.md`；同步修订 `agent.md` 中的 C++20 表述）。
- CMake 最低版本由 3.12 提升到 **3.25**（`CXX_STANDARD 26` 的最低版本要求）。
- 统一运行路径：套接字/PID 放 `$RUNTIME`（`$XDG_RUNTIME_DIR/lsw`），磁盘与 UEFI 变量放 `$DATA/vm/`，日志与状态放 `$STATE`；清理 `SKILL.md` 中混用 `$STATE` 存放 socket 的示例。
- 统一 SSH 用户：取 `config.user.windows_user`（默认 `lsw`）；`agent.md`、`SKILL.md` 示例参数化，`PROMPT.md` 中的 `user` 明确为该字段。
- `CMakeLists.txt` 自动 glob 改为按模块目录：`src/core`、`src/util` 进 `lsw_core`，`src/cli` 进 `lsw`（`CONFIGURE_DEPENDS`）。
- CMake 目标拆分：`lsw_core`（静态库）+ `lsw`（CLI 可执行文件，链接 `lsw_core`）；`main.cpp` 迁移到 `src/cli/main.cpp`。
- 新增 `lswd` 可执行目标（`src/daemon/`）；`lsw_core` 纳入 `src/ipc/` 与 `src/vm/`。
- 按根目录 `.clang-format` 统一全部 C++ 代码格式；`agent.md` 编码规范加入强制格式化要求。
- smoke 测试改为 `lsw --version`，验证 CLI11 入口而非占位问候。
- CLI11 由 v2.4.2 升级到 v2.7.2。

### 修复

- 修复 GitHub Actions 的 Arch 容器未安装 QEMU 导致安装相关测试误报缺失依赖的问题：工作流补充 `qemu-system-x86` / `qemu-img`，使 `ctest` 132/132 在 CI 中真实执行。
- 修复 `Installer` 依赖检查与实际启动使用不同 QEMU 路径的问题：`InstallOptions::qemu_binary` 为空时回落到 `qemu_options.qemu_binary`，避免自定义/假 QEMU 时报告“缺少 qemu-system-x86_64”。
- 修复 `lsw <program>` 将拼写错误（如 `lsw statu`）误当程序执行、在 VM 未运行时返回 `VM_NOT_RUNNING` 的问题：CLI 先判断位置参数是否像程序（含路径分隔符、`.exe/.bat/.cmd/.com` 扩展名，或 cwd 下存在同名普通文件），否则按未知命令返回退出码 2。
- 修正可执行文件源文件路径：原 `add_executable(lsw main.cpp)` 指向不存在的根目录文件，实际为 `src/main.cpp`。
- `include/` 目录为空时不再向 `target_sources` 传入空列表（CMake 参数错误）。
- 统一 `docs/interfaces/config.md` 的字段引用名 `config.user.windows_user`，与 CLI、SKILL 保持一致。
- 修正 `docs/architecture.md` 的阶段编号（1~8 → P1~P9），与 `docs/TASKS.md` 对齐。
- 修复 `run_process` 自锁：父进程读取 exec 状态管道前未关闭自身写端，导致成功执行时死锁（由 P1 超时用例暴露）。
- 修复 CLI11 低版本 `cmake_minimum_required(VERSION 3.5)` 触发的 CMake 弃用警告：升级到 v2.7.2（要求 CMake >= 3.14）。
- 修复 `QmpClient` 读取粘包丢行：一次 `recv` 包含事件与响应两行时，原实现丢弃缓冲区剩余内容导致偶发超时；改为持久接收缓冲按行消费。
- 修复假 QEMU 自动退出检查只在收到消息后才执行、空闲时永不退出的问题。
- 修复 OVMF 探测遗漏 Arch `edk2-ovmf` 的 `.4m.fd` 路径问题；依赖提示改为按发行版生成，并合并重复的 OVMF 建议。
- 修复未安装 Samba 时 QEMU 因 `smb=$HOME` 直接退出、仅报 `QMP Connection refused` 的问题：检测 `smbd` 后才启用 user SMB，缺失时警告并继续；QMP 连接失败时附带 QEMU 日志尾部与退出码。
- CLI 对多余位置参数（如把 ISO 路径直接跟在命令后）给出中文用法提示并返回退出码 2，不再只显示 CLI11 原始报错。
- 改善 start/shutdown 可观测性：CLI 输出进度提示，lswd 日志记录收到的命令与关机阶段；关机优雅窗口默认 15s（`LSW_SHUTDOWN_TIMEOUT_MS` 可调），避免长时间无输出。
- 修复 start 假就绪：QEMU hostfwd 端口被监听不代表 guest sshd 可用；改为必须读取到 `SSH-` banner 才算 Windows OpenSSH 就绪。
- 修复 QEMU 11 + OVMF 加载 Windows 10 ISO 光驱 EFI 镜像超时导致落到 PXE 的问题：安装/运行默认改用 SeaBIOS（BIOS），安装模式 `-boot order=d` 优先 CD-ROM；新增 `--firmware bios|ovmf`，BIOS 模式不再要求 OVMF。
- 新增安装磁盘安全护栏：默认拒绝挂载 LSW 数据目录之外已存在的磁盘，需显式 `--allow-existing-disk`，避免覆盖 libvirt 等已有虚拟机。
- P6 文件系统：新增 `lsw::winfs` credentials/mount/z_mapping/host_smb 模块，start 启动 host Samba 并挂载 C$/D$（`Z:` 延迟到使用会话映射），shutdown 先卸载并停止 host Samba；新增 `lsw mount`/`lsw unmount`、`scripts/lsw-mount-helper` 最小权限 helper（含 `smb_start`/`smb_stop`）、keyring 凭据与 `--save-password`、残留挂载恢复及 `MountFailed`/`ShareSetupFailed` 错误映射。
- 修复 Windows 远端 `cmd.exe` 解析：`Z:` 映射脚本改用 `powershell -EncodedCommand`（UTF-16LE + Base64），避免 `| Out-Null` 被 cmd 当作管道；`run_process` 对外部输出做 UTF-8 清洗，IPC 响应增加安全编码兜底。
- 单盘 LSW 兼容：guest 只有 C: 时自动跳过不存在的 D$，`start` 仍成功，挂载列表只包含实际存在的盘。
- 明确 Windows 盘符映射的会话隔离语义：验收改为在同一个 SSH 会话内 `net use` + `Set-Location Z:\`；`/persistent:yes` 供交互登录恢复，P7 `lsw shell` 会在自身会话内再次确保映射。
- 明确凭据边界：host Samba 的 `qemu` 共享对 guest 匿名提供 `Z:`，映射不需要 Windows 密码；只有 CIFS 挂载 `C$`/`D$` 才解析 Windows 凭据（keyring / 交互 / 环境变量）。映射前会清理旧的不可用映射。

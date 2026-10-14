# 变更记录

本文件记录 LSW 的所有重要变更。格式参考 Keep a Changelog，版本号遵循 Semantic Versioning，当前尚未发布。

## [Unreleased]

### 新增

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
- CLI：CLI11 接入 `lsw --install` / `--iso` / `--virtio-iso` / `--disk` / `--version` / `--help`；裸 `lsw` 暂提示 P7 实现。
- `scripts/post-install.ps1`：Windows 内 OpenSSH / QEMU Guest Agent / SMB / `Z:` 检查。
- 测试替身支持 `LSW_FAKE_QEMU_AUTO_QUIT_MS`，安装流程集成探针 `lsw_install_probe` 与 P4 验收脚本 `scripts/verify-p4.sh`；测试增至 72 例。

### 变更

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

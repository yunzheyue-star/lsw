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

### 变更

- C++ 标准统一为 **C++26**（对齐 `PROMPT.md`；同步修订 `agent.md` 中的 C++20 表述）。
- CMake 最低版本由 3.12 提升到 **3.25**（`CXX_STANDARD 26` 的最低版本要求）。
- 统一运行路径：套接字/PID 放 `$RUNTIME`（`$XDG_RUNTIME_DIR/lsw`），磁盘与 UEFI 变量放 `$DATA/vm/`，日志与状态放 `$STATE`；清理 `SKILL.md` 中混用 `$STATE` 存放 socket 的示例。
- 统一 SSH 用户：取 `config.user.windows_user`（默认 `lsw`）；`agent.md`、`SKILL.md` 示例参数化，`PROMPT.md` 中的 `user` 明确为该字段。
- `CMakeLists.txt` 自动 glob 改为按模块目录：`src/core`、`src/util` 进 `lsw_core`，`src/cli` 进 `lsw`（`CONFIGURE_DEPENDS`）。
- CMake 目标拆分：`lsw_core`（静态库）+ `lsw`（CLI 可执行文件，链接 `lsw_core`）；`main.cpp` 迁移到 `src/cli/main.cpp`。
- 新增 `lswd` 可执行目标（`src/daemon/`）；`lsw_core` 纳入 `src/ipc/`。
- CLI11 由 v2.4.2 升级到 v2.7.2。

### 修复

- 修正可执行文件源文件路径：原 `add_executable(lsw main.cpp)` 指向不存在的根目录文件，实际为 `src/main.cpp`。
- `include/` 目录为空时不再向 `target_sources` 传入空列表（CMake 参数错误）。
- 统一 `docs/interfaces/config.md` 的字段引用名 `config.user.windows_user`，与 CLI、SKILL 保持一致。
- 修正 `docs/architecture.md` 的阶段编号（1~8 → P1~P9），与 `docs/TASKS.md` 对齐。
- 修复 `run_process` 自锁：父进程读取 exec 状态管道前未关闭自身写端，导致成功执行时死锁（由 P1 超时用例暴露）。
- 修复 CLI11 低版本 `cmake_minimum_required(VERSION 3.5)` 触发的 CMake 弃用警告：升级到 v2.7.2（要求 CMake >= 3.14）。

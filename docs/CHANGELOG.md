# 变更记录

本文件记录 LSW 的所有重要变更。格式参考 Keep a Changelog，版本号遵循 Semantic Versioning，当前尚未发布。

## [Unreleased]

### 新增

- 工程基线：CMake 目标 `lsw`、CTest 冒烟测试 `lsw_smoke`。
- 目录骨架：`include/lsw/{cli,core,vm,fs,shell,exec,ipc,util}`、`tests/`、`scripts/`、`cmake/`。
- 文档骨架：`docs/architecture.md`、`docs/interfaces/{qmp,filesystem,config}.md`、`docs/diagrams/sequence-{install,start,exec,shutdown}.puml`。
- 任务清单：`docs/TASKS.md`（P0~P9 共 47 项任务，权重合计 100%，含关键路径、里程碑与执行规约）。

### 变更

- C++ 标准统一为 **C++26**（对齐 `PROMPT.md`；同步修订 `agent.md` 中的 C++20 表述）。
- CMake 最低版本由 3.12 提升到 **3.25**（`CXX_STANDARD 26` 的最低版本要求）。
- 统一运行路径：套接字/PID 放 `$RUNTIME`（`$XDG_RUNTIME_DIR/lsw`），磁盘与 UEFI 变量放 `$DATA/vm/`，日志与状态放 `$STATE`；清理 `SKILL.md` 中混用 `$STATE` 存放 socket 的示例。
- 统一 SSH 用户：取 `config.user.windows_user`（默认 `lsw`）；`agent.md`、`SKILL.md` 示例参数化，`PROMPT.md` 中的 `user` 明确为该字段。
- `CMakeLists.txt` 自动 glob `src/**/*.cpp`（`CONFIGURE_DEPENDS`），新增源文件无需手动登记。

### 修复

- 修正可执行文件源文件路径：原 `add_executable(lsw main.cpp)` 指向不存在的根目录文件，实际为 `src/main.cpp`。
- `include/` 目录为空时不再向 `target_sources` 传入空列表（CMake 参数错误）。

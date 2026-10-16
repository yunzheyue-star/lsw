# docs/README.md

本目录保存 LSW 的架构、接口、调用关系图与任务清单。

## 目录

```text
docs/
├── architecture.md
├── CHANGELOG.md
├── TASKS.md
├── P6-回顾.md
├── P8-回顾.md
├── interfaces/
│   ├── cli.md
│   ├── core.md
│   ├── daemon-ipc.md
│   ├── qmp.md
│   ├── filesystem.md
│   └── config.md
└── diagrams/
    ├── component.puml
    ├── sequence-install.puml
    ├── sequence-start.puml
    ├── sequence-shell.puml
    ├── sequence-exec.puml
    ├── sequence-status.puml
    └── sequence-shutdown.puml
```

## 同步规则

任何以下变更必须更新文档：

- CLI 命令、参数、输出、错误码
- IPC 请求/响应
- QMP 命令
- 挂载点、共享目录、端口
- 配置文件字段
- 模块依赖、调用方向
- 安装/启动/关闭流程
- 任务拆分、权重与阶段状态（`docs/TASKS.md`）

## 每次任务完成检查

- [ ] `docs/interfaces/*.md` 已更新
- [ ] `docs/diagrams/*.puml` 已更新
- [ ] `docs/CHANGELOG.md` 已更新
- [ ] `docs/TASKS.md` 状态已更新
- [ ] 构建通过
- [ ] 测试通过
## 开发规范

- 语言标准：C++26；错误处理使用 `std::expected` / `Result<T>`，RAII 管理资源。
- 提交前必须通过 `clang-format --style=file --dry-run --Werror`（`include/`、`src/`、`tests/` 下全部 `.hpp` / `.cpp`）。
- 每个 C++ 文件必须以 Doxygen 文件头开头（`@file` / `@brief` / `@details` / `@author` / `@date`），公共接口使用 Doxygen 注释。
- 每次任务：构建 → `ctest` → 更新 `docs/interfaces/*.md` → 更新 `docs/diagrams/*.puml` → 更新 `docs/CHANGELOG.md`。

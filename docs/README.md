# docs/README.md

本目录保存 LSW 的架构、接口和调用关系图。

## 目录

```text
docs/
├── architecture.md
├── CHANGELOG.md
├── TASKS.md
├── interfaces/
│   ├── cli.md
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
# 架构总览

> 状态：**阶段 0（工程基线）**。除 CLI 冒烟输出外，下列模块均为已定义接口、待实现。
> 文档与代码不一致视为任务未完成，变更时同步更新本文件与 `docs/diagrams/`。

## 目标与非目标

| 类别 | 内容 |
| --- | --- |
| 目标 | 在 Linux 终端内提供接近 WSL 的体验：KVM/QEMU 运行 Windows，OpenSSH 进入 PowerShell，SMB/CIFS 打通文件系统 |
| 非目标 | 不提供/下载 Windows ISO；不启动 Windows GUI 程序；不修改 Windows 内核；不承诺支持所有 Windows 版本 |

## 进程结构

| 进程/组件 | 职责 |
| --- | --- |
| `lsw` | CLI 前端，解析命令，通过 Unix Socket 与 `lswd` 通信；`shell` 用 `execvp` 接管终端 |
| `lswd` | 守护进程，管理 QEMU/QMP、SSH 端口、CIFS 挂载、状态与幂等 |
| `lsw_core` | 核心库：配置、路径映射、进程 RAII、日志、IPC、Result 错误类型 |

## 模块划分

| 模块 | 代码位置 | 职责 | 对应技能 |
| --- | --- | --- | --- |
| CLI | `src/cli/`、`include/lsw/cli/` | 命令解析、帮助、退出码 | SKILL-01 |
| 核心 | `src/core/`、`include/lsw/core/` | 配置、状态、进程、路径映射 | SKILL-04 |
| 虚拟化 | `src/vm/`、`include/lsw/vm/` | QEMU 生命周期、QMP、安装模式 | SKILL-02、SKILL-03 |
| 文件系统 | `src/fs/`、`include/lsw/fs/` | CIFS 挂载、SMB 共享、`Z:` 映射 | SKILL-06、SKILL-07 |
| Shell | `src/shell/`、`include/lsw/shell/` | PowerShell SSH 桥接 | SKILL-04 |
| 执行 | `src/exec/`、`include/lsw/exec/` | Windows 程序执行与退出码 | SKILL-05 |
| IPC | `src/ipc/`、`include/lsw/ipc/` | Unix Socket JSON-RPC 客户端/服务端 | SKILL-01 |
| 工具 | `src/util/`、`include/lsw/util/` | 日志、错误类型等 | — |

## 路径规范（全局唯一）

| 用途 | 变量 | 默认路径 | 内容 |
| --- | --- | --- | --- |
| 配置 | `$CONFIG` | `${XDG_CONFIG_HOME:-$HOME/.config}/lsw/config.toml` | TOML 配置 |
| 持久数据 | `$DATA` | `${XDG_DATA_HOME:-$HOME/.local/share}/lsw` | `vm/disk.qcow2`、`vm/OVMF_VARS.fd` |
| 状态与日志 | `$STATE` | `${XDG_STATE_HOME:-$HOME/.local/state}/lsw` | 日志、安装标记 |
| 运行时 | `$RUNTIME` | `${XDG_RUNTIME_DIR}/lsw` | `lswd.sock`、`qmp.sock`、`qga.sock`、`lswd.pid` |

规则：套接字与 PID 生命周期跟随运行时，必须放 `$RUNTIME`；磁盘与 UEFI 变量必须放 `$DATA/vm/`；不得明文保存 Windows 密码。

## 核心数据流

| 流程 | 入口 | 说明 | 时序图 |
| --- | --- | --- | --- |
| 安装 | `lsw --install` | 依赖检查 → 交互输入 ISO → 创建磁盘 → GUI 安装 → 安装后指引 | `sequence-install.puml` |
| 启动 | `lsw start` | 拉起 `lswd` → QMP 启动 VM → 等待 2222 → 启动 host Samba → 挂载 CIFS（`Z:` 延迟到使用会话） | `sequence-start.puml` |
| Shell | `lsw` / `lsw shell` | CLI 上报 cwd → daemon 返回编码 PowerShell 的 `ssh_args` → `execvp ssh -t` 接管终端（P7 已实现） | `sequence-shell.puml` |
| 执行 | `lsw <program>`（`.exe`/`.bat`/`.cmd`） | daemon 解析/复制路径 → 映射 → 非交互 SSH 执行 → 透传 stdout/stderr/退出码（P8 已实现） | `sequence-exec.puml` |
| 状态 | `lsw status [--json]` | 磁盘/安装标记判断已安装；daemon 运行时 QMP + SSH 探测主机名/virtio，停止时读缓存（新增功能） | `sequence-status.puml` |
| 关闭 | `lsw shutdown` | SSH 关机 → QMP `system_powerdown` 兜底 → 卸载 → 停 `lswd` | `sequence-shutdown.puml` |

## 当前实现状态

| 项目 | 状态 |
| --- | --- |
| CMake 目标 `lsw`、C++26、CMake >= 3.25 | ✅ 已建立 |
| 第三方依赖：系统包优先 + FetchContent 兜底（`cmake/Dependencies.cmake`） | ✅ P1 已建立 |
| `lsw_core`：Result/错误码、XDG 路径、日志、配置、路径映射、进程封装 | ✅ P1 已建立 |
| IPC：JSON Lines 协议、Unix Socket Client/Server、幂等状态机 | ✅ P2 已建立 |
| `lswd`：单实例锁、信号处理、status 命令、socket 清理 | ✅ P2 骨架已建立 |
| QMP：QmpClient 握手 / execute / 事件跳过 / 错误分类 | ✅ P3 已建立 |
| `QemuVm`：运行与安装参数、进程 RAII、状态查询、优雅关闭、swtpm 可选 | ✅ P3 已建立 |
| `Installer`：依赖检查、ISO 校验、磁盘与 UEFI 变量准备、安装执行、安装后指引 | ✅ P4 已建立 |
| `DaemonService` + `wait_for_tcp_port`：start/shutdown 编排、幂等与清理 | ✅ P5 已建立 |
| CLI：CLI11 解析 `--install` / `--iso` / `--virtio-iso` / `--disk` / `--version` / `--help` | ✅ P4 已建立 |
| `lsw` 编译 `src/cli/` 并链接 `lsw_core`；模块目录自动 glob | ✅ P1 已建立 |
| 单元测试：CTest + Catch2（`tests/unit/`） | ✅ P1 已建立 |
| fs / shell / exec 功能模块 | ⏳ 规划中（P6~P9，见 `docs/TASKS.md` 与 `CHANGELOG.md`） |

> 核心库公开接口见 `docs/interfaces/core.md`，配置字段见 `docs/interfaces/config.md`。

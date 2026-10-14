# PROMPT.md

你是 LSW（Linux Subsystem for Windows）项目的资深 Linux 虚拟化 / C++ 开发代理。请严格遵守本提示词、`agent.md` 和 `SKILL.md`。

## 项目目标

开发一个 Linux 命令行工具 `lsw`，使用 KVM/QEMU 运行 Windows 虚拟机，功能尽量接近 WSL，但不提供 Windows ISO 镜像。

技术栈：
- C++26
- CMake
- KVM/QEMU
- QMP 管理虚拟机
- OpenSSH 实现 Linux 终端到 Windows PowerShell 的交互
- SMB/CIFS 实现文件共享
- Unix Domain Socket + JSON 实现 CLI 与守护进程通信
- 文档使用 Markdown + PlantUML/Mermaid

## 必须实现的命令

```bash
lsw --install        # 引导用户安装 Windows ISO
lsw start            # 开启虚拟机服务
lsw                  # 等价于 lsw shell
lsw shell            # 直接进入 PowerShell 终端
lsw test.exe         # 调用 Windows 环境运行 test.exe，暂不支持 GUI
lsw shutdown         # 关闭虚拟机服务
```

## 核心行为

1. `lsw --install`
   - 不提供、不下载 Windows ISO。
   - 交互式提示用户输入合法 Windows ISO 路径和 virtio-win ISO 路径。
   - 检查 `/dev/kvm`、QEMU、OVMF、swtpm 等依赖。
   - 创建 `~/.local/share/lsw/vm/disk.qcow2`。
   - 启动 QEMU 安装模式，显示 GUI，让用户完成 Windows 安装。
   - 安装完成后提示用户在 Windows 内启用 OpenSSH Server、QEMU Guest Agent、SMB，并映射 `Z:`。

2. `lsw start`
   - 启动 `lswd` 守护进程。
   - 通过 QMP 启动 Windows VM。
   - 等待 Windows OpenSSH 端口 `127.0.0.1:2222` 可用。
   - 将 Windows `C$`、`D$` 通过 CIFS 挂载到 `/mnt/Windows/C/`、`/mnt/Windows/D/`。
   - 确保 Windows 内 `Z:` 映射到 Linux `$HOME`。
   - 幂等：已启动时直接返回成功。

3. `lsw` / `lsw shell`
   - 读取用户当前 Linux 工作目录。
   - 如果当前目录是 `$HOME`，进入 PowerShell 默认路径。
   - 如果当前目录在 `$HOME` 下，例如 `/home/user/Project/demo`，映射为 `Z:\Project\demo`。
   - 通过 `exec ssh -t -p 2222 user@127.0.0.1 powershell.exe ...` 直接接管当前终端。
   - 不新开窗口。
   - 如果当前目录不在 `$HOME` 下，报错并提示用户进入 `$HOME` 下的目录，或使用临时复制策略。

4. `lsw test.exe`
   - 将相对路径解析为绝对路径。
   - 如果 exe 在 `$HOME` 下，映射为 `Z:\...`。
   - 如果不在 `$HOME` 下，复制到 `$HOME/.lsw/tmp/<hash>/` 后再映射。
   - 通过 SSH 在 Windows 中执行，捕获 stdout、stderr、退出码。
   - 暂不支持 GUI 程序；如果程序需要 GUI，应提示用户。

5. `lsw shutdown`
   - 优先通过 SSH 执行 Windows 关机。
   - 失败时通过 QMP 发送 `system_powerdown`。
   - 等待 QEMU 退出。
   - 卸载 `/mnt/Windows/C`、`/mnt/Windows/D`。
   - 停止 `lswd`。

## 硬性约束

- 禁止提供、下载、内置 Windows ISO。
- 禁止绕过用户授权或 KVM 权限。
- 禁止明文保存 Windows 密码；使用 libsecret/keyring 或交互输入。
- 禁止在运行中的 Windows 上直接以读写方式挂载其 NTFS 块设备；必须使用 SMB/CIFS。
- `lsw shell` 必须使用 `exec` 替换当前进程，确保终端体验类似 WSL2。
- 所有公开接口、模块依赖、命令行为、QEMU 参数、挂载点变更，必须同步更新 `docs/interfaces/` 和 `docs/diagrams/`。
- 每次任务完成后必须：
  1. 构建通过：`cmake --build build`
  2. 测试通过：`ctest --test-dir build`
  3. 更新 `docs/interfaces/*.md`
  4. 更新 `docs/diagrams/*.puml`
  5. 更新 `docs/CHANGELOG.md`

## 推荐架构

- `lsw`：CLI 前端，解析命令，连接 `lswd`。
- `lswd`：守护进程，管理 QEMU、QMP、SSH、CIFS、状态。
- `lsw_core`：核心库，配置、路径映射、进程、日志、IPC。
- `vm/`：QEMU 生命周期、QMP、安装模式。
- `fs/`：CIFS 挂载、SMB 配置、路径映射。
- `shell/`：PowerShell SSH 桥接。
- `exec/`：Windows 程序执行。
- `docs/`：接口文档、调用关系图、变更记录。

## 输出要求

每次修改代码后，输出：
1. 修改摘要
2. 新增/变更接口
3. 构建与测试结果
4. 已更新的文档路径
5. 未解决问题与风险
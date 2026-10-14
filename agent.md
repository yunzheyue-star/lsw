# agent.md

## 项目使命

LSW 是一个 Linux 上的 Windows 子系统工具，目标是在 Linux 终端内提供接近 WSL 的体验：用 KVM/QEMU 运行 Windows，用 OpenSSH 进入 PowerShell，用 SMB/CIFS 打通文件系统。

## 非目标

- 不提供 Windows ISO。
- 不实现 Windows GUI 程序启动。
- 不修改 Windows 内核。
- 不替代 WSL。
- 不承诺支持所有 Windows 版本；优先 Windows 10/11 Pro/Enterprise。

## 技术栈

- C++26
- CMake >= 3.25
- QEMU/KVM
- QMP
- OpenSSH Client / Windows OpenSSH Server
- SMB/CIFS
- Samba 或 QEMU user SMB
- swtpm（Windows 11 可选）
- CLI11、nlohmann/json、spdlog、fmt
- PlantUML/Mermaid 文档

## 目录结构

```text
lsw/
├── CMakeLists.txt
├── PROMPT.md
├── agent.md
├── SKILL.md
├── cmake/
├── include/lsw/
│   ├── cli/
│   ├── core/
│   ├── vm/
│   ├── fs/
│   ├── shell/
│   ├── exec/
│   ├── ipc/
│   └── util/
├── src/
│   ├── cli/main.cpp
│   ├── daemon/lswd.cpp
│   ├── core/config.cpp
│   ├── core/state.cpp
│   ├── core/process.cpp
│   ├── core/path_map.cpp
│   ├── vm/qemu.cpp
│   ├── vm/qmp.cpp
│   ├── vm/install.cpp
│   ├── fs/cifs_mount.cpp
│   ├── fs/smb_share.cpp
│   ├── shell/powershell.cpp
│   ├── exec/windows_runner.cpp
│   ├── ipc/client.cpp
│   ├── ipc/server.cpp
│   └── util/log.cpp
├── tests/
├── scripts/
│   ├── post-install.ps1
│   └── lsw-mount-helper
├── docs/
│   ├── architecture.md
│   ├── CHANGELOG.md
│   ├── interfaces/
│   │   ├── cli.md
│   │   ├── daemon-ipc.md
│   │   ├── qmp.md
│   │   ├── filesystem.md
│   │   └── config.md
│   └── diagrams/
│       ├── component.puml
│       ├── sequence-install.puml
│       ├── sequence-start.puml
│       ├── sequence-shell.puml
│       ├── sequence-exec.puml
│       └── sequence-shutdown.puml
```

## 核心模块职责

### `lsw` CLI

- 解析：
  - `lsw --install`
  - `lsw start`
  - `lsw`
  - `lsw shell`
  - `lsw test.exe [args...]`
  - `lsw shutdown`
- 与 `lswd` 通过 Unix Socket 通信。
- `lsw shell` 最终 `exec ssh -t`，替换当前进程。

### `lswd` 守护进程

- 管理 QEMU 生命周期。
- 管理 QMP Socket。
- 管理 SSH 端口 `127.0.0.1:2222`。
- 管理 CIFS 挂载 `/mnt/Windows/C`、`/mnt/Windows/D`。
- 管理 Windows 内 `Z:` 映射。
- 对外提供 JSON-RPC 风格 IPC。

### QEMU 模块

- 安装模式：GUI 显示，挂载 Windows ISO 和 virtio-win ISO。
- 运行模式：无显示或后台运行，端口转发 SSH/SMB。
- 使用 QMP 做关机、状态查询、热插拔。
- 可选 swtpm。

### 文件系统模块

- Linux -> Windows：
  - QEMU user SMB 共享 `$HOME`，Windows 映射 `Z:`。
  - 或 Linux 运行 Samba，Windows 映射 `\\10.0.2.2\lsw`。
- Windows -> Linux：
  - Windows 共享 `C$`、`D$`。
  - Linux 通过 `mount.cifs` 挂载到 `/mnt/Windows/C`、`/mnt/Windows/D`。
  - 需要 root 或 helper 权限。

### Shell 模块

- `linux_to_z(cwd, home)`：
  - `cwd == home` -> 默认路径。
  - `cwd` 在 `home` 下 -> `Z:\<relative>`。
  - 否则报错或复制。
- 构造 SSH 命令：
  ```bash
  # $WINDOWS_USER 来自 config.user.windows_user，默认 lsw
  ssh -t -p 2222 "$WINDOWS_USER"@127.0.0.1 -- powershell.exe -NoLogo -NoExit ...
  ```
- 使用 `execvp` 替换当前进程。

### 执行模块

- `lsw test.exe arg1 arg2`
- 映射 exe 到 `Z:`。
- SSH 执行：
  ```powershell
  & 'Z:\path\to\test.exe' arg1 arg2
  exit $LASTEXITCODE
  ```
- 捕获输出和退出码。

## 配置与状态

- 配置：`~/.config/lsw/config.toml`（遵循 `$XDG_CONFIG_HOME`）
- 状态：`~/.local/state/lsw/`（日志、安装标记等，遵循 `$XDG_STATE_HOME`）
- 数据：`~/.local/share/lsw/vm/`（`disk.qcow2`、`OVMF_VARS.fd`，遵循 `$XDG_DATA_HOME`）
- 运行时：`$XDG_RUNTIME_DIR/lsw/`（`lswd.sock`、`qmp.sock`、`qga.sock`、`lswd.pid`）

配置示例：

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
guest_smb_host = "10.0.2.4"
guest_smb_share = "qemu"

[filesystem]
mount_root = "/mnt/Windows"
linux_home_share = "~"
windows_drive_z = "Z:"

[user]
windows_user = "lsw"
```

## 开发命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 编码规范

- C++26。
- 使用 RAII 管理进程、socket、文件描述符。
- 禁止裸 `new/delete`。
- 错误处理使用 `std::expected` 或自定义 `Result<T>`。
- 日志使用 `spdlog`。
- JSON 使用 `nlohmann/json`。
- 外部命令调用必须参数化，避免 shell 注入。
- 路径处理使用 `std::filesystem`。
- 所有 C++ 代码必须通过仓库根目录 `.clang-format` 检查；提交前执行
  `clang-format -i`（对 `include/`、`src/`、`tests/` 下的 `.hpp` / `.cpp`）。
- 每个 C++ 文件必须以 Doxygen 文件头开头（`@file` / `@brief` / `@details`）；
  公共类、结构体、枚举与函数使用 `///` 或 `/** ... */` 写 Doxygen 注释。

## 任务执行清单

每次任务必须执行：

1. 阅读 `agent.md`、`SKILL.md` 和相关 `docs/interfaces`。
2. 修改代码。
3. 构建：
   ```bash
   cmake --build build -j
   ```
4. 测试：
   ```bash
   ctest --test-dir build --output-on-failure
   ```
5. 更新接口文档：
   - `docs/interfaces/cli.md`
   - `docs/interfaces/daemon-ipc.md`
   - `docs/interfaces/qmp.md`
   - `docs/interfaces/filesystem.md`
   - `docs/interfaces/config.md`
6. 更新调用关系图：
   - `docs/diagrams/component.puml`
   - 相关 `sequence-*.puml`
7. 更新 `docs/CHANGELOG.md`。
8. 输出修改摘要、测试结果、文档更新路径、风险。

## Definition of Done

- 代码可构建。
- 单元测试通过。
- 新命令或接口有文档。
- 调用关系图与实际模块一致。
- 不提供 Windows ISO。
- 不引入明文密码。
- `lsw shell` 不新开窗口。
- `lsw test.exe` 返回正确退出码。

## 安全与合规

- 用户必须自备合法 Windows 授权和 ISO。
- 不绕过 KVM 权限。
- CIFS 凭据使用 keyring。
- 挂载 helper 必须最小权限。
- 不在宿主机直接读写运行中的 Windows NTFS 块设备。
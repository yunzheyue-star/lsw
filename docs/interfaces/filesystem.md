# 文件系统接口

## 模块

`fs/`（C++ 命名空间 `lsw::winfs`，SKILL-06、SKILL-07）

实现文件：

| 文件 | 职责 |
| --- | --- |
| `include/lsw/fs/credentials.hpp` + `src/fs/credentials.cpp` | libsecret/keyring 凭据存取 |
| `include/lsw/fs/mount.hpp` + `src/fs/mount.cpp` | CIFS 挂载、卸载、残留检测与重挂 |
| `include/lsw/fs/z_mapping.hpp` + `src/fs/z_mapping.cpp` | Windows 内 `Z:` 映射与校验 |
| `include/lsw/fs/host_smb.hpp` + `src/fs/host_smb.cpp` | 宿主 root Samba（`127.0.0.1:445`）启停管理 |

## 职责

- Linux → Windows：宿主 root Samba 在 `127.0.0.1:445` 共享 `$HOME`，guest 通过 QEMU user-net 网关 `10.0.2.2` 访问；lswd 在启动/关闭时经 helper 启停 smbd，SSH 执行 `net use Z:`。helper 生成的 smb.conf 关闭 oplocks/level2/kernel oplocks，避免 Windows 从 `Z:` 直接执行 `.bat` 时被文件锁卡住。
- Windows → Linux：通过最小权限 helper 把 `C$`、`D$` 挂载到 `/mnt/Windows/C`、`/mnt/Windows/D`。
- 禁止在 Windows 运行中直接读写其 NTFS 块设备；必须使用 SMB/CIFS。

## 端口与挂载点

| 项目 | 值 | 来源 |
| --- | --- | --- |
| SSH 转发 | `127.0.0.1:2222` → guest `22` | `network.ssh_host_port` |
| SMB 转发 | `127.0.0.1:1445` → guest `445` | `network.smb_host_port` |
| host SMB 入口 | guest 访问 `\\10.0.2.2\qemu`；宿主 `127.0.0.1:445` | `network.guest_smb_host`、`network.guest_smb_share` |
| guest 自身 445 | `127.0.0.1:1445` → guest `445`（CIFS 挂载 C$/D$ 用） | `network.smb_host_port` |
| Windows 盘挂载根 | `/mnt/Windows` | `filesystem.mount_root` |
| Linux 家目录映射 | Windows `Z:` → Linux `$HOME` | `filesystem.windows_drive_z`、`filesystem.linux_home_share` |

## 公开接口

### `lsw::winfs::CredentialStore`

```cpp
struct WindowsCredential { std::string username; std::string password; };

class CredentialStore {
  bool available() const;
  Result<std::optional<WindowsCredential>> lookup(std::string_view username) const;
  Status store(const WindowsCredential &credential) const;
  Status erase(std::string_view username) const;
};
```

- 通过 `secret-tool`（libsecret）读写系统 keyring，service 默认 `lsw`。
- `store()` 的密码通过 stdin 传给 `secret-tool`，不写入 argv、配置文件或日志。
- `secret-tool` 不存在返回 `MissingDependency`；keyring 无记录返回 `nullopt`（由上层交互输入）。

### `lsw::winfs::CifsMountManager`

```cpp
struct MountOptions {
  std::filesystem::path mount_root{"/mnt/Windows"};
  std::filesystem::path helper_path{"/usr/local/lib/lsw/lsw-mount-helper"};
  std::string privilege{"sudo"};       // sudo / pkexec / none
  std::string cifs_version{"3.0"};
  std::vector<char> drives{'C', 'D'};
  std::filesystem::path mounts_file{"/proc/mounts"};
  std::chrono::milliseconds timeout{20000};
};

class CifsMountManager {
  Result<std::vector<std::string>> ensure_mounts(const WindowsCredential &);
  Status unmount_all();
  Result<std::vector<MountedDrive>> mounted_drives() const;
  Result<std::vector<std::string>> recover(const WindowsCredential &);
};
```

- `ensure_mounts()`：解析 `/proc/mounts`，已挂载则跳过；未挂载调用 helper；失败先尝试清理残留再重挂一次。`C:` 为必需盘；LSW 默认只创建一块系统盘，因此 guest 没有 `D:` 时 `D$` 会被自动跳过并记录 warning，不影响 `start` 成功。
- `unmount_all()`：卸载挂载根下全部 CIFS 条目，`lsw shutdown` 兜底调用，幂等。
- `mounted_drives()`：返回残留挂载，用于状态展示与异常恢复。

### `lsw::winfs::ZDriveMapper`

```cpp
struct ZMappingOptions {
  std::string ssh_binary{"ssh"};
  std::string host{"127.0.0.1"};
  std::optional<int> port;              // 空 = network.ssh_host_port
  std::chrono::milliseconds timeout{15000};
};

class ZDriveMapper {
  Status ensure_mapped();
  Result<bool> is_mapped();
};
```

- `ensure_mapped()`：先检查 `Test-Path <drive>\`；不存在时执行
  `net use <drive> \\10.0.2.2\qemu /persistent:yes`，再复查。宿主 root
  Samba 支持 SMB2/3 与多会话，不再受 QEMU user SMB1 单会话限制。
- Windows 盘符映射按登录会话隔离：SSH 无头会话里创建的 `Z:` 不一定会出现在另一个新 SSH 会话或交互桌面会话中。因此 `lsw start` 只启动 host Samba 并报告配置，不建立 SMB 会话；`ZDriveMapper` 由真正使用 `Z:` 的 SSH 会话（P6 验收、P7 `lsw shell`、P8 exec）在各自会话内调用，避免提前占用 QEMU 网关的首次会话。
- SSH 使用 `BatchMode=yes`，依赖已配置的免密登录；脚本通过 `-EncodedCommand`（UTF-16LE + Base64）传给 Windows PowerShell，避免 `cmd.exe` 把脚本里的 `|` 当管道拆开。host Samba 的 `[qemu]` 共享对 guest 匿名开放，`net use Z:` 不需要 Windows 密码；Windows 密码仅用于 CIFS 挂载（`mount.cifs`）。stdout/stderr/退出码用于错误提示。
- 配置字段会校验引号/换行等非法字符，防止 PowerShell 注入。

## 挂载 helper 协议

`scripts/lsw-mount-helper` 由 `lswd` 通过 `sudo -n` 或 `pkexec` 调用：

```text
lsw-mount-helper mount <盘符> <CIFS源> <挂载点> <端口> <用户名> <协议版本>
    密码从 stdin 读取（第一行）
lsw-mount-helper unmount <挂载点>
lsw-mount-helper smb_start <共享目录> <共享名> <Linux用户>
lsw-mount-helper smb_stop
```

安全约束：

- 密码只走 stdin；helper 用 `umask 077` + `mktemp` 生成临时凭据文件，`trap` 退出时删除，绝不进入 argv/日志。
- 挂载点必须位于 `/mnt/Windows/` 下，CIFS 源必须是 `//127.0.0.1/...`。
- 固定 `PATH`，参数逐个校验；失败返回码为 mount/umount 原始退出码。

推荐的最小权限配置（二选一）：

```bash
# sudoers（/etc/sudoers.d/lsw，属主 root:root 0440）
lsw ALL=(root) NOPASSWD: /usr/local/lib/lsw/lsw-mount-helper
```

```xml
<!-- polkit（/etc/polkit-1/rules.d/50-lsw-mount.rules） -->
polkit.addRule(function(action, subject) {
  if (action.id == "org.freedesktop.policykit.exec" &&
      subject.isInGroup("lsw") &&
      action.lookup("program") == "/usr/local/lib/lsw/lsw-mount-helper") {
    return polkit.Result.YES;
  }
});
```

> 安装 helper：`install -o root -g root -m 0755 scripts/lsw-mount-helper /usr/local/lib/lsw/lsw-mount-helper`，再配置上面的 sudo/polkit 规则。helper 本身不得 setuid。

## 凭据与安全

- 首次交互：`lsw start` / `lsw mount` 在 TTY 中隐藏回显提示输入密码，密码仅通过 IPC 内存参数传给 `lswd`，不落盘。
- `--save-password` 会把交互输入的密码写入系统 keyring（libsecret）。
- 自动化：可设置 `LSW_WINDOWS_PASSWORD`（仅本次进程环境）配合免交互测试/CI；**禁止**写入配置文件。
- 无 keyring 且非 TTY：返回 `MOUNT_FAILED` 并提示配置 `secret-tool` 或环境变量。

## 环境变量覆盖（测试/特殊环境）

| 环境变量 | 作用 |
| --- | --- |
| `LSW_MOUNT_HELPER` | 覆盖 helper 路径 |
| `LSW_MOUNT_PRIVILEGE` | `sudo` / `pkexec` / `none` |
| `LSW_MOUNT_ROOT` | 覆盖挂载根 |
| `LSW_CIFS_VERSION` | mount.cifs 协议版本 |
| `LSW_MOUNTS_FILE` | 覆盖挂载表路径（测试/容器） |
| `LSW_MOUNT_TIMEOUT_MS` | helper 超时 |
| `LSW_DISABLE_MOUNTS` | `1` 跳过 CIFS 挂载 |
| `LSW_DISABLE_Z_MAPPING` | `1` 跳过 `Z:` 映射 |
| `LSW_SSH_BINARY` | 覆盖 `Z:` 映射使用的 ssh |
| `LSW_WINDOWS_PASSWORD` / `LSW_WINDOWS_USER` | 一次性凭据 |

## 错误码

| 错误 | 说明 | CLI 退出码 |
| --- | --- | --- |
| `MountFailed` | `mount.cifs` 失败（权限、凭据、端口不通） | 8 |
| `UnmountFailed` | 目标正被占用或未挂载 | 8 |
| `ShareSetupFailed` | Windows 内 `net use Z:` 失败 | 8 |
| `PermissionDenied` | 缺少挂载权限或 helper 提权失败 | 8 |
| `MissingDependency` | helper / secret-tool / ssh 缺失 | 3 |

## 恢复策略

1. `start` 前若检测到 `/mnt/Windows/*` 残留 CIFS 挂载：先 `unmount`，再重新挂载。
2. 单盘挂载失败：清理该挂载点后自动重挂一次；仍失败则返回错误并终止本次启动流程。
3. `shutdown` 先卸载全部 CIFS 挂载，再关闭 Windows；卸载失败只记录 warning，不阻塞关机。
4. `lsw mount` / `lsw unmount` 可单独手动恢复。

## 调用关系

`lswd` → `lsw::winfs::CredentialStore`（keyring）→ `CifsMountManager` → `lsw-mount-helper`（sudo/pkexec）→ `mount.cifs`；
`lswd` → `ZDriveMapper` → `ssh` → Windows `net use`。详见 `docs/diagrams/component.puml`、`sequence-start.puml`、`sequence-shutdown.puml`。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 建立接口文档骨架，统一端口、挂载点与安全约束 |
| 未发布 | P6 实现：keyring 凭据、CIFS helper 挂载/卸载/重挂、SSH `Z:` 映射、start/shutdown 集成与错误码 |

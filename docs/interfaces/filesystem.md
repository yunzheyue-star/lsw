# 文件系统接口

## 模块

`fs/`（SKILL-06、SKILL-07）

## 职责

- Linux → Windows：把 Linux `$HOME` 共享给 Windows，并映射为 `Z:`。
- Windows → Linux：把 Windows `C$`、`D$` 挂载到 `/mnt/Windows/C`、`/mnt/Windows/D`。
- 禁止在 Windows 运行中直接读写其 NTFS 块设备。

## 端口与挂载点

| 项目 | 值 | 来源 |
| --- | --- | --- |
| SSH 转发 | `127.0.0.1:2222` → guest `22` | `network.ssh_host_port` |
| SMB 转发 | `127.0.0.1:1445` → guest `445` | `network.smb_host_port` |
| QEMU user SMB 地址 | `10.0.2.4`，共享名 `qemu` | `network.guest_smb_host`、`network.guest_smb_share` |
| Windows 盘挂载根 | `/mnt/Windows` | `filesystem.mount_root` |
| Linux 家目录映射 | Windows `Z:` → Linux `$HOME` | `filesystem.windows_drive_z` |

## 方案

### Windows 映射 `Z:`（优先 QEMU user SMB）

QEMU 启动参数：

```bash
-netdev user,id=net0,...,smb=$HOME
```

Windows 内（`scripts/post-install.ps1` 引导执行）：

```powershell
net use Z: \\10.0.2.4\qemu /persistent:yes
```

备选：Linux 运行 Samba，Windows 映射 `\\10.0.2.2\lsw`。

### Linux 挂载 Windows 盘

```bash
sudo mkdir -p /mnt/Windows/C /mnt/Windows/D

sudo mount -t cifs //127.0.0.1/C$ /mnt/Windows/C \
  -o port=1445,username=<user>,vers=3.0

sudo mount -t cifs //127.0.0.1/D$ /mnt/Windows/D \
  -o port=1445,username=<user>,vers=3.0
```

卸载使用 `umount /mnt/Windows/C /mnt/Windows/D`，由 `lsw shutdown` 兜底执行。

## 输入

- `Config` 中的网络与文件系统字段。
- CIFS 凭据：通过 libsecret/keyring 获取，或交互输入；**禁止明文保存**。

## 输出

- 挂载点可用，`Z:` 映射在 Windows 内持久化。
- 路径映射函数：`linux_to_z(cwd, home)`（见 SKILL-04）。

## 错误码

| 错误 | 说明 |
| --- | --- |
| `MountFailed` | `mount.cifs` 失败（权限、凭据、端口不通） |
| `UnmountFailed` | 目标正被占用或未挂载 |
| `ShareSetupFailed` | Windows 内 `net use Z:` 失败 |
| `PermissionDenied` | 缺少挂载权限或未通过 helper 提权 |

## 依赖

`mount.cifs`、QEMU user SMB 或 Samba、libsecret/keyring、`scripts/lsw-mount-helper`（最小权限 helper）。

## 调用关系

`lswd` → `fs/` → 内核 CIFS；`lswd` → SSH → Windows `net use`；见 `docs/diagrams/component.puml`、`sequence-start.puml`。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 建立接口文档骨架，统一端口、挂载点与安全约束 |

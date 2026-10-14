# 配置接口

## 模块

`core/config`

## 职责

加载、校验并提供 LSW 的 TOML 配置；为进程内其他模块提供只读配置对象。

## 路径

`$CONFIG` = `${XDG_CONFIG_HOME:-$HOME/.config}/lsw/config.toml`

运行时会自动创建配置目录与默认配置；不存在时使用下表的默认值。

## 配置示例

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

## 字段说明

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `vm.name` | string | `lsw-windows` | QEMU 进程名 |
| `vm.memory_mb` | int | `8192` | 内存（MB） |
| `vm.cpus` | int | `4` | vCPU 数量 |
| `vm.disk` | path | `~/.local/share/lsw/vm/disk.qcow2` | 系统盘 |
| `vm.windows_iso` | path | `""` | 用户自备的 Windows ISO，安装时交互填写 |
| `vm.virtio_iso` | path | `""` | 用户自备的 virtio-win ISO |
| `network.ssh_host_port` | int | `2222` | 宿主机 SSH 转发端口 |
| `network.smb_host_port` | int | `1445` | 宿主机 SMB 转发端口 |
| `network.guest_smb_host` | string | `10.0.2.4` | QEMU user SMB 网关 |
| `network.guest_smb_share` | string | `qemu` | SMB 共享名 |
| `filesystem.mount_root` | path | `/mnt/Windows` | Windows 盘挂载根 |
| `filesystem.linux_home_share` | path | `~` | 共享给 Windows 的 Linux 目录 |
| `filesystem.windows_drive_z` | string | `Z:` | Windows 内映射的盘符 |
| `user.windows_user` | string | `lsw` | Windows SSH 用户名；`PROMPT.md` 示例中的 `user` 即此字段 |

## 输入 / 输出

- 输入：配置文件路径（可被 CLI 参数覆盖，后续阶段定义）。
- 输出：校验后的 `Config` 结构；字段缺失时补默认值，非法值返回错误。

## 错误码

| 错误 | 说明 |
| --- | --- |
| `ConfigNotFound` | 指定路径不存在（使用默认值时不算错误） |
| `ConfigParseError` | TOML 语法错误 |
| `ConfigInvalid` | 字段类型或取值非法（如端口越界） |
| `UnsupportedSchema` | 配置版本不受支持 |

## 依赖

TOML 解析库（`toml++`）、`std::filesystem`、`Result<T>`。

## 调用关系

`lsw`、`lswd`、`vm/`、`fs/` 均通过 `core/config` 读取配置。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 建立接口文档骨架，字段与默认值对齐 `agent.md` |

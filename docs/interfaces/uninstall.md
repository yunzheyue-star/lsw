# `lsw uninstall` / `lsw --uninstall` 卸载接口

> 实现状态：P13 扩展功能；默认只删除虚拟机，保留配置、helper 与依赖，便于重装。

## 命令

```bash
lsw uninstall [--yes] [--purge | --keep] [--force] [--dry-run]
lsw --uninstall [--yes] [--purge | --keep] [--force] [--dry-run]
```

两者完全等价。

| 参数 | 说明 |
| --- | --- |
| 无参数 | 交互：确认删除 VM → 询问是否连带清理配置/依赖/组件（默认否） |
| `--yes` | 跳过“确认删除 VM”；无 `--purge/--keep` 时仍会询问清理模式（非 TTY 默认只删 VM） |
| `--keep` | 只删除 VM，保留配置、helper、依赖，便于重装 |
| `--purge` | 连带清理配置、helper、数据/状态目录与 `lsw init` 记录的受管包 |
| `--force` | 允许删除 LSW 数据目录之外的磁盘（危险） |
| `--dry-run` | 只打印计划，不停止 VM、不删文件、不卸载包 |

`--purge` 与 `--keep` 互斥。

## 默认（--keep）删除范围

- VM 磁盘 `config.vm.disk`
- `$DATA/lsw/vm/OVMF_VARS.fd`
- `$STATE/lsw/install.json` 安装标记
- `$STATE/lsw/guest.json` guest 缓存

保留：

- `$CONFIG/lsw/config.toml`
- `/usr/local/lib/lsw/lsw-mount-helper` 与 `/etc/sudoers.d/lsw`
- 系统依赖包（QEMU/Samba/cifs-utils/OVMF/OpenSSH 等）
- 日志（`$STATE/lsw/lsw.log`）

## 彻底清理（--purge）额外范围

- `$CONFIG/lsw/config.toml`
- `$DATA/lsw`、`$STATE/lsw`、`$RUNTIME/lsw`
- `/usr/local/lib/lsw/lsw-mount-helper`、`/etc/sudoers.d/lsw`
- `$STATE/lsw/managed-packages.json` 中记录的包：由 `lsw init` 实际安装并记录的包才会被卸载；未记录的包只打印手动命令，避免误删用户其它软件依赖。

包管理器卸载命令：

| 包管理器 | 命令 |
| --- | --- |
| pacman | `pacman -R --noconfirm <packages>` |
| apt-get | `apt-get remove --auto-remove -y <packages>` |
| dnf | `dnf remove -y <packages>` |

## 运行中 VM

- 若 `lswd` 正在运行，先经 IPC `shutdown` 正常关机并等待退出；
- 若 daemon 未运行但检测到进程仍在使用磁盘，拒绝删除并提示先 `lsw shutdown`；
- 不会直接 kill QEMU，避免 Windows 文件系统未正常关机。

## 安全约束

- 默认只删除 `$DATA/lsw` 内的磁盘；外部路径必须显式 `--force`；
- 默认不卸载系统包；`--purge` 也只卸载 `lsw init` 记录过的受管包；
- `--purge` 会删除配置文件，之后 `lsw init` 会重新生成默认配置；
- 不触碰 Windows ISO、Windows 授权文件与用户其它虚拟机磁盘。

## 退出码

| 退出码 | 含义 |
| --- | --- |
| `0` | 成功 / 用户取消 / dry-run |
| `2` | 参数错误（`--purge` + `--keep`、多余位置参数、外部磁盘未加 `--force` 等） |
| `8` | 停止 VM、删除文件、package/helper 卸载失败 |
| `3` | 缺少 sudo/pkexec 或未知包管理器 |

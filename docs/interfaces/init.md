# `lsw init` 初始化接口

> 实现状态：P12 扩展功能；命令 `lsw init [--yes] [--dry-run]`。

## 目的

在首次使用 LSW 前初始化宿主环境：

1. 识别 Linux 发行版与包管理器；
2. 检查系统依赖/组件，已有项直接跳过；
3. 对缺失项经用户确认后调用包管理器安装；
4. 生成 `$CONFIG/lsw/config.toml`（若不存在），并记录实际安装的包到 `$STATE/lsw/managed-packages.json`，供 `lsw uninstall --purge` 精确卸载；
5. 提示 Windows 侧组件需在 VM 启动后执行。

## 组件与检测

| 组件 | 检测方式 | 必需 |
| --- | --- | --- |
| `/dev/kvm` 权限 | `access("/dev/kvm", R_OK\|W_OK)` | 是（无法自动修复，需加入 `kvm` 组并重新登录） |
| QEMU | `qemu-system-x86_64` | 是 |
| qemu-img | `qemu-img` | 是 |
| OVMF | `OVMF_CODE` + `OVMF_VARS` 常见路径 | 是 |
| Samba | `smbd` | 是 |
| cifs-utils | `mount.cifs` | 是 |
| OpenSSH 客户端 | `ssh` | 是 |
| swtpm | `swtpm` | 否（Windows 11 TPM） |
| libsecret | `secret-tool` | 否（keyring 保存密码） |
| LSW helper | `/usr/local/lib/lsw/lsw-mount-helper` 可执行 | 是 |

## 包名映射

| 组件 | Arch (pacman) | Debian/Ubuntu (apt) | Fedora/RHEL (dnf) |
| --- | --- | --- | --- |
| QEMU | `qemu-full` | `qemu-system-x86` `qemu-utils` | `qemu-kvm` `qemu-img` |
| OVMF | `edk2-ovmf` | `ovmf` | `edk2-ovmf` |
| Samba | `samba` | `samba` | `samba` |
| cifs-utils | `cifs-utils` | `cifs-utils` | `cifs-utils` |
| OpenSSH 客户端 | `openssh` | `openssh-client` | `openssh-clients` |
| swtpm | `swtpm` | `swtpm` | `swtpm` |
| libsecret | `libsecret` | `libsecret-tools` | `libsecret` |

未知发行版不会自动安装，只输出包名与手动命令。

## 执行流程

```text
lsw init
  → ensure_directories + 生成/保留 config.toml
  → detect_package_manager (/etc/os-release + 命令回退)
  → plan：逐组件检测，收集缺失包
  → 打印计划（sudo pacman/apt/dnf ...）
  → --dry-run 到此为止；--yes 跳过确认；否则 [Y/n]
  → 安装缺失包（sudo/pkexec，继承终端）
  → 安装 helper + /etc/sudoers.d/lsw（如缺失）
  → 复检：必需组件全部满足则成功
```

## 交互与安全

- 默认每条安装计划都会先展示，用户确认后才执行；
- `--yes` 用于自动化，非交互环境必须配合 `--yes`；
- `--dry-run` 只检测、只打印，不写配置、不执行安装；
- 不下载/内置 Windows ISO；不修改 Windows；
- 不绕过 KVM 权限；`/dev/kvm` 不可访问时只给出 `usermod -aG kvm` 提示；
- helper 安装写入 `/usr/local/lib/lsw/lsw-mount-helper` 与 `/etc/sudoers.d/lsw`，需 root 提权。

## 退出码

| 退出码 | 含义 |
| --- | --- |
| `0` | 成功 / dry-run / 用户取消 |
| `2` | 参数错误（非交互缺 `--yes`、多余位置参数等） |
| `3` | 未知包管理器、安装失败或仍有必需组件缺失 |
| `8` | 提权/helper 安装失败 |

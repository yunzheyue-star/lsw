# CLI 接口

## 命令

### `lsw --install`

引导安装 Windows。

参数：
- 无参数时交互式询问 ISO 路径。
- `--iso <path>`：Windows ISO。
- `--virtio-iso <path>`：virtio-win ISO。
- `--disk <path>`：qcow2 路径。

退出码：
- 0 成功
- 2 参数错误
- 3 依赖缺失
- 8 QEMU 错误

### `lsw start`

启动虚拟机服务。幂等。

### `lsw` / `lsw shell`

进入 PowerShell。自动映射当前目录到 `Z:`。

### `lsw test.exe [args...]`

在 Windows 中运行 exe。不支持 GUI。

### `lsw shutdown`

关闭虚拟机服务。
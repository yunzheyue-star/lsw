# QEMU / QMP 接口

## 模块

`vm/qmp`（SKILL-03）、`vm/qemu`（SKILL-02）

## 职责

- `QmpClient`：通过 QEMU Monitor Protocol 控制虚拟机（状态查询、关机、热插拔）。
- `QemuVm`：构建 QEMU 启动参数、后台进程生命周期、状态查询与优雅关闭。

## 实现状态

P3 已落地：QMP 握手/命令/事件跳过/错误分类、运行与安装两种启动参数、进程 RAII、`query-status` 状态映射、`system_powerdown` + 信号兜底的关闭流程、swtpm 可选参数。P4/P5 负责磁盘创建、依赖检查与 lswd 编排。

## QmpClient 公开接口

```cpp
struct QmpOptions {
  std::filesystem::path socket_path;
  std::chrono::milliseconds connect_timeout{5000};
  std::chrono::milliseconds io_timeout{5000};
};

class QmpClient {
public:
  Result<void> connect();                       // 等待 socket + greeting + qmp_capabilities
  Result<json> execute(std::string_view command, const json& args = {});
  Result<void> system_powerdown();
  Result<bool> query_status();                  // running / paused → true
  void close() noexcept;
  bool connected() const noexcept;
};
```

实现要点：

- Unix Domain Socket + 行分隔 JSON；持久接收缓冲，正确处理一次 `recv` 含多行的情况。
- 自动跳过异步事件（`{"event": ...}`），按 `id` 匹配响应。
- 连接阶段在 `connect_timeout` 内重试等待 socket 出现。

## QemuVm 公开接口

```cpp
enum class VmStatus { kStopped, kRunning, kPaused, kUnknown };

struct QemuVmOptions {
  std::filesystem::path qemu_binary{"qemu-system-x86_64"};
  std::filesystem::path ovmf_code;   // 空 = 自动探测
  std::filesystem::path ovmf_vars;   // 空 = $DATA/vm/OVMF_VARS.fd
  std::filesystem::path disk_image;  // 空 = config.vm.disk 或 $DATA/vm/disk.qcow2
  std::filesystem::path home_dir;    // 空 = 当前 $HOME
  bool enable_kvm{true};             // false → accel=tcg（无 KVM 环境/测试）
  bool enable_firmware{true};        // false → 不挂 pflash（测试替身）
  bool enable_swtpm{false};
  std::filesystem::path swtpm_binary{"swtpm"};
  std::string machine{"q35"};
  std::chrono::milliseconds shutdown_timeout{30000};
  std::chrono::milliseconds qmp_connect_timeout{5000};
};

class QemuVm {
public:
  QemuVm(Config config, XdgPaths paths, QemuVmOptions options = {});
  Result<void> start();          // -display none
  Result<void> start_install();  // -display gtk + Windows ISO / virtio ISO
  Result<VmStatus> status();     // 进程存活 + QMP query-status
  Result<void> shutdown();       // QMP system_powerdown → SIGTERM → SIGKILL
  Result<int> wait();
  Status kill();
  bool running();
  int pid() const noexcept;
};
```

## 安装流程（Installer，P4）

```cpp
struct InstallOptions {
  std::filesystem::path windows_iso, virtio_iso, disk_image;
  std::uint64_t disk_size_gb{64};
  std::filesystem::path kvm_device, qemu_binary, qemu_img_binary;
  std::filesystem::path ovmf_code, ovmf_vars_template, swtpm_binary;
  bool require_kvm{true}, require_swtpm{false}, interactive{true};
  bool overwrite_disk{false};
  QemuVmOptions qemu_options;
};

class Installer {
public:
  Result<DependencyReport> check_dependencies() const;
  Status validate_isos();
  Status prepare_storage();
  Result<InstallResult> run();
  std::vector<std::string> post_install_guidance() const;
};
```

- 依赖检查区分必选（QEMU / qemu-img / OVMF）与可选（swtpm）；缺失信息聚合为可执行的修复建议。
- `prepare_storage()` 幂等：已存在的 `disk.qcow2` 与 `OVMF_VARS.fd` 不覆盖。
- `run()` 依次执行依赖检查 → ISO 校验 → 存储准备 → `QemuVm::start_install()` → `wait()`；QEMU 非零退出映射为 `kQemuError`。

## 启动参数

| 模式 | 关键参数 |
| --- | --- |
| 公共 | `-name <vm.name>`、`-smp`、`-m`、`-drive file=$DATA/vm/disk.qcow2,if=virtio,format=qcow2` |
| 加速 | KVM：`-enable-kvm -machine q35,accel=kvm -cpu host`；TCG：`-machine q35,accel=tcg -cpu max` |
| 固件 | `-drive if=pflash,format=raw,readonly=on,file=<OVMF_CODE>`、`-drive if=pflash,format=raw,file=$DATA/vm/OVMF_VARS.fd` |
| 网络 | `-netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22,hostfwd=tcp:127.0.0.1:1445-:445`；检测到 `smbd` 时追加 `smb=$HOME`（QEMU user SMB 需要 Samba），否则跳过并警告 |
| QMP / QGA | `-qmp unix:$RUNTIME/lsw/qmp.sock,server=on,wait=off`、`-chardev socket,path=$RUNTIME/lsw/qga.sock,...` |
| 运行模式 | `-display none` |
| 安装模式 | Windows ISO + virtio-win ISO（`media=cdrom,readonly=on`）、`-display gtk` |

## OVMF 固件探测

`detect_ovmf_code()` / `detect_ovmf_vars_template()` 按以下候选路径探测：

| 发行版 / 包 | 典型路径 |
| --- | --- |
| Arch `edk2-ovmf` | `/usr/share/edk2/x64/OVMF_CODE.4m.fd`、`OVMF_VARS.4m.fd` |
| Debian / Ubuntu `ovmf` | `/usr/share/OVMF/OVMF_CODE.fd`、`OVMF_VARS.fd`（含 `_4M` 变体） |
| Fedora `edk2-ovmf` | `/usr/share/edk2/ovmf/OVMF_CODE.fd`、`OVMF_VARS.fd` |
| 其他 | `/usr/share/ovmf/x64/`、`/usr/share/qemu/` 下同名文件 |

`Installer::check_dependencies()` 会按发行版给出安装提示：Arch 用
`sudo pacman -S edk2-ovmf`，Debian/Ubuntu 用 `sudo apt install ovmf`，
Fedora 用 `sudo dnf install edk2-ovmf`。

## swtpm（可选）

- `swtpm_available()` / `find_executable()` 检测二进制；不可用时启用返回 `kMissingDependency`。
- 启用后 QEMU 追加 `-chardev socket,id=chrtpm,... -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis,tpmdev=tpm0`，并由 `QemuVm` 启动 `swtpm socket --tpm2 --tpmstate dir=$DATA/vm/tpm ...`。
- Windows 10 不需要；Windows 11 建议启用（`PROMPT.md` 中 swtpm 为可选依赖）。

## Socket 与协议

| 项目 | 值 |
| --- | --- |
| 路径 | `$RUNTIME/lsw/qmp.sock`（默认 `$XDG_RUNTIME_DIR/lsw/qmp.sock`） |
| 传输 | Unix Domain Socket，行分隔 JSON（每条消息一行，以 `\n` 结束） |
| 握手 | 连接后必须先执行 `qmp_capabilities` |
| 消息格式 | 请求 `{ "execute": "...", "arguments": { ... }, "id": "..." }`；响应 `{ "return": ... }` 或 `{ "error": { "class": "...", "desc": "..." } }` |

示例：

```json
{ "execute": "qmp_capabilities" }
{ "execute": "query-status" }
{ "execute": "system_powerdown" }
```

## 错误码

| 场景 | `ErrorCode` |
| --- | --- |
| socket 不存在、连接/握手/读写失败、QMP `error` 对象 | `kQemuError`（IPC `QMP_ERROR`） |
| `/dev/kvm`、OVMF、swtpm 缺失 | `kMissingDependency`（CLI 退出码 3） |
| 安装模式缺少 Windows ISO | `kConfigInvalid`（CLI 退出码 2） |
| QEMU 未启动时查询/关闭 | 返回 `kStopped`（关闭幂等成功） |

## 测试

- `tests/unit/test_qmp_client.cpp`：假 QEMU 上验证握手、事件跳过、错误分类、读取超时、socket 缺失。
- `tests/unit/test_qemu_args.cpp`：运行/安装参数、OVMF 校验、KVM/TCG、swtpm 拼接。
- `tests/unit/test_qemu_vm.cpp`：假 QEMU 完整生命周期（start → status → shutdown/kill）。
- `tests/unit/test_child_process.cpp`：后台进程 RAII、输出重定向、超时。
- `tests/integration/fake_qemu.cpp`、`tests/integration/qmp_probe.cpp`：测试替身与真实 QEMU QMP 探针。

## 依赖

`nlohmann/json`、Unix Domain Socket、`core/process`（`ChildProcess`）、`core/config`、`util/paths`、`Result<T>`。

## 调用关系

`lswd` → `QemuVm` → `QmpClient` → QEMU；安装/启动时序见 `docs/diagrams/sequence-install.puml`、`sequence-start.puml`、`sequence-shutdown.puml`。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | 建立接口文档骨架，定义 socket 路径与协议格式 |
| 未发布 | P3 实现：QmpClient、QemuVm 生命周期、启动参数、swtpm 可选、状态映射与错误分类 |

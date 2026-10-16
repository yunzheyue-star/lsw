# LSW 任务清单（TASKS）

> 版本基线：阶段 0 完成后
> 权重口径：按「工作量 × 风险 × 依赖深度」综合估算，全部任务合计 **100%**，代表相对投入占比，不代表工期承诺。
> 状态图例：`[x]` 已完成 ｜ `[~]` 进行中 ｜ `[ ]` 未开始
> 每项任务完成后必须执行强制动作：构建、`ctest`、更新受影响的 `docs/interfaces` 与 `docs/diagrams`、更新 `docs/CHANGELOG.md`、在本文档更新状态。

---

## 状态维护分工（自 P5 起）

- **主人**：只维护「总览」表的「状态」列。
- **开发代理**：维护其余内容——阶段明细状态、当前进度、里程碑、验收方式、权重与说明。

## 总览

| 阶段 | 名称 | 权重 | 状态 |
| --- | --- | ---: | :---: |
| P0 | 工程基线 | 5% | [x] |
| P1 | `lsw_core` 核心库 | 15% | [x] |
| P2 | IPC 与 `lswd` 骨架 | 10% | [x] |
| P3 | QEMU / QMP | 15% | [x] |
| P4 | `lsw --install` | 10% | [x] |
| P5 | `lsw start` / `lsw shutdown` | 10% | [x] |
| P6 | 文件系统（Z: / CIFS） | 12% | [x] |
| P7 | `lsw shell` 桥接 | 8% | [x] |
| P8 | `lsw test.exe` 执行 | 5% | [ ] |
| P9 | 系统测试与 1.0 收口 | 10% | [ ] |
| **合计** | | **100%** | 当前 **85%** |

## 权重分布

```text
P1  lsw_core        ███████████████ 15%
P3  QEMU / QMP      ███████████████ 15%
P6  文件系统        ████████████    12%
P2  IPC / lswd      ██████████      10%
P4  --install       ██████████      10%
P5  start/shutdown  ██████████      10%
P9  测试与收口      ██████████      10%
P7  shell           ████████         8%
P0  工程基线        █████            5%
P8  test.exe        █████            5%
```

## 关键路径

```text
P0 → P1 → P2 → P3 ─┬─→ P4（安装链路）────────────────┐
                   └─→ P5（启停）→ P6（文件系统）→ P7（shell）→ P8（exec）→ P9（收口）
```

- P4 与 P5/P6 在 P3 完成后可并行推进（P4 依赖真机安装环境，P5/P6 可先用既有磁盘调试）。
- P7 必须在 P5（SSH 端口就绪）与 P6（`Z:` 映射）之后才能闭环验收。
- P8 复用 P7 的 SSH 通道，顺序不得提前。

## 里程碑

| 里程碑 | 内容 | 完成标志 | 累计权重 |
| --- | --- | --- | ---: |
| ✅ M0 | 工程基线 | CMake C++26 可构建，CTest 通过，文档基线齐全 | 5% |
| ✅ M1 | 核心库可用 | `lsw_core` 单测全绿，配置/路径/进程接口冻结 | 20% |
| ✅ M2 | VM 可控 | `lswd` 能通过 QMP 启停 Mock/真实 QEMU | 45% |
| ✅ M3 | 安装可用 | `lsw --install` 真机走通安装模式 | 55% |
| M4 | 日常可用 | `start` / `shell` / `test.exe` / `shutdown` 闭环 | 90% |
| M5 | 1.0 发布 | DoD 全勾选，文档与渲染图一致，打 `v1.0.0` | 100% |

---

## P0 · 工程基线（5%，已完成）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P0-1 | CMake C++26 基线 | `CMakeLists.txt`：最低 3.25、`CMAKE_CXX_STANDARD 26`、构建通过 | — | 1% | [x] |
| P0-2 | 源文件自动识别 | 按模块 glob `src/{cli,core,util}/**/*.cpp` + `CONFIGURE_DEPENDS`，新增文件免登记 | P0-1 | 1% | [x] |
| P0-3 | 测试与目录骨架 | `tests/` + CTest `lsw_smoke`；`include/`、`scripts/`、`cmake/` 就位 | P0-1 | 1% | [x] |
| P0-4 | 文档与规范基线 | `architecture.md`、≥5 份接口文档、≥6 张图；统一路径与 SSH 用户 | P0-1 | 2% | [x] |

### P0 验收方式（一键）

```bash
bash scripts/verify-p0.sh
```

预期：全部检查通过并返回退出码 `0`；脚本内部会执行一次临时目录的干净构建与 `ctest`（1/1 通过），并验证新增 `.cpp` 免登记。手工核对入口：`docs/architecture.md`、`docs/interfaces/*.md`、`docs/diagrams/*.puml`。

## P1 · `lsw_core` 核心库（15%，已完成，待验收）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P1-1 | 依赖接入 | `cmake/Dependencies.cmake`：CLI11、nlohmann/json、spdlog、fmt、toml++、Catch2；支持 FetchContent 与系统包，离线可构建 | P0 | 2% | [x] |
| P1-2 | `Result<T>` 与错误码 | `core/error.hpp`；统一错误分类与 CLI 退出码映射；单测覆盖成功/失败路径 | P1-1 | 3% | [x] |
| P1-3 | 日志与 XDG 路径 | `util/paths` + spdlog 初始化；`$CONFIG/$DATA/$STATE/$RUNTIME` 解析与创建；XDG 环境变量覆盖单测 | P1-1 | 2% | [x] |
| P1-4 | 配置加载与校验 | `core/config`：TOML 解析、默认值、非法值报错、生成模板；单测对齐 `docs/interfaces/config.md` | P1-3 | 3% | [x] |
| P1-5 | 路径映射 `linux_to_z` | `core/path_map`：`$HOME`/`$HOME` 下/`$HOME` 外三态；空格、中文、深层路径单测 | P1-3 | 2% | [x] |
| P1-6 | 进程 RAII 封装 | `core/process`：参数化 spawn、stdout/stderr/退出码捕获、超时与回收；无 shell 注入；单测 | P1-2 | 2% | [x] |
| P1-7 | `lsw_core` 目标 | 静态库目标 + 单测接入 CTest；`lsw` 改为链接 `lsw_core` | P1-1~6 | 1% | [x] |

### P1 验收方式（一键）

```bash
bash scripts/verify-p1.sh
```

预期：`-std=c++26` 编译参数生效、`lsw_core` 构建成功、`ctest` 全部通过（33/33 = 1 冒烟 + 32 单元）。手工核对：`docs/interfaces/core.md`、`docs/interfaces/config.md`、`tests/unit/*.cpp`。

## P2 · IPC 与 `lswd` 骨架（10%）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P2-1 | IPC 协议与编解码 | `ipc/protocol.hpp`：请求/响应/错误结构、JSON Lines、`id` 匹配；roundtrip 单测 | P1 | 3% | [ ] |
| P2-2 | IPC Server | `$RUNTIME/lsw/lswd.sock` listen、多连接、权限 0700、优雅关闭；集成测试 | P2-1 | 2% | [ ] |
| P2-3 | IPC Client | 连接/超时/重试、请求 `id`、错误转 `Result`；socketpair 单测 | P2-1 | 2% | [ ] |
| P2-4 | `lswd` 进程骨架 | daemon main、`lswd.pid` 单实例锁、信号处理、socket 清理、`status` 命令 | P2-2 | 2% | [ ] |
| P2-5 | 幂等与并发骨架 | `start`/`mount`/`unmount` 幂等；并发重复调用串行化测试 | P2-4 | 1% | [ ] |

### P2 验收方式（一键）

```bash
bash scripts/verify-p2.sh
```

预期：`lswd` 构建成功，`ctest` 全部通过（47/47 = 1 冒烟 + 46 单元），真实 lswd 进程可查询 `status`、`mount`，未知命令与 `shell` 返回正确错误码，第二实例被单实例锁拒绝，退出后 socket/PID 清理。手工核对：`docs/interfaces/daemon-ipc.md`、`tests/unit/test_ipc.cpp`、`tests/integration/ipc_probe.cpp`。

## P3 · QEMU / QMP（15%）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P3-1 | `QmpClient` | 连接、`qmp_capabilities` 握手、`execute`、错误分类；mock socket 单测 | P1、P2 | 5% | [ ] |
| P3-2 | `QemuVm` 运行模式 | QEMU 参数（q35/KVM/virtio/OVMF/hostfwd 2222+1445/QMP）、`-display none`、进程持有；`Z:` 由宿主 root Samba + guest `10.0.2.2` 提供 | P3-1 | 4% | [ ] |
| P3-3 | 状态查询与回收 | `query-status`、进程 wait/僵尸回收、异常退出检测、`VmStatus` | P3-2 | 2% | [ ] |
| P3-4 | 安装模式参数 | Windows ISO + virtio ISO + OVMF pflash + `-display gtk`，与运行模式参数复用 | P3-2 | 2% | [ ] |
| P3-5 | swtpm 可选支持 | 检测与参数拼接；不影响 Win10；Win11 文档说明 | P3-4 | 2% | [ ] |

### P3 验收方式（一键）

```bash
bash scripts/verify-p3.sh
```

预期：`lsw_core` 构建成功，`ctest` 全部通过（66/66 = 1 冒烟 + 65 单元/集成）；假 QEMU 覆盖 QMP 握手/状态/关机/错误/超时与 QemuVm 生命周期；若本机有真 QEMU，额外验证 `query-status` 与 `quit`。手工核对：`docs/interfaces/qmp.md`、`tests/unit/test_qmp_client.cpp`、`tests/unit/test_qemu_vm.cpp`、`tests/integration/fake_qemu.cpp`。

## P4 · `lsw --install`（10%，已完成）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P4-1 | 依赖检查 | `/dev/kvm` 可访问、`qemu-system-x86_64`、OVMF_CODE/VARS、swtpm；缺失退出码 3 + 修复指引 | P3 | 2% | [x] |
| P4-2 | ISO 交互与校验 | `--iso` / `--virtio-iso` 或交互输入；存在/可读/类型校验；不下载、不内置 | P4-1 | 2% | [x] |
| P4-3 | 磁盘与 UEFI 变量 | `qemu-img create` 到 `$DATA/vm/disk.qcow2`；复制 OVMF_VARS；重复执行不覆盖 | P4-1 | 2% | [x] |
| P4-4 | 安装模式执行 | GUI 正常显示、等待 QEMU 退出、异常退出码 8、安装完成检测 | P3-4、P4-3 | 2% | [x] |
| P4-5 | 安装后引导 | `scripts/post-install.ps1`（OpenSSH/QGA/SMB/`Z:` 检查）+ CLI 指引 + 文档更新 | P4-4 | 2% | [x] |

### P4 验收方式（一键）

```bash
bash scripts/verify-p4.sh
```

预期：全部 C++ 文件通过 `.clang-format` 检查；`ctest` 全部通过（72/72）；`lsw --version` / `lsw --help` 正常；依赖缺失时退出码 3 并给出修复建议；假 QEMU 安装探针完成 `Installer::run()`，创建 `disk.qcow2`、复制 `OVMF_VARS.fd` 并输出安装后指引。手工核对：`docs/interfaces/cli.md`、`scripts/post-install.ps1`、`tests/unit/test_install.cpp`。

## P5 · `lsw start` / `lsw shutdown`（10%，已完成）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P5-1 | `start` 编排 | 拉起 `lswd` → QMP 启动 VM → 轮询 2222（超时/退避）→ 失败映射 6/8 | P3-3、P4 | 3% | [x] |
| P5-2 | 幂等与并发 | 已启动直接返回成功；并发 `start` 串行化；测试覆盖 | P5-1 | 2% | [x] |
| P5-3 | `shutdown` | SSH 关机优先，失败走 QMP `system_powerdown`，等待 QEMU 退出 | P5-1 | 3% | [x] |
| P5-4 | 清理 | 卸载 `/mnt/Windows/{C,D}`、清理 `$RUNTIME/lsw/*`、停止 daemon；重复调用幂等 | P5-3 | 2% | [x] |

### P5 验收方式（一键）

```bash
bash scripts/verify-p5.sh
```

预期：`ctest` 全部通过（83/83）；假 QEMU 下 `lswd + lsw start` 返回 running、重复 start 幂等、`lsw shutdown` 后 lswd 退出并清理 socket/PID；`clang-format` 与 Doxygen 文件头检查通过。手工核对：`docs/interfaces/cli.md`、`docs/interfaces/daemon-ipc.md`、`tests/unit/test_daemon_service.cpp`。

## P6 · 文件系统（Z: / CIFS）（12%，已完成）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P6-1 | Windows 映射 `Z:` | 宿主 root Samba（`127.0.0.1:445`）共享 `$HOME`；guest 经 `10.0.2.2` 执行 `net use Z:`；`Set-Location Z:\...` 验证 | P5-1 | 4% | [x] |
| P6-2 | Linux 挂载 `C$`/`D$` | `mkdir` + `mount.cifs port=1445,vers=3.0`；挂载点可读写 | P5-1 | 3% | [x] |
| P6-3 | 凭据安全 | libsecret 存储/读取；无 keyring 时交互输入；禁止明文落盘与日志 | P6-2 | 2% | [x] |
| P6-4 | 挂载 helper | `scripts/lsw-mount-helper` 最小权限方案（sudoers/polkit，setuid 需审计） | P6-2 | 2% | [x] |
| P6-5 | 异常恢复 | 残留挂载检测、失败重挂、`shutdown` 兜底卸载、`MOUNT_FAILED` 映射 | P6-2 | 1% | [x] |

### P6 验收方式（一键）

```bash
bash scripts/verify-p6.sh
```

预期：`ctest` 全部通过（≥100）；假 helper / 假 ssh / 假 QEMU 下 `lsw start` 返回 `mounts` 与 `z_drive`，`shutdown` 前调用卸载并清理；残留挂载重挂、keyring/环境凭据、`MountFailed` 映射均有单测覆盖；`clang-format` 与 Doxygen 文件头全部通过。

### P6 真机验收记录

- 2026-10-16：`bash scripts/lsw-p6-real-test.sh` 真机 **30/30 通过，0 失败，0 跳过**；`Z: → \\10.0.2.2\qemu` 可 `Set-Location` 并列出 Linux 家目录；`/mnt/Windows/C` 读写通过。
- 架构结论与定位过程详见 [`docs/P6-回顾.md`](P6-回顾.md)。

## P7 · `lsw shell` 桥接（8%，已完成）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P7-1 | shell IPC | CLI 上报 `cwd`；daemon 返回 `ssh_args`；`$HOME` 外返回 `PATH_OUTSIDE_HOME`；VM 未运行返回 `VM_NOT_RUNNING` | P2、P6 | 2% | [x] |
| P7-2 | SSH 命令构造 | `ssh -t -p 2222 windows_user@127.0.0.1 -- powershell.exe -NoLogo -NoExit -EncodedCommand ...`；参数不经 shell 拼接；编码脚本内含 Z: 映射 + Set-Location | P7-1 | 2% | [x] |
| P7-3 | `execvp` 接管终端 | 替换当前进程、不新开窗口、退出后回原 shell、退出码透传；交互式验收 | P7-2 | 3% | [x] |
| P7-4 | 边界场景 | `$HOME` 默认路径、空格/中文/深层目录、`$HOME` 外提示（PATH_OUTSIDE_HOME）；临时复制策略留待 P8 | P7-3 | 1% | [x] |

### P7 验收方式（一键）

```bash
bash scripts/verify-p7.sh
```

预期：构建与 `ctest` 通过；shell 单测覆盖 `$HOME`/子目录/越界/ssh argv；假 QEMU + 假 ssh E2E 验证 `lsw shell` 与裸 `lsw` 均以 `execvp` 执行，`EncodedCommand` 内含 `net use Z:` 与 `Set-Location Z:\Project\demo`，`$HOME` 外返回退出码 5。

### P7 真机交互验收

1. `lsw start` 后执行 `cd ~/Project/demo && lsw`，PowerShell 应位于 `Z:\Project\demo`；
2. 在 `~` 执行 `lsw`，应进入 PowerShell 默认路径；
3. 在 `$HOME` 外执行 `lsw`，应返回 `PATH_OUTSIDE_HOME`（退出码 5）；
4. `exit` 退出 PowerShell 后应回到原 Linux shell，且不新开窗口。

### P7 真机验收记录

- 2026-10-16：主人真机交互验收通过（子目录进入 `Z:\...`、`$HOME` 默认路径、`$HOME` 外退出码 5、`exit` 回原 shell、不新开窗口）；
- 自动化：`bash scripts/verify-p7.sh` **36/36 通过**，`ctest` **108/108 通过**；P4/P5/P6 回归仍全绿。


## P8 · `lsw test.exe` 执行（5%）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P8-1 | 路径解析与临时复制 | 绝对化、`$HOME` 判定、`$HOME` 外复制到 `~/.lsw/tmp/<hash>/`、清理策略 | P1 | 2% | [ ] |
| P8-2 | SSH 执行与透传 | `& 'Z:\path\test.exe' args; exit $LASTEXITCODE`；stdout/stderr/退出码/超时 | P7、P8-1 | 2% | [ ] |
| P8-3 | GUI 提示与错误码 | GUI 程序明确提示不支持；`0/5/6/7` 映射并与文档一致 | P8-2 | 1% | [ ] |

## P9 · 系统测试与 1.0 收口（10%）

| 编号 | 任务 | 交付物 / 验收标准 | 依赖 | 权重 | 状态 |
| --- | --- | --- | --- | ---: | :---: |
| P9-1 | 单元测试补全 | core / ipc / qmp（mock QEMU）覆盖率达标；`ctest` 全绿 | P1~P8 | 3% | [ ] |
| P9-2 | 集成 / E2E | 真机 QEMU 验收 5 个核心命令；`--install` 人工步骤清单脚本化 | P4~P8 | 3% | [ ] |
| P9-3 | 文档全量对齐 | 接口 / 图 / CHANGELOG / TASKS 状态一致；PlantUML 实际渲染复核 | P9-2 | 2% | [ ] |
| P9-4 | 安全与合规审计 | 不提供 ISO、无明文密码、helper 最小权限、无 shell 注入、不越权 KVM | P6、P9-2 | 1% | [ ] |
| P9-5 | DoD 终验与发布 | 勾选 `agent.md` Definition of Done；打 `v1.0.0`；CHANGELOG 归档 | P9-4 | 1% | [ ] |

---

## 执行规约

1. **先阅读**：动工前读 `agent.md`、对应 SKILL 技能与相关 `docs/interfaces`。
2. **先测试后交付**：每项任务结束必须 `cmake --build build -j` + `ctest --test-dir build --output-on-failure`。
3. **文档同源**：代码/接口/端口/挂载点/命令行为变更，必须同步 interfaces、diagrams、CHANGELOG、TASKS 状态。
4. **权重调整**：只有完成阶段复盘后才可调整权重，且必须在 `docs/CHANGELOG.md` 记录原因。
5. **阻塞上报**：遇到依赖缺失或需主人裁决（如真实 QEMU 环境、权限方案）时，标记 `[~]` 并说明阻塞点，不静默跳过。

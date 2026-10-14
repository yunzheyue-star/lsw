# lsw_core 接口

## 模块

`include/lsw/{core,util}` · `src/{core,util}`（静态库目标 `lsw_core`）

## 职责

为 CLI、守护进程与各功能模块提供统一地基：错误类型、XDG 路径、日志、配置、路径映射与进程执行。

## 公开接口

### 错误与结果（`lsw/core/error.hpp`）

```cpp
enum class ErrorCode { kOk = 0, kInternal = 1, /* ... */ };

template <typename T>
using Result = std::expected<T, Error>;

using Status = Result<void>;

[[nodiscard]] std::unexpected<Error> fail(ErrorCode code, std::string message);
[[nodiscard]] int to_exit_code(ErrorCode code) noexcept;
```

### XDG 路径（`lsw/util/paths.hpp`）

```cpp
struct PathEnvironment { std::optional<fs::path> home, xdg_config_home, ...; };
struct XdgPaths { fs::path config_file, data_dir, state_dir, runtime_dir, log_file; };

Result<XdgPaths> resolve_paths(const PathEnvironment& env);
Status ensure_directories(const XdgPaths& paths);
Result<fs::path> expand_tilde(const fs::path& path, const PathEnvironment& env);
```

| 字段 | 默认位置 | 说明 |
| --- | --- | --- |
| `config_file` | `$XDG_CONFIG_HOME/lsw/config.toml` | 缺省 `${HOME}/.config` |
| `data_dir` | `$XDG_DATA_HOME/lsw` | `vm/disk.qcow2`、`vm/OVMF_VARS.fd` |
| `state_dir` | `$XDG_STATE_HOME/lsw` | 日志、安装标记 |
| `runtime_dir` | `$XDG_RUNTIME_DIR/lsw` | socket / PID；缺失时回退 `/tmp/lsw-runtime-<uid>` |
| `log_file` | `$STATE/lsw/lsw.log` | rotating 日志 |

`ensure_directories` 创建全部目录，并将 `runtime_dir` 权限收敛为 `0700`。

### 日志（`lsw/util/log.hpp`）

```cpp
Status log::init(const fs::path& log_file, spdlog::level::level_enum = spdlog::level::info);
std::shared_ptr<spdlog::logger> log::logger();
void log::shutdown() noexcept;
```

### 配置（`lsw/core/config.hpp`）

字段、默认值与校验规则见 `docs/interfaces/config.md`。

### 路径映射（`lsw/core/path_map.hpp`）

| 输入 | 行为 |
| --- | --- |
| `cwd == home` | `map_cwd_to_z` 返回空串（PowerShell 默认路径），`linux_to_z` 返回 nullopt |
| `cwd` 在 `home` 下 | 返回 `Z:\<相对路径>`，保留空格与中文 |
| 其他 | `map_cwd_to_z` 返回 `kPathMappingFailed`，`linux_to_z` 返回 nullopt |

相似前缀（`/home/u2` 相对 `/home/u`）与 `..` 逃逸均判定为不在 `$HOME` 内。

### 进程执行（`lsw/core/process.hpp`）

```cpp
struct RunOptions { std::optional<fs::path> cwd; std::chrono::milliseconds timeout{0}; };
struct ProcessResult { int exit_code; std::string stdout_data, stderr_data; };

Result<ProcessResult> run_process(const std::vector<std::string>& argv,
                                  const RunOptions& options = {});
```

- `fork` + `execvp`，参数不经 shell，无注入风险。
- 捕获 stdout / stderr / 退出码；被信号终止时 `exit_code = 128 + signal`。
- 超时返回 `kProcessTimeout` 并强制回收子进程；无法执行返回 `kProcessError`。

## 错误码 → CLI 退出码

| 错误码 | 退出码 |
| --- | --- |
| `kOk` | 0 |
| `kInvalidArgument`、`kConfig*` | 2 |
| `kMissingDependency` | 3 |
| `kPathMappingFailed` | 5 |
| `kSshFailed` | 6 |
| `kProgramFailed` | 7 |
| `kQemuError` | 8 |
| `kInternal`、`kIoError`、`kProcess*` | 1 |

## 依赖

| 依赖 | 用途 | 解析方式 |
| --- | --- | --- |
| CLI11 | CLI 解析（P2 起使用） | 系统包 → FetchContent |
| nlohmann/json | IPC/QMP JSON | 系统包 → FetchContent |
| spdlog | 日志 | 系统包 → FetchContent |
| fmt | 格式化 | 系统包 → FetchContent |
| toml++ | 配置解析 | 系统包 → FetchContent |
| Catch2 | 单元测试 | 系统包 → FetchContent |

`cmake/Dependencies.cmake` 先 `find_package`，缺失且 `LSW_FETCH_DEPS=ON`（默认）时 FetchContent；`LSW_FETCH_DEPS=OFF` 可强制只用系统包。

## 调用关系

`lsw` → `lsw_core`；后续 `lswd` 与功能模块复用同一核心库。见 `docs/diagrams/component.puml`。

## 变更记录

| 版本 | 变更 |
| --- | --- |
| 未发布 | P1 建立：错误码、XDG 路径、日志、配置、路径映射、进程执行与 32 例单元测试 |

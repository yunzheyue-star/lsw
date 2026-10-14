/**
 * @file paths.hpp
 * @brief XDG 路径解析、目录创建与 ~ 展开。
 * @details 文件位置：include/lsw/util/paths.hpp（头文件）。
 *          属于 LSW 工具库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <lsw/core/error.hpp>

namespace lsw {

namespace fs = std::filesystem;

/// @brief 解析 XDG 路径所需的环境快照。测试可直接构造，不必修改进程环境。
struct PathEnvironment {
    std::optional<fs::path> home;
    std::optional<fs::path> xdg_config_home;
    std::optional<fs::path> xdg_data_home;
    std::optional<fs::path> xdg_state_home;
    std::optional<fs::path> xdg_runtime_dir;

    [[nodiscard]] static PathEnvironment from_process();
};

/// @brief LSW 的全局路径集合，规则见 docs/architecture.md「路径规范」。
struct XdgPaths {
    fs::path config_file; ///< $CONFIG/lsw/config.toml
    fs::path data_dir;    ///< $DATA/lsw（disk.qcow2、OVMF_VARS.fd）
    fs::path state_dir;   ///< $STATE/lsw（日志、安装标记）
    fs::path runtime_dir; ///< $RUNTIME/lsw（socket、PID）
    fs::path log_file;    ///< $STATE/lsw/lsw.log
};

/// @brief 按 XDG 规范解析路径；HOME 缺失返回 kInvalidArgument。
/// XDG_RUNTIME_DIR 缺失时回退到系统临时目录下的
/// lsw-runtime-<uid>（仅无会话环境）。
[[nodiscard]] Result<XdgPaths> resolve_paths(const PathEnvironment &env);

/// @brief 创建全部目录；runtime_dir 权限收敛为 0700。
[[nodiscard]] Status ensure_directories(const XdgPaths &paths);

/// @brief 展开开头的 `~` 与 `~/`；其他路径原样返回。HOME 缺失时返回错误。
[[nodiscard]] Result<fs::path> expand_tilde(const fs::path &path,
                                            const PathEnvironment &env);

} // namespace lsw

/**
 * @file paths.cpp
 * @brief paths 实现（工具库）。
 * @details 文件位置：src/util/paths.cpp（源文件）。
 *          属于 LSW 工具库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <lsw/util/paths.hpp>

#include <cstdlib>
#include <system_error>
#include <unistd.h>

namespace lsw {
namespace {

/// @brief 从进程环境读取可选路径变量。
/// @param name 环境变量名。
/// @return 变量存在且非空时返回路径；否则返回 std::nullopt。
std::optional<fs::path> env_path(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return fs::path(value);
}

} // namespace

/// @brief 从当前进程环境构造路径解析所需的快照。
/// @return 包含 HOME 与各 XDG 环境变量的路径快照。
PathEnvironment PathEnvironment::from_process() {
    PathEnvironment env;
    env.home = env_path("HOME");
    env.xdg_config_home = env_path("XDG_CONFIG_HOME");
    env.xdg_data_home = env_path("XDG_DATA_HOME");
    env.xdg_state_home = env_path("XDG_STATE_HOME");
    env.xdg_runtime_dir = env_path("XDG_RUNTIME_DIR");
    return env;
}

/// @brief 根据 XDG 规范解析 LSW 使用的全部路径。
/// @param env 路径解析所需的环境快照。
/// @return 解析后的路径集合；HOME 缺失时返回 kInvalidArgument。
Result<XdgPaths> resolve_paths(const PathEnvironment &env) {
    if (!env.home.has_value() || env.home->empty()) {
        return fail(ErrorCode::kInvalidArgument,
                    "HOME 未设置，无法解析 XDG 路径");
    }

    const fs::path &home = *env.home;
    XdgPaths paths;
    paths.config_file =
        env.xdg_config_home.value_or(home / ".config") / "lsw" / "config.toml";
    paths.data_dir = env.xdg_data_home.value_or(home / ".local/share") / "lsw";
    paths.state_dir =
        env.xdg_state_home.value_or(home / ".local/state") / "lsw";
    if (env.xdg_runtime_dir.has_value() && !env.xdg_runtime_dir->empty()) {
        paths.runtime_dir = *env.xdg_runtime_dir / "lsw";
    } else {
        paths.runtime_dir = fs::temp_directory_path() /
                            ("lsw-runtime-" + std::to_string(::getuid()));
    }
    paths.log_file = paths.state_dir / "lsw.log";
    return paths;
}

/// @brief 创建全部 LSW 目录并收紧运行时目录权限。
/// @param paths 待创建的路径集合。
/// @return 成功返回空 Status；创建或设置权限失败返回 kIoError。
Status ensure_directories(const XdgPaths &paths) {
    try {
        fs::create_directories(paths.config_file.parent_path());
        fs::create_directories(paths.data_dir);
        fs::create_directories(paths.state_dir);
        fs::create_directories(paths.runtime_dir);
        fs::permissions(paths.runtime_dir, fs::perms::owner_all,
                        fs::perm_options::replace);
        return {};
    } catch (const fs::filesystem_error &error) {
        return fail(ErrorCode::kIoError,
                    std::string("创建 LSW 目录失败: ") + error.what());
    }
}

/// @brief 展开路径开头的 ~ 与 ~/；其他路径原样返回。
/// @param path 待展开的路径。
/// @param env 提供 HOME 的环境快照。
/// @return 展开后的路径；需要 HOME 但缺失时返回 kInvalidArgument。
Result<fs::path> expand_tilde(const fs::path &path,
                              const PathEnvironment &env) {
    const std::string text = path.string();
    if (text == "~") {
        if (!env.home.has_value() || env.home->empty()) {
            return fail(ErrorCode::kInvalidArgument,
                        "路径包含 ~，但 HOME 未设置");
        }
        return *env.home;
    }
    if (text.starts_with("~/")) {
        if (!env.home.has_value() || env.home->empty()) {
            return fail(ErrorCode::kInvalidArgument,
                        "路径包含 ~，但 HOME 未设置");
        }
        return *env.home / text.substr(2);
    }
    return path;
}

} // namespace lsw

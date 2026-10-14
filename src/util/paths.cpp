/**
 * @file paths.cpp
 * @brief paths 实现（工具库）。
 * @details 文件位置：src/util/paths.cpp（源文件）。
 *          属于 LSW 工具库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 */

#include <lsw/util/paths.hpp>

#include <cstdlib>
#include <system_error>
#include <unistd.h>

namespace lsw {
namespace {

std::optional<fs::path> env_path(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return fs::path(value);
}

} // namespace

PathEnvironment PathEnvironment::from_process() {
    PathEnvironment env;
    env.home = env_path("HOME");
    env.xdg_config_home = env_path("XDG_CONFIG_HOME");
    env.xdg_data_home = env_path("XDG_DATA_HOME");
    env.xdg_state_home = env_path("XDG_STATE_HOME");
    env.xdg_runtime_dir = env_path("XDG_RUNTIME_DIR");
    return env;
}

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

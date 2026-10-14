#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include <lsw/util/paths.hpp>

#include "test_support.hpp"

using lsw::ensure_directories;
using lsw::ErrorCode;
using lsw::expand_tilde;
using lsw::PathEnvironment;
using lsw::resolve_paths;

TEST_CASE("默认 XDG 路径基于 HOME", "[core][paths]") {
    PathEnvironment env;
    env.home = "/home/alice";

    const auto paths = resolve_paths(env);
    REQUIRE(paths.has_value());
    CHECK(paths->config_file == "/home/alice/.config/lsw/config.toml");
    CHECK(paths->data_dir == "/home/alice/.local/share/lsw");
    CHECK(paths->state_dir == "/home/alice/.local/state/lsw");
    CHECK(paths->log_file == paths->state_dir / "lsw.log");
    CHECK(paths->runtime_dir.string().find("lsw-runtime-") !=
          std::string::npos);
}

TEST_CASE("显式 XDG 变量优先", "[core][paths]") {
    PathEnvironment env;
    env.home = "/home/alice";
    env.xdg_config_home = "/custom/config";
    env.xdg_data_home = "/custom/data";
    env.xdg_state_home = "/custom/state";
    env.xdg_runtime_dir = "/run/user/1000";

    const auto paths = resolve_paths(env);
    REQUIRE(paths.has_value());
    CHECK(paths->config_file == "/custom/config/lsw/config.toml");
    CHECK(paths->data_dir == "/custom/data/lsw");
    CHECK(paths->state_dir == "/custom/state/lsw");
    CHECK(paths->runtime_dir == "/run/user/1000/lsw");
}

TEST_CASE("HOME 缺失返回错误", "[core][paths]") {
    const PathEnvironment env;
    const auto paths = resolve_paths(env);
    REQUIRE_FALSE(paths.has_value());
    CHECK(paths.error().code == ErrorCode::kInvalidArgument);
}

TEST_CASE("ensure_directories 创建目录并收敛运行时权限", "[core][paths]") {
    const lsw::test::TempDir temp("paths");
    PathEnvironment env;
    env.home = temp.path();
    env.xdg_runtime_dir = temp.path() / "run";

    const auto paths = resolve_paths(env);
    REQUIRE(paths.has_value());
    REQUIRE(ensure_directories(*paths).has_value());

    CHECK(std::filesystem::is_directory(paths->config_file.parent_path()));
    CHECK(std::filesystem::is_directory(paths->data_dir));
    CHECK(std::filesystem::is_directory(paths->state_dir));
    CHECK(std::filesystem::is_directory(paths->runtime_dir));

    const auto permissions =
        std::filesystem::status(paths->runtime_dir).permissions();
    CHECK((permissions & std::filesystem::perms::owner_all) !=
          std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::group_all) ==
          std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::others_all) ==
          std::filesystem::perms::none);
}

TEST_CASE("expand_tilde 展开 HOME", "[core][paths]") {
    PathEnvironment env;
    env.home = "/home/alice";

    CHECK(expand_tilde("~", env).value() == "/home/alice");
    CHECK(expand_tilde("~/Project", env).value() == "/home/alice/Project");
    CHECK(expand_tilde("/absolute/path", env).value() == "/absolute/path");
    CHECK(expand_tilde("relative/path", env).value() == "relative/path");

    env.home.reset();
    const auto missing = expand_tilde("~/Project", env);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::kInvalidArgument);
}

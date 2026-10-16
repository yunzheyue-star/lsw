/**
 * @file test_uninstall.cpp
 * @brief test_uninstall 实现：卸载计划、受管包与只删 VM 流程测试。
 * @details 文件位置：tests/unit/test_uninstall.cpp（单元测试）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <lsw/core/bootstrap.hpp>
#include <lsw/core/config.hpp>
#include <lsw/core/uninstall.hpp>

#include "test_support.hpp"

using lsw::Config;
using lsw::XdgPaths;
using lsw::bootstrap::PackageManager;
using lsw::uninstall::Options;
using lsw::uninstall::plan;
using lsw::uninstall::run;

namespace {

/// @brief 构造隔离的 XDG 路径。
/// @param temp 临时目录。
/// @return 已创建的 XDG 路径。
XdgPaths make_paths(const lsw::test::TempDir &temp) {
    lsw::PathEnvironment env;
    env.home = temp.path() / "home";
    env.xdg_config_home = temp.path() / "config";
    env.xdg_data_home = temp.path() / "data";
    env.xdg_state_home = temp.path() / "state";
    env.xdg_runtime_dir = temp.path() / "run";
    auto paths = lsw::resolve_paths(env);
    REQUIRE(paths.has_value());
    REQUIRE(lsw::ensure_directories(*paths).has_value());
    return *paths;
}

/// @brief 判断路径列表中是否包含指定路径。
/// @param list 路径列表。
/// @param path 目标路径。
/// @return 包含返回 true。
bool contains(const std::vector<std::filesystem::path> &list,
              const std::filesystem::path &path) {
    return std::find(list.begin(), list.end(), path) != list.end();
}

/// @brief 创建测试用 VM 文件。
/// @param paths XDG 路径。
/// @return 磁盘路径。
std::filesystem::path create_vm_files(const XdgPaths &paths) {
    const auto disk = paths.data_dir / "vm" / "disk.qcow2";
    std::filesystem::create_directories(disk.parent_path());
    std::ofstream(disk).close();
    std::ofstream(paths.data_dir / "vm" / "OVMF_VARS.fd").close();
    std::filesystem::create_directories(paths.state_dir);
    std::ofstream(paths.state_dir / "install.json") << "{}";
    std::ofstream(paths.state_dir / "guest.json") << "{}";
    return disk;
}

} // namespace

TEST_CASE("uninstall：默认计划只清理 VM 专属文件", "[uninstall]") {
    lsw::test::TempDir temp("uninstall-plan");
    const auto paths = make_paths(temp);
    const auto disk = create_vm_files(paths);
    Config config = lsw::default_config();
    config.vm.disk = disk;

    auto planned = plan(config, paths, Options{});
    REQUIRE(planned.has_value());
    CHECK(planned->disk == disk);
    CHECK(contains(planned->vm_files, disk));
    CHECK(contains(planned->vm_files, paths.data_dir / "vm" / "OVMF_VARS.fd"));
    CHECK(contains(planned->vm_files, paths.state_dir / "install.json"));
    CHECK(contains(planned->vm_files, paths.state_dir / "guest.json"));
    CHECK(contains(planned->purge_files, paths.config_file));
    CHECK(planned->packages.empty());
}

TEST_CASE("uninstall：外部磁盘需要 --force", "[uninstall]") {
    lsw::test::TempDir temp("uninstall-external");
    const auto paths = make_paths(temp);
    const auto external = temp.path() / "other" / "disk.qcow2";
    Config config = lsw::default_config();
    config.vm.disk = external;

    auto refused = plan(config, paths, Options{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == lsw::ErrorCode::kInvalidArgument);

    auto forced = plan(config, paths, Options{.force = true});
    REQUIRE(forced.has_value());
    CHECK(forced->disk == external);
}

TEST_CASE("uninstall：读取 lsw init 记录的受管包", "[uninstall]") {
    lsw::test::TempDir temp("uninstall-managed");
    const auto paths = make_paths(temp);
    Config config = lsw::default_config();
    config.vm.disk = paths.data_dir / "vm" / "disk.qcow2";
    create_vm_files(paths);
    REQUIRE(lsw::bootstrap::record_managed_packages(
                paths, PackageManager::kPacman, {"qemu-full", "samba"})
                .has_value());

    auto planned = plan(config, paths, Options{.purge = true});
    REQUIRE(planned.has_value());
    CHECK(planned->package_manager == "pacman");
    CHECK(planned->packages == std::vector<std::string>{"qemu-full", "samba"});
}

TEST_CASE("uninstall：--keep --yes 只删 VM 文件并保留配置", "[uninstall]") {
    lsw::test::TempDir temp("uninstall-keep");
    const auto paths = make_paths(temp);
    Config config = lsw::default_config();
    config.vm.disk = paths.data_dir / "vm" / "disk.qcow2";
    const auto disk = create_vm_files(paths);
    std::filesystem::create_directories(paths.config_file.parent_path());
    std::ofstream(paths.config_file) << lsw::to_toml(config);

    REQUIRE(run(paths, Options{.assume_yes = true, .keep = true}).has_value());
    CHECK_FALSE(std::filesystem::exists(disk));
    CHECK_FALSE(
        std::filesystem::exists(paths.data_dir / "vm" / "OVMF_VARS.fd"));
    CHECK_FALSE(std::filesystem::exists(paths.state_dir / "install.json"));
    CHECK(std::filesystem::exists(paths.config_file));
}

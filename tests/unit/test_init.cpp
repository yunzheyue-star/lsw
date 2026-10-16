/**
 * @file test_init.cpp
 * @brief test_init 实现：`lsw init` 包管理器识别、安装命令与配置初始化测试。
 * @details 文件位置：tests/unit/test_init.cpp（单元测试）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <lsw/core/bootstrap.hpp>

#include "test_support.hpp"

using lsw::XdgPaths;
using lsw::bootstrap::detect_package_manager;
using lsw::bootstrap::install_command;
using lsw::bootstrap::PackageManager;

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

/// @brief 写入 os-release 测试文件。
/// @param path 文件路径。
/// @param id 发行版 ID。
void write_os_release(const std::filesystem::path &path,
                      const std::string &id) {
    std::ofstream(path) << "ID=" << id << "\n";
}

} // namespace

TEST_CASE("init：包管理器识别", "[init]") {
    lsw::test::TempDir temp("init-distro");
    const auto arch = temp.path() / "arch-release";
    const auto ubuntu = temp.path() / "ubuntu-release";
    const auto fedora = temp.path() / "fedora-release";
    write_os_release(arch, "arch");
    write_os_release(ubuntu, "ubuntu");
    write_os_release(fedora, "fedora");

    CHECK(detect_package_manager(arch) == PackageManager::kPacman);
    CHECK(detect_package_manager(ubuntu) == PackageManager::kApt);
    CHECK(detect_package_manager(fedora) == PackageManager::kDnf);
}

TEST_CASE("init：包安装命令构造", "[init]") {
    const std::vector<std::string> packages{"samba", "swtpm"};
    CHECK(
        install_command(PackageManager::kPacman, packages) ==
        std::vector<std::string>{"pacman", "-S", "--needed", "samba", "swtpm"});
    CHECK(
        install_command(PackageManager::kApt, packages) ==
        std::vector<std::string>{"apt-get", "install", "-y", "samba", "swtpm"});
    CHECK(install_command(PackageManager::kDnf, packages) ==
          std::vector<std::string>{"dnf", "install", "-y", "samba", "swtpm"});
    CHECK(install_command(PackageManager::kUnknown, packages).empty());
}

TEST_CASE("init：配置文件缺失时生成默认配置", "[init]") {
    lsw::test::TempDir temp("init-config");
    const auto paths = make_paths(temp);
    std::filesystem::remove(paths.config_file);
    REQUIRE(lsw::bootstrap::initialize_config(paths).has_value());

    std::ifstream input(paths.config_file);
    REQUIRE(input.good());
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    CHECK(content.find("[vm]") != std::string::npos);
    CHECK(content.find("[user]") != std::string::npos);
    CHECK(content.find("windows_user") != std::string::npos);
}

TEST_CASE("init：已有配置不被覆盖", "[init]") {
    lsw::test::TempDir temp("init-keep");
    const auto paths = make_paths(temp);
    std::filesystem::create_directories(paths.config_file.parent_path());
    const std::string marker = "# keep-me\n[user]\nwindows_user = \"custom\"\n";
    std::ofstream(paths.config_file) << marker;

    REQUIRE(lsw::bootstrap::initialize_config(paths).has_value());
    std::ifstream input(paths.config_file);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    CHECK(content == marker);
}

TEST_CASE("init：检查计划包含组件与包管理器", "[init]") {
    lsw::test::TempDir temp("init-plan");
    const auto paths = make_paths(temp);
    auto plan = lsw::bootstrap::plan(paths);
    REQUIRE(plan.has_value());
    CHECK_FALSE(plan->manager_name.empty());
    CHECK(plan->components.size() >= 10);
    bool has_qemu = false;
    bool has_kvm = false;
    for (const auto &component : plan->components) {
        has_qemu = has_qemu || component.id == "qemu";
        has_kvm = has_kvm || component.id == "kvm";
    }
    CHECK(has_qemu);
    CHECK(has_kvm);
}

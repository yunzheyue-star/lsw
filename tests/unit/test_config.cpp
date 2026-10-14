/**
 * @file test_config.cpp
 * @brief test_config 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_config.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <lsw/core/config.hpp>
#include <lsw/util/paths.hpp>

#include "test_support.hpp"

using lsw::Config;
using lsw::ErrorCode;
using lsw::PathEnvironment;

namespace {

constexpr std::string_view kSample = R"toml(
[vm]
name = "my-windows"
memory_mb = 4096
cpus = 2
disk = "~/vm/disk.qcow2"
windows_iso = "/iso/win11.iso"
virtio_iso = "~/iso/virtio.iso"

[network]
ssh_host_port = 2200
smb_host_port = 1500
guest_smb_host = "10.0.2.4"
guest_smb_share = "qemu"

[filesystem]
mount_root = "/mnt/Windows"
linux_home_share = "~"
windows_drive_z = "Z:"

[user]
windows_user = "alice"
)toml";

PathEnvironment test_env() {
    PathEnvironment env;
    env.home = "/home/alice";
    return env;
}

} // namespace

TEST_CASE("默认配置与文档一致", "[core][config]") {
    const Config config = lsw::default_config();
    CHECK(config.vm.name == "lsw-windows");
    CHECK(config.vm.memory_mb == 8192);
    CHECK(config.vm.cpus == 4);
    CHECK(config.vm.disk == "~/.local/share/lsw/vm/disk.qcow2");
    CHECK(config.network.ssh_host_port == 2222);
    CHECK(config.network.smb_host_port == 1445);
    CHECK(config.network.guest_smb_host == "10.0.2.4");
    CHECK(config.network.guest_smb_share == "qemu");
    CHECK(config.filesystem.mount_root == "/mnt/Windows");
    CHECK(config.filesystem.linux_home_share == "~");
    CHECK(config.filesystem.windows_drive_z == "Z:");
    CHECK(config.user.windows_user == "lsw");
}

TEST_CASE("解析用户配置并展开 ~", "[core][config]") {
    const auto config = lsw::parse_config(kSample, test_env());
    REQUIRE(config.has_value());
    CHECK(config->vm.name == "my-windows");
    CHECK(config->vm.memory_mb == 4096);
    CHECK(config->vm.cpus == 2);
    CHECK(config->vm.disk == "/home/alice/vm/disk.qcow2");
    CHECK(config->vm.windows_iso == "/iso/win11.iso");
    CHECK(config->vm.virtio_iso == "/home/alice/iso/virtio.iso");
    CHECK(config->network.ssh_host_port == 2200);
    CHECK(config->network.smb_host_port == 1500);
    CHECK(config->user.windows_user == "alice");
}

TEST_CASE("缺失字段回落到默认值", "[core][config]") {
    const auto config =
        lsw::parse_config("[user]\nwindows_user = \"bob\"\n", test_env());
    REQUIRE(config.has_value());
    CHECK(config->vm.memory_mb == 8192);
    CHECK(config->network.ssh_host_port == 2222);
    CHECK(config->user.windows_user == "bob");
}

TEST_CASE("端口越界被拒绝", "[core][config]") {
    const auto config =
        lsw::parse_config("[network]\nssh_host_port = 70000\n", test_env());
    REQUIRE_FALSE(config.has_value());
    CHECK(config.error().code == ErrorCode::kConfigInvalid);
}

TEST_CASE("类型错误被拒绝", "[core][config]") {
    const auto config =
        lsw::parse_config("[vm]\ncpus = \"four\"\n", test_env());
    REQUIRE_FALSE(config.has_value());
    CHECK(config.error().code == ErrorCode::kConfigInvalid);
}

TEST_CASE("语法错误映射为解析错误", "[core][config]") {
    const auto config = lsw::parse_config("[vm\nname = ", test_env());
    REQUIRE_FALSE(config.has_value());
    CHECK(config.error().code == ErrorCode::kConfigParseError);
}

TEST_CASE("配置文件缺失时按 allow_missing 处理", "[core][config]") {
    const lsw::test::TempDir temp("config");
    const auto missing = temp.path() / "no-such.toml";

    const auto defaults = lsw::load_config(missing, true, test_env());
    REQUIRE(defaults.has_value());
    CHECK(defaults->vm.name == "lsw-windows");

    const auto required = lsw::load_config(missing, false, test_env());
    REQUIRE_FALSE(required.has_value());
    CHECK(required.error().code == ErrorCode::kConfigNotFound);
}

TEST_CASE("从文件加载配置", "[core][config]") {
    const lsw::test::TempDir temp("config-load");
    const auto file = temp.path() / "config.toml";
    {
        std::ofstream out(file);
        out << kSample;
    }

    const auto config = lsw::load_config(file, false, test_env());
    REQUIRE(config.has_value());
    CHECK(config->vm.name == "my-windows");
}

TEST_CASE("to_toml 可往返解析", "[core][config]") {
    Config original = lsw::default_config();
    original.vm.cpus = 8;
    original.user.windows_user = "alice";

    const auto parsed = lsw::parse_config(lsw::to_toml(original), test_env());
    REQUIRE(parsed.has_value());
    CHECK(parsed->vm.name == original.vm.name);
    CHECK(parsed->vm.cpus == 8);
    CHECK(parsed->network.smb_host_port == original.network.smb_host_port);
    CHECK(parsed->filesystem.mount_root == original.filesystem.mount_root);
    CHECK(parsed->user.windows_user == "alice");
    CHECK(parsed->vm.disk == "/home/alice/.local/share/lsw/vm/disk.qcow2");

    const auto defaults =
        lsw::parse_config(lsw::default_config_toml(), test_env());
    REQUIRE(defaults.has_value());
    CHECK(defaults->vm.memory_mb == lsw::default_config().vm.memory_mb);
}

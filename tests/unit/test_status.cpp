/**
 * @file test_status.cpp
 * @brief test_status 实现：`lsw status` 离线检查、guest 缓存与探测测试。
 * @details 文件位置：tests/unit/test_status.cpp（单元测试）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <lsw/vm/status.hpp>

#include "test_support.hpp"

using lsw::Config;
using lsw::XdgPaths;
using lsw::vm::GuestStatus;
using lsw::vm::InstallMarker;
using lsw::vm::load_guest_status;
using lsw::vm::load_install_marker;
using lsw::vm::offline_status_json;
using lsw::vm::probe_guest_status;
using lsw::vm::save_guest_status;
using lsw::vm::save_install_marker;

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

/// @brief 写一个可执行的假 ssh。
/// @param path 脚本路径。
/// @param body 标准输出内容。
void write_fake_ssh(const std::filesystem::path &path,
                    const std::string &body) {
    std::ofstream(path) << "#!/usr/bin/env bash\n" << body << "\nexit 0\n";
    std::filesystem::permissions(path, std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::add);
}

} // namespace

TEST_CASE("status：磁盘存在时报告已安装", "[status]") {
    lsw::test::TempDir temp("status-installed");
    const auto paths = make_paths(temp);
    Config config = lsw::default_config();
    config.vm.disk = temp.path() / "disk.qcow2";
    std::ofstream(config.vm.disk).close();

    const auto status = offline_status_json(config, paths);
    CHECK(status["daemon"] == "stopped");
    CHECK(status["vm_status"] == "stopped");
    CHECK(status["vm_installed"] == true);
    CHECK(status["disk_exists"] == true);
    CHECK(status["pid"] == -1);
}

TEST_CASE("status：磁盘缺失时报告未安装", "[status]") {
    lsw::test::TempDir temp("status-missing");
    const auto paths = make_paths(temp);
    Config config = lsw::default_config();
    config.vm.disk = temp.path() / "missing.qcow2";

    const auto status = offline_status_json(config, paths);
    CHECK(status["vm_installed"] == false);
    CHECK(status["disk_exists"] == false);
}

TEST_CASE("status：guest 缓存写入与读取", "[status]") {
    lsw::test::TempDir temp("status-cache");
    const auto paths = make_paths(temp);
    GuestStatus guest;
    guest.valid = true;
    guest.hostname = "LSW-TEST";
    guest.virtio_win = "installed";
    guest.source = "runtime_probe";
    guest.checked_at = 123;
    REQUIRE(save_guest_status(paths, guest).has_value());

    auto loaded = load_guest_status(paths);
    REQUIRE(loaded.has_value());
    CHECK(loaded->valid);
    CHECK(loaded->hostname == "LSW-TEST");
    CHECK(loaded->virtio_win == "installed");

    Config config = lsw::default_config();
    config.vm.disk = temp.path() / "disk.qcow2";
    const auto status = offline_status_json(config, paths);
    CHECK(status["hostname"] == "LSW-TEST");
    CHECK(status["virtio_win"] == "installed");
    CHECK(status["virtio_source"] == "runtime_probe");
}

TEST_CASE("status：安装标记写入与读取", "[status]") {
    lsw::test::TempDir temp("status-marker");
    const auto paths = make_paths(temp);
    InstallMarker marker;
    marker.exists = true;
    marker.disk = "/tmp/disk.qcow2";
    marker.virtio_iso = "/tmp/virtio.iso";
    REQUIRE(save_install_marker(paths, marker).has_value());

    auto loaded = load_install_marker(paths);
    REQUIRE(loaded.has_value());
    CHECK(loaded->exists);
    CHECK(loaded->virtio_iso == "/tmp/virtio.iso");

    Config config = lsw::default_config();
    config.vm.disk = temp.path() / "disk.qcow2";
    const auto status = offline_status_json(config, paths);
    CHECK(status["install_marker"]["exists"] == true);
    CHECK(status["virtio_source"] == "install_marker");
}

TEST_CASE("status：SSH 探测主机名与 virtio 驱动", "[status]") {
    lsw::test::TempDir temp("status-probe");
    const auto fake_ssh = temp.path() / "fake-ssh";
    write_fake_ssh(fake_ssh, "echo LSW_HOSTNAME=FAKE-WIN\n"
                             "echo LSW_VIRTIO=installed:vioscsi,netkvm");

    Config config = lsw::default_config();
    config.network.ssh_host_port = 2222;
    config.user.windows_user = "lsw";
    auto status =
        probe_guest_status(config, fake_ssh.string(), std::chrono::seconds(2));
    REQUIRE(status.has_value());
    CHECK(status->hostname == "FAKE-WIN");
    CHECK(status->virtio_win == "installed");
    CHECK(status->source == "runtime_probe");
}

TEST_CASE("status：SSH 探测 virtio 未安装", "[status]") {
    lsw::test::TempDir temp("status-probe-none");
    const auto fake_ssh = temp.path() / "fake-ssh";
    write_fake_ssh(fake_ssh, "echo LSW_HOSTNAME=FAKE-WIN\n"
                             "echo LSW_VIRTIO=not_installed");

    Config config = lsw::default_config();
    auto status =
        probe_guest_status(config, fake_ssh.string(), std::chrono::seconds(2));
    REQUIRE(status.has_value());
    CHECK(status->virtio_win == "not_installed");
}

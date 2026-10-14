/**
 * @file test_daemon_service.cpp
 * @brief test_daemon_service 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_daemon_service.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <lsw/daemon/service.hpp>

#include "test_support.hpp"

#ifndef LSW_FAKE_QEMU_PATH
#error "测试需要 CMake 定义 LSW_FAKE_QEMU_PATH"
#endif

using lsw::Config;
using lsw::ErrorCode;
using lsw::PathEnvironment;
using lsw::XdgPaths;
using lsw::daemon::DaemonService;
using lsw::daemon::DaemonServiceOptions;

namespace {

struct Fixture {
    lsw::test::TempDir temp{"daemon-service"};
    XdgPaths paths;
    Config config;

    /// @brief 构造测试夹具并初始化临时目录、路径与配置。
    Fixture() {
        PathEnvironment env;
        env.home = temp.path() / "home";
        env.xdg_config_home = temp.path() / "config";
        env.xdg_data_home = temp.path() / "data";
        env.xdg_state_home = temp.path() / "state";
        env.xdg_runtime_dir = temp.path() / "run";
        auto resolved = lsw::resolve_paths(env);
        REQUIRE(resolved.has_value());
        paths = *resolved;
        REQUIRE(lsw::ensure_directories(paths).has_value());

        config = lsw::default_config();
        config.vm.disk = temp.path() / "disk.qcow2";
        std::ofstream(config.vm.disk).close();
    }

    /// @brief 构造守护进程服务测试所需的选项。
    /// @return 配置好的守护进程服务选项。
    DaemonServiceOptions options() const {
        DaemonServiceOptions options;
        options.qemu_options.qemu_binary = LSW_FAKE_QEMU_PATH;
        options.qemu_options.enable_kvm = false;
        options.qemu_options.enable_firmware = false;
        options.qemu_options.disk_image = config.vm.disk;
        options.qemu_options.home_dir = temp.path();
        options.qemu_options.qmp_connect_timeout = std::chrono::seconds(3);
        options.qemu_options.shutdown_timeout = std::chrono::seconds(2);
        options.wait_for_ssh = false;
        options.try_ssh_shutdown = false;
        return options;
    }
};

} // namespace

TEST_CASE("DaemonService start 幂等且并发安全", "[daemon]") {
    Fixture fixture;
    DaemonService service(fixture.config, fixture.paths, fixture.options());

    const auto first = service.handle("start");
    REQUIRE(first.has_value());
    REQUIRE((*first)["pid"].is_number_integer());
    const int pid = (*first)["pid"].get<int>();
    CHECK(pid > 0);
    CHECK((*first)["already_running"] == false);

    const auto second = service.handle("start");
    REQUIRE(second.has_value());
    CHECK((*second)["already_running"] == true);
    CHECK((*second)["pid"].get<int>() == pid);

    constexpr int kThreads = 8;
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&] {
            const auto result = service.handle("start");
            if (!result || (*result)["pid"].get<int>() != pid) {
                ++failures;
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    CHECK(failures.load() == 0);

    const auto status = service.handle("status");
    REQUIRE(status.has_value());
    CHECK((*status)["vm_status"] == "running");
    CHECK((*status)["pid"].get<int>() == pid);
}

TEST_CASE("DaemonService shutdown 幂等并触发退出", "[daemon]") {
    Fixture fixture;
    DaemonService service(fixture.config, fixture.paths, fixture.options());
    REQUIRE(service.handle("start").has_value());
    CHECK_FALSE(service.shutdown_requested());

    const auto first = service.handle("shutdown");
    REQUIRE(first.has_value());
    CHECK((*first)["stopped"] == true);
    CHECK((*first)["already_stopped"] == false);
    CHECK(service.shutdown_requested());

    const auto second = service.handle("shutdown");
    REQUIRE(second.has_value());
    CHECK((*second)["already_stopped"] == true);

    const auto status = service.handle("status");
    REQUIRE(status.has_value());
    CHECK((*status)["vm_status"] == "stopped");
}

TEST_CASE("DaemonService mount/unmount 幂等", "[daemon]") {
    Fixture fixture;
    DaemonService service(fixture.config, fixture.paths, fixture.options());

    const auto mount_first = service.handle("mount");
    REQUIRE(mount_first.has_value());
    CHECK((*mount_first)["mounts"].size() == 2);

    const auto mount_second = service.handle("mount");
    REQUIRE(mount_second.has_value());
    CHECK((*mount_second)["mounts"].size() == 2);

    const auto unmount = service.handle("unmount");
    REQUIRE(unmount.has_value());
    CHECK((*unmount)["mounts"].empty());
}

TEST_CASE("DaemonService 未实现命令与未知命令", "[daemon]") {
    Fixture fixture;
    DaemonService service(fixture.config, fixture.paths, fixture.options());
    CHECK(service.handle("shell").error().code == ErrorCode::kNotImplemented);
    CHECK(service.handle("exec").error().code == ErrorCode::kNotImplemented);
    CHECK(service.handle("nope").error().code == ErrorCode::kUnknownCommand);
}

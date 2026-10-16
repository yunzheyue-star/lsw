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
        // 生命周期测试不依赖真实 keyring/helper，文件系统集成见
        // test_daemon_fs.cpp。
        options.enable_host_smb = false;
        options.enable_mounts = false;
        options.enable_z_mapping = false;
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

TEST_CASE("DaemonService 未实现命令与未知命令", "[daemon]") {
    Fixture fixture;
    DaemonService service(fixture.config, fixture.paths, fixture.options());
    CHECK(service.handle("shell", nlohmann::json{{"cwd", "/"}}).error().code ==
          ErrorCode::kVMNotRunning);
    CHECK(
        service.handle("exec", nlohmann::json{{"argv", {"x"}}}).error().code ==
        ErrorCode::kVMNotRunning);
    CHECK(service.handle("nope").error().code == ErrorCode::kUnknownCommand);
}

TEST_CASE("DaemonService shell 返回 ssh_args 并限制在 $HOME 内",
          "[daemon][shell]") {
    Fixture fixture;
    fixture.config.filesystem.linux_home_share = fixture.temp.path() / "home";
    std::filesystem::create_directories(
        fixture.config.filesystem.linux_home_share / "Project/demo");

    DaemonService service(fixture.config, fixture.paths, fixture.options());
    REQUIRE(service.handle("start").has_value());

    const auto inside = service.handle(
        "shell",
        nlohmann::json{{"cwd", (fixture.config.filesystem.linux_home_share /
                                "Project/demo")
                                   .string()}});
    REQUIRE(inside.has_value());
    REQUIRE((*inside)["ssh_args"].is_array());
    CHECK((*inside)["ssh_args"][0] == "ssh");
    bool has_tty = false;
    bool has_encoded = false;
    for (const auto &arg : (*inside)["ssh_args"]) {
        has_tty = has_tty || arg == "-t";
        has_encoded = has_encoded || arg == "-EncodedCommand";
    }
    CHECK(has_tty);
    CHECK(has_encoded);

    const auto outside =
        service.handle("shell", nlohmann::json{{"cwd", "/tmp"}});
    REQUIRE_FALSE(outside.has_value());
    CHECK(outside.error().code == ErrorCode::kPathMappingFailed);

    REQUIRE(service.handle("shutdown").has_value());
}

TEST_CASE("DaemonService exec 返回 stdout/stderr/exit_code", "[daemon][exec]") {
    Fixture fixture;
    const auto home = fixture.temp.path() / "home";
    std::filesystem::create_directories(home);
    std::ofstream(home / "test.bat") << "@echo off\r\necho hi\r\n";
    fixture.config.filesystem.linux_home_share = home;

    const auto fake_ssh = fixture.temp.path() / "fake-ssh";
    std::ofstream(fake_ssh) << "#!/usr/bin/env bash\n"
                               "echo FAKE_OUT\n"
                               "echo FAKE_ERR >&2\n"
                               "exit 7\n";
    std::filesystem::permissions(fake_ssh, std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::add);

    auto options = fixture.options();
    options.z_mapping_options.ssh_binary = fake_ssh.string();
    DaemonService service(fixture.config, fixture.paths, options);
    REQUIRE(service.handle("start").has_value());

    const auto result =
        service.handle("exec", nlohmann::json{{"argv", {"test.bat", "a"}},
                                              {"cwd", home.string()}});
    REQUIRE(result.has_value());
    CHECK((*result)["exit_code"] == 7);
    CHECK((*result)["stdout"].get<std::string>().find("FAKE_OUT") !=
          std::string::npos);
    CHECK((*result)["stderr"].get<std::string>().find("FAKE_ERR") !=
          std::string::npos);

    REQUIRE(service.handle("shutdown").has_value());
}

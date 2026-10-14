/**
 * @file test_state.cpp
 * @brief test_state 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_state.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <lsw/core/state.hpp>

using lsw::ErrorCode;
using lsw::core::DaemonState;

TEST_CASE("初始状态为已停止且无挂载", "[ipc][state]") {
    DaemonState state("test");
    const auto status = state.handle("status");
    REQUIRE(status.has_value());
    CHECK((*status)["daemon"] == "running");
    CHECK((*status)["vm_status"] == "stopped");
    CHECK((*status)["mounts"].empty());
    CHECK((*status)["version"] == "test");
}

TEST_CASE("start 与 shutdown 幂等", "[ipc][state]") {
    DaemonState state;

    const auto first = state.handle("start");
    REQUIRE(first.has_value());
    CHECK((*first)["already_running"] == false);

    const auto second = state.handle("start");
    REQUIRE(second.has_value());
    CHECK((*second)["already_running"] == true);

    CHECK((*state.handle("status"))["vm_status"] == "running");

    const auto stop_first = state.handle("shutdown");
    REQUIRE(stop_first.has_value());
    CHECK((*stop_first)["already_stopped"] == false);

    const auto stop_second = state.handle("shutdown");
    REQUIRE(stop_second.has_value());
    CHECK((*stop_second)["already_stopped"] == true);
}

TEST_CASE("mount 与 unmount 幂等", "[ipc][state]") {
    DaemonState state;

    const auto mount_first = state.handle("mount");
    REQUIRE(mount_first.has_value());
    CHECK((*mount_first)["mounts"].size() == 2);

    const auto mount_second = state.handle("mount");
    REQUIRE(mount_second.has_value());
    CHECK((*mount_second)["mounts"].size() == 2);

    const auto unmount_first = state.handle("unmount");
    REQUIRE(unmount_first.has_value());
    CHECK((*unmount_first)["mounts"].empty());

    const auto unmount_second = state.handle("unmount");
    REQUIRE(unmount_second.has_value());
    CHECK((*unmount_second)["mounts"].empty());
}

TEST_CASE("未知命令与未实现命令返回对应错误", "[ipc][state]") {
    DaemonState state;
    CHECK(state.handle("no-such").error().code == ErrorCode::kUnknownCommand);
    CHECK(state.handle("shell").error().code == ErrorCode::kNotImplemented);
    CHECK(state.handle("exec").error().code == ErrorCode::kNotImplemented);
}

TEST_CASE("并发调用保持状态一致", "[ipc][state]") {
    DaemonState state;
    constexpr int kThreads = 8;
    constexpr int kIterations = 200;
    std::atomic<int> failures{0};

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&] {
            for (int j = 0; j < kIterations; ++j) {
                if (!state.handle("start")) {
                    ++failures;
                }
                if (!state.handle("mount")) {
                    ++failures;
                }
                if (!state.handle("status")) {
                    ++failures;
                }
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }

    CHECK(failures.load() == 0);
    const auto status = state.handle("status");
    REQUIRE(status.has_value());
    CHECK((*status)["vm_status"] == "running");
    CHECK((*status)["mounts"].size() == 2);
}

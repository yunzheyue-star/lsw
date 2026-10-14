/**
 * @file test_qmp_client.cpp
 * @brief test_qmp_client 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_qmp_client.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 */

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <unistd.h>

#include <lsw/core/process.hpp>
#include <lsw/vm/qmp_client.hpp>

#include "test_support.hpp"

#ifndef LSW_FAKE_QEMU_PATH
#error "测试需要 CMake 定义 LSW_FAKE_QEMU_PATH"
#endif

using lsw::ErrorCode;
using lsw::vm::QmpClient;
using lsw::vm::QmpOptions;

namespace {

/// 启动假 QEMU 并等待 QMP socket 就绪。
struct FakeQemu {
    lsw::test::TempDir dir{"fake-qemu"};
    std::filesystem::path socket = dir.path() / "qmp.sock";
    lsw::core::ChildProcess process;

    FakeQemu() {
        auto spawned = lsw::core::ChildProcess::spawn(
            {LSW_FAKE_QEMU_PATH, "-qmp",
             "unix:" + socket.string() + ",server=on,wait=off"});
        REQUIRE(spawned.has_value());
        process = std::move(*spawned);
        for (int i = 0; i < 100 && !std::filesystem::exists(socket); ++i) {
            ::usleep(20 * 1000);
        }
    }

    ~FakeQemu() {
        if (process.running()) {
            (void)process.kill();
            (void)process.wait();
        }
    }
};

} // namespace

TEST_CASE("QmpClient 握手、查询与关机", "[vm][qmp]") {
    FakeQemu fake;
    QmpClient client(QmpOptions{fake.socket, std::chrono::milliseconds(2000),
                                std::chrono::milliseconds(2000)});

    REQUIRE(client.connect().has_value());
    CHECK(client.connected());

    const auto alive = client.query_status();
    REQUIRE(alive.has_value());
    CHECK(*alive);

    const auto raw = client.execute("query-status");
    REQUIRE(raw.has_value());
    CHECK((*raw)["status"] == "running");

    REQUIRE(client.system_powerdown().has_value());
    for (int i = 0; i < 100 && fake.process.running(); ++i) {
        ::usleep(10 * 1000);
    }
    CHECK_FALSE(fake.process.running());
    client.close();
}

TEST_CASE("QMP 错误对象映射为 kQemuError", "[vm][qmp]") {
    FakeQemu fake;
    QmpClient client(QmpOptions{fake.socket, std::chrono::milliseconds(2000),
                                std::chrono::milliseconds(2000)});
    REQUIRE(client.connect().has_value());

    const auto result = client.execute("no-such-command");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kQemuError);
    CHECK(result.error().message.find("CommandNotFound") != std::string::npos);
}

TEST_CASE("QMP 读取超时", "[vm][qmp]") {
    FakeQemu fake;
    QmpClient client(QmpOptions{fake.socket, std::chrono::milliseconds(2000),
                                std::chrono::milliseconds(200)});
    REQUIRE(client.connect().has_value());

    const auto result = client.execute("no-reply");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kQemuError);
    CHECK(result.error().message.find("超时") != std::string::npos);
}

TEST_CASE("QMP socket 不存在时连接失败", "[vm][qmp]") {
    const lsw::test::TempDir temp("qmp-missing");
    QmpClient client(QmpOptions{temp.path() / "missing.sock",
                                std::chrono::milliseconds(100),
                                std::chrono::milliseconds(100)});
    const auto result = client.connect();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kQemuError);
}

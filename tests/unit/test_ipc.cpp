/**
 * @file test_ipc.cpp
 * @brief test_ipc 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_ipc.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <lsw/ipc/client.hpp>
#include <lsw/ipc/server.hpp>

#include "test_support.hpp"

using lsw::ErrorCode;
using lsw::ipc::Client;
using lsw::ipc::Server;

TEST_CASE("Server/Client 基本往返与错误映射", "[ipc]") {
    const lsw::test::TempDir temp("ipc-basic");
    const auto socket = temp.path() / "lswd.sock";

    Server server({.socket_path = socket});
    server.register_handler("echo",
                            [](const nlohmann::json &args) { return args; });
    server.register_handler("boom", [](const nlohmann::json &) {
        return lsw::fail(ErrorCode::kPathMappingFailed, "outside");
    });
    REQUIRE(server.start().has_value());
    auto worker = server.run();

    Client client({.socket_path = socket,
                   .connect_timeout = std::chrono::milliseconds(500),
                   .io_timeout = std::chrono::milliseconds(2000)});

    const auto echoed = client.call("echo", nlohmann::json{{"x", 1}});
    REQUIRE(echoed.has_value());
    CHECK((*echoed)["x"] == 1);

    const auto mapped = client.call("boom");
    REQUIRE_FALSE(mapped.has_value());
    CHECK(mapped.error().code == ErrorCode::kPathMappingFailed);

    const auto unknown = client.call("no-such-command");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().code == ErrorCode::kUnknownCommand);

    Client missing({.socket_path = temp.path() / "missing.sock"});
    const auto unavailable = missing.call("status");
    REQUIRE_FALSE(unavailable.has_value());
    CHECK(unavailable.error().code == ErrorCode::kDaemonUnavailable);
}

TEST_CASE("Server 支持多客户端并发", "[ipc]") {
    const lsw::test::TempDir temp("ipc-concurrent");
    const auto socket = temp.path() / "lswd.sock";

    Server server({.socket_path = socket});
    server.register_handler("echo",
                            [](const nlohmann::json &args) { return args; });
    REQUIRE(server.start().has_value());
    auto worker = server.run();

    constexpr int kClients = 8;
    std::atomic<int> successes{0};
    std::vector<std::thread> clients;
    clients.reserve(kClients);
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([&, i] {
            Client client({.socket_path = socket});
            const auto result = client.call("echo", nlohmann::json{{"n", i}});
            if (result.has_value() && (*result)["n"] == i) {
                ++successes;
            }
        });
    }
    for (auto &client : clients) {
        client.join();
    }
    CHECK(successes.load() == kClients);

    server.request_stop();
    worker.join();
    server.stop();
}

TEST_CASE("Server 停止后 socket 被清理", "[ipc]") {
    const lsw::test::TempDir temp("ipc-cleanup");
    const auto socket = temp.path() / "lswd.sock";

    {
        Server server({.socket_path = socket});
        REQUIRE(server.start().has_value());
        CHECK(std::filesystem::exists(socket));
        server.stop();
    }
    CHECK_FALSE(std::filesystem::exists(socket));
}

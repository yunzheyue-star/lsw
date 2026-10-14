/**
 * @file test_net.cpp
 * @brief test_net 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_net.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <lsw/util/net.hpp>

using lsw::ErrorCode;
using lsw::wait_for_tcp_port;

namespace {

/// 绑定 127.0.0.1 的临时端口，返回监听 fd 与端口号。
std::pair<int, std::uint16_t> listen_loopback() {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    REQUIRE(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) == 0);
    REQUIRE(::listen(fd, 4) == 0);
    socklen_t length = sizeof(address);
    REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr *>(&address),
                          &length) == 0);
    return {fd, ntohs(address.sin_port)};
}

} // namespace

TEST_CASE("wait_for_tcp_port 在端口监听后成功", "[net]") {
    auto [listen_fd, port] = listen_loopback();
    std::atomic<bool> accepted{false};
    std::thread acceptor([listen_fd, &accepted] {
        const int client = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client >= 0) {
            ::close(client);
            accepted.store(true);
        }
    });

    const auto ready =
        wait_for_tcp_port("127.0.0.1", port, std::chrono::seconds(1),
                          std::chrono::milliseconds(50));
    CHECK(ready.has_value());
    acceptor.join();
    CHECK(accepted.load());
    ::close(listen_fd);
}

TEST_CASE("wait_for_ssh_banner 识别真实 SSH 服务", "[net]") {
    auto [listen_fd, port] = listen_loopback();
    std::thread server([listen_fd] {
        const int client = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client >= 0) {
            const char banner[] = "SSH-2.0-lsw-test\r\n";
            (void)::send(client, banner, sizeof(banner) - 1, MSG_NOSIGNAL);
            ::close(client);
        }
    });

    const auto ready =
        lsw::wait_for_ssh_banner("127.0.0.1", port, std::chrono::seconds(1),
                                 std::chrono::milliseconds(50));
    CHECK(ready.has_value());
    server.join();
    ::close(listen_fd);
}

TEST_CASE("只有 TCP 监听、没有 SSH banner 时失败", "[net]") {
    auto [listen_fd, port] = listen_loopback();
    std::thread server([listen_fd] {
        const int client = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client >= 0) {
            ::close(client); // QEMU hostfwd 在没有 guest sshd 时的表现
        }
    });

    const auto ready = lsw::wait_for_ssh_banner("127.0.0.1", port,
                                                std::chrono::milliseconds(300),
                                                std::chrono::milliseconds(50));
    REQUIRE_FALSE(ready.has_value());
    CHECK(ready.error().code == ErrorCode::kSshFailed);
    server.join();
    ::close(listen_fd);
}

TEST_CASE("wait_for_tcp_port 超时返回 kSshFailed", "[net]") {
    auto [listen_fd, port] = listen_loopback();
    ::close(listen_fd);

    const auto ready =
        wait_for_tcp_port("127.0.0.1", port, std::chrono::milliseconds(200),
                          std::chrono::milliseconds(50));
    REQUIRE_FALSE(ready.has_value());
    CHECK(ready.error().code == ErrorCode::kSshFailed);
}

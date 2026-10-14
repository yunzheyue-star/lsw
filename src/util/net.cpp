/**
 * @file net.cpp
 * @brief net 实现（LSW 工具库）。
 * @details 文件位置：src/util/net.cpp（源文件）。
 *          属于 LSW 工具库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 */

#include <lsw/util/net.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace lsw {
namespace {

/// 非阻塞连接一次，成功返回已连接 fd，失败返回 -1。
int connect_once(std::string_view host, std::uint16_t port,
                 std::chrono::milliseconds attempt_timeout) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *result = nullptr;
    const std::string host_text(host);
    const std::string port_text = std::to_string(port);
    if (::getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &result) !=
        0) {
        return -1;
    }

    int connected_fd = -1;
    for (addrinfo *entry = result; entry != nullptr && connected_fd < 0;
         entry = entry->ai_next) {
        const int fd =
            ::socket(entry->ai_family, entry->ai_socktype | SOCK_CLOEXEC,
                     entry->ai_protocol);
        if (fd < 0) {
            continue;
        }
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        bool connected = false;
        const int rc = ::connect(fd, entry->ai_addr,
                                 static_cast<socklen_t>(entry->ai_addrlen));
        if (rc == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            pollfd pfd{fd, POLLOUT, 0};
            const int ready =
                ::poll(&pfd, 1, static_cast<int>(attempt_timeout.count()));
            if (ready > 0) {
                int socket_error = 0;
                socklen_t length = sizeof(socket_error);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                                 &length) == 0 &&
                    socket_error == 0) {
                    connected = true;
                }
            }
        }
        if (connected) {
            connected_fd = fd;
        } else {
            ::close(fd);
        }
    }
    ::freeaddrinfo(result);
    return connected_fd;
}

/// 尝试读取 SSH banner；读到 "SSH-" 开头返回 true。
bool read_ssh_banner(int fd) {
    char buffer[256];
    pollfd pfd{fd, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, 1500);
    if (ready <= 0) {
        return false;
    }
    const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
    return count >= 4 && std::strncmp(buffer, "SSH-", 4) == 0;
}

} // namespace

Result<void> wait_for_tcp_port(std::string_view host, std::uint16_t port,
                               std::chrono::milliseconds timeout,
                               std::chrono::milliseconds interval) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const int fd = connect_once(
            host, port, std::min(interval, std::chrono::milliseconds(500)));
        if (fd >= 0) {
            ::close(fd);
            return {};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(interval);
    }
    return fail(ErrorCode::kSshFailed, "等待 " + std::string(host) + ":" +
                                           std::to_string(port) +
                                           " 超时（端口未就绪）");
}

Result<void> wait_for_ssh_banner(std::string_view host, std::uint16_t port,
                                 std::chrono::milliseconds timeout,
                                 std::chrono::milliseconds interval) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const int fd = connect_once(
            host, port, std::min(interval, std::chrono::milliseconds(500)));
        if (fd >= 0) {
            const bool has_banner = read_ssh_banner(fd);
            ::close(fd);
            if (has_banner) {
                return {};
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(interval);
    }
    return fail(ErrorCode::kSshFailed,
                "等待 " + std::string(host) + ":" + std::to_string(port) +
                    " 的 SSH banner 超时（Windows sshd 未安装或未启动）");
}

} // namespace lsw

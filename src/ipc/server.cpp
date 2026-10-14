/**
 * @file server.cpp
 * @brief server 实现（IPC 与守护进程通信）。
 * @details 文件位置：src/ipc/server.cpp（源文件）。
 *          属于 LSW IPC 与守护进程通信；公共行为以
 * docs/interfaces/daemon-ipc.md 与 agent.md 为准。
 */

#include <lsw/ipc/server.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <lsw/ipc/protocol.hpp>

namespace lsw::ipc {
namespace {

class ScopedFd {
  public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}
    ~ScopedFd() { reset(); }
    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;
    ScopedFd(ScopedFd &&other) noexcept : fd_(other.release()) {}
    ScopedFd &operator=(ScopedFd &&other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    [[nodiscard]] int get() const noexcept { return fd_; }
    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

Result<std::string> read_line(int fd, std::chrono::milliseconds timeout,
                              std::size_t max_bytes = 1024 * 1024) {
    std::string buffer;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                .count();
        if (remaining <= 0) {
            return fail(ErrorCode::kIoError, "读取请求超时");
        }
        pollfd pfd{fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (ready == 0) {
            return fail(ErrorCode::kIoError, "读取请求超时");
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail(ErrorCode::kIoError,
                        std::string("poll 失败: ") + std::strerror(errno));
        }
        char chunk[4096];
        const ssize_t count = ::recv(fd, chunk, sizeof(chunk), 0);
        if (count > 0) {
            buffer.append(chunk, static_cast<std::size_t>(count));
            if (buffer.size() > max_bytes) {
                return fail(ErrorCode::kProtocolError, "请求超过大小上限");
            }
            if (const auto pos = buffer.find('\n'); pos != std::string::npos) {
                return buffer.substr(0, pos);
            }
            continue;
        }
        if (count == 0) {
            return fail(ErrorCode::kProtocolError, "客户端提前关闭连接");
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        return fail(ErrorCode::kIoError,
                    std::string("读取请求失败: ") + std::strerror(errno));
    }
}

Status write_all(int fd, std::string_view data,
                 std::chrono::milliseconds timeout) {
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (offset < data.size()) {
        const ssize_t count = ::send(fd, data.data() + offset,
                                     data.size() - offset, MSG_NOSIGNAL);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now())
                    .count();
            if (remaining <= 0) {
                return fail(ErrorCode::kIoError, "发送响应超时");
            }
            pollfd pfd{fd, POLLOUT, 0};
            const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
            if (ready < 0 && errno != EINTR) {
                return fail(ErrorCode::kIoError,
                            std::string("poll 失败: ") + std::strerror(errno));
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return fail(ErrorCode::kIoError,
                    std::string("发送响应失败: ") + std::strerror(errno));
    }
    return {};
}

} // namespace

Server::Server(ServerOptions options) : options_(std::move(options)) {}

Server::~Server() { stop(); }

void Server::register_handler(std::string command, Handler handler) {
    if (!started_.load()) {
        handlers_[std::move(command)] = std::move(handler);
    }
}

Status Server::start() {
    if (started_.load()) {
        return {};
    }

    const std::string path_text = options_.socket_path.string();
    if (path_text.size() >= sizeof(sockaddr_un::sun_path)) {
        return fail(ErrorCode::kInvalidArgument,
                    "socket 路径过长: " + path_text);
    }

    std::error_code error;
    if (!options_.socket_path.parent_path().empty()) {
        std::filesystem::create_directories(options_.socket_path.parent_path(),
                                            error);
        if (error) {
            return fail(ErrorCode::kIoError,
                        "创建 socket 目录失败: " + error.message());
        }
    }

    std::filesystem::remove(options_.socket_path, error);
    error.clear();

    ScopedFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0) {
        return fail(ErrorCode::kIoError,
                    std::string("创建 socket 失败: ") + std::strerror(errno));
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path_text.c_str(), path_text.size());

    if (::bind(fd.get(), reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) != 0) {
        return fail(ErrorCode::kIoError,
                    "绑定 socket 失败: " + std::string(std::strerror(errno)));
    }
    if (::chmod(path_text.c_str(), static_cast<mode_t>(options_.mode)) != 0) {
        ::unlink(path_text.c_str());
        return fail(ErrorCode::kIoError, "设置 socket 权限失败: " +
                                             std::string(std::strerror(errno)));
    }
    if (::listen(fd.get(), options_.backlog) != 0) {
        ::unlink(path_text.c_str());
        return fail(ErrorCode::kIoError,
                    "监听 socket 失败: " + std::string(std::strerror(errno)));
    }

    listen_fd_ = fd.release();
    stop_requested_.store(false);
    started_.store(true);
    return {};
}

void Server::stop() noexcept {
    stop_requested_.store(true);
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    {
        std::unique_lock lock(connections_mutex_);
        connections_cv_.wait_for(lock, std::chrono::seconds(5),
                                 [this] { return active_connections_ == 0; });
    }
    started_.store(false);
    std::error_code error;
    std::filesystem::remove(options_.socket_path, error);
}

Result<bool> Server::serve_once() {
    if (!started_.load() || stop_requested_.load()) {
        return false;
    }
    pollfd pfd{listen_fd_, POLLIN, 0};
    const int ready =
        ::poll(&pfd, 1, static_cast<int>(options_.accept_timeout.count()));
    if (ready == 0) {
        return false;
    }
    if (ready < 0) {
        if (errno == EINTR) {
            return false;
        }
        return fail(ErrorCode::kIoError,
                    std::string("accept poll 失败: ") + std::strerror(errno));
    }

    const int client_fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (client_fd < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }
        if (stop_requested_.load()) {
            return false;
        }
        return fail(ErrorCode::kIoError,
                    std::string("accept 失败: ") + std::strerror(errno));
    }

    {
        std::lock_guard lock(connections_mutex_);
        ++active_connections_;
    }
    std::thread([this, client_fd] {
        handle_connection(client_fd);
        ::close(client_fd);
        {
            std::lock_guard lock(connections_mutex_);
            --active_connections_;
        }
        connections_cv_.notify_all();
    }).detach();
    return true;
}

std::jthread Server::run() {
    return std::jthread([this](std::stop_token token) {
        while (!token.stop_requested() && !stop_requested_.load()) {
            auto served = serve_once();
            if (!served) {
                break;
            }
        }
    });
}

void Server::handle_connection(int fd) {
    auto line = read_line(fd, std::chrono::milliseconds(5000));
    if (!line) {
        return;
    }

    auto request = parse_request(*line);
    if (!request) {
        const Response response = Response::failure(
            "", std::string(kBadRequest), request.error().message);
        (void)write_all(fd, encode_line(response),
                        std::chrono::milliseconds(2000));
        return;
    }

    Response response;
    const auto handler = handlers_.find(request->cmd);
    if (handler == handlers_.end()) {
        response = Response::failure(request->id, std::string(kUnknownCommand),
                                     "未知命令: " + request->cmd);
    } else {
        try {
            auto result = handler->second(request->args);
            if (result) {
                response = Response::success(request->id, *result);
            } else {
                response = Response::failure(
                    request->id, to_ipc_error_code(result.error().code),
                    result.error().message);
            }
        } catch (const std::exception &error) {
            response = Response::failure(request->id, std::string(kInternal),
                                         error.what());
        } catch (...) {
            response = Response::failure(request->id, std::string(kInternal),
                                         "处理器内部异常");
        }
    }
    (void)write_all(fd, encode_line(response), std::chrono::milliseconds(2000));
}

} // namespace lsw::ipc

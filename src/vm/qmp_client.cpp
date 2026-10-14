#include <lsw/vm/qmp_client.hpp>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace lsw::vm {
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

Status set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return fail(ErrorCode::kQemuError,
                    std::string("QMP 设置非阻塞失败: ") + std::strerror(errno));
    }
    return {};
}

} // namespace

QmpClient::QmpClient(QmpOptions options) : options_(std::move(options)) {}

QmpClient::~QmpClient() { close(); }

QmpClient::QmpClient(QmpClient &&other) noexcept
    : options_(std::move(other.options_)), fd_(other.fd_),
      read_buffer_(std::move(other.read_buffer_)), next_id_(other.next_id_),
      connected_(other.connected_) {
    other.fd_ = -1;
    other.connected_ = false;
    other.read_buffer_.clear();
}

QmpClient &QmpClient::operator=(QmpClient &&other) noexcept {
    if (this != &other) {
        close();
        options_ = std::move(other.options_);
        fd_ = other.fd_;
        read_buffer_ = std::move(other.read_buffer_);
        next_id_ = other.next_id_;
        connected_ = other.connected_;
        other.fd_ = -1;
        other.connected_ = false;
    }
    return *this;
}

void QmpClient::close() noexcept {
    read_buffer_.clear();
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

Result<int> QmpClient::connect_once() {
    const std::string path = options_.socket_path.string();
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        return fail(ErrorCode::kQemuError, "QMP socket 路径过长: " + path);
    }
    if (!std::filesystem::exists(options_.socket_path)) {
        return fail(ErrorCode::kQemuError, "QMP socket 不存在: " + path);
    }

    ScopedFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0) {
        return fail(ErrorCode::kQemuError,
                    std::string("QMP 创建 socket 失败: ") +
                        std::strerror(errno));
    }
    if (auto status = set_nonblocking(fd.get()); !status) {
        return std::unexpected(status.error());
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size());

    if (::connect(fd.get(), reinterpret_cast<sockaddr *>(&address),
                  sizeof(address)) != 0 &&
        errno != EINPROGRESS) {
        return fail(ErrorCode::kQemuError,
                    "QMP 连接失败: " + std::string(std::strerror(errno)));
    }

    pollfd pfd{fd.get(), POLLOUT, 0};
    const int ready =
        ::poll(&pfd, 1, static_cast<int>(options_.connect_timeout.count()));
    if (ready <= 0) {
        return fail(ErrorCode::kQemuError, "QMP 连接超时");
    }
    int socket_error = 0;
    socklen_t length = sizeof(socket_error);
    if (::getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &socket_error, &length) !=
            0 ||
        socket_error != 0) {
        return fail(ErrorCode::kQemuError,
                    "QMP 连接失败: " +
                        std::string(std::strerror(socket_error)));
    }
    return fd.release();
}

Result<std::string> QmpClient::read_line() {
    // 持久缓冲：一次 recv 可能包含多行，必须保留未消费部分。
    const auto deadline =
        std::chrono::steady_clock::now() + options_.io_timeout;
    while (true) {
        if (const auto pos = read_buffer_.find('\n');
            pos != std::string::npos) {
            std::string line = read_buffer_.substr(0, pos);
            read_buffer_.erase(0, pos + 1);
            return line;
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                .count();
        if (remaining <= 0) {
            return fail(ErrorCode::kQemuError, "读取 QMP 响应超时");
        }
        pollfd pfd{fd_, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (ready == 0) {
            return fail(ErrorCode::kQemuError, "读取 QMP 响应超时");
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail(ErrorCode::kQemuError,
                        std::string("QMP poll 失败: ") + std::strerror(errno));
        }
        char chunk[4096];
        const ssize_t count = ::recv(fd_, chunk, sizeof(chunk), 0);
        if (count > 0) {
            read_buffer_.append(chunk, static_cast<std::size_t>(count));
            if (read_buffer_.size() > 4 * 1024 * 1024) {
                return fail(ErrorCode::kQemuError, "QMP 响应超过大小上限");
            }
            continue;
        }
        if (count == 0) {
            return fail(ErrorCode::kQemuError, "QMP 连接已关闭");
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        return fail(ErrorCode::kQemuError,
                    std::string("读取 QMP 失败: ") + std::strerror(errno));
    }
}

Status QmpClient::write_all(std::string_view data) {
    const auto deadline =
        std::chrono::steady_clock::now() + options_.io_timeout;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = ::send(fd_, data.data() + offset,
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
                return fail(ErrorCode::kQemuError, "发送 QMP 命令超时");
            }
            pollfd pfd{fd_, POLLOUT, 0};
            const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
            if (ready < 0 && errno != EINTR) {
                return fail(ErrorCode::kQemuError,
                            std::string("QMP poll 失败: ") +
                                std::strerror(errno));
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return fail(ErrorCode::kQemuError,
                    std::string("发送 QMP 命令失败: ") + std::strerror(errno));
    }
    return {};
}

Result<nlohmann::json> QmpClient::send_command(std::string_view command,
                                               const nlohmann::json &args,
                                               bool require_connected) {
    if (require_connected && !connected_) {
        return fail(ErrorCode::kQemuError, "QMP 尚未完成握手");
    }
    const std::string id = std::to_string(++next_id_);
    nlohmann::json message{{"execute", std::string(command)}, {"id", id}};
    if (!args.empty()) {
        message["arguments"] = args;
    }
    if (auto status = write_all(message.dump() + "\n"); !status) {
        return std::unexpected(status.error());
    }

    while (true) {
        auto line = read_line();
        if (!line) {
            return std::unexpected(line.error());
        }
        const auto value = nlohmann::json::parse(*line, nullptr, false);
        if (value.is_discarded() || !value.is_object()) {
            return fail(ErrorCode::kQemuError, "QMP 返回非法 JSON");
        }
        if (value.contains("event")) {
            continue; // 异步事件：跳过
        }
        if (value.contains("error")) {
            const auto &error = value["error"];
            const std::string error_class =
                error.value("class", "GenericError");
            const std::string description = error.value("desc", "unknown");
            return fail(ErrorCode::kQemuError,
                        "QMP 命令失败: " + error_class + ": " + description);
        }
        if (!value.contains("return")) {
            continue; // greeting 或未知消息
        }
        if (value.contains("id") && value["id"].is_string() &&
            value["id"].get<std::string>() != id) {
            continue; // 过期响应
        }
        return value["return"];
    }
}

Result<void> QmpClient::handshake() {
    auto greeting = read_line();
    if (!greeting) {
        return std::unexpected(greeting.error());
    }
    const auto value = nlohmann::json::parse(*greeting, nullptr, false);
    if (value.is_discarded() || !value.is_object() || !value.contains("QMP")) {
        return fail(ErrorCode::kQemuError, "QMP greeting 格式非法");
    }
    auto capabilities =
        send_command("qmp_capabilities", nlohmann::json::object(), false);
    if (!capabilities) {
        return std::unexpected(capabilities.error());
    }
    connected_ = true;
    return {};
}

Result<void> QmpClient::connect() {
    close();
    const auto deadline =
        std::chrono::steady_clock::now() + options_.connect_timeout;
    while (true) {
        auto fd = connect_once();
        if (fd) {
            fd_ = *fd;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(fd.error());
        }
        ::usleep(50 * 1000);
    }
    if (auto status = handshake(); !status) {
        close();
        return std::unexpected(status.error());
    }
    return {};
}

Result<nlohmann::json> QmpClient::execute(std::string_view command,
                                          const nlohmann::json &args) {
    return send_command(command, args, true);
}

Result<void> QmpClient::system_powerdown() {
    auto result = execute("system_powerdown");
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

Result<bool> QmpClient::query_status() {
    auto result = execute("query-status");
    if (!result) {
        return std::unexpected(result.error());
    }
    const std::string status = result->value("status", "unknown");
    return status == "running" || status == "paused";
}

} // namespace lsw::vm

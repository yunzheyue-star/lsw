/**
 * @file client.cpp
 * @brief client 实现（IPC 与守护进程通信）。
 * @details 文件位置：src/ipc/client.cpp（源文件）。
 *          属于 LSW IPC 与守护进程通信；公共行为以
 * docs/interfaces/daemon-ipc.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/ipc/client.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include <lsw/ipc/protocol.hpp>

namespace lsw::ipc {
namespace {

class ScopedFd {
  public:
    /** @brief 默认构造，文件描述符置为 -1。
     */
    ScopedFd() = default;
    /** @brief 接管一个已有的文件描述符。
     * @param fd 要接管的文件描述符。
     */
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}
    /** @brief 析构时关闭持有的文件描述符。
     */
    ~ScopedFd() { reset(); }
    /** @brief 禁止拷贝构造，避免重复关闭文件描述符。
     * @param other 被拷贝的源对象；拷贝已删除。
     */
    ScopedFd(const ScopedFd &) = delete;
    /** @brief 禁止拷贝赋值，避免重复关闭文件描述符。
     * @param other 被拷贝的源对象；拷贝已删除。
     * @return 该运算符被删除，始终不可用。
     */
    ScopedFd &operator=(const ScopedFd &) = delete;
    /** @brief 移动构造，接管源对象释放的文件描述符。
     * @param other 被移动的源对象。
     */
    ScopedFd(ScopedFd &&other) noexcept : fd_(other.release()) {}
    /** @brief 移动赋值，释放当前描述符后接管源对象的描述符。
     * @param other 被移动的源对象。
     * @return 自身引用。
     */
    ScopedFd &operator=(ScopedFd &&other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    /** @brief 获取当前持有的文件描述符。
     * @return 文件描述符；未持有时为 -1。
     */
    [[nodiscard]] int get() const noexcept { return fd_; }
    /** @brief 释放所有权并返回文件描述符，此后不再负责关闭。
     * @return 原持有的文件描述符。
     */
    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    /** @brief 关闭当前文件描述符并接管新的文件描述符。
     * @param fd 新的文件描述符，默认为 -1 表示不持有。
     */
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

/** @brief 将文件描述符设置为非阻塞模式。
 * @param fd 目标文件描述符。
 * @return 成功返回空状态；失败返回 IO 错误。
 */
Status set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return fail(ErrorCode::kIoError,
                    std::string("设置非阻塞失败: ") + std::strerror(errno));
    }
    return {};
}

/** @brief 在超时时间内连接到指定的 Unix Socket。
 * @param path Unix Socket 路径。
 * @param timeout 连接超时时间。
 * @return 成功返回已连接的 ScopedFd；失败返回相应错误。
 */
Result<ScopedFd> connect_with_timeout(const std::filesystem::path &path,
                                      std::chrono::milliseconds timeout) {
    const std::string text = path.string();
    if (text.size() >= sizeof(sockaddr_un::sun_path)) {
        return fail(ErrorCode::kInvalidArgument, "socket 路径过长: " + text);
    }
    if (!std::filesystem::exists(path)) {
        return fail(ErrorCode::kDaemonUnavailable,
                    "lswd 未运行（socket 不存在）: " + text);
    }

    ScopedFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (fd.get() < 0) {
        return fail(ErrorCode::kIoError,
                    std::string("创建 socket 失败: ") + std::strerror(errno));
    }
    if (auto status = set_nonblocking(fd.get()); !status) {
        return std::unexpected(status.error());
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, text.c_str(), text.size());

    if (::connect(fd.get(), reinterpret_cast<sockaddr *>(&address),
                  sizeof(address)) != 0) {
        if (errno != EINPROGRESS) {
            return fail(ErrorCode::kDaemonUnavailable,
                        "连接 lswd 失败: " + std::string(std::strerror(errno)));
        }
        pollfd pfd{fd.get(), POLLOUT, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (ready == 0) {
            return fail(ErrorCode::kDaemonUnavailable, "连接 lswd 超时");
        }
        if (ready < 0) {
            return fail(ErrorCode::kIoError,
                        std::string("poll 失败: ") + std::strerror(errno));
        }
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);
        if (::getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &socket_error,
                         &length) != 0 ||
            socket_error != 0) {
            return fail(ErrorCode::kDaemonUnavailable,
                        "连接 lswd 失败: " +
                            std::string(std::strerror(socket_error)));
        }
    }
    return fd;
}

/** @brief 等待文件描述符变为可写。
 * @param fd 目标文件描述符。
 * @param timeout 等待超时时间。
 * @return 可写返回 true；超时或轮询失败返回错误。
 */
Result<bool> wait_writable(int fd, std::chrono::milliseconds timeout) {
    pollfd pfd{fd, POLLOUT, 0};
    const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (ready > 0) {
        return true;
    }
    if (ready == 0) {
        return fail(ErrorCode::kIoError, "发送请求超时");
    }
    return fail(ErrorCode::kIoError,
                std::string("poll 失败: ") + std::strerror(errno));
}

/** @brief 将数据完整写入文件描述符，必要时等待可写。
 * @param fd 目标文件描述符。
 * @param data 待发送的数据。
 * @param timeout 等待可写的超时时间。
 * @return 成功返回空状态；失败返回 IO 错误。
 */
Status write_all(int fd, std::string_view data,
                 std::chrono::milliseconds timeout) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = ::send(fd, data.data() + offset,
                                     data.size() - offset, MSG_NOSIGNAL);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (auto ready = wait_writable(fd, timeout); !ready) {
                return std::unexpected(ready.error());
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return fail(ErrorCode::kIoError,
                    std::string("发送请求失败: ") + std::strerror(errno));
    }
    return {};
}

/** @brief 读取一行以换行符结尾的响应文本。
 * @param fd 目标文件描述符。
 * @param timeout 读取超时时间。
 * @param max_bytes 单行响应的最大字节数。
 * @return 成功返回不含换行符的一行文本；失败返回相应错误。
 */
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
            return fail(ErrorCode::kIoError, "等待 lswd 响应超时");
        }
        pollfd pfd{fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (ready == 0) {
            return fail(ErrorCode::kIoError, "等待 lswd 响应超时");
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
                return fail(ErrorCode::kProtocolError, "响应超过大小上限");
            }
            if (const auto pos = buffer.find('\n'); pos != std::string::npos) {
                return buffer.substr(0, pos);
            }
            continue;
        }
        if (count == 0) {
            return fail(ErrorCode::kProtocolError, "lswd 提前关闭连接");
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        return fail(ErrorCode::kIoError,
                    std::string("读取响应失败: ") + std::strerror(errno));
    }
}

} // namespace

/** @brief 构造客户端并记录进程前缀用于生成请求 id。
 * @param options 客户端配置。
 */
Client::Client(ClientOptions options) : options_(std::move(options)) {
    id_prefix_ = std::to_string(::getpid()) + "-";
}

/** @brief 生成单调递增的请求 id。
 * @return 形如 "进程号-序号" 的请求 id。
 */
std::string Client::next_id() {
    ++counter_;
    return id_prefix_ + std::to_string(counter_);
}

/** @brief 调用命令，并按配置对可重试错误进行重试。
 * @param cmd 命令名。
 * @param args 命令参数。
 * @return 成功返回响应 data；失败返回相应错误。
 */
Result<nlohmann::json> Client::call(std::string_view cmd,
                                    const nlohmann::json &args) {
    Result<nlohmann::json> result = fail(ErrorCode::kIoError, "未执行任何调用");
    for (int attempt = 0; attempt <= options_.retries; ++attempt) {
        result = call_once(cmd, args);
        if (result) {
            return result;
        }
        // 只在“守护进程不可用/IO 失败”时重试；业务错误直接返回。
        if (result.error().code != ErrorCode::kDaemonUnavailable &&
            result.error().code != ErrorCode::kIoError) {
            return result;
        }
        if (attempt < options_.retries) {
            ::usleep(static_cast<useconds_t>(options_.retry_delay.count()) *
                     1000);
        }
    }
    return result;
}

/** @brief 执行一次完整的 IPC 请求与响应交互。
 * @param cmd 命令名。
 * @param args 命令参数。
 * @return 成功返回响应 data；失败返回相应错误。
 */
Result<nlohmann::json> Client::call_once(std::string_view cmd,
                                         const nlohmann::json &args) {
    auto fd =
        connect_with_timeout(options_.socket_path, options_.connect_timeout);
    if (!fd) {
        return std::unexpected(fd.error());
    }

    const std::string id = next_id();
    const Request request{id, std::string(cmd), args};
    if (auto status =
            write_all(fd->get(), encode_line(request), options_.io_timeout);
        !status) {
        return std::unexpected(status.error());
    }

    auto line = read_line(fd->get(), options_.io_timeout);
    if (!line) {
        return std::unexpected(line.error());
    }
    auto response = parse_response(*line);
    if (!response) {
        return std::unexpected(response.error());
    }
    if (response->id != id) {
        return fail(ErrorCode::kProtocolError, "响应 id 与请求不匹配");
    }
    if (!response->ok) {
        return fail(from_ipc_error_code(response->error_code),
                    response->error_code + ": " + response->error_message);
    }
    return response->data;
}

} // namespace lsw::ipc

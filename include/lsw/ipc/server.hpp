#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <lsw/core/error.hpp>

namespace lsw::ipc {

using Handler =
    std::function<Result<nlohmann::json>(const nlohmann::json &args)>;

struct ServerOptions {
    std::filesystem::path socket_path;
    unsigned int mode{0700};
    int backlog{16};
    std::chrono::milliseconds accept_timeout{200};
};

/// lswd IPC 服务端：Unix Domain Socket + JSON Lines。
/// - start() 绑定并监听；run() 启动后台线程；stop() 关闭监听并等待连接收尾。
/// - 每个连接一个线程处理，支持多客户端并发。
class Server {
  public:
    explicit Server(ServerOptions options);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    /// 必须在 start() 之前注册；同名命令后注册者覆盖先注册者。
    void register_handler(std::string command, Handler handler);

    [[nodiscard]] Status start();
    void stop() noexcept;
    void request_stop() noexcept { stop_requested_.store(true); }

    /// 等待并处理一个连接；超时返回 false，错误返回 kIoError。
    [[nodiscard]] Result<bool> serve_once();

    /// 后台服务线程：析构时请求停止并 join。
    [[nodiscard]] std::jthread run();

    [[nodiscard]] bool running() const noexcept { return started_.load(); }
    [[nodiscard]] const std::filesystem::path &socket_path() const noexcept {
        return options_.socket_path;
    }

  private:
    void handle_connection(int fd);

    ServerOptions options_;
    std::unordered_map<std::string, Handler> handlers_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> started_{false};
    int listen_fd_{-1};
    std::mutex connections_mutex_;
    std::condition_variable connections_cv_;
    int active_connections_{0};
};

} // namespace lsw::ipc

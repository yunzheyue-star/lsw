/**
 * @file server.hpp
 * @brief lswd Unix Socket IPC 服务端（多连接）。
 * @details 文件位置：include/lsw/ipc/server.hpp（头文件）。
 *          属于 LSW IPC 与守护进程通信；公共行为以
 * docs/interfaces/daemon-ipc.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

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

/// @brief lswd IPC 服务端：Unix Domain Socket + JSON Lines。
/// - start() 绑定并监听；run() 启动后台线程；stop() 关闭监听并等待连接收尾。
/// - 每个连接一个线程处理，支持多客户端并发。
class Server {
  public:
    /// @brief 使用指定选项构造 IPC 服务端。
    /// @param options 服务端选项。
    explicit Server(ServerOptions options);
    /// @brief 停止服务端并释放资源。
    ~Server();

    /// @brief 禁用拷贝构造。
    Server(const Server &) = delete;
    /// @brief 禁用拷贝赋值。
    /// @return 当前服务端引用（该删除函数不会被调用）。
    Server &operator=(const Server &) = delete;

    /// @brief 必须在 start() 之前注册；同名命令后注册者覆盖先注册者。
    /// @param command 命令名。
    /// @param handler 命令处理器。
    void register_handler(std::string command, Handler handler);

    /// @brief 绑定 socket 并开始监听。
    /// @return 操作状态。
    [[nodiscard]] Status start();
    /// @brief 停止服务端并等待连接收尾。
    void stop() noexcept;
    /// @brief 请求停止服务端。
    void request_stop() noexcept { stop_requested_.store(true); }

    /// @brief 等待并处理一个连接；超时返回 false，错误返回 kIoError。
    /// @return 处理连接后返回是否继续服务，或错误状态。
    [[nodiscard]] Result<bool> serve_once();

    /// @brief 后台服务线程：析构时请求停止并 join。
    /// @return 后台线程对象。
    [[nodiscard]] std::jthread run();

    /// @brief 查询服务端是否正在运行。
    /// @return 运行中返回 true。
    [[nodiscard]] bool running() const noexcept { return started_.load(); }
    /// @brief 获取监听 socket 路径。
    /// @return socket 路径的常量引用。
    [[nodiscard]] const std::filesystem::path &socket_path() const noexcept {
        return options_.socket_path;
    }

  private:
    /// @brief 处理单个客户端连接。
    /// @param fd 已接受的连接文件描述符。
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

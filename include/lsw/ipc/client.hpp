/**
 * @file client.hpp
 * @brief lswd Unix Socket IPC 客户端。
 * @details 文件位置：include/lsw/ipc/client.hpp（头文件）。
 *          属于 LSW IPC 与守护进程通信；公共行为以
 * docs/interfaces/daemon-ipc.md 与 agent.md 为准。
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <lsw/core/error.hpp>

namespace lsw::ipc {

struct ClientOptions {
    std::filesystem::path socket_path;
    std::chrono::milliseconds connect_timeout{1000};
    std::chrono::milliseconds io_timeout{5000};
    int retries{0};
    std::chrono::milliseconds retry_delay{50};
};

/// @brief lswd IPC 客户端：每次调用建立一条 Unix Socket
/// 连接，发送一行请求、读取一行响应。
class Client {
  public:
    explicit Client(ClientOptions options);

    /// @brief 调用命令；通信失败或服务端 error 统一转为 Result 错误。
    [[nodiscard]] Result<nlohmann::json>
    call(std::string_view cmd,
         const nlohmann::json &args = nlohmann::json::object());

    [[nodiscard]] const ClientOptions &options() const noexcept {
        return options_;
    }

  private:
    [[nodiscard]] Result<nlohmann::json> call_once(std::string_view cmd,
                                                   const nlohmann::json &args);
    [[nodiscard]] std::string next_id();

    ClientOptions options_;
    std::uint64_t counter_{0};
    std::string id_prefix_;
};

} // namespace lsw::ipc

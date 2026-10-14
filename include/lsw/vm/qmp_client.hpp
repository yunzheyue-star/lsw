/**
 * @file qmp_client.hpp
 * @brief QEMU QMP 客户端（握手/命令/事件跳过）。
 * @details 文件位置：include/lsw/vm/qmp_client.hpp（头文件）。
 *          属于 LSW 虚拟机管理（QEMU/QMP/安装）；公共行为以
 * docs/interfaces/qmp.md 与 agent.md 为准。
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <lsw/core/error.hpp>

namespace lsw::vm {

struct QmpOptions {
    std::filesystem::path socket_path;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds io_timeout{5000};
};

/// @brief QEMU Monitor Protocol 客户端（SKILL-03）：
/// - Unix Domain Socket + 行分隔 JSON
/// - 连接后自动读取 greeting 并执行 `qmp_capabilities` 握手
/// - 自动跳过异步事件（"event"），按 id 匹配响应
class QmpClient {
  public:
    QmpClient() = default;
    explicit QmpClient(QmpOptions options);
    ~QmpClient();

    QmpClient(const QmpClient &) = delete;
    QmpClient &operator=(const QmpClient &) = delete;
    QmpClient(QmpClient &&other) noexcept;
    QmpClient &operator=(QmpClient &&other) noexcept;

    /// @brief 连接并在 connect_timeout 内等待 socket 出现、完成握手。
    [[nodiscard]] Result<void> connect();

    /// @brief 执行 QMP 命令，返回 `return` 字段。
    [[nodiscard]] Result<nlohmann::json>
    execute(std::string_view command,
            const nlohmann::json &args = nlohmann::json::object());

    [[nodiscard]] Result<void> system_powerdown();

    /// @brief true = VM 处于 running 或 paused（仍存活）。
    [[nodiscard]] Result<bool> query_status();

    void close() noexcept;
    [[nodiscard]] bool connected() const noexcept {
        return fd_ >= 0 && connected_;
    }
    [[nodiscard]] const std::filesystem::path &socket_path() const noexcept {
        return options_.socket_path;
    }

  private:
    [[nodiscard]] Result<int> connect_once();
    [[nodiscard]] Result<nlohmann::json>
    send_command(std::string_view command, const nlohmann::json &args,
                 bool require_connected);
    [[nodiscard]] Result<std::string> read_line();
    [[nodiscard]] Status write_all(std::string_view data);
    [[nodiscard]] Result<void> handshake();

    QmpOptions options_;
    int fd_{-1};
    std::string read_buffer_;
    std::uint64_t next_id_{0};
    bool connected_{false};
};

} // namespace lsw::vm

/**
 * @file qmp_client.hpp
 * @brief QEMU QMP 客户端（握手/命令/事件跳过）。
 * @details 文件位置：include/lsw/vm/qmp_client.hpp（头文件）。
 *          属于 LSW 虚拟机管理（QEMU/QMP/安装）；公共行为以
 * docs/interfaces/qmp.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
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
    /// @brief 构造未初始化的 QMP 客户端。
    QmpClient() = default;
    /// @brief 使用指定选项构造 QMP 客户端。
    /// @param options QMP 客户端选项。
    explicit QmpClient(QmpOptions options);
    /// @brief 关闭连接并释放资源。
    ~QmpClient();

    /// @brief 禁用拷贝构造。
    QmpClient(const QmpClient &) = delete;
    /// @brief 禁用拷贝赋值。
    /// @return 当前客户端引用（该删除函数不会被调用）。
    QmpClient &operator=(const QmpClient &) = delete;
    /// @brief 移动构造 QMP 客户端。
    /// @param other 被移动的客户端。
    QmpClient(QmpClient &&other) noexcept;
    /// @brief 移动赋值 QMP 客户端。
    /// @param other 被移动的客户端。
    /// @return 当前客户端引用。
    QmpClient &operator=(QmpClient &&other) noexcept;

    /// @brief 连接并在 connect_timeout 内等待 socket 出现、完成握手。
    /// @return 连接成功返回成功状态，否则返回错误。
    [[nodiscard]] Result<void> connect();

    /// @brief 执行 QMP 命令，返回 `return` 字段。
    /// @param command QMP 命令名。
    /// @param args 命令参数。
    /// @return 成功时返回命令的 return 字段。
    [[nodiscard]] Result<nlohmann::json>
    execute(std::string_view command,
            const nlohmann::json &args = nlohmann::json::object());

    /// @brief 请求虚拟机电源关闭。
    /// @return 命令执行状态。
    [[nodiscard]] Result<void> system_powerdown();

    /// @brief true = VM 处于 running 或 paused（仍存活）。
    /// @return 查询结果布尔值。
    [[nodiscard]] Result<bool> query_status();

    /// @brief 关闭连接。
    void close() noexcept;
    /// @brief 查询连接是否有效。
    /// @return 已连接返回 true。
    [[nodiscard]] bool connected() const noexcept {
        return fd_ >= 0 && connected_;
    }
    /// @brief 获取 QMP socket 路径。
    /// @return socket 路径的常量引用。
    [[nodiscard]] const std::filesystem::path &socket_path() const noexcept {
        return options_.socket_path;
    }

  private:
    /// @brief 尝试连接一次 QMP socket。
    /// @return 成功时返回文件描述符。
    [[nodiscard]] Result<int> connect_once();
    /// @brief 发送 QMP 命令并等待匹配响应。
    /// @param command 命令名。
    /// @param args 命令参数。
    /// @param require_connected 是否要求已建立连接。
    /// @return 成功时返回命令的 return 字段。
    [[nodiscard]] Result<nlohmann::json>
    send_command(std::string_view command, const nlohmann::json &args,
                 bool require_connected);
    /// @brief 从连接读取一行。
    /// @return 读取到的一行文本。
    [[nodiscard]] Result<std::string> read_line();
    /// @brief 将数据完整写入连接。
    /// @param data 待写入的数据。
    /// @return 写入状态。
    [[nodiscard]] Status write_all(std::string_view data);
    /// @brief 执行 QMP greeting 与 capabilities 握手。
    /// @return 握手状态。
    [[nodiscard]] Result<void> handshake();

    QmpOptions options_;
    int fd_{-1};
    std::string read_buffer_;
    std::uint64_t next_id_{0};
    bool connected_{false};
};

} // namespace lsw::vm

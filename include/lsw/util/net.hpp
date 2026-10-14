/**
 * @file net.hpp
 * @brief TCP 端口就绪等待工具。
 * @details 文件位置：include/lsw/util/net.hpp（头文件）。
 *          属于 LSW 工具库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include <lsw/core/error.hpp>

namespace lsw {

/// @brief 等待 TCP 端口可连接（用于 `lsw start` 等待 Windows OpenSSH 2222）。
/// @param host      目标地址，通常为 127.0.0.1
/// @param port      目标端口
/// @param timeout   总超时
/// @param interval  重试间隔
/// @return 成功返回空 Status；超时返回 kSshFailed
[[nodiscard]] Result<void> wait_for_tcp_port(
    std::string_view host, std::uint16_t port,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds interval = std::chrono::milliseconds(250));

/// @brief 等待真正的 SSH 服务：连接后必须读到以 "SSH-" 开头的 banner。
/// @details QEMU hostfwd 只要宿主机端口被监听就会 accept，不能代表 guest
/// sshd 已就绪。
/// @param host      目标地址，通常为 127.0.0.1
/// @param port      目标端口
/// @param timeout   总超时
/// @param interval  重试间隔
/// @return 成功返回空 Status；超时或未读到合法 banner 返回 kSshFailed
[[nodiscard]] Result<void> wait_for_ssh_banner(
    std::string_view host, std::uint16_t port,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds interval = std::chrono::milliseconds(250));

} // namespace lsw

/**
 * @file state.hpp
 * @brief P2 内存态守护进程状态机。
 * @details 文件位置：include/lsw/core/state.hpp（头文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <lsw/core/error.hpp>

namespace lsw::core {

/// @brief lswd 的内存状态机（P2 骨架）。
/// P3 起由 QEMU/QMP 驱动 vm_status 与 mounts；本阶段保证命令幂等与线程安全。
class DaemonState {
  public:
    /// @brief 构造守护进程状态机。
    /// @param version 上报给客户端的版本号。
    explicit DaemonState(std::string version = "0.1.0");

    /// @brief 分发 IPC 命令：status/start/shutdown/mount/unmount/shell/exec。
    /// @param command 命令名。
    /// @param args 命令参数，默认为空对象。
    /// @return 命令处理结果，失败返回错误。
    [[nodiscard]] Result<nlohmann::json>
    handle(const std::string &command,
           const nlohmann::json &args = nlohmann::json::object());

  private:
    /// @brief 处理 start 命令。
    /// @return 命令处理结果，失败返回错误。
    [[nodiscard]] Result<nlohmann::json> on_start();
    /// @brief 处理 shutdown 命令。
    /// @return 命令处理结果，失败返回错误。
    [[nodiscard]] Result<nlohmann::json> on_shutdown();
    /// @brief 处理 mount 命令。
    /// @return 命令处理结果，失败返回错误。
    [[nodiscard]] Result<nlohmann::json> on_mount();
    /// @brief 处理 unmount 命令。
    /// @return 命令处理结果，失败返回错误。
    [[nodiscard]] Result<nlohmann::json> on_unmount();
    /// @brief 处理 status 命令，返回当前状态快照。
    /// @return 命令处理结果，失败返回错误。
    [[nodiscard]] Result<nlohmann::json> on_status() const;

    mutable std::mutex mutex_;
    bool vm_running_{false};
    std::vector<std::string> mounts_;
    std::string version_;
    std::chrono::steady_clock::time_point started_at_{
        std::chrono::steady_clock::now()};
};

} // namespace lsw::core

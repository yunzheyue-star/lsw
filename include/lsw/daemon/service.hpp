/**
 * @file service.hpp
 * @brief lswd 业务服务：幂等 start/shutdown 与状态。
 * @details 文件位置：include/lsw/daemon/service.hpp（头文件）。
 *          属于 LSW lswd 守护进程；公共行为以 docs/interfaces/daemon-ipc.md 与
 * agent.md 为准。
 */

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/qemu.hpp>

namespace lsw::daemon {

/// @brief lswd 服务运行参数；支持通过环境变量覆盖，便于测试与特殊环境。
struct DaemonServiceOptions {
    vm::QemuVmOptions qemu_options;
    bool wait_for_ssh{true};
    std::chrono::milliseconds ssh_ready_timeout{120000};
    std::chrono::milliseconds ssh_poll_interval{250};
    bool try_ssh_shutdown{true};
};

/// @brief
/// 从环境变量构造默认参数（LSW_QEMU_*、LSW_WAIT_SSH、LSW_SSH_READY_TIMEOUT_MS）。
[[nodiscard]] DaemonServiceOptions default_daemon_service_options();

/// @brief lswd 的业务服务：管理 QemuVm 生命周期、幂等启停与运行状态。
class DaemonService {
  public:
    DaemonService(
        Config config, XdgPaths paths,
        DaemonServiceOptions options = default_daemon_service_options());

    /// @brief 分发 IPC 命令：start / shutdown / status / mount / unmount /
    /// shell / exec。
    [[nodiscard]] Result<nlohmann::json>
    handle(const std::string &command,
           const nlohmann::json &args = nlohmann::json::object());

    [[nodiscard]] bool shutdown_requested() const noexcept {
        return shutdown_requested_.load();
    }

  private:
    [[nodiscard]] Result<nlohmann::json> on_start();
    [[nodiscard]] Result<nlohmann::json> on_shutdown();
    [[nodiscard]] Result<nlohmann::json> on_status();
    [[nodiscard]] Result<nlohmann::json> on_mount();
    [[nodiscard]] Result<nlohmann::json> on_unmount();

    void try_ssh_shutdown();
    void cleanup_runtime() noexcept;

    mutable std::mutex mutex_;
    Config config_;
    XdgPaths paths_;
    DaemonServiceOptions options_;
    std::optional<vm::QemuVm> vm_;
    std::vector<std::string> mounts_;
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace lsw::daemon

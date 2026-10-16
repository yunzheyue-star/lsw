/**
 * @file service.hpp
 * @brief lswd 业务服务：幂等 start/shutdown 与状态。
 * @details 文件位置：include/lsw/daemon/service.hpp（头文件）。
 *          属于 LSW lswd 守护进程；公共行为以 docs/interfaces/daemon-ipc.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
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
#include <lsw/exec/windows_runner.hpp>
#include <lsw/fs/host_smb.hpp>
#include <lsw/fs/mount.hpp>
#include <lsw/fs/z_mapping.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/qemu.hpp>

namespace lsw::daemon {

/// @brief lswd 服务运行参数；支持通过环境变量覆盖，便于测试与特殊环境。
struct DaemonServiceOptions {
    vm::QemuVmOptions qemu_options;
    winfs::MountOptions mount_options;
    winfs::ZMappingOptions z_mapping_options;
    winfs::HostSmbOptions host_smb_options;
    bool enable_host_smb{true};
    bool wait_for_ssh{true};
    bool enable_mounts{true};
    bool enable_z_mapping{true};
    std::chrono::milliseconds ssh_ready_timeout{120000};
    std::chrono::milliseconds ssh_poll_interval{250};
    std::chrono::milliseconds exec_timeout{300000};
    std::chrono::milliseconds status_probe_timeout{8000};
    bool try_ssh_shutdown{true};
};

/// @brief 构造默认守护进程服务选项。
/// 从环境变量构造默认参数（LSW_QEMU_*、LSW_WAIT_SSH、LSW_SSH_READY_TIMEOUT_MS）。
/// @return 默认守护进程服务选项。
[[nodiscard]] DaemonServiceOptions default_daemon_service_options();

/// @brief lswd 的业务服务：管理 QemuVm 生命周期、幂等启停与运行状态。
class DaemonService {
  public:
    /// @brief 构造守护进程业务服务。
    /// @param config 全局配置。
    /// @param paths XDG 路径配置。
    /// @param options 服务选项。
    DaemonService(
        Config config, XdgPaths paths,
        DaemonServiceOptions options = default_daemon_service_options());

    /// @brief 分发 IPC 命令：start / shutdown / status / mount / unmount /
    /// shell / exec。
    /// @param command 命令名。
    /// @param args 命令参数。
    /// @return 成功时返回命令结果 JSON。
    [[nodiscard]] Result<nlohmann::json>
    handle(const std::string &command,
           const nlohmann::json &args = nlohmann::json::object());

    /// @brief 查询是否收到关闭请求。
    /// @return 收到关闭请求返回 true。
    [[nodiscard]] bool shutdown_requested() const noexcept {
        return shutdown_requested_.load();
    }

  private:
    /// @brief 处理 start 命令。
    /// @param args 命令参数（可含一次性 password，禁止记录日志）。
    /// @return 命令结果 JSON。
    [[nodiscard]] Result<nlohmann::json> on_start(const nlohmann::json &args);
    /// @brief 处理 shutdown 命令。
    /// @return 命令结果 JSON。
    [[nodiscard]] Result<nlohmann::json> on_shutdown();
    /// @brief 处理 status 命令：daemon/VM/磁盘/guest 信息。
    /// @return 命令结果 JSON。
    [[nodiscard]] Result<nlohmann::json> on_status();
    /// @brief 处理 mount 命令。
    /// @param args 命令参数（可含一次性 password，禁止记录日志）。
    /// @return 命令结果 JSON。
    [[nodiscard]] Result<nlohmann::json> on_mount(const nlohmann::json &args);
    /// @brief 处理 unmount 命令。
    /// @param args 命令参数（保留扩展）。
    /// @return 命令结果 JSON。
    [[nodiscard]] Result<nlohmann::json> on_unmount(const nlohmann::json &args);
    /// @brief 处理 shell 命令：把 cwd 映射为 ssh/powershell 参数。
    /// @param args 命令参数，必须含 cwd。
    /// @return 成功返回 { ssh_args: [...] }；VM 未运行返回 kVMNotRunning。
    [[nodiscard]] Result<nlohmann::json> on_shell(const nlohmann::json &args);
    /// @brief 处理 exec 命令：解析路径并在 Windows 内执行程序。
    /// @param args 命令参数：argv 与可选 cwd。
    /// @return 成功返回 stdout/stderr/exit_code。
    [[nodiscard]] Result<nlohmann::json> on_exec(const nlohmann::json &args);

    /// @brief 尝试通过 SSH 关闭虚拟机。
    void try_ssh_shutdown();
    /// @brief 清理运行时资源。
    void cleanup_runtime() noexcept;

    mutable std::mutex mutex_;
    Config config_;
    XdgPaths paths_;
    DaemonServiceOptions options_;
    std::optional<vm::QemuVm> vm_;
    std::optional<winfs::HostSmbManager> smb_manager_;
    std::vector<std::string> mounts_;
    std::atomic<bool> shutdown_requested_{false};
};

} // namespace lsw::daemon

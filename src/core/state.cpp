/**
 * @file state.cpp
 * @brief state 实现（核心库）。
 * @details 文件位置：src/core/state.cpp（源文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/core/state.hpp>

#include <array>
#include <utility>

namespace lsw::core {
namespace {

constexpr std::array<std::string_view, 2> kWindowsMounts = {"/mnt/Windows/C",
                                                            "/mnt/Windows/D"};

} // namespace

/// @brief 构造守护进程状态机并记录版本号。
/// @param version 守护进程版本字符串。
DaemonState::DaemonState(std::string version) : version_(std::move(version)) {}

/// @brief 分发 IPC 命令到对应的状态处理函数。
/// @param command 命令名称。
/// @param args 命令参数；当前实现暂未使用。
/// @return 命令处理结果 JSON；未知命令或未实现时返回对应错误。
Result<nlohmann::json> DaemonState::handle(const std::string &command,
                                           const nlohmann::json &) {
    if (command == "status") {
        return on_status();
    }
    if (command == "start") {
        return on_start();
    }
    if (command == "shutdown") {
        return on_shutdown();
    }
    if (command == "mount") {
        return on_mount();
    }
    if (command == "unmount") {
        return on_unmount();
    }
    if (command == "shell" || command == "exec") {
        return fail(ErrorCode::kNotImplemented,
                    command + " 命令将在后续阶段实现");
    }
    return fail(ErrorCode::kUnknownCommand, "未知命令: " + command);
}

/// @brief 处理 start 命令：将状态置为运行并返回幂等结果。
/// @return 包含 vm_status 与 already_running 的 JSON。
Result<nlohmann::json> DaemonState::on_start() {
    std::lock_guard lock(mutex_);
    const bool already_running = vm_running_;
    vm_running_ = true;
    return nlohmann::json{{"vm_status", "running"},
                          {"already_running", already_running}};
}

/// @brief 处理 shutdown 命令：将状态置为停止并返回幂等结果。
/// @return 包含 vm_status 与 already_stopped 的 JSON。
Result<nlohmann::json> DaemonState::on_shutdown() {
    std::lock_guard lock(mutex_);
    const bool already_stopped = !vm_running_;
    vm_running_ = false;
    return nlohmann::json{{"vm_status", "stopped"},
                          {"already_stopped", already_stopped}};
}

/// @brief 处理 mount 命令：确保挂载点已记录并返回当前列表。
/// @return 包含 mounts 的 JSON。
Result<nlohmann::json> DaemonState::on_mount() {
    std::lock_guard lock(mutex_);
    if (mounts_.empty()) {
        mounts_.assign(kWindowsMounts.begin(), kWindowsMounts.end());
    }
    return nlohmann::json{{"mounts", mounts_}};
}

/// @brief 处理 unmount 命令：清空挂载点并返回当前列表。
/// @return 包含 mounts 的 JSON。
Result<nlohmann::json> DaemonState::on_unmount() {
    std::lock_guard lock(mutex_);
    mounts_.clear();
    return nlohmann::json{{"mounts", mounts_}};
}

/// @brief 处理 status 命令：汇总版本、运行状态、挂载点与运行时长。
/// @return 包含守护进程状态的 JSON。
Result<nlohmann::json> DaemonState::on_status() const {
    std::lock_guard lock(mutex_);
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - started_at_)
                            .count();
    return nlohmann::json{{"daemon", "running"},
                          {"vm_status", vm_running_ ? "running" : "stopped"},
                          {"mounts", mounts_},
                          {"version", version_},
                          {"uptime_seconds", uptime}};
}

} // namespace lsw::core

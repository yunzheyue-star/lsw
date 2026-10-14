#include <lsw/core/state.hpp>

#include <array>
#include <utility>

namespace lsw::core {
namespace {

constexpr std::array<std::string_view, 2> kWindowsMounts = {"/mnt/Windows/C",
                                                            "/mnt/Windows/D"};

} // namespace

DaemonState::DaemonState(std::string version) : version_(std::move(version)) {}

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

Result<nlohmann::json> DaemonState::on_start() {
    std::lock_guard lock(mutex_);
    const bool already_running = vm_running_;
    vm_running_ = true;
    return nlohmann::json{{"vm_status", "running"},
                          {"already_running", already_running}};
}

Result<nlohmann::json> DaemonState::on_shutdown() {
    std::lock_guard lock(mutex_);
    const bool already_stopped = !vm_running_;
    vm_running_ = false;
    return nlohmann::json{{"vm_status", "stopped"},
                          {"already_stopped", already_stopped}};
}

Result<nlohmann::json> DaemonState::on_mount() {
    std::lock_guard lock(mutex_);
    if (mounts_.empty()) {
        mounts_.assign(kWindowsMounts.begin(), kWindowsMounts.end());
    }
    return nlohmann::json{{"mounts", mounts_}};
}

Result<nlohmann::json> DaemonState::on_unmount() {
    std::lock_guard lock(mutex_);
    mounts_.clear();
    return nlohmann::json{{"mounts", mounts_}};
}

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

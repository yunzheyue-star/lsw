/**
 * @file service.cpp
 * @brief service 实现（lswd 守护进程）。
 * @details 文件位置：src/daemon/service.cpp（源文件）。
 *          属于 LSW lswd 守护进程；公共行为以 docs/interfaces/daemon-ipc.md 与
 * agent.md 为准。
 */

#include <lsw/daemon/service.hpp>

#include <array>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

#include <lsw/core/process.hpp>
#include <lsw/util/log.hpp>
#include <lsw/util/net.hpp>

namespace lsw::daemon {
namespace {

constexpr std::array<std::string_view, 2> kWindowsMounts = {
    "/mnt/Windows/C",
    "/mnt/Windows/D",
};

std::optional<std::string> env_string(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

std::optional<bool> env_bool(const char *name) {
    const auto value = env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return *value == "1" || *value == "true" || *value == "on";
}

std::optional<long> env_long(const char *name) {
    const auto value = env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::strtol(value->c_str(), nullptr, 10);
}

} // namespace

DaemonServiceOptions default_daemon_service_options() {
    DaemonServiceOptions options;
    // Win10 默认 SeaBIOS；设置 LSW_QEMU_ENABLE_FIRMWARE=1 可启用 OVMF。
    options.qemu_options.enable_firmware = false;
    // 默认给 guest 15 秒优雅关机窗口，可用 LSW_SHUTDOWN_TIMEOUT_MS 调整。
    options.qemu_options.shutdown_timeout = std::chrono::seconds(15);
    if (auto value = env_string("LSW_QEMU_BINARY"); value.has_value()) {
        options.qemu_options.qemu_binary = *value;
    }
    if (auto value = env_bool("LSW_QEMU_ENABLE_KVM"); value.has_value()) {
        options.qemu_options.enable_kvm = *value;
    }
    if (auto value = env_bool("LSW_QEMU_ENABLE_FIRMWARE"); value.has_value()) {
        options.qemu_options.enable_firmware = *value;
    }
    if (auto value = env_string("LSW_QEMU_MACHINE"); value.has_value()) {
        options.qemu_options.machine = *value;
    }
    if (auto value = env_string("LSW_QEMU_DISK"); value.has_value()) {
        options.qemu_options.disk_image = *value;
    }
    if (auto value = env_string("LSW_QEMU_HOME"); value.has_value()) {
        options.qemu_options.home_dir = *value;
    }
    if (auto value = env_bool("LSW_WAIT_SSH"); value.has_value()) {
        options.wait_for_ssh = *value;
    }
    if (auto value = env_long("LSW_SSH_READY_TIMEOUT_MS"); value.has_value()) {
        options.ssh_ready_timeout = std::chrono::milliseconds(*value);
    }
    if (auto value = env_bool("LSW_TRY_SSH_SHUTDOWN"); value.has_value()) {
        options.try_ssh_shutdown = *value;
    }
    if (auto value = env_long("LSW_SHUTDOWN_TIMEOUT_MS"); value.has_value()) {
        options.qemu_options.shutdown_timeout =
            std::chrono::milliseconds(*value);
    }
    return options;
}

DaemonService::DaemonService(Config config, XdgPaths paths,
                             DaemonServiceOptions options)
    : config_(std::move(config)), paths_(std::move(paths)),
      options_(std::move(options)) {}

Result<nlohmann::json> DaemonService::handle(const std::string &command,
                                             const nlohmann::json &args) {
    log::logger()->info("lswd 收到命令: {}", command);
    if (command == "start") {
        return on_start();
    }
    if (command == "shutdown") {
        return on_shutdown();
    }
    if (command == "status") {
        return on_status();
    }
    if (command == "mount") {
        return on_mount();
    }
    if (command == "unmount") {
        return on_unmount();
    }
    if (command == "shell" || command == "exec") {
        (void)args;
        return fail(ErrorCode::kNotImplemented,
                    command + " 命令将在后续阶段实现");
    }
    return fail(ErrorCode::kUnknownCommand, "未知命令: " + command);
}

Result<nlohmann::json> DaemonService::on_start() {
    std::lock_guard lock(mutex_);
    if (!vm_.has_value()) {
        vm_.emplace(config_, paths_, options_.qemu_options);
    }

    const bool already_running = vm_->running();
    if (!already_running) {
        log::logger()->info("正在启动 QEMU 运行模式...");
        if (auto started = vm_->start(); !started) {
            vm_.reset();
            return std::unexpected(started.error());
        }
    }

    if (options_.wait_for_ssh) {
        log::logger()->info("等待 Windows OpenSSH 服务就绪（SSH banner）...");
        auto ready = wait_for_ssh_banner(
            "127.0.0.1", config_.network.ssh_host_port,
            options_.ssh_ready_timeout, options_.ssh_poll_interval);
        if (!ready) {
            (void)vm_->kill();
            vm_.reset();
            return std::unexpected(ready.error());
        }
    }

    return nlohmann::json{{"pid", vm_->pid()},
                          {"vm_status", "running"},
                          {"already_running", already_running}};
}

Result<nlohmann::json> DaemonService::on_shutdown() {
    std::lock_guard lock(mutex_);
    const bool already_stopped = !vm_.has_value() || !vm_->running();
    log::logger()->info("收到 shutdown 请求（already_stopped={}）",
                        already_stopped);

    if (!already_stopped) {
        if (options_.try_ssh_shutdown) {
            log::logger()->info("尝试通过 SSH 关闭 Windows...");
            try_ssh_shutdown();
        }
        log::logger()->info("通过 QMP/信号关闭 QEMU（最长等待 {} ms）",
                            options_.qemu_options.shutdown_timeout.count());
        (void)vm_->shutdown();
    }
    vm_.reset();
    mounts_.clear();
    cleanup_runtime();
    shutdown_requested_.store(true);

    return nlohmann::json{{"stopped", true},
                          {"already_stopped", already_stopped}};
}

Result<nlohmann::json> DaemonService::on_status() {
    std::lock_guard lock(mutex_);
    std::string vm_status = "stopped";
    int pid = -1;
    if (vm_.has_value() && vm_->running()) {
        pid = vm_->pid();
        if (auto status = vm_->status(); status) {
            vm_status = std::string(vm::to_string(*status));
        } else {
            vm_status = "unknown";
        }
    }
    return nlohmann::json{{"daemon", "running"},
                          {"vm_status", vm_status},
                          {"mounts", mounts_},
                          {"pid", pid}};
}

Result<nlohmann::json> DaemonService::on_mount() {
    std::lock_guard lock(mutex_);
    if (mounts_.empty()) {
        mounts_.assign(kWindowsMounts.begin(), kWindowsMounts.end());
    }
    return nlohmann::json{{"mounts", mounts_}};
}

Result<nlohmann::json> DaemonService::on_unmount() {
    std::lock_guard lock(mutex_);
    mounts_.clear();
    return nlohmann::json{{"mounts", mounts_}};
}

void DaemonService::try_ssh_shutdown() {
    const std::string target = config_.user.windows_user + "@127.0.0.1";
    (void)run_process({"ssh", "-p",
                       std::to_string(config_.network.ssh_host_port), "-o",
                       "BatchMode=yes", "-o", "ConnectTimeout=3", target,
                       "shutdown", "/s", "/t", "0"},
                      RunOptions{.timeout = std::chrono::milliseconds(5000)});
}

void DaemonService::cleanup_runtime() noexcept {
    std::error_code error;
    std::filesystem::remove(paths_.runtime_dir / "qmp.sock", error);
    error.clear();
    std::filesystem::remove(paths_.runtime_dir / "qga.sock", error);
}

} // namespace lsw::daemon

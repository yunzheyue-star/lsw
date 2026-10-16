/**
 * @file service.cpp
 * @brief service 实现（lswd 守护进程）。
 * @details 文件位置：src/daemon/service.cpp（源文件）。
 *          属于 LSW lswd 守护进程；公共行为以 docs/interfaces/daemon-ipc.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/daemon/service.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

#include <lsw/core/process.hpp>
#include <lsw/exec/windows_runner.hpp>
#include <lsw/fs/credentials.hpp>
#include <lsw/fs/host_smb.hpp>
#include <lsw/fs/mount.hpp>
#include <lsw/fs/z_mapping.hpp>
#include <lsw/shell/powershell.hpp>
#include <lsw/util/log.hpp>
#include <lsw/util/net.hpp>

namespace lsw::daemon {
namespace {

/** @brief 读取字符串环境变量。
 * @param name 环境变量名。
 * @return 变量存在且非空时返回其值；否则返回 nullopt。
 */
std::optional<std::string> env_string(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

/** @brief 读取布尔环境变量。
 * @param name 环境变量名。
 * @return 变量存在时返回布尔值；否则返回 nullopt。
 */
std::optional<bool> env_bool(const char *name) {
    const auto value = env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return *value == "1" || *value == "true" || *value == "on";
}

/** @brief 解析本次 mount/start 使用的 Windows 凭据。
 * 优先级：IPC 一次性 password > 环境变量 > keyring；禁止记录密码。
 * @param config 全局配置。
 * @param args IPC 参数。
 * @return 成功返回凭据；无处获取或 keyring 出错时返回错误。
 */
Result<winfs::WindowsCredential>
resolve_windows_credential(const Config &config, const nlohmann::json &args) {
    const std::string user = config.user.windows_user;
    if (args.contains("password") && args["password"].is_string() &&
        !args["password"].get<std::string>().empty()) {
        return winfs::WindowsCredential{user,
                                        args["password"].get<std::string>()};
    }
    if (auto from_env = winfs::credential_from_environment(user);
        from_env.has_value()) {
        return *from_env;
    }
    winfs::CredentialStore store;
    auto stored = store.lookup(user);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    if (stored->has_value()) {
        return **stored;
    }
    return fail(
        ErrorCode::kMountFailed,
        "未找到 Windows 凭据：请用 `lsw start` 交互输入、配置 "
        "keyring（secret-tool），或设置 LSW_WINDOWS_PASSWORD 仅供本次使用");
}

/** @brief 读取长整型环境变量。
 * @param name 环境变量名。
 * @return 变量存在时返回数值；否则返回 nullopt。
 */
std::optional<long> env_long(const char *name) {
    const auto value = env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::strtol(value->c_str(), nullptr, 10);
}

} // namespace

/** @brief 从环境变量构造守护进程服务默认选项。
 * @return 组装完成的服务选项。
 */
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
    if (auto value = env_string("LSW_MOUNT_HELPER"); value.has_value()) {
        options.mount_options.helper_path = *value;
    }
    if (auto value = env_string("LSW_MOUNT_PRIVILEGE"); value.has_value()) {
        options.mount_options.privilege = *value;
    }
    if (auto value = env_string("LSW_MOUNT_ROOT"); value.has_value()) {
        options.mount_options.mount_root = *value;
    }
    if (auto value = env_string("LSW_CIFS_VERSION"); value.has_value()) {
        options.mount_options.cifs_version = *value;
    }
    if (auto value = env_string("LSW_MOUNTS_FILE"); value.has_value()) {
        options.mount_options.mounts_file = *value;
    }
    if (auto value = env_long("LSW_MOUNT_TIMEOUT_MS"); value.has_value()) {
        options.mount_options.timeout = std::chrono::milliseconds(*value);
    }
    if (auto value = env_bool("LSW_DISABLE_MOUNTS"); value.has_value()) {
        options.enable_mounts = !*value;
    }
    if (auto value = env_bool("LSW_DISABLE_Z_MAPPING"); value.has_value()) {
        options.enable_z_mapping = !*value;
    }
    if (auto value = env_string("LSW_SSH_BINARY"); value.has_value()) {
        options.z_mapping_options.ssh_binary = *value;
    }
    if (auto value = env_bool("LSW_DISABLE_HOST_SMB"); value.has_value()) {
        options.enable_host_smb = !*value;
    }
    if (auto value = env_string("LSW_HOST_SMB_HELPER"); value.has_value()) {
        options.host_smb_options.helper_path = *value;
    }
    if (auto value = env_string("LSW_HOST_SMB_PRIVILEGE"); value.has_value()) {
        options.host_smb_options.privilege = *value;
    }
    if (auto value = env_string("LSW_HOST_SMB_HOME"); value.has_value()) {
        options.host_smb_options.home = *value;
    }
    if (auto value = env_string("LSW_HOST_SMB_SHARE"); value.has_value()) {
        options.host_smb_options.share = *value;
    }
    if (auto value = env_long("LSW_EXEC_TIMEOUT_MS"); value.has_value()) {
        options.exec_timeout = std::chrono::milliseconds(*value);
    }
    return options;
}

/** @brief 构造守护进程服务并保存配置。
 * @param config 全局配置。
 * @param paths XDG 数据与运行时路径。
 * @param options 服务运行参数。
 */
DaemonService::DaemonService(Config config, XdgPaths paths,
                             DaemonServiceOptions options)
    : config_(std::move(config)), paths_(std::move(paths)),
      options_(std::move(options)) {}

/** @brief 分发并执行 IPC 命令。
 * @param command 命令名。
 * @param args 命令参数。
 * @return 成功返回命令结果；失败返回相应错误。
 */
Result<nlohmann::json> DaemonService::handle(const std::string &command,
                                             const nlohmann::json &args) {
    log::logger()->info("lswd 收到命令: {}", command);
    if (command == "start") {
        return on_start(args);
    }
    if (command == "shutdown") {
        return on_shutdown();
    }
    if (command == "status") {
        return on_status();
    }
    if (command == "mount") {
        return on_mount(args);
    }
    if (command == "unmount") {
        return on_unmount(args);
    }
    if (command == "shell") {
        return on_shell(args);
    }
    if (command == "exec") {
        return on_exec(args);
    }
    return fail(ErrorCode::kUnknownCommand, "未知命令: " + command);
}

/** @brief 处理 start 命令：幂等启动 QEMU 并按需等待 SSH。
 * @return 成功返回 pid 与状态信息；失败返回相应错误。
 */
/// @brief 处理 start 命令：幂等启动 QEMU、等待 SSH，然后确保 Z: 与 CIFS
/// 挂载。
/// @param args 命令参数（可含一次性 password，禁止记录日志）。
/// @return 成功返回 pid、状态、挂载点与 Z:；失败返回相应错误。
Result<nlohmann::json> DaemonService::on_start(const nlohmann::json &args) {
    std::lock_guard lock(mutex_);
    if (options_.enable_host_smb && options_.enable_z_mapping) {
        if (!smb_manager_.has_value()) {
            if (options_.host_smb_options.home.empty()) {
                if (const char *home = std::getenv("HOME"); home != nullptr) {
                    options_.host_smb_options.home = home;
                }
            }
            smb_manager_.emplace(options_.host_smb_options);
            if (auto started = smb_manager_->start(); !started) {
                smb_manager_.reset();
                return std::unexpected(started.error());
            }
        }
    }
    if (!vm_.has_value()) {
        vm_.emplace(config_, paths_, options_.qemu_options);
    }

    const bool already_running = vm_->running();
    if (!already_running) {
        log::logger()->info("正在启动 QEMU 运行模式...");
        if (auto started = vm_->start(); !started) {
            vm_.reset();
            if (smb_manager_.has_value()) {
                smb_manager_->stop();
                smb_manager_.reset();
            }
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
            if (smb_manager_.has_value()) {
                smb_manager_->stop();
                smb_manager_.reset();
            }
            return std::unexpected(ready.error());
        }
    }

    std::string z_drive;
    std::vector<std::string> mount_points;
    if (options_.enable_z_mapping) {
        // Windows 盘符按登录会话隔离：daemon 启动期不建立 SMB 会话，
        // 只报告配置；实际映射由需要 Z: 的 SSH 会话（lsw shell / 验收）
        // 在各自会话内调用 ZDriveMapper 完成，避免占用 QEMU 网关的唯一
        // 首次会话。
        z_drive = config_.filesystem.windows_drive_z;
    }
    if (options_.enable_mounts) {
        auto credential = resolve_windows_credential(config_, args);
        if (!credential) {
            return std::unexpected(credential.error());
        }
        winfs::CifsMountManager manager(config_, options_.mount_options);
        auto mounted = manager.ensure_mounts(*credential);
        if (!mounted) {
            return std::unexpected(mounted.error());
        }
        mounts_ = *mounted;
        mount_points = mounts_;
    }

    nlohmann::json response{{"pid", vm_->pid()},
                            {"vm_status", "running"},
                            {"already_running", already_running},
                            {"mounts", mount_points}};
    if (!z_drive.empty()) {
        response["z_drive"] = z_drive;
    }
    return response;
}

/** @brief 处理 shutdown 命令：关闭虚拟机并清理运行时资源。
 * @return 成功返回停止状态信息。
 */
Result<nlohmann::json> DaemonService::on_shutdown() {
    std::lock_guard lock(mutex_);
    const bool already_stopped = !vm_.has_value() || !vm_->running();
    log::logger()->info("收到 shutdown 请求（already_stopped={}）",
                        already_stopped);

    if (options_.enable_mounts) {
        winfs::CifsMountManager manager(config_, options_.mount_options);
        if (auto unmounted = manager.unmount_all(); !unmounted) {
            log::logger()->warn("卸载 Windows 共享失败: {}",
                                unmounted.error().message);
        } else {
            log::logger()->info("Windows 共享已卸载");
        }
    }

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
    if (smb_manager_.has_value()) {
        smb_manager_->stop();
        smb_manager_.reset();
    }
    mounts_.clear();
    cleanup_runtime();
    shutdown_requested_.store(true);

    return nlohmann::json{{"stopped", true},
                          {"already_stopped", already_stopped}};
}

/** @brief 处理 status 命令：返回守护进程、虚拟机与挂载状态。
 * @return 状态信息 JSON。
 */
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

/** @brief 处理 mount 命令：初始化并返回 Windows 挂载点。
 * @return 挂载点列表 JSON。
 */
/// @brief 处理 mount 命令：确保 C$/D$ 已挂载。
/// @param args 命令参数（可含一次性 password，禁止记录日志）。
/// @return 挂载点列表 JSON。
Result<nlohmann::json> DaemonService::on_mount(const nlohmann::json &args) {
    std::lock_guard lock(mutex_);
    if (!options_.enable_mounts) {
        if (mounts_.empty()) {
            for (const char letter : options_.mount_options.drives) {
                mounts_.push_back(
                    (config_.filesystem.mount_root / std::string(1, letter))
                        .string());
            }
        }
        return nlohmann::json{{"mounts", mounts_}};
    }
    auto credential = resolve_windows_credential(config_, args);
    if (!credential) {
        return std::unexpected(credential.error());
    }
    winfs::CifsMountManager manager(config_, options_.mount_options);
    auto mounted = manager.ensure_mounts(*credential);
    if (!mounted) {
        return std::unexpected(mounted.error());
    }
    mounts_ = *mounted;
    return nlohmann::json{{"mounts", mounts_}};
}

/** @brief 处理 unmount 命令：清空挂载点。
 * @return 清空后的挂载点列表 JSON。
 */

/** @brief 处理 shell 命令：把 cwd 映射为 ssh/powershell 参数。
 * @param args 命令参数，必须含 cwd。
 * @return 成功返回 { ssh_args: [...] }；VM 未运行返回 kVMNotRunning。
 */
Result<nlohmann::json> DaemonService::on_shell(const nlohmann::json &args) {
    std::lock_guard lock(mutex_);
    if (!vm_.has_value() || !vm_->running()) {
        return fail(ErrorCode::kVMNotRunning,
                    "Windows 虚拟机未运行，请先执行 lsw start");
    }
    if (!args.contains("cwd") || !args["cwd"].is_string()) {
        return fail(ErrorCode::kInvalidArgument,
                    "shell 命令缺少字符串参数 cwd");
    }
    const std::filesystem::path cwd = args["cwd"].get<std::string>();
    auto home = expand_tilde(config_.filesystem.linux_home_share,
                             PathEnvironment::from_process());
    if (!home) {
        return std::unexpected(home.error());
    }
    if (home->empty()) {
        return fail(ErrorCode::kInvalidArgument,
                    "无法确定 Linux $HOME，无法映射 cwd");
    }
    auto ssh_args = shell::build_ssh_args(
        config_, cwd, *home, options_.z_mapping_options.ssh_binary);
    if (!ssh_args) {
        return std::unexpected(ssh_args.error());
    }
    return nlohmann::json{{"ssh_args", *ssh_args}};
}
/// @brief 处理 unmount 命令：卸载挂载根下全部 CIFS 挂载点。
/// @param args 命令参数（保留扩展）。
/// @return 清空后的挂载点列表 JSON。

/// @brief 处理 exec 命令：解析程序路径并在 Windows 内执行。
/// @param args 命令参数：argv（必填）与 cwd（可选）。
/// @return 成功返回 stdout/stderr/exit_code；VM 未运行返回 kVMNotRunning。
Result<nlohmann::json> DaemonService::on_exec(const nlohmann::json &args) {
    std::lock_guard lock(mutex_);
    if (!vm_.has_value() || !vm_->running()) {
        return fail(ErrorCode::kVMNotRunning,
                    "Windows 虚拟机未运行，请先执行 lsw start");
    }
    if (!args.contains("argv") || !args["argv"].is_array() ||
        args["argv"].empty()) {
        return fail(ErrorCode::kInvalidArgument,
                    "exec 命令缺少非空数组参数 argv");
    }
    std::vector<std::string> argv;
    for (const auto &item : args["argv"]) {
        if (!item.is_string()) {
            return fail(ErrorCode::kInvalidArgument,
                        "exec.argv 含非字符串元素");
        }
        argv.push_back(item.get<std::string>());
    }

    auto home = expand_tilde(config_.filesystem.linux_home_share,
                             PathEnvironment::from_process());
    if (!home) {
        return std::unexpected(home.error());
    }
    std::filesystem::path cwd = *home;
    if (args.contains("cwd") && args["cwd"].is_string()) {
        cwd = args["cwd"].get<std::string>();
    }

    exec::ExecOptions exec_options;
    exec_options.ssh_binary = options_.z_mapping_options.ssh_binary;
    exec_options.timeout = options_.exec_timeout;
    auto target = exec::resolve_target(argv[0], cwd, *home, exec_options);
    if (!target) {
        return std::unexpected(target.error());
    }
    std::vector<std::string> program_args(argv.begin() + 1, argv.end());
    auto ssh_args = exec::build_ssh_args(config_, target->windows_path,
                                         program_args, exec_options.ssh_binary);
    if (!ssh_args) {
        return std::unexpected(ssh_args.error());
    }
    auto result =
        run_process(*ssh_args, RunOptions{.timeout = options_.exec_timeout});
    if (!result) {
        if (result.error().code == ErrorCode::kProcessTimeout) {
            return fail(ErrorCode::kSshFailed,
                        "Windows 程序执行超时（" +
                            std::to_string(options_.exec_timeout.count()) +
                            " ms）");
        }
        return std::unexpected(result.error());
    }
    return nlohmann::json{{"stdout", result->stdout_data},
                          {"stderr", result->stderr_data},
                          {"exit_code", result->exit_code}};
}
Result<nlohmann::json> DaemonService::on_unmount(const nlohmann::json &args) {
    std::lock_guard lock(mutex_);
    (void)args;
    winfs::CifsMountManager manager(config_, options_.mount_options);
    if (auto unmounted = manager.unmount_all(); !unmounted) {
        return std::unexpected(unmounted.error());
    }
    mounts_.clear();
    return nlohmann::json{{"mounts", mounts_}};
}

/** @brief 尝试通过 SSH 请求 Windows 关机。
 */
void DaemonService::try_ssh_shutdown() {
    const std::string target = config_.user.windows_user + "@127.0.0.1";
    (void)run_process(
        {"ssh", "-p", std::to_string(config_.network.ssh_host_port), "-o",
         "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new", "-o",
         "ConnectTimeout=3", target, "shutdown", "/s", "/t", "0"},
        RunOptions{.timeout = std::chrono::milliseconds(5000)});
}

/** @brief 清理运行时 socket 文件。
 */
void DaemonService::cleanup_runtime() noexcept {
    std::error_code error;
    std::filesystem::remove(paths_.runtime_dir / "qmp.sock", error);
    error.clear();
    std::filesystem::remove(paths_.runtime_dir / "qga.sock", error);
}

} // namespace lsw::daemon

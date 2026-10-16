/**
 * @file main.cpp
 * @brief lsw CLI 入口：install/start/shutdown 命令解析。
 * @details 文件位置：src/cli/main.cpp（源文件）。
 *          属于 LSW lsw CLI；公共行为以 docs/interfaces/cli.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include <CLI/CLI.hpp>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/core/process.hpp>
#include <lsw/fs/credentials.hpp>
#include <lsw/ipc/client.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/install.hpp>
#include <lsw/vm/qemu.hpp>
#include <lsw/vm/status.hpp>

#ifndef LSW_VERSION
#define LSW_VERSION "0.1.0"
#endif

namespace {

/// @brief 输出错误信息并转换为进程退出码。
/// @param error 待报告的错误对象。
/// @return 对应的进程退出码。
int report_error(const lsw::Error &error) {
    std::cerr << "lsw: " << error.message << "\n";
    return lsw::to_exit_code(error.code);
}

/// @brief 查找当前 CLI 同目录或 PATH 中的 lswd 可执行文件。
/// @return lswd 可执行文件路径；找不到时返回空路径。
std::filesystem::path daemon_binary_path() {
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        const auto candidate = self.parent_path() / "lswd";
        std::error_code exists_error;
        if (std::filesystem::exists(candidate, exists_error)) {
            return candidate;
        }
    }
    return lsw::vm::find_executable("lswd");
}

/// @brief 确保 lswd 守护进程已运行，必要时启动并等待其就绪。
/// @param paths 当前用户 XDG 路径集合。
/// @return 成功返回空状态，否则返回具体错误。
lsw::Status ensure_daemon(const lsw::XdgPaths &paths) {
    const auto socket = paths.runtime_dir / "lswd.sock";
    lsw::ipc::ClientOptions probe_options;
    probe_options.socket_path = socket;
    probe_options.connect_timeout = std::chrono::milliseconds(500);
    probe_options.io_timeout = std::chrono::milliseconds(1000);
    lsw::ipc::Client probe(probe_options);
    if (probe.call("status")) {
        return {};
    }

    if (auto status = lsw::ensure_directories(paths); !status) {
        return status;
    }
    const auto binary = daemon_binary_path();
    if (binary.empty()) {
        return lsw::fail(lsw::ErrorCode::kMissingDependency,
                         "找不到 lswd 可执行文件");
    }
    const auto log_file = paths.state_dir / "lswd.log";
    if (auto status = lsw::spawn_detached(
            {binary.string()},
            lsw::core::SpawnOptions{.stdout_file = log_file,
                                    .stderr_file = log_file});
        !status) {
        return status;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (probe.call("status")) {
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return lsw::fail(lsw::ErrorCode::kDaemonUnavailable,
                     "lswd 启动超时，请查看 " + log_file.string());
}

/// @brief 读取隐藏输入的密码（仅 TTY，密码不落盘、不进 argv）。
/// @param prompt 提示文本。
/// @return 读取到的非空密码；非 TTY 或用户空输入时返回 nullopt。
std::optional<std::string> prompt_password(const std::string &prompt) {
    if (::isatty(STDIN_FILENO) == 0) {
        return std::nullopt;
    }
    termios original{};
    if (::tcgetattr(STDIN_FILENO, &original) != 0) {
        return std::nullopt;
    }
    termios quiet = original;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) != 0) {
        return std::nullopt;
    }
    std::cout << prompt << std::flush;
    std::string password;
    std::getline(std::cin, password);
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    std::cout << "\n";
    if (password.empty()) {
        return std::nullopt;
    }
    return password;
}

/// @brief 为 start/mount 准备凭据参数：环境变量 > 交互输入 > keyring 留空。
/// @param config 全局配置。
/// @param save_password 是否把交互输入保存到 keyring。
/// @return 传给 lswd 的参数 JSON；密码字段只存在于内存与 Unix socket。
nlohmann::json credential_args(const lsw::Config &config, bool save_password) {
    nlohmann::json args = nlohmann::json::object();
    if (std::getenv("LSW_WINDOWS_PASSWORD") != nullptr) {
        return args;
    }
    const char *disable_mounts = std::getenv("LSW_DISABLE_MOUNTS");
    const char *disable_z = std::getenv("LSW_DISABLE_Z_MAPPING");
    if (disable_mounts != nullptr && std::string(disable_mounts) == "1" &&
        disable_z != nullptr && std::string(disable_z) == "1") {
        return args;
    }
    lsw::winfs::CredentialStore store;
    auto stored = store.lookup(config.user.windows_user);
    if (stored && stored->has_value()) {
        return args;
    }
    auto password =
        prompt_password("请输入 Windows 用户 " + config.user.windows_user +
                        " 的密码（仅本次使用，不会明文落盘）: ");
    if (!password.has_value()) {
        return args;
    }
    args["password"] = *password;
    if (save_password && store.available()) {
        if (auto saved = store.store({config.user.windows_user, *password});
            !saved) {
            std::cerr << "lsw: 保存到 keyring 失败: " << saved.error().message
                      << "\n";
        }
    }
    return args;
}

/// @brief 通过 IPC 向 lswd 发送命令并输出响应。
/// @param paths 当前用户 XDG 路径集合。
/// @param command 要发送给 lswd 的命令名称。
/// @param args 命令参数（可含一次性密码，禁止记录日志）。
/// @return 进程退出码，0 表示命令执行成功。
int run_daemon_command(const lsw::XdgPaths &paths, const std::string &command,
                       const nlohmann::json &args = nlohmann::json::object()) {
    lsw::ipc::ClientOptions options;
    options.socket_path = paths.runtime_dir / "lswd.sock";
    options.connect_timeout = std::chrono::seconds(1);
    options.io_timeout = command == "start" ? std::chrono::seconds(180)
                                            : std::chrono::seconds(60);
    lsw::ipc::Client client(options);

    auto result = client.call(command, args);
    if (!result) {
        if (command == "shutdown" &&
            result.error().code == lsw::ErrorCode::kDaemonUnavailable) {
            std::cout << "lswd 未运行，Windows 已处于停止状态。\n";
            return 0;
        }
        return report_error(result.error());
    }
    std::cout << result->dump() << "\n";
    return 0;
}

/// @brief 进入 Windows PowerShell：读取 cwd → IPC shell → execvp ssh。
/// @param paths 当前用户 XDG 路径。
/// @return 仅在失败时返回退出码；成功时进程已被 ssh 替换。
int run_shell_command(const lsw::XdgPaths &paths) {
    std::error_code error;
    const auto cwd = std::filesystem::current_path(error);
    if (error) {
        return report_error(
            lsw::make_error(lsw::ErrorCode::kPathMappingFailed,
                            "无法获取当前目录: " + error.message()));
    }
    if (auto status = ensure_daemon(paths); !status) {
        return report_error(status.error());
    }

    lsw::ipc::ClientOptions options;
    options.socket_path = paths.runtime_dir / "lswd.sock";
    options.connect_timeout = std::chrono::seconds(1);
    options.io_timeout = std::chrono::seconds(60);
    lsw::ipc::Client client(options);
    auto result = client.call("shell", nlohmann::json{{"cwd", cwd.string()}});
    if (!result) {
        return report_error(result.error());
    }
    if (!result->contains("ssh_args") || !(*result)["ssh_args"].is_array()) {
        return report_error(
            lsw::make_error(lsw::ErrorCode::kProtocolError,
                            "lswd 返回的 shell 响应缺少 ssh_args"));
    }
    std::vector<std::string> args;
    for (const auto &item : (*result)["ssh_args"]) {
        if (!item.is_string()) {
            return report_error(
                lsw::make_error(lsw::ErrorCode::kProtocolError,
                                "lswd 返回的 ssh_args 含非字符串元素"));
        }
        args.push_back(item.get<std::string>());
    }
    if (args.empty()) {
        return report_error(lsw::make_error(lsw::ErrorCode::kProtocolError,
                                            "lswd 返回的 ssh_args 为空"));
    }

    // 交给 ssh 之前清掉本地终端（含滚动回退与启动命令行），让 PowerShell
    // 从干净屏幕开始；stdout 不是 TTY 时保持原样。
    if (::isatty(STDOUT_FILENO) != 0) {
        constexpr std::string_view kClearTerminal = "\x1b[2J\x1b[3J\x1b[H";
        const ssize_t ignored = ::write(STDOUT_FILENO, kClearTerminal.data(),
                                        kClearTerminal.size());
        static_cast<void>(ignored);
    }

    std::vector<char *> raw;
    raw.reserve(args.size() + 1);
    for (auto &arg : args) {
        raw.push_back(arg.data());
    }
    raw.push_back(nullptr);
    ::execvp(raw.front(), raw.data());
    const int saved = errno;
    const auto code = saved == ENOENT ? lsw::ErrorCode::kMissingDependency
                                      : lsw::ErrorCode::kSshFailed;
    return report_error(lsw::make_error(code, "无法执行 " + args.front() +
                                                  ": " + std::strerror(saved)));
}

/// @brief 在 Windows 内执行 `lsw <program> [args...]`。
/// @param paths 当前用户 XDG 路径。
/// @param argv 程序路径及参数（argv[0] 为程序）。
/// @return 程序退出码（透传）；daemon 错误返回对应退出码。
int run_exec_command(const lsw::XdgPaths &paths,
                     const std::vector<std::string> &argv) {
    std::error_code error;
    const auto cwd = std::filesystem::current_path(error);
    if (error) {
        return report_error(
            lsw::make_error(lsw::ErrorCode::kPathMappingFailed,
                            "无法获取当前目录: " + error.message()));
    }
    if (auto status = ensure_daemon(paths); !status) {
        return report_error(status.error());
    }

    lsw::ipc::ClientOptions options;
    options.socket_path = paths.runtime_dir / "lswd.sock";
    options.connect_timeout = std::chrono::seconds(1);
    options.io_timeout = std::chrono::seconds(360);
    lsw::ipc::Client client(options);
    auto result = client.call(
        "exec", nlohmann::json{{"argv", argv}, {"cwd", cwd.string()}});
    if (!result) {
        return report_error(result.error());
    }
    const std::string stdout_data = result->value("stdout", std::string{});
    const std::string stderr_data = result->value("stderr", std::string{});
    if (!stdout_data.empty()) {
        std::cout << stdout_data;
        if (stdout_data.back() != '\n') {
            std::cout << "\n";
        }
    }
    if (!stderr_data.empty()) {
        std::cerr << stderr_data;
        if (stderr_data.back() != '\n') {
            std::cerr << "\n";
        }
    }
    const int exit_code = result->value("exit_code", 0);
    if (exit_code < 0) {
        return 1;
    }
    return exit_code & 0xFF;
}

/// @brief 判断位置参数是否像一个要交给 Windows 执行的程序。
/// 满足任一条件即视为程序：包含路径分隔符、扩展名为
/// .exe/.bat/.cmd/.com（大小写不敏感）、或当前目录下存在同名普通文件。
/// @param value 用户输入的第一个位置参数。
/// @return 像程序返回 true；否则返回 false（按未知命令处理）。
bool looks_like_program(const std::string &value) {
    if (value.find('/') != std::string::npos ||
        value.find('\\') != std::string::npos) {
        return true;
    }
    std::string extension = std::filesystem::path(value).extension().string();
    for (char &ch : extension) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (extension == ".exe" || extension == ".bat" || extension == ".cmd" ||
        extension == ".com") {
        return true;
    }
    std::error_code error;
    const auto candidate = std::filesystem::absolute(value, error);
    if (error) {
        return false;
    }
    return std::filesystem::is_regular_file(candidate, error) && !error;
}

/// @brief 人类可读的 virtio 来源说明。
/// @param source virtio_source 字段。
/// @return 中文说明。
std::string virtio_source_text(const std::string &source) {
    if (source == "runtime_probe") {
        return "运行时探测";
    }
    if (source == "cache") {
        return "上次运行缓存";
    }
    if (source == "install_marker") {
        return "安装标记（仅表示提供过 virtio ISO）";
    }
    return "无";
}

/// @brief 人类可读的 virtio 状态。
/// @param value virtio_win 字段。
/// @return 中文说明。
std::string virtio_status_text(const std::string &value) {
    if (value == "installed") {
        return "已安装";
    }
    if (value == "not_installed") {
        return "未检测到";
    }
    return "未知（启动 VM 后可探测）";
}

/// @brief 执行 `lsw status`：优先询问 daemon，失败时离线读取磁盘/缓存。
/// @param paths XDG 路径。
/// @param json_output 是否输出 JSON。
/// @return 进程退出码。
int run_status_command(const lsw::XdgPaths &paths, bool json_output) {
    lsw::Config config = lsw::default_config();
    if (auto loaded = lsw::load_config(paths.config_file, true); loaded) {
        config = *loaded;
    } else {
        return report_error(loaded.error());
    }

    lsw::ipc::ClientOptions options;
    options.socket_path = paths.runtime_dir / "lswd.sock";
    options.connect_timeout = std::chrono::milliseconds(300);
    options.io_timeout = std::chrono::milliseconds(2000);
    lsw::ipc::Client client(options);
    auto daemon_status = client.call("status");
    nlohmann::json status = daemon_status
                                ? *daemon_status
                                : lsw::vm::offline_status_json(config, paths);
    if (!daemon_status) {
        status["daemon"] = "stopped";
    }

    if (json_output) {
        std::cout << status.dump(2) << "\n";
        return 0;
    }

    const bool daemon_running = status.value("daemon", "stopped") == "running";
    const bool installed = status.value("vm_installed", false);
    const bool disk_exists = status.value("disk_exists", false);
    const std::string vm_status = status.value("vm_status", "stopped");
    const int pid = status.value("pid", -1);
    const std::string disk = status.value("disk", std::string{});

    std::string vm_state = "未知";
    if (vm_status == "running") {
        vm_state = "运行中";
        if (pid > 0) {
            vm_state += "（pid " + std::to_string(pid) + "）";
        }
    } else if (vm_status == "stopped" || vm_status == "shutdown") {
        vm_state = "已停止";
    }

    std::cout << "LSW 状态\n";
    std::cout << "  守护进程       : " << (daemon_running ? "运行中" : "未运行")
              << "\n";
    std::cout << "  Windows 虚拟机 : " << (installed ? "已安装" : "未安装")
              << "\n";
    std::cout << "  磁盘镜像       : " << disk << "（"
              << (disk_exists ? "存在" : "缺失") << "）\n";
    std::cout << "  虚拟机状态     : " << vm_state << "\n";
    const auto hostname = status.value("hostname", std::string{});
    std::cout << "  Windows 主机名 : "
              << (hostname.empty() ? "未知（启动 VM 后可查询）" : hostname)
              << "\n";
    std::cout << "  virtio-win     : "
              << virtio_status_text(status.value("virtio_win", "unknown"))
              << "（来源: "
              << virtio_source_text(status.value("virtio_source", "none"))
              << "）\n";
    return 0;
}

} // namespace

/// @brief lsw CLI 入口，解析并执行 install/start/shutdown 命令。
/// @param argc 命令行参数数量。
/// @param argv 命令行参数数组。
/// @return 进程退出码，0 表示执行成功。
int main(int argc, char **argv) {
    CLI::App app{"lsw - Linux Subsystem for Windows"};
    app.name("lsw");
    app.allow_extras();
    app.set_version_flag("--version", std::string("lsw ") + LSW_VERSION);

    bool install_flag = false;
    bool allow_existing_disk = false;
    bool save_password = false;
    std::string iso;
    std::string virtio_iso;
    std::string disk;
    std::string firmware = "bios";

    app.add_flag("--install", install_flag,
                 "引导安装 Windows（不提供、不下载 ISO）");
    app.add_option("--iso", iso, "Windows ISO 路径");
    app.add_option("--virtio-iso", virtio_iso, "virtio-win ISO 路径");
    app.add_option("--disk", disk, "系统盘 qcow2 路径");
    app.add_option("--firmware", firmware, "安装固件：bios（默认）或 ovmf")
        ->check(CLI::IsMember({"bios", "ovmf"}));
    app.add_flag("--allow-existing-disk", allow_existing_disk,
                 "允许复用 LSW 数据目录之外已存在的磁盘（危险）");
    app.add_flag("--save-password", save_password,
                 "把交互输入的 Windows 密码保存到系统 keyring");

    auto *start_command =
        app.add_subcommand("start", "启动 Windows 虚拟机服务");
    auto *shutdown_command =
        app.add_subcommand("shutdown", "关闭 Windows 虚拟机服务");
    auto *mount_command =
        app.add_subcommand("mount", "挂载 Windows C$/D$ 到 /mnt/Windows");
    auto *unmount_command =
        app.add_subcommand("unmount", "卸载 /mnt/Windows 下的 Windows 共享");
    auto *shell_command =
        app.add_subcommand("shell", "进入 Windows PowerShell 终端");
    bool status_json = false;
    auto *status_command =
        app.add_subcommand("status", "查看 LSW 与 Windows 虚拟机状态");
    status_command->add_flag("--json", status_json, "以 JSON 输出状态");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ExtrasError &error) {
        std::cerr << error.what() << "\n"
                  << "提示：ISO / 磁盘路径必须配合选项使用：--iso、"
                     "--virtio-iso、--disk；\n"
                  << "      只运行 `lsw --install` 可在终端中交互输入路径。\n";
        return lsw::to_exit_code(lsw::ErrorCode::kInvalidArgument);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }

    const auto extras = app.remaining();
    if (install_flag && !extras.empty()) {
        std::cerr << "lsw: 多余的参数:";
        for (const auto &item : extras) {
            std::cerr << " " << item;
        }
        std::cerr << "\n提示：ISO / 磁盘路径必须配合选项使用：--iso、"
                     "--virtio-iso、--disk。\n";
        return lsw::to_exit_code(lsw::ErrorCode::kInvalidArgument);
    }
    if (install_flag) {
        auto paths = lsw::resolve_paths(lsw::PathEnvironment::from_process());
        if (!paths) {
            return report_error(paths.error());
        }
        lsw::Config config;
        if (auto loaded = lsw::load_config(paths->config_file, true); loaded) {
            config = *loaded;
        } else {
            return report_error(loaded.error());
        }
        lsw::vm::InstallOptions options;
        options.windows_iso = iso;
        options.virtio_iso = virtio_iso;
        options.disk_image = disk;
        options.allow_existing_disk = allow_existing_disk;
        options.qemu_options.enable_firmware = firmware == "ovmf";
        options.interactive = true;
        lsw::vm::Installer installer(std::move(config), std::move(*paths),
                                     std::move(options));
        auto result = installer.run();
        if (!result) {
            return report_error(result.error());
        }
        std::cout << "\n安装流程结束（QEMU 退出码 " << result->qemu_exit_code
                  << "）。\n";
        for (const auto &line : result->guidance) {
            std::cout << "  " << line << "\n";
        }
        return 0;
    }

    if (start_command->parsed() || shutdown_command->parsed() ||
        mount_command->parsed() || unmount_command->parsed()) {
        if (!extras.empty()) {
            std::cerr << "lsw: 子命令不接受额外位置参数:";
            for (const auto &item : extras) {
                std::cerr << " " << item;
            }
            std::cerr << "\n";
            return lsw::to_exit_code(lsw::ErrorCode::kInvalidArgument);
        }
        auto paths = lsw::resolve_paths(lsw::PathEnvironment::from_process());
        if (!paths) {
            return report_error(paths.error());
        }
        lsw::Config config;
        if (auto loaded = lsw::load_config(paths->config_file, true); loaded) {
            config = *loaded;
        } else {
            return report_error(loaded.error());
        }
        if (start_command->parsed()) {
            std::cout
                << "正在启动 Windows 虚拟机服务（等待 SSH 127.0.0.1:2222，"
                   "最长 120 秒）...\n"
                << std::flush;
            if (auto status = ensure_daemon(*paths); !status) {
                return report_error(status.error());
            }
            nlohmann::json start_args = nlohmann::json::object();
            const char *disable_mounts = std::getenv("LSW_DISABLE_MOUNTS");
            if (disable_mounts == nullptr ||
                std::string(disable_mounts) != "1") {
                start_args = credential_args(config, save_password);
            }
            return run_daemon_command(*paths, "start", start_args);
        }
        if (mount_command->parsed()) {
            std::cout << "正在挂载 Windows 共享盘...\n" << std::flush;
            if (auto status = ensure_daemon(*paths); !status) {
                return report_error(status.error());
            }
            return run_daemon_command(*paths, "mount",
                                      credential_args(config, save_password));
        }
        if (unmount_command->parsed()) {
            std::cout << "正在卸载 Windows 共享盘...\n" << std::flush;
            if (auto status = ensure_daemon(*paths); !status) {
                return report_error(status.error());
            }
            return run_daemon_command(*paths, "unmount");
        }
        std::cout << "正在关闭 Windows 虚拟机（优先 SSH，QMP/信号兜底）...\n"
                  << std::flush;
        return run_daemon_command(*paths, "shutdown");
    }

    if (status_command->parsed()) {
        if (!extras.empty()) {
            std::cerr << "lsw status 不接受额外位置参数\n";
            return lsw::to_exit_code(lsw::ErrorCode::kInvalidArgument);
        }
        auto paths = lsw::resolve_paths(lsw::PathEnvironment::from_process());
        if (!paths) {
            return report_error(paths.error());
        }
        return run_status_command(*paths, status_json);
    }
    if (!extras.empty()) {
        if (shell_command->parsed()) {
            std::cerr << "lsw shell 不接受额外位置参数\n";
            return lsw::to_exit_code(lsw::ErrorCode::kInvalidArgument);
        }
        if (!looks_like_program(extras.front())) {
            std::cerr << "lsw: 未知命令: " << extras.front() << "\n";
            std::cout << app.help();
            return lsw::to_exit_code(lsw::ErrorCode::kUnknownCommand);
        }
        auto paths = lsw::resolve_paths(lsw::PathEnvironment::from_process());
        if (!paths) {
            return report_error(paths.error());
        }
        return run_exec_command(*paths, extras);
    }
    if (shell_command->parsed() || argc == 1) {
        auto paths = lsw::resolve_paths(lsw::PathEnvironment::from_process());
        if (!paths) {
            return report_error(paths.error());
        }
        return run_shell_command(*paths);
    }
    std::cerr << "lsw: 未识别的命令\n";
    std::cout << app.help();
    return lsw::to_exit_code(lsw::ErrorCode::kInvalidArgument);
}

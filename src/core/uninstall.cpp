/**
 * @file uninstall.cpp
 * @brief `lsw uninstall` 卸载流程实现。
 * @details 文件位置：src/core/uninstall.cpp（源文件）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <lsw/core/uninstall.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <lsw/core/bootstrap.hpp>
#include <lsw/ipc/client.hpp>

#ifndef LSW_INSTALL_HELPER_PATH
#define LSW_INSTALL_HELPER_PATH "/usr/local/lib/lsw/lsw-mount-helper"
#endif

namespace lsw::uninstall {
namespace {

/// @brief 在 PATH 与常见目录中查找可执行文件。
/// @param name 文件名。
/// @return 路径；找不到返回空。
std::string find_program(std::string_view name) {
    if (auto path = find_executable(name); !path.empty()) {
        return path;
    }
    for (const auto &dir : {"/usr/local/sbin", "/usr/local/bin", "/usr/sbin",
                            "/usr/bin", "/sbin", "/bin"}) {
        const auto candidate = std::filesystem::path(dir) / name;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error &&
            ::access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return {};
}

/// @brief 组装提权命令。
/// @param argv 原始 argv。
/// @return sudo/pkexec 前缀后的 argv；不可用返回空。
std::vector<std::string>
privilege_command(const std::vector<std::string> &argv) {
    if (::geteuid() == 0) {
        return argv;
    }
    std::vector<std::string> result;
    if (const auto sudo = find_program("sudo"); !sudo.empty()) {
        result.push_back(sudo);
        if (::isatty(STDIN_FILENO) == 0) {
            result.push_back("-n");
        }
        result.insert(result.end(), argv.begin(), argv.end());
        return result;
    }
    if (const auto pkexec = find_program("pkexec"); !pkexec.empty()) {
        result.push_back(pkexec);
        result.insert(result.end(), argv.begin(), argv.end());
        return result;
    }
    return {};
}

/// @brief 交互式同步执行命令。
/// @param argv 命令。
/// @return 退出码；fork/exec 失败返回 -1。
int run_interactive(const std::vector<std::string> &argv) {
    if (argv.empty()) {
        return -1;
    }
    std::vector<char *> raw;
    raw.reserve(argv.size() + 1);
    for (const auto &item : argv) {
        raw.push_back(const_cast<char *>(item.c_str()));
    }
    raw.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        ::execvp(raw.front(), raw.data());
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/// @brief 执行提权命令。
/// @param argv 命令。
/// @param action 动作描述。
/// @return 成功返回空。
Status run_privileged(const std::vector<std::string> &argv,
                      std::string_view action) {
    const auto command = privilege_command(argv);
    if (command.empty()) {
        return fail(ErrorCode::kMissingDependency,
                    "找不到 sudo/pkexec，无法" + std::string(action));
    }
    if (const int code = run_interactive(command); code != 0) {
        return fail(ErrorCode::kPermissionDenied,
                    std::string(action) + "失败（退出码 " +
                        std::to_string(code) + "）");
    }
    return {};
}

/// @brief 读取布尔确认，默认值是“否”。
/// @param prompt 提示。
/// @return 用户 y/Y 返回 true。
bool confirm(std::string_view prompt) {
    if (::isatty(STDIN_FILENO) == 0) {
        return false;
    }
    std::cout << prompt << " [y/N] " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line) || line.empty()) {
        return false;
    }
    const char first =
        static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));
    return first == 'y';
}

/// @brief 判断 path 是否位于 root 下（两者先弱规范化）。
/// @param path 待判断路径。
/// @param root 根目录。
/// @return 位于 root 下返回 true。
bool is_within(const std::filesystem::path &path,
               const std::filesystem::path &root) {
    std::error_code error;
    const auto canonical_path = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return false;
    }
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const auto relative = canonical_path.lexically_relative(canonical_root);
    if (relative.empty()) {
        return false;
    }
    for (const auto &part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

/// @brief 扫描 /proc，查找命令行包含指定路径的进程。
/// @param path 文件路径。
/// @return 找到的第一个 PID；没有返回 nullopt。
std::optional<int> process_using(const std::filesystem::path &path) {
    std::error_code error;
    for (const auto &entry :
         std::filesystem::directory_iterator("/proc", error)) {
        if (error) {
            break;
        }
        const auto name = entry.path().filename().string();
        if (name.empty() ||
            !std::all_of(name.begin(), name.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            continue;
        }
        std::ifstream cmdline(entry.path() / "cmdline", std::ios::binary);
        if (!cmdline) {
            continue;
        }
        std::string content((std::istreambuf_iterator<char>(cmdline)),
                            std::istreambuf_iterator<char>());
        if (content.find(path.string()) != std::string::npos) {
            return std::stoi(name);
        }
    }
    return std::nullopt;
}

/// @brief 删除文件（存在才删）。
/// @param path 文件路径。
/// @return 成功返回空。
Status remove_file(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return {};
    }
    if (!std::filesystem::remove(path, error) || error) {
        return fail(ErrorCode::kIoError, "删除失败: " + path.string() + "（" +
                                             error.message() + "）");
    }
    return {};
}

/// @brief 递归删除目录（存在才删）。
/// @param path 目录。
/// @return 成功返回空。
Status remove_directory(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return {};
    }
    std::filesystem::remove_all(path, error);
    if (error) {
        return fail(ErrorCode::kIoError, "删除失败: " + path.string() + "（" +
                                             error.message() + "）");
    }
    return {};
}

/// @brief 受管包对应的包管理器卸载命令。
/// @param manager 包管理器名（pacman/apt-get/dnf）。
/// @param packages 包列表。
/// @return argv；无法识别返回空。
std::vector<std::string>
package_remove_command(const std::string &manager,
                       const std::vector<std::string> &packages) {
    if (packages.empty()) {
        return {};
    }
    if (manager == "pacman") {
        std::vector<std::string> argv{"pacman", "-R", "--noconfirm"};
        argv.insert(argv.end(), packages.begin(), packages.end());
        return argv;
    }
    if (manager == "apt-get") {
        std::vector<std::string> argv{"apt-get", "remove", "--auto-remove",
                                      "-y"};
        argv.insert(argv.end(), packages.begin(), packages.end());
        return argv;
    }
    if (manager == "dnf") {
        std::vector<std::string> argv{"dnf", "remove", "-y"};
        argv.insert(argv.end(), packages.begin(), packages.end());
        return argv;
    }
    return {};
}

/// @brief 解析受管包记录。
/// @param paths XDG 路径。
/// @param plan 输出计划。
void read_managed_packages(const XdgPaths &paths, Plan &plan) {
    auto managed = bootstrap::load_managed_packages(paths);
    if (!managed || !managed->is_object()) {
        return;
    }
    plan.package_manager = managed->value("manager", std::string{});
    for (const auto &item :
         managed->value("packages", nlohmann::json::array())) {
        if (item.is_string()) {
            plan.packages.push_back(item.get<std::string>());
        }
    }
}

} // namespace

/** @brief 生成卸载计划。 */
Result<Plan> plan(const Config &config, const XdgPaths &paths,
                  const Options &options) {
    Plan result;
    result.disk = config.vm.disk;
    if (result.disk.empty() || result.disk.string().starts_with("~")) {
        if (auto expanded =
                expand_tilde(result.disk, PathEnvironment::from_process());
            expanded) {
            result.disk = *expanded;
        } else if (result.disk.empty()) {
            result.disk = paths.data_dir / "vm" / "disk.qcow2";
        }
    }
    if (!options.force && !is_within(result.disk, paths.data_dir)) {
        return fail(ErrorCode::kInvalidArgument,
                    "磁盘不在 LSW 数据目录内，拒绝删除: " +
                        result.disk.string() + "；如确认要删除请加 --force");
    }

    result.vm_files = {
        result.disk,
        paths.data_dir / "vm" / "OVMF_VARS.fd",
        paths.state_dir / "install.json",
        paths.state_dir / "guest.json",
    };
    result.purge_files = {paths.config_file};
    result.purge_dirs = {paths.data_dir, paths.state_dir, paths.runtime_dir};
    result.remove_helper = true;
    read_managed_packages(paths, result);
    return result;
}

/** @brief 执行卸载流程。 */
Status run(const XdgPaths &paths, const Options &options) {
    Config config = default_config();
    if (auto loaded = load_config(paths.config_file, true); loaded) {
        config = *loaded;
    } else {
        return std::unexpected(loaded.error());
    }
    auto planned = plan(config, paths, options);
    if (!planned) {
        return std::unexpected(planned.error());
    }
    Plan p = *planned;

    // 1. 处理运行中的 daemon / VM。
    lsw::ipc::ClientOptions client_options;
    client_options.socket_path = paths.runtime_dir / "lswd.sock";
    client_options.connect_timeout = std::chrono::milliseconds(300);
    client_options.io_timeout = std::chrono::milliseconds(2000);
    lsw::ipc::Client client(client_options);
    const auto daemon_status = client.call("status");
    const bool daemon_running = daemon_status.has_value();
    const bool vm_running =
        daemon_running &&
        daemon_status->value("vm_status", std::string{}) == "running";

    if (options.dry_run) {
        std::cout << "[lsw uninstall] 计划删除:\n";
        for (const auto &file : p.vm_files) {
            std::cout << "  - " << file.string() << "\n";
        }
        if (daemon_running || vm_running) {
            std::cout << "  - 先停止 lswd/VM";
            if (vm_running) {
                std::cout << "（VM 正在运行）";
            }
            std::cout << "\n";
        }
        std::cout << "[lsw uninstall] 彻底清理模式: "
                  << (options.purge ? "是" : "否") << "\n";
        std::cout << "[lsw uninstall] dry-run：未执行任何变更。\n";
        return {};
    }

    if (daemon_running) {
        if (vm_running && !options.assume_yes &&
            !confirm("检测到 Windows 虚拟机正在运行，是否关机后继续删除？")) {
            std::cout << "[lsw uninstall] 已取消。\n";
            return {};
        }
        auto stopped = client.call("shutdown");
        if (!stopped) {
            return fail(ErrorCode::kQemuError,
                        "停止 lswd/VM 失败: " + stopped.error().message);
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
            std::error_code error;
            if (!std::filesystem::exists(paths.runtime_dir / "lswd.sock",
                                         error) ||
                error) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } else if (const auto pid = process_using(p.disk); pid.has_value()) {
        return fail(ErrorCode::kQemuError, "磁盘仍被进程 " +
                                               std::to_string(*pid) +
                                               " 使用，请先执行 lsw shutdown");
    }

    // 2. 展示并确认删除 VM。
    std::cout << "[lsw uninstall] 将删除虚拟机:\n";
    for (const auto &file : p.vm_files) {
        std::cout << "  - " << file.string() << "\n";
    }
    if (!options.assume_yes && !confirm("确认删除以上虚拟机文件？")) {
        std::cout << "[lsw uninstall] 已取消。\n";
        return {};
    }

    // 3. 决定是否彻底清理。
    bool purge = options.purge;
    if (options.keep) {
        purge = false;
    } else if (!options.purge) {
        if (::isatty(STDIN_FILENO) != 0) {
            purge = confirm("是否连同配置文件、helper、依赖和组件一起卸载？");
        } else {
            std::cout << "[lsw uninstall] 非交互环境未指定 --purge，默认只删"
                         "除虚拟机。\n";
            purge = false;
        }
    }

    for (const auto &file : p.vm_files) {
        if (auto status = remove_file(file); !status) {
            return status;
        }
    }

    if (!purge) {
        std::cout << "[lsw uninstall] 已删除虚拟机；配置、helper 与依赖已保留"
                     "，可重新执行 lsw install。\n";
        return {};
    }

    std::cout << "[lsw uninstall] 彻底清理模式：删除配置、helper、数据与"
                 "受管包。\n";
    for (const auto &file : p.purge_files) {
        if (auto status = remove_file(file); !status) {
            return status;
        }
    }
    for (const auto &dir : p.purge_dirs) {
        if (auto status = remove_directory(dir); !status) {
            return status;
        }
    }

    if (p.remove_helper) {
        const std::vector<std::string> helper_remove{
            "rm", "-f", LSW_INSTALL_HELPER_PATH, "/etc/sudoers.d/lsw"};
        if (auto status = run_privileged(helper_remove, "删除 helper/sudoers");
            !status) {
            return status;
        }
    }

    if (!p.packages.empty()) {
        const auto command =
            package_remove_command(p.package_manager, p.packages);
        if (command.empty()) {
            std::cout << "[lsw uninstall] 无法识别包管理器，请手动卸载:";
            for (const auto &package : p.packages) {
                std::cout << " " << package;
            }
            std::cout << "\n";
        } else if (auto status = run_privileged(command, "卸载受管系统包");
                   !status) {
            return status;
        }
    } else {
        std::cout << "[lsw uninstall] 没有 lsw init 记录的受管包，跳过系统包"
                     "卸载。\n";
    }

    std::cout << "[lsw uninstall] 彻底清理完成。\n";
    return {};
}

} // namespace lsw::uninstall

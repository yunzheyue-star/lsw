/**
 * @file bootstrap.cpp
 * @brief `lsw init` 宿主环境初始化实现。
 * @details 文件位置：src/core/bootstrap.cpp（源文件）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <lsw/core/bootstrap.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include <lsw/util/paths.hpp>

namespace lsw::bootstrap {
namespace {

/// @brief 读取 os-release 中的字段。
/// @param path os-release 文件。
/// @param key 字段名。
/// @return 字段值；不存在返回空。
std::string os_release_value(const std::filesystem::path &path,
                             std::string_view key) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    std::string line;
    while (std::getline(input, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos || line.substr(0, pos) != key) {
            continue;
        }
        std::string value = line.substr(pos + 1);
        if (!value.empty() && value.front() == '"') {
            value.erase(0, 1);
        }
        if (!value.empty() && value.back() == '"') {
            value.pop_back();
        }
        return value;
    }
    return {};
}

/// @brief 在 PATH 与常见 sbin 目录中查找可执行文件。
/// @param name 文件名。
/// @return 找到的路径；找不到返回空。
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

/// @brief 检测 OVMF CODE 是否存在。
/// @return 找到任意候选返回 true。
bool has_ovmf_code() {
    for (const auto &candidate :
         {"/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_CODE.4m.fd",
          "/usr/share/edk2/ovmf/OVMF_CODE.fd",
          "/usr/share/edk2/x64/OVMF_CODE.4m.fd",
          "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return true;
        }
    }
    return false;
}

/// @brief 检测 OVMF VARS 模板是否存在。
/// @return 找到任意候选返回 true。
bool has_ovmf_vars() {
    for (const auto &candidate :
         {"/usr/share/OVMF/OVMF_VARS.fd", "/usr/share/OVMF/OVMF_VARS.4m.fd",
          "/usr/share/edk2/ovmf/OVMF_VARS.fd",
          "/usr/share/edk2/x64/OVMF_VARS.4m.fd",
          "/usr/share/edk2-ovmf/x64/OVMF_VARS.fd"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return true;
        }
    }
    return false;
}

/// @brief 当前用户是否可读写 /dev/kvm。
/// @return 可访问返回 true。
bool has_kvm_access() { return ::access("/dev/kvm", R_OK | W_OK) == 0; }

/// @brief 宿主 helper 是否已安装。
/// @return 存在且可执行返回 true。
bool has_helper() {
    const std::filesystem::path helper{"/usr/local/lib/lsw/lsw-mount-helper"};
    std::error_code error;
    return std::filesystem::is_regular_file(helper, error) && !error &&
           ::access(helper.c_str(), X_OK) == 0;
}

/// @brief 每个组件的包名映射。
/// @param id 组件 ID。
/// @param manager 包管理器。
/// @return 需要安装的包名列表。
std::vector<std::string> packages_for(std::string_view id,
                                      PackageManager manager) {
    if (id == "qemu") {
        switch (manager) {
        case PackageManager::kPacman:
            return {"qemu-full"};
        case PackageManager::kApt:
            return {"qemu-system-x86", "qemu-utils"};
        case PackageManager::kDnf:
            return {"qemu-kvm", "qemu-img"};
        case PackageManager::kUnknown:
            break;
        }
    }
    if (id == "qemu-img") {
        switch (manager) {
        case PackageManager::kPacman:
            return {"qemu-full"};
        case PackageManager::kApt:
            return {"qemu-utils"};
        case PackageManager::kDnf:
            return {"qemu-img"};
        case PackageManager::kUnknown:
            break;
        }
    }
    if (id == "ovmf") {
        switch (manager) {
        case PackageManager::kPacman:
        case PackageManager::kDnf:
            return {"edk2-ovmf"};
        case PackageManager::kApt:
            return {"ovmf"};
        case PackageManager::kUnknown:
            break;
        }
    }
    if (id == "samba") {
        return {"samba"};
    }
    if (id == "cifs") {
        return {"cifs-utils"};
    }
    if (id == "ssh") {
        switch (manager) {
        case PackageManager::kPacman:
            return {"openssh"};
        case PackageManager::kApt:
            return {"openssh-client"};
        case PackageManager::kDnf:
            return {"openssh-clients"};
        case PackageManager::kUnknown:
            break;
        }
    }
    if (id == "swtpm") {
        return {"swtpm"};
    }
    if (id == "libsecret") {
        switch (manager) {
        case PackageManager::kPacman:
        case PackageManager::kDnf:
            return {"libsecret"};
        case PackageManager::kApt:
            return {"libsecret-tools"};
        case PackageManager::kUnknown:
            break;
        }
    }
    return {};
}

/// @brief 生成组件检查结果。
/// @param id 组件 ID。
/// @param display 显示名。
/// @param satisfied 是否满足。
/// @param required 是否必需。
/// @param detail 状态说明。
/// @param manager 包管理器。
/// @return 组件对象。
Component make_component(std::string id, std::string display, bool satisfied,
                         bool required, std::string detail,
                         PackageManager manager) {
    Component component;
    component.id = std::move(id);
    component.display = std::move(display);
    component.required = required;
    component.satisfied = satisfied;
    component.detail = std::move(detail);
    component.packages = packages_for(component.id, manager);
    return component;
}

/// @brief 去重追加包名。
/// @param target 目标列表。
/// @param packages 待追加包。
void append_unique(std::vector<std::string> &target,
                   const std::vector<std::string> &packages) {
    for (const auto &package : packages) {
        if (std::find(target.begin(), target.end(), package) == target.end()) {
            target.push_back(package);
        }
    }
}

/// @brief 组装需要 root 权限的命令。
/// @param argv 原始 argv。
/// @return 带 sudo/pkexec 前缀的 argv；找不到提权工具返回空。
std::vector<std::string>
privilege_command(const std::vector<std::string> &argv) {
    if (::geteuid() == 0) {
        return argv;
    }
    std::vector<std::string> result;
    const auto sudo = find_program("sudo");
    if (!sudo.empty()) {
        result.push_back(sudo);
        if (::isatty(STDIN_FILENO) == 0) {
            result.push_back("-n");
        }
        result.insert(result.end(), argv.begin(), argv.end());
        return result;
    }
    const auto pkexec = find_program("pkexec");
    if (!pkexec.empty()) {
        result.push_back(pkexec);
        result.insert(result.end(), argv.begin(), argv.end());
        return result;
    }
    return {};
}

/// @brief 同步执行命令并继承终端。
/// @param argv 命令参数。
/// @return 子进程退出码；fork/exec 失败返回 -1。
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
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/// @brief 解析 /proc/self/exe 所在目录。
/// @return 可执行文件目录；失败返回空。
std::filesystem::path executable_dir() {
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return {};
    }
    return self.parent_path();
}

/// @brief 查找 lsw-mount-helper 源文件。
/// @return 源文件路径；找不到返回空。
std::filesystem::path find_helper_source() {
    if (const char *env = std::getenv("LSW_HELPER_SOURCE");
        env != nullptr && *env != '\0') {
        std::error_code error;
        if (std::filesystem::is_regular_file(env, error) && !error) {
            return env;
        }
    }
    std::vector<std::filesystem::path> candidates;
    if (const auto dir = executable_dir(); !dir.empty()) {
        candidates.push_back(dir / "lsw-mount-helper");
        candidates.push_back(dir / ".." / "scripts" / "lsw-mount-helper");
        candidates.push_back(dir / ".." / ".." / "scripts" /
                             "lsw-mount-helper");
    }
    std::error_code error;
    const auto cwd = std::filesystem::current_path(error);
    if (!error) {
        candidates.push_back(cwd / "scripts" / "lsw-mount-helper");
    }
    candidates.push_back("/usr/local/share/lsw/lsw-mount-helper");
    for (const auto &candidate : candidates) {
        std::error_code exists_error;
        if (std::filesystem::is_regular_file(candidate, exists_error) &&
            !exists_error) {
            return candidate;
        }
    }
    return {};
}

/// @brief 查找 Windows 侧 post-install 脚本。
/// @return 路径；找不到返回空。
std::filesystem::path find_post_install_script() {
    if (const char *env = std::getenv("LSW_POST_INSTALL_SCRIPT");
        env != nullptr && *env != '\0') {
        std::error_code error;
        if (std::filesystem::is_regular_file(env, error) && !error) {
            return env;
        }
    }
    std::error_code error;
    const auto cwd = std::filesystem::current_path(error);
    if (!error) {
        const auto candidate = cwd / "scripts" / "post-install.ps1";
        std::error_code exists_error;
        if (std::filesystem::is_regular_file(candidate, exists_error) &&
            !exists_error) {
            return candidate;
        }
    }
    return {};
}

/// @brief 读取用户确认。
/// @param prompt 提示文本。
/// @return 用户同意返回 true；EOF/非 TTY 返回 false。
bool confirm(std::string_view prompt) {
    if (::isatty(STDIN_FILENO) == 0) {
        return false;
    }
    std::cout << prompt << " [Y/n] " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }
    if (line.empty()) {
        return true;
    }
    const char first =
        static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));
    return first == 'y';
}

/// @brief 执行提权命令，失败时返回错误。
/// @param argv 原始命令。
/// @param action 人类可读动作描述。
/// @return 成功返回空；失败返回权限/依赖错误。
Status run_privileged(const std::vector<std::string> &argv,
                      std::string_view action) {
    const auto command = privilege_command(argv);
    if (command.empty()) {
        return fail(ErrorCode::kMissingDependency,
                    "找不到 sudo/pkexec，无法执行: " + std::string(action));
    }
    const int code = run_interactive(command);
    if (code != 0) {
        return fail(ErrorCode::kPermissionDenied,
                    std::string(action) + "失败（退出码 " +
                        std::to_string(code) + "）");
    }
    return {};
}

/// @brief 安装宿主 helper 与 sudoers 规则。
/// @return 成功返回空；失败返回错误。
Status install_helper() {
    const auto source = find_helper_source();
    if (source.empty()) {
        return fail(ErrorCode::kMissingDependency,
                    "找不到 lsw-mount-helper 源文件；可设置 "
                    "LSW_HELPER_SOURCE 指向 scripts/lsw-mount-helper");
    }
    const std::string destination = "/usr/local/lib/lsw/lsw-mount-helper";
    if (auto status =
            run_privileged({"install", "-d", "-o", "root", "-g", "root", "-m",
                            "0755", "/usr/local/lib/lsw"},
                           "创建 helper 目录");
        !status) {
        return status;
    }
    if (auto status =
            run_privileged({"install", "-o", "root", "-g", "root", "-m", "0755",
                            source.string(), destination},
                           "安装 helper");
        !status) {
        return status;
    }

    const char *user = std::getenv("USER");
    if (user == nullptr || *user == '\0') {
        return fail(ErrorCode::kInvalidArgument,
                    "无法确定当前用户名（USER 为空）");
    }
    std::string sudoers =
        std::string(user) + " ALL=(root) NOPASSWD: " + destination + "\n";
    const auto temp_template =
        std::filesystem::temp_directory_path() / "lsw-sudoers-XXXXXX";
    std::string temp_path = temp_template.string();
    const int fd = ::mkstemp(temp_path.data());
    if (fd < 0) {
        return fail(ErrorCode::kIoError, "无法创建 sudoers 临时文件");
    }
    {
        const ssize_t written = ::write(fd, sudoers.data(), sudoers.size());
        ::close(fd);
        if (written != static_cast<ssize_t>(sudoers.size())) {
            std::filesystem::remove(temp_path);
            return fail(ErrorCode::kIoError, "写入 sudoers 临时文件失败");
        }
    }
    if (auto status =
            run_privileged({"install", "-o", "root", "-g", "root", "-m", "0440",
                            temp_path, "/etc/sudoers.d/lsw"},
                           "安装 sudoers 规则");
        !status) {
        std::filesystem::remove(temp_path);
        return status;
    }
    std::filesystem::remove(temp_path);
    return run_privileged({"visudo", "-cf", "/etc/sudoers.d/lsw"},
                          "校验 sudoers 规则");
}

/// @brief 打印 Windows 侧组件提示。
void print_windows_hint() {
    const auto script = find_post_install_script();
    if (script.empty()) {
        std::cout << "  Windows 侧组件: 启动 VM 后请在 Windows 内启用 "
                     "OpenSSH Server / QEMU Guest Agent / SMB，并映射 Z:。\n";
        return;
    }
    std::cout << "  Windows 侧组件: 启动 VM 后把 " << script.string()
              << " 放入 Windows 并执行，检查 sshd / QEMU Guest Agent / Z:。\n";
}

} // namespace

/** @brief 包管理器枚举转名称。 */
std::string to_string(PackageManager manager) {
    switch (manager) {
    case PackageManager::kPacman:
        return "pacman";
    case PackageManager::kApt:
        return "apt-get";
    case PackageManager::kDnf:
        return "dnf";
    case PackageManager::kUnknown:
        break;
    }
    return "unknown";
}

/** @brief 识别包管理器。 */
PackageManager
detect_package_manager(const std::filesystem::path &os_release_path) {
    const std::string id = os_release_value(os_release_path, "ID");
    const std::string like = os_release_value(os_release_path, "ID_LIKE");
    const bool system_os_release =
        os_release_path == std::filesystem::path("/etc/os-release");
    const auto matches = [&id, &like](std::string_view family) {
        return id == family || like.find(family) != std::string::npos;
    };
    if (matches("arch") || matches("archarm") ||
        (system_os_release && std::filesystem::exists("/etc/arch-release"))) {
        return PackageManager::kPacman;
    }
    if (matches("debian") || matches("ubuntu") ||
        (system_os_release && std::filesystem::exists("/etc/debian_version"))) {
        return PackageManager::kApt;
    }
    if (matches("fedora") || matches("rhel") || matches("centos")) {
        return PackageManager::kDnf;
    }
    if (!find_program("pacman").empty()) {
        return PackageManager::kPacman;
    }
    if (!find_program("apt-get").empty()) {
        return PackageManager::kApt;
    }
    if (!find_program("dnf").empty()) {
        return PackageManager::kDnf;
    }
    return PackageManager::kUnknown;
}

/** @brief 构造包安装命令。 */
std::vector<std::string>
install_command(PackageManager manager,
                const std::vector<std::string> &packages) {
    if (packages.empty() || manager == PackageManager::kUnknown) {
        return {};
    }
    switch (manager) {
    case PackageManager::kPacman: {
        std::vector<std::string> argv{"pacman", "-S", "--needed"};
        argv.insert(argv.end(), packages.begin(), packages.end());
        return argv;
    }
    case PackageManager::kApt: {
        std::vector<std::string> argv{"apt-get", "install", "-y"};
        argv.insert(argv.end(), packages.begin(), packages.end());
        return argv;
    }
    case PackageManager::kDnf: {
        std::vector<std::string> argv{"dnf", "install", "-y"};
        argv.insert(argv.end(), packages.begin(), packages.end());
        return argv;
    }
    case PackageManager::kUnknown:
        break;
    }
    return {};
}

/** @brief 扫描宿主环境，生成 init 计划。 */
Result<Plan> plan(const XdgPaths &paths) {
    (void)paths;
    Plan result;
    result.manager = detect_package_manager();
    result.manager_name = to_string(result.manager);
    result.distro = os_release_value("/etc/os-release", "PRETTY_NAME");
    if (result.distro.empty()) {
        result.distro = os_release_value("/etc/os-release", "NAME");
    }
    if (result.distro.empty()) {
        result.distro = "未知发行版";
    }

    const bool qemu_ok = !find_program("qemu-system-x86_64").empty();
    result.components.push_back(
        make_component("qemu", "QEMU", qemu_ok, true,
                       qemu_ok ? find_program("qemu-system-x86_64") : "未找到",
                       result.manager));

    const bool qemu_img_ok = !find_program("qemu-img").empty();
    result.components.push_back(make_component(
        "qemu-img", "qemu-img", qemu_img_ok, true,
        qemu_img_ok ? find_program("qemu-img") : "未找到", result.manager));

    const bool ovmf_ok = has_ovmf_code() && has_ovmf_vars();
    result.components.push_back(make_component(
        "ovmf", "OVMF 固件", ovmf_ok, true,
        ovmf_ok ? "已找到 OVMF_CODE/VARS" : "未找到 OVMF_CODE/VARS",
        result.manager));

    const bool kvm_ok = has_kvm_access();
    result.components.push_back(make_component(
        "kvm", "/dev/kvm 权限", kvm_ok, true,
        kvm_ok ? "当前用户可读写"
               : "不可访问：sudo usermod -aG kvm $USER 后重新登录",
        result.manager));

    const auto smbd = find_program("smbd");
    result.components.push_back(make_component(
        "samba", "Samba (smbd)", !smbd.empty(), true,
        smbd.empty() ? "未找到 smbd（Windows Z: 共享需要）" : smbd,
        result.manager));

    const auto mount_cifs = find_program("mount.cifs");
    result.components.push_back(make_component(
        "cifs", "cifs-utils (mount.cifs)", !mount_cifs.empty(), true,
        mount_cifs.empty() ? "未找到 mount.cifs（C$/D$ 挂载需要）" : mount_cifs,
        result.manager));

    const auto ssh = find_program("ssh");
    result.components.push_back(make_component(
        "ssh", "OpenSSH 客户端", !ssh.empty(), true,
        ssh.empty() ? "未找到 ssh（shell/exec 需要）" : ssh, result.manager));

    const auto swtpm = find_program("swtpm");
    result.components.push_back(
        make_component("swtpm", "swtpm（可选）", !swtpm.empty(), false,
                       swtpm.empty() ? "未找到（Windows 11 TPM 需要）" : swtpm,
                       result.manager));

    const auto secret_tool = find_program("secret-tool");
    result.components.push_back(make_component(
        "libsecret", "libsecret (secret-tool，可选)", !secret_tool.empty(),
        false,
        secret_tool.empty() ? "未找到（keyring 保存密码需要）" : secret_tool,
        result.manager));

    const bool helper_ok = has_helper();
    result.helper_missing = !helper_ok;
    result.components.push_back(make_component(
        "helper", "LSW helper (/usr/local/lib/lsw/lsw-mount-helper)", helper_ok,
        true, helper_ok ? "已安装" : "未安装（CIFS/Samba 提权需要）",
        result.manager));

    for (const auto &component : result.components) {
        if (!component.satisfied) {
            append_unique(result.missing_packages, component.packages);
        }
    }
    return result;
}

/** @brief 如果配置文件不存在则写入默认 config.toml。 */
Status initialize_config(const XdgPaths &paths) {
    if (auto status = ensure_directories(paths); !status) {
        return status;
    }
    std::error_code error;
    if (std::filesystem::exists(paths.config_file, error) && !error) {
        std::cout << "[lsw init] 保留现有配置: " << paths.config_file.string()
                  << "\n";
        return {};
    }
    std::filesystem::create_directories(paths.config_file.parent_path(), error);
    if (error) {
        return fail(ErrorCode::kIoError,
                    "创建配置目录失败: " + error.message());
    }
    std::ofstream output(paths.config_file, std::ios::trunc);
    if (!output) {
        return fail(ErrorCode::kIoError,
                    "无法写入配置: " + paths.config_file.string());
    }
    output << default_config_toml();
    if (!output) {
        return fail(ErrorCode::kIoError,
                    "写入配置失败: " + paths.config_file.string());
    }
    std::cout << "[lsw init] 已生成默认配置: " << paths.config_file.string()
              << "\n";
    return {};
}

/** @brief 执行完整 init 流程。 */
Status run(const XdgPaths &paths, const Options &options) {
    if (options.dry_run) {
        std::error_code error;
        const bool exists =
            std::filesystem::exists(paths.config_file, error) && !error;
        std::cout << "[lsw init] 配置: "
                  << (exists ? "保留现有 " : "将生成默认 ")
                  << paths.config_file.string() << "\n";
    } else if (auto status = initialize_config(paths); !status) {
        return status;
    }
    auto checked = plan(paths);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    Plan current = *checked;

    std::cout << "[lsw init] 发行版: " << current.distro << "\n";
    std::cout << "[lsw init] 包管理器: " << current.manager_name << "\n";
    std::cout << "[lsw init] 组件检查:\n";
    for (const auto &component : current.components) {
        std::cout << "  [" << (component.satisfied ? "OK" : "缺失") << "] "
                  << component.display;
        if (!component.satisfied && !component.detail.empty()) {
            std::cout << " — " << component.detail;
        }
        std::cout << "\n";
    }

    const auto package_command =
        install_command(current.manager, current.missing_packages);
    const bool need_packages = !current.missing_packages.empty();
    const bool need_helper = current.helper_missing;
    const auto required_missing = [](const Plan &value) {
        return std::any_of(value.components.begin(), value.components.end(),
                           [](const Component &component) {
                               return component.required &&
                                      !component.satisfied;
                           });
    };
    const bool manual_required = required_missing(current);

    if (!need_packages && !need_helper) {
        if (!manual_required) {
            std::cout << "[lsw init] 所有必需依赖已满足，跳过安装。\n";
            print_windows_hint();
            return {};
        }
        std::cout << "[lsw init] 以下必需组件无法自动安装，请手动处理：\n";
        for (const auto &component : current.components) {
            if (component.required && !component.satisfied) {
                std::cout << "  - " << component.display << " — "
                          << component.detail << "\n";
            }
        }
        print_windows_hint();
        if (options.dry_run) {
            std::cout << "[lsw init] dry-run：未执行任何变更。\n";
            return {};
        }
        return fail(ErrorCode::kMissingDependency,
                    "仍有必需组件无法自动安装，请按上方提示处理后重试");
    }

    if (need_packages && package_command.empty()) {
        std::cout << "[lsw init] 无法识别包管理器，请手动安装：";
        for (const auto &package : current.missing_packages) {
            std::cout << " " << package;
        }
        std::cout << "\n";
        print_windows_hint();
        if (options.dry_run) {
            std::cout << "[lsw init] dry-run：未执行任何变更。\n";
            return {};
        }
        return fail(ErrorCode::kMissingDependency,
                    "未知发行版/包管理器，未自动安装");
    }

    std::cout << "[lsw init] 计划执行:\n";
    if (need_packages) {
        std::cout << "  sudo";
        for (const auto &item : package_command) {
            std::cout << " " << item;
        }
        std::cout << "\n";
    }
    if (need_helper) {
        std::cout << "  sudo install ... lsw-mount-helper + "
                     "/etc/sudoers.d/lsw\n";
    }
    print_windows_hint();

    if (options.dry_run) {
        std::cout << "[lsw init] dry-run：未执行任何变更。\n";
        return {};
    }
    if (!options.assume_yes) {
        if (!confirm("[lsw init] 是否执行以上安装？")) {
            std::cout << "[lsw init] 已取消。\n";
            return {};
        }
    }

    if (need_packages) {
        if (auto status = run_privileged(package_command, "安装系统依赖");
            !status) {
            return status;
        }
    }
    if (need_helper) {
        if (auto status = install_helper(); !status) {
            return status;
        }
    }

    auto after = plan(paths);
    if (!after) {
        return std::unexpected(after.error());
    }
    bool all_required_ok = true;
    for (const auto &component : after->components) {
        if (component.required && !component.satisfied) {
            all_required_ok = false;
            std::cout << "[lsw init] 仍缺少: " << component.display << " — "
                      << component.detail << "\n";
        }
    }
    if (!all_required_ok) {
        return fail(ErrorCode::kMissingDependency,
                    "安装完成后仍有必需组件缺失，请检查上方输出");
    }
    std::cout << "[lsw init] 初始化完成。\n";
    print_windows_hint();
    return {};
}

} // namespace lsw::bootstrap

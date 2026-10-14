/**
 * @file install.cpp
 * @brief install 实现（虚拟机管理（QEMU/QMP/安装））。
 * @details 文件位置：src/vm/install.cpp（源文件）。
 *          属于 LSW 虚拟机管理（QEMU/QMP/安装）；公共行为以
 * docs/interfaces/qmp.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/vm/install.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace lsw::vm {
namespace {

/** @brief 判断路径是否为可读的普通文件。
 * @param path 待检查的路径。
 * @return 可读返回 true，否则返回 false。
 */
bool readable_file(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) &&
           ::access(path.c_str(), R_OK) == 0;
}

/** @brief 获取文件扩展名并转换为小写。
 * @param path 文件路径。
 * @return 小写扩展名（含点号）。
 */
std::string lower_extension(const std::filesystem::path &path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

enum class DistroFamily { kArch, kDebian, kFedora, kOther };

/** @brief 根据 /etc/os-release 等文件识别发行版家族。
 * @return 识别出的发行版家族；未知时返回 kOther。
 */
DistroFamily detect_distro_family() {
    std::ifstream release("/etc/os-release");
    std::string line;
    while (std::getline(release, line)) {
        if (line.rfind("ID=", 0) != 0) {
            continue;
        }
        std::string id = line.substr(3);
        if (!id.empty() && id.front() == '"') {
            id.erase(0, 1);
        }
        if (!id.empty() && id.back() == '"') {
            id.pop_back();
        }
        if (id == "arch" || id == "archarm") {
            return DistroFamily::kArch;
        }
        if (id == "debian" || id == "ubuntu") {
            return DistroFamily::kDebian;
        }
        if (id == "fedora" || id == "rhel" || id == "centos") {
            return DistroFamily::kFedora;
        }
    }
    if (std::filesystem::exists("/etc/arch-release")) {
        return DistroFamily::kArch;
    }
    if (std::filesystem::exists("/etc/debian_version")) {
        return DistroFamily::kDebian;
    }
    if (std::filesystem::exists("/etc/fedora-release")) {
        return DistroFamily::kFedora;
    }
    return DistroFamily::kOther;
}

/** @brief 生成 QEMU 安装提示。
 * @param distro 发行版家族。
 * @return 对应发行版的安装建议。
 */
std::string qemu_install_hint(DistroFamily distro) {
    switch (distro) {
    case DistroFamily::kArch:
        return "安装 QEMU：sudo pacman -S qemu-full";
    case DistroFamily::kDebian:
        return "安装 QEMU：sudo apt install qemu-system-x86";
    case DistroFamily::kFedora:
        return "安装 QEMU：sudo dnf install qemu-kvm";
    case DistroFamily::kOther:
        break;
    }
    return "安装 QEMU（Arch: qemu-full；Debian/Ubuntu: qemu-system-x86；"
           "Fedora: qemu-kvm）";
}

/** @brief 生成 qemu-img 安装提示。
 * @param distro 发行版家族。
 * @return 对应发行版的安装建议。
 */
std::string qemu_img_install_hint(DistroFamily distro) {
    switch (distro) {
    case DistroFamily::kArch:
        return "安装 qemu-img：sudo pacman -S qemu-full";
    case DistroFamily::kDebian:
        return "安装 qemu-img：sudo apt install qemu-utils";
    case DistroFamily::kFedora:
        return "安装 qemu-img：sudo dnf install qemu-img";
    case DistroFamily::kOther:
        break;
    }
    return "安装 qemu-img（Arch: qemu-full；Debian/Ubuntu: qemu-utils；"
           "Fedora: qemu-img）";
}

/** @brief 生成 OVMF 固件安装提示。
 * @param distro 发行版家族。
 * @return 对应发行版的安装建议。
 */
std::string ovmf_install_hint(DistroFamily distro) {
    switch (distro) {
    case DistroFamily::kArch:
        return "安装 UEFI 固件：sudo pacman -S edk2-ovmf";
    case DistroFamily::kDebian:
        return "安装 UEFI 固件：sudo apt install ovmf";
    case DistroFamily::kFedora:
        return "安装 UEFI 固件：sudo dnf install edk2-ovmf";
    case DistroFamily::kOther:
        break;
    }
    return "安装 OVMF 固件（Arch: edk2-ovmf；Debian/Ubuntu: ovmf；"
           "Fedora: edk2-ovmf）";
}

/** @brief 生成 Samba 安装提示。
 * @param distro 发行版家族。
 * @return 对应发行版的安装建议。
 */
std::string samba_install_hint(DistroFamily distro) {
    switch (distro) {
    case DistroFamily::kArch:
        return "安装 Samba：sudo pacman -S samba";
    case DistroFamily::kDebian:
        return "安装 Samba：sudo apt install samba";
    case DistroFamily::kFedora:
        return "安装 Samba：sudo dnf install samba";
    case DistroFamily::kOther:
        break;
    }
    return "安装 Samba（Windows 内 Z: 共享需要 smbd）";
}

/** @brief 生成 swtpm 安装提示。
 * @param distro 发行版家族。
 * @return 对应发行版的安装建议。
 */
std::string swtpm_install_hint(DistroFamily distro) {
    switch (distro) {
    case DistroFamily::kArch:
        return "Windows 11 需要 TPM：sudo pacman -S swtpm（Windows 10 可忽略）";
    case DistroFamily::kDebian:
        return "Windows 11 需要 TPM：sudo apt install swtpm（Windows 10 "
               "可忽略）";
    case DistroFamily::kFedora:
        return "Windows 11 需要 TPM：sudo dnf install swtpm（Windows 10 "
               "可忽略）";
    case DistroFamily::kOther:
        break;
    }
    return "Windows 11 需要 TPM，请安装 swtpm（Windows 10 可忽略）";
}

/** @brief 使用分隔符连接字符串列表。
 * @param items 待连接的字符串列表。
 * @param separator 分隔符。
 * @return 连接后的字符串。
 */
std::string join(const std::vector<std::string> &items,
                 const std::string &separator) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out += separator;
        }
        out += items[i];
    }
    return out;
}

/** @brief 校验 ISO 路径存在、可读且扩展名为 .iso。
 * @param path ISO 文件路径。
 * @param label 用于错误信息的标签。
 * @return 校验通过返回空状态；失败返回参数错误。
 */
Status validate_iso(const std::filesystem::path &path, const char *label) {
    if (path.empty()) {
        return fail(ErrorCode::kInvalidArgument,
                    std::string(label) + " 路径为空");
    }
    if (!std::filesystem::exists(path)) {
        return fail(ErrorCode::kInvalidArgument,
                    std::string(label) + " 不存在: " + path.string());
    }
    if (!readable_file(path)) {
        return fail(ErrorCode::kInvalidArgument,
                    std::string(label) +
                        " 不是可读的普通文件: " + path.string());
    }
    if (lower_extension(path) != ".iso") {
        return fail(ErrorCode::kInvalidArgument,
                    std::string(label) + " 必须是 .iso 文件: " + path.string());
    }
    return {};
}

} // namespace

/** @brief 构造安装流程并保存配置。
 * @param config 全局配置。
 * @param paths XDG 数据与运行时路径。
 * @param options 安装选项。
 */
Installer::Installer(Config config, XdgPaths paths, InstallOptions options)
    : config_(std::move(config)), paths_(std::move(paths)),
      options_(std::move(options)) {}

/** @brief 解析最终使用的系统盘路径。
 * @return 成功返回系统盘路径；失败返回参数错误。
 */
Result<std::filesystem::path> Installer::resolve_disk_image() const {
    std::filesystem::path disk = options_.disk_image;
    if (disk.empty()) {
        disk = config_.vm.disk;
    }
    if (disk.empty() || disk.string().starts_with("~")) {
        disk = paths_.data_dir / "vm" / "disk.qcow2";
    }
    if (disk.empty()) {
        return fail(ErrorCode::kInvalidArgument, "无法确定系统盘路径");
    }
    return disk;
}

/** @brief 检查安装所需的外部依赖。
 * @return 成功返回依赖检查报告；失败返回错误。
 */
Result<DependencyReport> Installer::check_dependencies() const {
    DependencyReport report;
    const DistroFamily distro = detect_distro_family();
    const auto add_hint = [&report](std::string hint) {
        if (std::find(report.hints.begin(), report.hints.end(), hint) ==
            report.hints.end()) {
            report.hints.push_back(std::move(hint));
        }
    };

    report.qemu_binary = find_executable(options_.qemu_binary.string());
    if (report.qemu_binary.empty()) {
        report.missing.push_back("qemu-system-x86_64");
        add_hint(qemu_install_hint(distro));
    }

    report.qemu_img_binary = find_executable(options_.qemu_img_binary.string());
    if (report.qemu_img_binary.empty()) {
        report.missing.push_back("qemu-img");
        add_hint(qemu_img_install_hint(distro));
    }

    if (options_.require_kvm &&
        ::access(options_.kvm_device.c_str(), R_OK | W_OK) != 0) {
        report.missing.push_back(options_.kvm_device.string());
        add_hint("确保 /dev/kvm 存在且当前用户可访问：sudo usermod -aG kvm "
                 "$USER 后重新登录");
    }

    if (options_.qemu_options.enable_firmware) {
        report.ovmf_code = options_.ovmf_code;
        if (report.ovmf_code.empty()) {
            const auto detected = detect_ovmf_code();
            if (detected) {
                report.ovmf_code = *detected;
            }
        }
        if (!readable_file(report.ovmf_code)) {
            report.missing.push_back("OVMF_CODE.fd");
            add_hint(ovmf_install_hint(distro));
        }

        report.ovmf_vars_template = options_.ovmf_vars_template;
        if (report.ovmf_vars_template.empty()) {
            const auto detected = detect_ovmf_vars_template();
            if (detected) {
                report.ovmf_vars_template = *detected;
            }
        }
        if (!readable_file(report.ovmf_vars_template)) {
            report.missing.push_back("OVMF_VARS.fd 模板");
            add_hint(ovmf_install_hint(distro));
        }
    }

    if (options_.require_swtpm) {
        report.swtpm_binary = find_executable(options_.swtpm_binary.string());
        if (report.swtpm_binary.empty()) {
            report.missing.push_back("swtpm");
            add_hint(swtpm_install_hint(distro));
        }
    }
    return report;
}

/** @brief 校验或在交互模式下获取 Windows 与 virtio ISO 路径。
 * @return 校验通过返回空状态；失败返回参数错误。
 */
Status Installer::validate_isos() {
    if (options_.windows_iso.empty()) {
        if (!options_.interactive) {
            return fail(ErrorCode::kInvalidArgument,
                        "未提供 Windows ISO，请使用 --iso <path>");
        }
        if (::isatty(STDIN_FILENO) == 0) {
            return fail(ErrorCode::kInvalidArgument,
                        "非交互环境请显式使用 --iso <path>");
        }
        std::cout << "请输入合法的 Windows ISO 路径: " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line) || line.empty()) {
            return fail(ErrorCode::kInvalidArgument, "未输入 Windows ISO 路径");
        }
        options_.windows_iso = line;
    }

    if (options_.virtio_iso.empty() && options_.interactive &&
        ::isatty(STDIN_FILENO) != 0) {
        std::cout << "请输入 virtio-win ISO 路径（可留空跳过）: " << std::flush;
        std::string line;
        if (std::getline(std::cin, line) && !line.empty()) {
            options_.virtio_iso = line;
        }
    }

    if (auto status = validate_iso(options_.windows_iso, "Windows ISO");
        !status) {
        return status;
    }
    if (!options_.virtio_iso.empty()) {
        if (auto status = validate_iso(options_.virtio_iso, "virtio-win ISO");
            !status) {
            return status;
        }
    }
    return {};
}

/** @brief 准备系统盘与 UEFI 变量文件。
 * @return 成功返回空状态；失败返回相应错误。
 */
Status Installer::prepare_storage() {
    auto disk = resolve_disk_image();
    if (!disk) {
        return std::unexpected(disk.error());
    }

    std::error_code error;
    if (!disk->parent_path().empty()) {
        std::filesystem::create_directories(disk->parent_path(), error);
        if (error) {
            return fail(ErrorCode::kIoError,
                        "创建磁盘目录失败: " + error.message());
        }
    }

    if (std::filesystem::exists(*disk) && !options_.overwrite_disk) {
        // 安全护栏：LSW 数据目录之外的已有磁盘默认拒绝，防止覆盖其他虚拟机。
        const auto relative = disk->lexically_relative(paths_.data_dir);
        const bool under_data_dir =
            !relative.empty() && *relative.begin() != "..";
        if (!under_data_dir && !options_.allow_existing_disk) {
            return fail(
                ErrorCode::kInvalidArgument,
                "拒绝把安装模式挂到已有的外部磁盘（防止覆盖其他虚拟机）：" +
                    disk->string() +
                    "。如确认要复用该磁盘，请显式添加 --allow-existing-disk；"
                    "更推荐为 LSW 指定一个新磁盘路径。");
        }
        // 幂等：已存在的 LSW 系统盘不覆盖。
    } else {
        if (options_.overwrite_disk && std::filesystem::exists(*disk)) {
            std::filesystem::remove(*disk, error);
            if (error) {
                return fail(ErrorCode::kIoError,
                            "删除旧系统盘失败: " + error.message());
            }
        }
        const auto qemu_img =
            find_executable(options_.qemu_img_binary.string());
        if (qemu_img.empty()) {
            return fail(ErrorCode::kMissingDependency, "qemu-img 不可用");
        }
        auto created = run_process(
            {qemu_img.string(), "create", "-f", "qcow2", disk->string(),
             std::to_string(options_.disk_size_gb) + "G"});
        if (!created) {
            return std::unexpected(created.error());
        }
        if (created->exit_code != 0) {
            return fail(ErrorCode::kQemuError,
                        "qemu-img 创建磁盘失败: " + created->stderr_data);
        }
    }

    if (!options_.qemu_options.enable_firmware) {
        return {};
    }

    const auto vars = paths_.data_dir / "vm" / "OVMF_VARS.fd";
    if (!std::filesystem::exists(vars)) {
        const auto template_path = options_.ovmf_vars_template.empty()
                                       ? detect_ovmf_vars_template()
                                       : std::optional<std::filesystem::path>(
                                             options_.ovmf_vars_template);
        if (!template_path || !readable_file(*template_path)) {
            return fail(ErrorCode::kMissingDependency,
                        "OVMF_VARS.fd 模板不存在，无法创建 UEFI 变量文件");
        }
        std::filesystem::create_directories(vars.parent_path(), error);
        if (error) {
            return fail(ErrorCode::kIoError,
                        "创建 UEFI 变量目录失败: " + error.message());
        }
        std::filesystem::copy_file(*template_path, vars,
                                   std::filesystem::copy_options::none, error);
        if (error) {
            return fail(ErrorCode::kIoError,
                        "复制 OVMF_VARS.fd 失败: " + error.message());
        }
    }
    return {};
}

/** @brief 执行完整的安装流程。
 * @return 成功返回安装结果；失败返回相应错误。
 */
Result<InstallResult> Installer::run() {
    auto report = check_dependencies();
    if (!report) {
        return std::unexpected(report.error());
    }
    if (!report->ok()) {
        std::string message = "缺少安装依赖: " + join(report->missing, "、");
        if (!report->hints.empty()) {
            message += "。修复建议: " + join(report->hints, "；");
        }
        return fail(ErrorCode::kMissingDependency, message);
    }

    if (auto status = validate_isos(); !status) {
        return std::unexpected(status.error());
    }
    if (auto status = prepare_storage(); !status) {
        return std::unexpected(status.error());
    }

    if (!options_.qemu_options.home_dir.empty() &&
        find_executable("smbd").empty()) {
        std::cerr << "lsw: 警告：未检测到 smbd，Windows 内 Z: 共享将不可用。"
                  << samba_install_hint(detect_distro_family()) << "\n";
    }

    auto disk = resolve_disk_image();
    if (!disk) {
        return std::unexpected(disk.error());
    }

    QemuVmOptions qemu_options = options_.qemu_options;
    qemu_options.disk_image = *disk;
    if (qemu_options.enable_firmware) {
        if (qemu_options.ovmf_code.empty()) {
            qemu_options.ovmf_code = report->ovmf_code;
        }
        if (qemu_options.ovmf_vars.empty()) {
            qemu_options.ovmf_vars = paths_.data_dir / "vm" / "OVMF_VARS.fd";
        }
    }

    Config config = config_;
    config.vm.windows_iso = options_.windows_iso;
    config.vm.virtio_iso = options_.virtio_iso;

    QemuVm vm(std::move(config), paths_, qemu_options);
    if (auto started = vm.start_install(); !started) {
        return std::unexpected(started.error());
    }
    auto exit_code = vm.wait();
    if (!exit_code) {
        return std::unexpected(exit_code.error());
    }
    if (*exit_code != 0) {
        return fail(ErrorCode::kQemuError, "安装模式 QEMU 异常退出，退出码 " +
                                               std::to_string(*exit_code));
    }
    return InstallResult{*exit_code, post_install_guidance()};
}

/** @brief 返回安装后的操作指引。
 * @return 指引文本列表。
 */
std::vector<std::string> Installer::post_install_guidance() const {
    return {
        "Windows 安装完成后，请在 Windows 内确认以下事项：",
        "1. 启用 OpenSSH Server：设置 → 系统 → 可选功能 → OpenSSH 服务器",
        "2. 安装并启动 QEMU Guest Agent（virtio-win ISO 内提供）",
        "3. 启用 SMB：共享 C$、D$，开放 445 端口",
        "4. 映射 Z:：net use Z: \\\\10.0.2.4\\qemu /persistent:yes",
        "可运行 scripts/post-install.ps1 自动检查以上项目",
    };
}

} // namespace lsw::vm

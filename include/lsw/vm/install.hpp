#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/qemu.hpp>

namespace lsw::vm {

struct DependencyReport {
    std::filesystem::path qemu_binary;
    std::filesystem::path qemu_img_binary;
    std::filesystem::path ovmf_code;
    std::filesystem::path ovmf_vars_template;
    std::filesystem::path swtpm_binary;
    std::vector<std::string> missing;
    std::vector<std::string> hints;

    [[nodiscard]] bool ok() const noexcept { return missing.empty(); }
};

struct InstallResult {
    int qemu_exit_code{0};
    std::vector<std::string> guidance;
};

struct InstallOptions {
    std::filesystem::path windows_iso;
    std::filesystem::path virtio_iso;
    std::filesystem::path
        disk_image; ///< 空 = config.vm.disk 或 $DATA/vm/disk.qcow2
    std::uint64_t disk_size_gb{64};
    std::filesystem::path kvm_device{"/dev/kvm"};
    std::filesystem::path qemu_binary{"qemu-system-x86_64"};
    std::filesystem::path qemu_img_binary{"qemu-img"};
    std::filesystem::path ovmf_code;          ///< 空 = 自动探测
    std::filesystem::path ovmf_vars_template; ///< 空 = 自动探测
    std::filesystem::path swtpm_binary{"swtpm"};
    bool require_kvm{true};
    bool require_swtpm{false};
    bool interactive{true};
    bool overwrite_disk{false};
    QemuVmOptions qemu_options;
};

/// `lsw --install` 安装流程（P4）：
/// 依赖检查 → ISO 校验/交互输入 → 磁盘/UEFI 变量准备 → GUI 安装 → 安装后指引。
class Installer {
  public:
    Installer(Config config, XdgPaths paths, InstallOptions options);

    [[nodiscard]] Result<DependencyReport> check_dependencies() const;
    [[nodiscard]] Status validate_isos();
    [[nodiscard]] Result<std::filesystem::path> resolve_disk_image() const;
    [[nodiscard]] Status prepare_storage();
    [[nodiscard]] Result<InstallResult> run();

    [[nodiscard]] std::vector<std::string> post_install_guidance() const;

  private:
    Config config_;
    XdgPaths paths_;
    InstallOptions options_;
};

} // namespace lsw::vm

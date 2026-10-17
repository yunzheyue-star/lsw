/**
 * @file install.hpp
 * @brief `lsw --install` 安装流程编排。
 * @details 文件位置：include/lsw/vm/install.hpp（头文件）。
 *          属于 LSW 虚拟机管理（QEMU/QMP/安装）；公共行为以
 * docs/interfaces/qmp.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

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

    /// @brief 依赖是否齐全。
    /// @return 没有缺失依赖返回 true。
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
    /// 空 = 使用 qemu_options.qemu_binary（依赖检查与启动保持同一路径）。
    std::filesystem::path qemu_binary;
    std::filesystem::path qemu_img_binary{"qemu-img"};
    std::filesystem::path ovmf_code;          ///< 空 = 自动探测
    std::filesystem::path ovmf_vars_template; ///< 空 = 自动探测
    std::filesystem::path swtpm_binary{"swtpm"};
    bool require_kvm{true};
    bool require_swtpm{false};
    bool interactive{true};
    bool overwrite_disk{false};
    /// 允许挂载 LSW 数据目录之外已存在的磁盘（默认拒绝，防止覆盖其他虚拟机）。
    bool allow_existing_disk{false};
    QemuVmOptions qemu_options;
};

/// @brief `lsw --install` 安装流程（P4）：
/// 依赖检查 → ISO 校验/交互输入 → 磁盘/UEFI 变量准备 → GUI 安装 → 安装后指引。
class Installer {
  public:
    /// @brief 构造安装器。
    /// @param config 全局配置。
    /// @param paths XDG 路径配置。
    /// @param options 安装选项。
    Installer(Config config, XdgPaths paths, InstallOptions options);

    /// @brief 检查安装所需外部依赖。
    /// @return 成功时返回依赖报告。
    [[nodiscard]] Result<DependencyReport> check_dependencies() const;
    /// @brief 校验 Windows ISO 与 virtio ISO。
    /// @return 校验状态。
    [[nodiscard]] Status validate_isos();
    /// @brief 解析安装使用的磁盘镜像路径。
    /// @return 成功时返回磁盘镜像路径。
    [[nodiscard]] Result<std::filesystem::path> resolve_disk_image() const;
    /// @brief 准备磁盘镜像与 UEFI 变量等存储资源。
    /// @return 准备状态。
    [[nodiscard]] Status prepare_storage();
    /// @brief 执行完整安装流程。
    /// @return 成功时返回安装结果。
    [[nodiscard]] Result<InstallResult> run();

    /// @brief 生成安装后指引。
    /// @return 指引文本列表。
    [[nodiscard]] std::vector<std::string> post_install_guidance() const;

  private:
    Config config_;
    XdgPaths paths_;
    InstallOptions options_;
};

} // namespace lsw::vm

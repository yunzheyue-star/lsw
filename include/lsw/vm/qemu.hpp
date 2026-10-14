#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/core/process.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/qmp_client.hpp>

namespace lsw::vm {

enum class VmStatus { kStopped, kRunning, kPaused, kUnknown };

[[nodiscard]] std::string_view to_string(VmStatus status) noexcept;

struct QemuVmOptions {
    std::filesystem::path qemu_binary{"qemu-system-x86_64"};
    std::filesystem::path ovmf_code; ///< 空 = 自动探测
    std::filesystem::path ovmf_vars; ///< 空 = $DATA/vm/OVMF_VARS.fd
    std::filesystem::path
        disk_image; ///< 空 = config.vm.disk 或 $DATA/vm/disk.qcow2
    std::filesystem::path home_dir; ///< 空 = 当前 $HOME（QEMU user SMB）
    bool enable_kvm{true};          ///< false → accel=tcg（无 KVM 环境/测试）
    bool enable_firmware{true};     ///< false → 不挂 pflash（测试替身）
    bool enable_swtpm{false};
    std::filesystem::path swtpm_binary{"swtpm"};
    std::string machine{"q35"};
    std::chrono::milliseconds shutdown_timeout{30000};
    std::chrono::milliseconds qmp_connect_timeout{5000};
};

/// 在 PATH（或给定路径）中查找可执行文件；找不到返回空路径。
[[nodiscard]] std::filesystem::path find_executable(std::string_view name);

/// OVMF CODE / VARS 的候选路径（覆盖 Debian/Ubuntu、Fedora、Arch 等布局）。
[[nodiscard]] std::vector<std::filesystem::path> ovmf_code_candidates();
[[nodiscard]] std::vector<std::filesystem::path> ovmf_vars_candidates();

/// 探测 OVMF CODE 固件常见位置；找不到返回 nullopt。
[[nodiscard]] std::optional<std::filesystem::path> detect_ovmf_code();

/// 探测 OVMF VARS 模板常见位置；找不到返回 nullopt。
[[nodiscard]] std::optional<std::filesystem::path> detect_ovmf_vars_template();

/// swtpm 是否可用（binary 为空时按 PATH 查找）。
[[nodiscard]] bool
swtpm_available(const std::filesystem::path &binary = "swtpm");

/// 构造 swtpm 进程参数（P3-5）；不修改文件系统。
[[nodiscard]] Result<std::vector<std::string>>
build_swtpm_args(const QemuVmOptions &options, const XdgPaths &paths);

/// 构造完整 QEMU 参数；供 QemuVm 与单元测试复用。
[[nodiscard]] Result<std::vector<std::string>>
build_qemu_args(const Config &config, const XdgPaths &paths,
                const QemuVmOptions &options, bool install_mode);

/// QEMU 生命周期（SKILL-02）：启动、QMP 状态、优雅关闭。
class QemuVm {
  public:
    QemuVm(Config config, XdgPaths paths, QemuVmOptions options = {});

    QemuVm(const QemuVm &) = delete;
    QemuVm &operator=(const QemuVm &) = delete;

    /// 运行模式：-display none，后台进程，QMP 握手完成后返回。
    [[nodiscard]] Result<void> start();
    /// 安装模式：-display gtk + Windows ISO / virtio ISO。
    [[nodiscard]] Result<void> start_install();
    [[nodiscard]] Result<VmStatus> status();
    /// 优先 QMP system_powerdown；超时后 SIGTERM → SIGKILL 兜底。
    [[nodiscard]] Result<void> shutdown();
    [[nodiscard]] Result<int> wait();
    [[nodiscard]] Status kill();

    [[nodiscard]] bool running();
    [[nodiscard]] int pid() const noexcept;
    [[nodiscard]] const std::vector<std::string> &last_args() const noexcept {
        return last_args_;
    }

  private:
    [[nodiscard]] Result<void> launch(bool install_mode);

    Config config_;
    XdgPaths paths_;
    QemuVmOptions options_;
    std::filesystem::path disk_image_;
    std::filesystem::path ovmf_vars_;
    std::filesystem::path qmp_socket_;
    std::filesystem::path qga_socket_;
    std::filesystem::path swtpm_socket_;
    std::optional<core::ChildProcess> process_;
    std::optional<core::ChildProcess> swtpm_process_;
    std::optional<QmpClient> qmp_;
    std::vector<std::string> last_args_;
};

} // namespace lsw::vm

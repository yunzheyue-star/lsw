/**
 * @file qemu.hpp
 * @brief QEMU 启动参数、生命周期与状态查询。
 * @details 文件位置：include/lsw/vm/qemu.hpp（头文件）。
 *          属于 LSW 虚拟机管理（QEMU/QMP/安装）；公共行为以
 * docs/interfaces/qmp.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

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

/// @brief 将虚拟机状态转换为字符串。
/// @param status 虚拟机状态。
/// @return 状态名称。
[[nodiscard]] std::string_view to_string(VmStatus status) noexcept;

struct QemuVmOptions {
    std::filesystem::path qemu_binary{"qemu-system-x86_64"};
    std::filesystem::path ovmf_code; ///< 空 = 自动探测
    std::filesystem::path ovmf_vars; ///< 空 = $DATA/vm/OVMF_VARS.fd
    std::filesystem::path
        disk_image; ///< 空 = config.vm.disk 或 $DATA/vm/disk.qcow2
    std::filesystem::path home_dir; ///< 空 = 当前 $HOME（QEMU user SMB）
    bool enable_kvm{true};          ///< false → accel=tcg（无 KVM 环境/测试）
    bool enable_firmware{
        false}; ///< false = SeaBIOS（BIOS，Win10 推荐）；true = OVMF/UEFI
    bool enable_swtpm{false};
    std::filesystem::path swtpm_binary{"swtpm"};
    std::string machine{"q35"};
    std::chrono::milliseconds shutdown_timeout{30000};
    std::chrono::milliseconds qmp_connect_timeout{5000};
};

/// @brief 在 PATH（或给定路径）中查找可执行文件；找不到返回空路径。
/// @param name 可执行文件名或路径。
/// @return 找到时返回完整路径，否则返回空路径。
[[nodiscard]] std::filesystem::path find_executable(std::string_view name);

/// @brief OVMF CODE / VARS 的候选路径（覆盖 Debian/Ubuntu、Fedora、Arch
/// 等布局）。
/// @return OVMF CODE 候选路径列表。
[[nodiscard]] std::vector<std::filesystem::path> ovmf_code_candidates();
/// @brief OVMF VARS 的候选路径。
/// @return OVMF VARS 候选路径列表。
[[nodiscard]] std::vector<std::filesystem::path> ovmf_vars_candidates();

/// @brief 探测 OVMF CODE 固件常见位置；找不到返回 nullopt。
/// @return 找到时返回 OVMF CODE 路径，否则返回 nullopt。
[[nodiscard]] std::optional<std::filesystem::path> detect_ovmf_code();

/// @brief 探测 OVMF VARS 模板常见位置；找不到返回 nullopt。
/// @return 找到时返回 OVMF VARS 模板路径，否则返回 nullopt。
[[nodiscard]] std::optional<std::filesystem::path> detect_ovmf_vars_template();

/// @brief swtpm 是否可用（binary 为空时按 PATH 查找）。
/// @param binary swtpm 可执行文件路径。
/// @return 可用返回 true。
[[nodiscard]] bool
swtpm_available(const std::filesystem::path &binary = "swtpm");

/// @brief 构造 swtpm 进程参数（P3-5）；不修改文件系统。
/// @param options QEMU 虚拟机选项。
/// @param paths XDG 路径配置。
/// @return 成功时返回 swtpm 参数列表。
[[nodiscard]] Result<std::vector<std::string>>
build_swtpm_args(const QemuVmOptions &options, const XdgPaths &paths);

/// @brief 构造完整 QEMU 参数；供 QemuVm 与单元测试复用。
/// @param config 全局配置。
/// @param paths XDG 路径配置。
/// @param options QEMU 虚拟机选项。
/// @param install_mode 是否为安装模式。
/// @return 成功时返回 QEMU 参数列表。
[[nodiscard]] Result<std::vector<std::string>>
build_qemu_args(const Config &config, const XdgPaths &paths,
                const QemuVmOptions &options, bool install_mode);

/// @brief QEMU 生命周期（SKILL-02）：启动、QMP 状态、优雅关闭。
class QemuVm {
  public:
    /// @brief 构造 QEMU 虚拟机管理器。
    /// @param config 全局配置。
    /// @param paths XDG 路径配置。
    /// @param options 虚拟机选项。
    QemuVm(Config config, XdgPaths paths, QemuVmOptions options = {});

    /// @brief 禁用拷贝构造。
    QemuVm(const QemuVm &) = delete;
    /// @brief 禁用拷贝赋值。
    /// @return 当前虚拟机引用（该删除函数不会被调用）。
    QemuVm &operator=(const QemuVm &) = delete;

    /// @brief 运行模式：-display none，后台进程，QMP 握手完成后返回。
    /// @return 启动状态。
    [[nodiscard]] Result<void> start();
    /// @brief 安装模式：-display gtk + Windows ISO / virtio ISO。
    /// @return 启动状态。
    [[nodiscard]] Result<void> start_install();
    /// @brief 查询虚拟机运行状态。
    /// @return 成功时返回虚拟机状态。
    [[nodiscard]] Result<VmStatus> status();
    /// @brief 优先 QMP system_powerdown；超时后 SIGTERM → SIGKILL 兜底。
    /// @return 关闭状态。
    [[nodiscard]] Result<void> shutdown();
    /// @brief 等待 QEMU 进程退出。
    /// @return 成功时返回进程退出码。
    [[nodiscard]] Result<int> wait();
    /// @brief 强制终止 QEMU 进程。
    /// @return 操作状态。
    [[nodiscard]] Status kill();

    /// @brief 查询 QEMU 进程是否仍在运行。
    /// @return 仍在运行返回 true。
    [[nodiscard]] bool running();
    /// @brief 获取 QEMU 进程 pid。
    /// @return 进程 pid；未运行时返回 -1。
    [[nodiscard]] int pid() const noexcept;
    /// @brief 获取最近一次启动使用的参数。
    /// @return 参数列表的常量引用。
    [[nodiscard]] const std::vector<std::string> &last_args() const noexcept {
        return last_args_;
    }

  private:
    /// @brief 按模式启动 QEMU 进程并完成 QMP 握手。
    /// @param install_mode 是否为安装模式。
    /// @return 启动状态。
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

/**
 * @file mount.hpp
 * @brief Windows 共享盘（C$/D$）的 CIFS 挂载与恢复接口。
 * @details 文件位置：include/lsw/fs/mount.hpp（头文件）。
 *          属于 LSW 文件系统模块；公共行为以 docs/interfaces/filesystem.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/fs/credentials.hpp>

#ifndef LSW_INSTALL_HELPER_PATH
#define LSW_INSTALL_HELPER_PATH "/usr/local/lib/lsw/lsw-mount-helper"
#endif

namespace lsw::winfs {

/// @brief CIFS 挂载行为选项。
struct MountOptions {
    std::filesystem::path mount_root{"/mnt/Windows"}; ///< Linux 挂载根目录。
    std::filesystem::path helper_path{
        LSW_INSTALL_HELPER_PATH};  ///< 最小权限挂载 helper。
    std::string privilege{"sudo"}; ///< 提权方式：sudo / pkexec / none（测试）。
    std::string cifs_version{"3.0"};    ///< mount.cifs 协议版本。
    std::vector<char> drives{'C', 'D'}; ///< 需要挂载的 Windows 盘符。
    std::filesystem::path mounts_file{
        "/proc/mounts"};                      ///< 挂载表（测试可注入）。
    std::chrono::milliseconds timeout{20000}; ///< 单次 helper 超时。
};

/// @brief 已挂载的 Windows 盘信息。
struct MountedDrive {
    char letter{'C'};                  ///< 盘符（C/D）。
    std::filesystem::path source;      ///< CIFS 源，例如 //127.0.0.1/C$。
    std::filesystem::path mount_point; ///< Linux 挂载点。
};

/// @brief 通过 helper 挂载 Windows C$/D$ 到 Linux，并处理残留与重挂。
class CifsMountManager {
  public:
    /// @brief 构造 CIFS 挂载管理器。
    /// @param config 全局配置（网络端口、挂载根、用户名）。
    /// @param options 挂载选项。
    CifsMountManager(Config config, MountOptions options = {});

    /// @brief 确保配置中的 Windows 盘全部挂载（幂等，失败自动重挂一次）。
    /// @param credential Windows SMB 凭据；密码只通过 stdin 传给 helper。
    /// @return 成功返回挂载点字符串列表；失败返回 kMountFailed /
    /// kMissingDependency / kPermissionDenied。
    [[nodiscard]] Result<std::vector<std::string>>
    ensure_mounts(const WindowsCredential &credential);

    /// @brief 卸载挂载根下所有 CIFS 挂载点（幂等，shutdown 兜底）。
    /// @return 全部成功或已无挂载返回空状态；否则返回 kUnmountFailed。
    [[nodiscard]] Status unmount_all();

    /// @brief 列出挂载根下当前残留的 CIFS 挂载点。
    /// @return 挂载点列表；读取挂载表失败返回错误。
    [[nodiscard]] Result<std::vector<MountedDrive>> mounted_drives() const;

    /// @brief 异常恢复：先卸载残留挂载，再重新挂载全部盘符。
    /// @param credential Windows SMB 凭据。
    /// @return 成功返回挂载点字符串列表；失败返回错误。
    [[nodiscard]] Result<std::vector<std::string>>
    recover(const WindowsCredential &credential);

  private:
    /// @brief 挂载单个盘符。
    /// @param letter 盘符字符（C/D）。
    /// @param credential Windows SMB 凭据。
    /// @return 成功返回空状态，失败返回错误。
    [[nodiscard]] Status mount_drive(char letter,
                                     const WindowsCredential &credential);

    /// @brief 卸载指定挂载点。
    /// @param mount_point Linux 挂载点。
    /// @return 成功返回空状态，失败返回错误。
    [[nodiscard]] Status
    unmount_point(const std::filesystem::path &mount_point);

    /// @brief 构造 helper 参数并执行。
    /// @param helper_args helper 子命令及其参数（不含 helper 路径与提权）。
    /// @param stdin_data 需要写入 helper stdin 的数据（密码）。
    /// @return 成功返回空状态，失败返回错误。
    [[nodiscard]] Status
    run_helper(const std::vector<std::string> &helper_args,
               const std::optional<std::string> &stdin_data) const;

    /// @brief 检查挂载点是否已在挂载表中且类型为 CIFS。
    /// @param mount_point 待检查的挂载点。
    /// @return 已挂载返回 true，否则 false。
    [[nodiscard]] bool
    is_mounted(const std::filesystem::path &mount_point) const;

    Config config_;        ///< 全局配置副本。
    MountOptions options_; ///< 挂载选项副本。
};

} // namespace lsw::winfs

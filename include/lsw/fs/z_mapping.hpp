/**
 * @file z_mapping.hpp
 * @brief Windows 内 `Z:` 盘符映射到 Linux `$HOME` 的接口。
 * @details 文件位置：include/lsw/fs/z_mapping.hpp（头文件）。
 *          属于 LSW 文件系统模块；公共行为以 docs/interfaces/filesystem.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/core/process.hpp>
#include <lsw/fs/credentials.hpp>

namespace lsw::winfs {

/// @brief `Z:` 映射选项；未设置端口时使用 `network.ssh_host_port`。
struct ZMappingOptions {
    std::string ssh_binary{"ssh"}; ///< SSH 可执行文件；测试可替换为假脚本。
    std::string host{"127.0.0.1"}; ///< 宿主机地址。
    std::optional<int> port;       ///< SSH 端口；空表示用配置值。
    std::chrono::milliseconds timeout{
        60000}; ///< 单次 SSH 执行超时（覆盖重试循环）。
};

/// @brief 通过 SSH + PowerShell 确保 Windows 内 `Z:` 指向 QEMU user SMB。
class ZDriveMapper {
  public:
    /// @brief 构造 `Z:` 映射器。
    /// @param config 全局配置（端口、用户名、guest SMB 地址）。
    /// @param options 映射选项。
    ZDriveMapper(Config config, ZMappingOptions options = {});

    /// @brief 确保 `Z:` 已映射：未映射时执行 `net use` 并持久化。
    /// @param credential 可选 Windows 凭据；无交互登录缓存时需显式提供，
    ///        密码通过 SSH stdin 传给 PowerShell，不进入 argv。
    /// @return 成功返回空状态；失败返回 kShareSetupFailed /
    /// kMissingDependency。
    [[nodiscard]] Status ensure_mapped(
        const std::optional<WindowsCredential> &credential = std::nullopt);

    /// @brief 检查 `Z:` 当前是否已映射。
    /// @return 已映射返回 true；未映射返回 false；SSH 执行失败返回错误。
    [[nodiscard]] Result<bool> is_mapped();

  private:
    /// @brief 通过 SSH 执行 PowerShell 脚本。
    /// @param script PowerShell 脚本文本。
    /// @param stdin_data 可选 stdin 数据（例如密码）；脚本可用
    ///        `[Console]::In.ReadLine()` 读取，避免密码进入 argv。
    /// @return 进程执行结果；SSH 不可用或超时返回错误。
    [[nodiscard]] Result<ProcessResult> ssh_powershell(
        const std::string &script,
        const std::optional<std::string> &stdin_data = std::nullopt) const;

    /// @brief 校验配置字段可安全拼入 PowerShell 脚本。
    /// @return 合法返回空状态，否则返回 kConfigInvalid。
    [[nodiscard]] Status validate() const;

    Config config_;           ///< 全局配置副本。
    ZMappingOptions options_; ///< 映射选项副本。
};

} // namespace lsw::winfs

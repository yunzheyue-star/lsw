/**
 * @file powershell.hpp
 * @brief `lsw shell` 的 SSH / PowerShell 命令构造。
 * @details 文件位置：include/lsw/shell/powershell.hpp（头文件）。
 *          属于 LSW shell 模块；负责把 Linux cwd 映射为 Windows `Z:`，
 *          并在远端 PowerShell 会话内先确保映射、再 Set-Location，最后
 *          以 `-NoExit` 进入交互式终端。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>

namespace lsw::shell {

/// @brief 构造远端 PowerShell 初始化脚本（未编码）。
/// - cwd == home → 不 Set-Location，进入 PowerShell 默认路径；
/// - cwd 在 home 下 → 先映射 `Z:`，再 `Set-Location Z:\<relative>`；
/// - cwd 不在 home 下 → kPathMappingFailed。
/// @param config 全局配置（SSH 端口、host SMB 地址、Windows 用户）。
/// @param cwd 当前 Linux 工作目录。
/// @param home Linux 家目录。
/// @return 成功返回 PowerShell 脚本文本；失败返回 kPathMappingFailed。
[[nodiscard]] Result<std::string>
build_powershell_script(const Config &config, const std::filesystem::path &cwd,
                        const std::filesystem::path &home);

/// @brief 构造完整 ssh argv。
/// @param config 全局配置。
/// @param cwd 当前 Linux 工作目录。
/// @param home Linux 家目录。
/// @param ssh_binary ssh 可执行文件（默认 `ssh`，测试可替换）。
/// @return 成功返回可直接 execvp 的 argv；失败返回路径映射错误。
[[nodiscard]] Result<std::vector<std::string>>
build_ssh_args(const Config &config, const std::filesystem::path &cwd,
               const std::filesystem::path &home,
               std::string ssh_binary = "ssh");

} // namespace lsw::shell

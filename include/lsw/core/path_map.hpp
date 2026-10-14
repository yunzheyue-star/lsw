/**
 * @file path_map.hpp
 * @brief Linux 路径到 Windows Z: 的映射规则。
 * @details 文件位置：include/lsw/core/path_map.hpp（头文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <lsw/core/error.hpp>

namespace lsw {

/// @brief cwd 是否位于 home 内（包含 home 本身）。
/// @param cwd 待判断的路径。
/// @param home 用户 home 目录。
/// @return cwd 位于 home 内（含 home 本身）返回 true，否则 false。
[[nodiscard]] bool is_within_home(const std::filesystem::path &cwd,
                                  const std::filesystem::path &home);

/// @brief 与 SKILL-04 一致的映射函数：
/// - cwd 在 home 下（不含 home 本身）→ `Z:\relative`
/// - cwd == home 或不在 home 下 → nullopt
/// @param cwd 待映射的 Linux 当前目录。
/// @param home 用户 home 目录。
/// @return 映射后的 Windows 路径；不满足条件时返回 nullopt。
[[nodiscard]] std::optional<std::string>
linux_to_z(const std::filesystem::path &cwd, const std::filesystem::path &home);

/// @brief 面向调用方的完整映射：
/// - cwd == home → 返回空字符串（表示 PowerShell 默认路径）
/// - cwd 在 home 下 → 返回 `Z:\relative`
/// - 否则 → kPathMappingFailed
/// @param cwd 待映射的 Linux 当前目录。
/// @param home 用户 home 目录。
/// @return 成功返回 Windows 路径（home 本身为空字符串），否则返回
/// kPathMappingFailed。
[[nodiscard]] Result<std::string>
map_cwd_to_z(const std::filesystem::path &cwd,
             const std::filesystem::path &home);

} // namespace lsw

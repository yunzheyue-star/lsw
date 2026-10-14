#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <lsw/core/error.hpp>

namespace lsw {

/// cwd 是否位于 home 内（包含 home 本身）。
[[nodiscard]] bool is_within_home(const std::filesystem::path &cwd,
                                  const std::filesystem::path &home);

/// 与 SKILL-04 一致的映射函数：
/// - cwd 在 home 下（不含 home 本身）→ `Z:\relative`
/// - cwd == home 或不在 home 下 → nullopt
[[nodiscard]] std::optional<std::string>
linux_to_z(const std::filesystem::path &cwd, const std::filesystem::path &home);

/// 面向调用方的完整映射：
/// - cwd == home → 返回空字符串（表示 PowerShell 默认路径）
/// - cwd 在 home 下 → 返回 `Z:\relative`
/// - 否则 → kPathMappingFailed
[[nodiscard]] Result<std::string>
map_cwd_to_z(const std::filesystem::path &cwd,
             const std::filesystem::path &home);

} // namespace lsw

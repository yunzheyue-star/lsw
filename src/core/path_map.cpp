/**
 * @file path_map.cpp
 * @brief path_map 实现（核心库）。
 * @details 文件位置：src/core/path_map.cpp（源文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <lsw/core/path_map.hpp>

#include <system_error>

namespace lsw {
namespace {

/// @brief 规范化路径；无法规范化时回退为绝对路径并记录错误。
/// @param path 待规范化的路径。
/// @param fallback 输出参数，规范化失败时写入错误码。
/// @return 规范化后的路径。
std::filesystem::path normalized(const std::filesystem::path &path,
                                 std::error_code &fallback) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }
    fallback = error;
    std::error_code absolute_error;
    auto absolute = std::filesystem::absolute(path, absolute_error);
    return (absolute_error ? path : absolute).lexically_normal();
}

/// @brief 返回相对 home 的路径；不在 home 下返回 nullopt。
/// @param cwd 待转换的当前工作目录。
/// @param home 用户主目录。
/// @return 相对 home 的路径；不在 home 下时返回 std::nullopt。
std::optional<std::filesystem::path>
relative_to_home(const std::filesystem::path &cwd,
                 const std::filesystem::path &home) {
    std::error_code ignored;
    const auto c = normalized(cwd, ignored);
    const auto h = normalized(home, ignored);
    auto relative = c.lexically_relative(h);
    if (relative.empty()) {
        return std::nullopt;
    }
    for (const auto &part : relative) {
        if (part == "..") {
            return std::nullopt;
        }
    }
    return relative;
}

/// @brief 将相对路径转换为 Windows Z: 盘路径。
/// @param relative 相对于 home 的路径。
/// @return 使用反斜杠分隔的 Windows Z: 路径。
std::string to_windows_path(const std::filesystem::path &relative) {
    std::string out = "Z:";
    for (const auto &part : relative) {
        if (part == "." || part.empty()) {
            continue;
        }
        out += "\\" + part.string();
    }
    return out;
}

} // namespace

/// @brief 判断 cwd 是否位于 home 内（包含 home 本身）。
/// @param cwd 待判断的当前工作目录。
/// @param home 用户主目录。
/// @return 位于 home 内时返回 true，否则返回 false。
bool is_within_home(const std::filesystem::path &cwd,
                    const std::filesystem::path &home) {
    return relative_to_home(cwd, home).has_value();
}

/// @brief 将 home 下的 Linux 路径映射为 Windows Z: 路径。
/// @param cwd 待映射的当前工作目录。
/// @param home 用户主目录。
/// @return 映射结果；cwd 等于 home 或不在 home 下时返回 std::nullopt。
std::optional<std::string> linux_to_z(const std::filesystem::path &cwd,
                                      const std::filesystem::path &home) {
    auto relative = relative_to_home(cwd, home);
    if (!relative.has_value() || *relative == ".") {
        return std::nullopt;
    }
    return to_windows_path(*relative);
}

/// @brief 将当前目录完整映射为 Windows Z: 路径。
/// @param cwd 待映射的当前工作目录。
/// @param home 用户主目录。
/// @return cwd 等于 home 时返回空字符串；位于 home 下返回 Z: 路径；否则返回
/// kPathMappingFailed。
Result<std::string> map_cwd_to_z(const std::filesystem::path &cwd,
                                 const std::filesystem::path &home) {
    auto relative = relative_to_home(cwd, home);
    if (!relative.has_value()) {
        return fail(ErrorCode::kPathMappingFailed,
                    "当前目录不在 $HOME 下，无法映射到 Z:：" + cwd.string());
    }
    if (*relative == ".") {
        return std::string{};
    }
    return to_windows_path(*relative);
}

} // namespace lsw

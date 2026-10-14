#include <lsw/core/path_map.hpp>

#include <system_error>

namespace lsw {
namespace {

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

/// 返回相对 home 的路径；不在 home 下返回 nullopt。
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

bool is_within_home(const std::filesystem::path &cwd,
                    const std::filesystem::path &home) {
    return relative_to_home(cwd, home).has_value();
}

std::optional<std::string> linux_to_z(const std::filesystem::path &cwd,
                                      const std::filesystem::path &home) {
    auto relative = relative_to_home(cwd, home);
    if (!relative.has_value() || *relative == ".") {
        return std::nullopt;
    }
    return to_windows_path(*relative);
}

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

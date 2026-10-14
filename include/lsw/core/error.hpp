#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace lsw {

/// 统一错误码。CLI 退出码映射见 to_exit_code()，与 docs/interfaces/cli.md
/// 一致。
enum class ErrorCode {
    kOk = 0,
    kInternal = 1,
    kInvalidArgument = 2,
    kMissingDependency = 3,
    kPathMappingFailed = 5,
    kSshFailed = 6,
    kProgramFailed = 7,
    kQemuError = 8,
    kConfigNotFound = 9,
    kConfigParseError = 10,
    kConfigInvalid = 11,
    kIoError = 12,
    kProcessError = 13,
    kProcessTimeout = 14,
    kUnknownCommand = 15,
    kNotImplemented = 16,
    kDaemonUnavailable = 17,
    kProtocolError = 18,
};

struct Error {
    ErrorCode code{ErrorCode::kInternal};
    std::string message;
};

[[nodiscard]] Error make_error(ErrorCode code, std::string message);

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

/// 错误码 → CLI 退出码。配置类错误按参数错误（2）处理，其余内部错误归为 1。
[[nodiscard]] int to_exit_code(ErrorCode code) noexcept;

template <typename T> using Result = std::expected<T, Error>;

using Status = Result<void>;

/// 构造失败结果：`return fail(ErrorCode::kXxx, "原因");`
[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code,
                                                 std::string message) {
    return std::unexpected(make_error(code, std::move(message)));
}

} // namespace lsw

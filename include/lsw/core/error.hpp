/**
 * @file error.hpp
 * @brief 统一错误码、Error 与 Result<T>/Status 类型。
 * @details 文件位置：include/lsw/core/error.hpp（头文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace lsw {

/// @brief 统一错误码。CLI 退出码映射见 to_exit_code()，与
/// docs/interfaces/cli.md 一致。
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

/// @brief 构造统一错误对象。
/// @param code 统一错误码。
/// @param message 错误描述信息。
/// @return 由错误码与描述构造的 Error。
[[nodiscard]] Error make_error(ErrorCode code, std::string message);

/// @brief 错误码转换为可读字符串。
/// @param code 统一错误码。
/// @return 错误码对应的字符串视图。
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

/// @brief 错误码 → CLI 退出码。配置类错误按参数错误（2）处理，其余内部错误归为
/// 1。
/// @param code 统一错误码。
/// @return 该错误码对应的 CLI 退出码。
[[nodiscard]] int to_exit_code(ErrorCode code) noexcept;

template <typename T> using Result = std::expected<T, Error>;

using Status = Result<void>;

/// @brief 构造失败结果：`return fail(ErrorCode::kXxx, "原因");`
/// @param code 统一错误码。
/// @param message 错误描述信息。
/// @return 含 Error 的 unexpected，可隐式转换为 Result<T>/Status。
[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code,
                                                 std::string message) {
    return std::unexpected(make_error(code, std::move(message)));
}

} // namespace lsw

/**
 * @file error.cpp
 * @brief error 实现（核心库）。
 * @details 文件位置：src/core/error.cpp（源文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <lsw/core/error.hpp>

namespace lsw {

/// @brief 构造统一错误对象。
/// @param code 错误码。
/// @param message 错误描述。
/// @return 包含错误码与描述的错误对象。
Error make_error(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

/// @brief 将错误码转换为稳定字符串。
/// @param code 待转换的错误码。
/// @return 与错误码对应的字符串视图。
std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::kOk:
        return "Ok";
    case ErrorCode::kInternal:
        return "Internal";
    case ErrorCode::kInvalidArgument:
        return "InvalidArgument";
    case ErrorCode::kMissingDependency:
        return "MissingDependency";
    case ErrorCode::kPathMappingFailed:
        return "PathMappingFailed";
    case ErrorCode::kSshFailed:
        return "SshFailed";
    case ErrorCode::kProgramFailed:
        return "ProgramFailed";
    case ErrorCode::kQemuError:
        return "QemuError";
    case ErrorCode::kConfigNotFound:
        return "ConfigNotFound";
    case ErrorCode::kConfigParseError:
        return "ConfigParseError";
    case ErrorCode::kConfigInvalid:
        return "ConfigInvalid";
    case ErrorCode::kIoError:
        return "IoError";
    case ErrorCode::kProcessError:
        return "ProcessError";
    case ErrorCode::kProcessTimeout:
        return "ProcessTimeout";
    case ErrorCode::kUnknownCommand:
        return "UnknownCommand";
    case ErrorCode::kNotImplemented:
        return "NotImplemented";
    case ErrorCode::kDaemonUnavailable:
        return "DaemonUnavailable";
    case ErrorCode::kProtocolError:
        return "ProtocolError";
    case ErrorCode::kMountFailed:
        return "MountFailed";
    case ErrorCode::kUnmountFailed:
        return "UnmountFailed";
    case ErrorCode::kShareSetupFailed:
        return "ShareSetupFailed";
    case ErrorCode::kPermissionDenied:
        return "PermissionDenied";
    case ErrorCode::kVMNotRunning:
        return "VMNotRunning";
    }
    return "Unknown";
}

/// @brief 将错误码映射为 CLI 退出码。
/// @param code 待映射的错误码。
/// @return 与错误码对应的退出码。
int to_exit_code(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::kOk:
        return 0;
    case ErrorCode::kInvalidArgument:
        return 2;
    case ErrorCode::kConfigNotFound:
        return 2;
    case ErrorCode::kConfigParseError:
        return 2;
    case ErrorCode::kConfigInvalid:
        return 2;
    case ErrorCode::kMissingDependency:
        return 3;
    case ErrorCode::kPathMappingFailed:
        return 5;
    case ErrorCode::kSshFailed:
        return 6;
    case ErrorCode::kProgramFailed:
        return 7;
    case ErrorCode::kQemuError:
    case ErrorCode::kMountFailed:
    case ErrorCode::kUnmountFailed:
    case ErrorCode::kShareSetupFailed:
    case ErrorCode::kPermissionDenied:
        return 8;
    case ErrorCode::kVMNotRunning:
        return 6;
    case ErrorCode::kInternal:
        return 1;
    case ErrorCode::kIoError:
        return 1;
    case ErrorCode::kProcessError:
        return 1;
    case ErrorCode::kProcessTimeout:
        return 1;
    case ErrorCode::kUnknownCommand:
        return 2;
    case ErrorCode::kNotImplemented:
        return 1;
    case ErrorCode::kDaemonUnavailable:
        return 1;
    case ErrorCode::kProtocolError:
        return 1;
    }
    return 1;
}

} // namespace lsw

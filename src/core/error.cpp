#include <lsw/core/error.hpp>

namespace lsw {

Error make_error(ErrorCode code, std::string message) {
  return Error{code, std::move(message)};
}

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk:              return "Ok";
    case ErrorCode::kInternal:        return "Internal";
    case ErrorCode::kInvalidArgument: return "InvalidArgument";
    case ErrorCode::kMissingDependency: return "MissingDependency";
    case ErrorCode::kPathMappingFailed: return "PathMappingFailed";
    case ErrorCode::kSshFailed:       return "SshFailed";
    case ErrorCode::kProgramFailed:   return "ProgramFailed";
    case ErrorCode::kQemuError:       return "QemuError";
    case ErrorCode::kConfigNotFound:  return "ConfigNotFound";
    case ErrorCode::kConfigParseError: return "ConfigParseError";
    case ErrorCode::kConfigInvalid:   return "ConfigInvalid";
    case ErrorCode::kIoError:         return "IoError";
    case ErrorCode::kProcessError:    return "ProcessError";
    case ErrorCode::kProcessTimeout:  return "ProcessTimeout";
    case ErrorCode::kUnknownCommand:  return "UnknownCommand";
    case ErrorCode::kNotImplemented:  return "NotImplemented";
    case ErrorCode::kDaemonUnavailable: return "DaemonUnavailable";
    case ErrorCode::kProtocolError:   return "ProtocolError";
  }
  return "Unknown";
}

int to_exit_code(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk:               return 0;
    case ErrorCode::kInvalidArgument:  return 2;
    case ErrorCode::kConfigNotFound:   return 2;
    case ErrorCode::kConfigParseError: return 2;
    case ErrorCode::kConfigInvalid:    return 2;
    case ErrorCode::kMissingDependency: return 3;
    case ErrorCode::kPathMappingFailed: return 5;
    case ErrorCode::kSshFailed:        return 6;
    case ErrorCode::kProgramFailed:    return 7;
    case ErrorCode::kQemuError:        return 8;
    case ErrorCode::kInternal:         return 1;
    case ErrorCode::kIoError:          return 1;
    case ErrorCode::kProcessError:     return 1;
    case ErrorCode::kProcessTimeout:   return 1;
    case ErrorCode::kUnknownCommand:   return 2;
    case ErrorCode::kNotImplemented:   return 1;
    case ErrorCode::kDaemonUnavailable: return 1;
    case ErrorCode::kProtocolError:    return 1;
  }
  return 1;
}

}  // namespace lsw

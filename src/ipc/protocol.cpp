/**
 * @file protocol.cpp
 * @brief protocol 实现（IPC 与守护进程通信）。
 * @details 文件位置：src/ipc/protocol.cpp（源文件）。
 *          属于 LSW IPC 与守护进程通信；公共行为以
 * docs/interfaces/daemon-ipc.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/ipc/protocol.hpp>

#include <utility>

namespace lsw::ipc {
namespace {

/** @brief 解析单行 JSON 报文并校验其为对象。
 * @param line 待解析的 JSON 文本行。
 * @return 解析出的 JSON 对象；失败返回协议错误。
 */
Result<nlohmann::json> parse_object(std::string_view line) {
    if (line.empty()) {
        return fail(ErrorCode::kProtocolError, "空报文");
    }
    auto value =
        nlohmann::json::parse(line.begin(), line.end(), nullptr, false);
    if (value.is_discarded()) {
        return fail(ErrorCode::kProtocolError, "JSON 解析失败");
    }
    if (!value.is_object()) {
        return fail(ErrorCode::kProtocolError, "报文体必须是 JSON 对象");
    }
    return value;
}

} // namespace

/** @brief 将请求序列化为 JSON 对象。
 * @return 包含 id、cmd 与 args 的 JSON 对象。
 */
nlohmann::json Request::to_json() const {
    return nlohmann::json{{"id", id}, {"cmd", cmd}, {"args", args}};
}

/** @brief 将响应序列化为 JSON 对象。
 * @return 成功响应包含 data，失败响应包含 error 的 JSON 对象。
 */
nlohmann::json Response::to_json() const {
    nlohmann::json value{{"id", id}, {"ok", ok}};
    if (ok) {
        value["data"] = data;
    } else {
        value["error"] =
            nlohmann::json{{"code", error_code}, {"message", error_message}};
    }
    return value;
}

/** @brief 构造成功响应。
 * @param id 响应关联的请求 id。
 * @param data 成功返回的数据对象。
 * @return 成功响应对象。
 */
Response Response::success(std::string id, nlohmann::json data) {
    Response response;
    response.id = std::move(id);
    response.ok = true;
    response.data = std::move(data);
    return response;
}

/** @brief 构造失败响应。
 * @param id 响应关联的请求 id。
 * @param code IPC 错误码字符串。
 * @param message 错误描述。
 * @return 失败响应对象。
 */
Response Response::failure(std::string id, std::string code,
                           std::string message) {
    Response response;
    response.id = std::move(id);
    response.ok = false;
    response.error_code = std::move(code);
    response.error_message = std::move(message);
    return response;
}

/** @brief 解析一行 JSON 请求。
 * @param line 待解析的 JSON 文本行。
 * @return 解析成功返回请求对象；失败返回协议错误。
 */
Result<Request> parse_request(std::string_view line) {
    auto value = parse_object(line);
    if (!value) {
        return std::unexpected(value.error());
    }

    if (!value->contains("id") || !(*value)["id"].is_string() ||
        (*value)["id"].get<std::string>().empty()) {
        return fail(ErrorCode::kProtocolError, "请求缺少合法的 id");
    }
    if (!value->contains("cmd") || !(*value)["cmd"].is_string() ||
        (*value)["cmd"].get<std::string>().empty()) {
        return fail(ErrorCode::kProtocolError, "请求缺少合法的 cmd");
    }
    if (value->contains("args") && !(*value)["args"].is_object()) {
        return fail(ErrorCode::kProtocolError, "args 必须是 JSON 对象");
    }

    Request request;
    request.id = (*value)["id"].get<std::string>();
    request.cmd = (*value)["cmd"].get<std::string>();
    if (value->contains("args")) {
        request.args = (*value)["args"];
    }
    return request;
}

/** @brief 解析一行 JSON 响应。
 * @param line 待解析的 JSON 文本行。
 * @return 解析成功返回响应对象；失败返回协议错误。
 */
Result<Response> parse_response(std::string_view line) {
    auto value = parse_object(line);
    if (!value) {
        return std::unexpected(value.error());
    }

    if (!value->contains("id") || !(*value)["id"].is_string()) {
        return fail(ErrorCode::kProtocolError, "响应缺少合法的 id");
    }
    if (!value->contains("ok") || !(*value)["ok"].is_boolean()) {
        return fail(ErrorCode::kProtocolError, "响应缺少合法的 ok");
    }

    Response response;
    response.id = (*value)["id"].get<std::string>();
    response.ok = (*value)["ok"].get<bool>();

    if (response.ok) {
        if (value->contains("data")) {
            if (!(*value)["data"].is_object()) {
                return fail(ErrorCode::kProtocolError, "data 必须是 JSON 对象");
            }
            response.data = (*value)["data"];
        }
        return response;
    }

    if (!value->contains("error") || !(*value)["error"].is_object()) {
        return fail(ErrorCode::kProtocolError, "失败响应缺少 error 对象");
    }
    const auto &error = (*value)["error"];
    if (!error.contains("code") || !error["code"].is_string() ||
        !error.contains("message") || !error["message"].is_string()) {
        return fail(ErrorCode::kProtocolError,
                    "error 必须包含 code 与 message");
    }
    response.error_code = error["code"].get<std::string>();
    response.error_message = error["message"].get<std::string>();
    return response;
}

/** @brief 将请求编码为一行 JSON Lines 文本。
 * @param request 待编码的请求。
 * @return 以换行符结尾的 JSON 文本。
 */
std::string encode_line(const Request &request) {
    return request.to_json().dump() + "\n";
}

/** @brief 将响应编码为一行 JSON Lines 文本。
 * @param response 待编码的响应。
 * @return 以换行符结尾的 JSON 文本。
 */
std::string encode_line(const Response &response) {
    return response.to_json().dump() + "\n";
}

/** @brief 将内部 ErrorCode 映射为 IPC 错误码字符串。
 * @param code 内部错误码。
 * @return 对应的 IPC 错误码字符串。
 */
std::string to_ipc_error_code(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::kInvalidArgument:
        return std::string(kBadRequest);
    case ErrorCode::kUnknownCommand:
        return std::string(kUnknownCommand);
    case ErrorCode::kNotImplemented:
        return std::string(kNotImplemented);
    case ErrorCode::kPathMappingFailed:
        return std::string(kPathOutsideHome);
    case ErrorCode::kVMNotRunning:
        return std::string(kVMNotRunning);
    case ErrorCode::kSshFailed:
        return std::string(kSshFailed);
    case ErrorCode::kProgramFailed:
        return std::string(kProgramFailed);
    case ErrorCode::kIoError:
        return std::string(kMountFailed);
    case ErrorCode::kMountFailed:
        return std::string(kMountFailed);
    case ErrorCode::kUnmountFailed:
        return std::string(kUnmountFailed);
    case ErrorCode::kShareSetupFailed:
        return std::string(kShareSetupFailed);
    case ErrorCode::kPermissionDenied:
        return std::string(kPermissionDenied);
    case ErrorCode::kMissingDependency:
        return std::string(kMissingDependency);
    case ErrorCode::kQemuError:
        return std::string(kQmpError);
    case ErrorCode::kProtocolError:
        return std::string(kBadRequest);
    default:
        return std::string(kInternal);
    }
}

/** @brief 将 IPC 错误码字符串映射为内部 ErrorCode。
 * @param code IPC 错误码字符串。
 * @return 对应的内部错误码；未知错误码返回 kProtocolError。
 */
ErrorCode from_ipc_error_code(std::string_view code) noexcept {
    if (code == kBadRequest)
        return ErrorCode::kInvalidArgument;
    if (code == kUnknownCommand)
        return ErrorCode::kUnknownCommand;
    if (code == kNotImplemented)
        return ErrorCode::kNotImplemented;
    if (code == kPathOutsideHome)
        return ErrorCode::kPathMappingFailed;
    if (code == kVMNotRunning)
        return ErrorCode::kVMNotRunning;
    if (code == kSshFailed)
        return ErrorCode::kSshFailed;
    if (code == kProgramFailed)
        return ErrorCode::kProgramFailed;
    if (code == kMountFailed)
        return ErrorCode::kMountFailed;
    if (code == kUnmountFailed)
        return ErrorCode::kUnmountFailed;
    if (code == kShareSetupFailed)
        return ErrorCode::kShareSetupFailed;
    if (code == kPermissionDenied)
        return ErrorCode::kPermissionDenied;
    if (code == kMissingDependency)
        return ErrorCode::kMissingDependency;
    if (code == kQmpError)
        return ErrorCode::kQemuError;
    if (code == kInternal)
        return ErrorCode::kInternal;
    return ErrorCode::kProtocolError;
}

} // namespace lsw::ipc

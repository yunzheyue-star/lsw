#include <lsw/ipc/protocol.hpp>

#include <utility>

namespace lsw::ipc {
namespace {

Result<nlohmann::json> parse_object(std::string_view line) {
  if (line.empty()) {
    return fail(ErrorCode::kProtocolError, "空报文");
  }
  auto value = nlohmann::json::parse(line.begin(), line.end(), nullptr, false);
  if (value.is_discarded()) {
    return fail(ErrorCode::kProtocolError, "JSON 解析失败");
  }
  if (!value.is_object()) {
    return fail(ErrorCode::kProtocolError, "报文体必须是 JSON 对象");
  }
  return value;
}

}  // namespace

nlohmann::json Request::to_json() const {
  return nlohmann::json{{"id", id}, {"cmd", cmd}, {"args", args}};
}

nlohmann::json Response::to_json() const {
  nlohmann::json value{{"id", id}, {"ok", ok}};
  if (ok) {
    value["data"] = data;
  } else {
    value["error"] = nlohmann::json{{"code", error_code},
                                    {"message", error_message}};
  }
  return value;
}

Response Response::success(std::string id, nlohmann::json data) {
  Response response;
  response.id = std::move(id);
  response.ok = true;
  response.data = std::move(data);
  return response;
}

Response Response::failure(std::string id, std::string code,
                           std::string message) {
  Response response;
  response.id = std::move(id);
  response.ok = false;
  response.error_code = std::move(code);
  response.error_message = std::move(message);
  return response;
}

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
  const auto& error = (*value)["error"];
  if (!error.contains("code") || !error["code"].is_string() ||
      !error.contains("message") || !error["message"].is_string()) {
    return fail(ErrorCode::kProtocolError, "error 必须包含 code 与 message");
  }
  response.error_code = error["code"].get<std::string>();
  response.error_message = error["message"].get<std::string>();
  return response;
}

std::string encode_line(const Request& request) {
  return request.to_json().dump() + "\n";
}

std::string encode_line(const Response& response) {
  return response.to_json().dump() + "\n";
}

std::string to_ipc_error_code(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kInvalidArgument:  return std::string(kBadRequest);
    case ErrorCode::kUnknownCommand:   return std::string(kUnknownCommand);
    case ErrorCode::kNotImplemented:   return std::string(kNotImplemented);
    case ErrorCode::kPathMappingFailed: return std::string(kPathOutsideHome);
    case ErrorCode::kSshFailed:        return std::string(kSshFailed);
    case ErrorCode::kIoError:          return std::string(kMountFailed);
    case ErrorCode::kQemuError:        return std::string(kQmpError);
    case ErrorCode::kProtocolError:    return std::string(kBadRequest);
    default:                           return std::string(kInternal);
  }
}

ErrorCode from_ipc_error_code(std::string_view code) noexcept {
  if (code == kBadRequest) return ErrorCode::kInvalidArgument;
  if (code == kUnknownCommand) return ErrorCode::kUnknownCommand;
  if (code == kNotImplemented) return ErrorCode::kNotImplemented;
  if (code == kPathOutsideHome) return ErrorCode::kPathMappingFailed;
  if (code == kSshFailed) return ErrorCode::kSshFailed;
  if (code == kMountFailed) return ErrorCode::kIoError;
  if (code == kQmpError) return ErrorCode::kQemuError;
  if (code == kInternal) return ErrorCode::kInternal;
  return ErrorCode::kProtocolError;
}

}  // namespace lsw::ipc

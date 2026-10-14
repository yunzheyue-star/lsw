#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <lsw/core/error.hpp>

namespace lsw::ipc {

/// IPC 错误码字符串（docs/interfaces/daemon-ipc.md）
inline constexpr std::string_view kBadRequest = "BAD_REQUEST";
inline constexpr std::string_view kUnknownCommand = "UNKNOWN_COMMAND";
inline constexpr std::string_view kNotImplemented = "NOT_IMPLEMENTED";
inline constexpr std::string_view kPathOutsideHome = "PATH_OUTSIDE_HOME";
inline constexpr std::string_view kSshFailed = "SSH_FAILED";
inline constexpr std::string_view kMountFailed = "MOUNT_FAILED";
inline constexpr std::string_view kQmpError = "QMP_ERROR";
inline constexpr std::string_view kInternal = "INTERNAL";

/// 请求：{ "id": "...", "cmd": "...", "args": { ... } }
struct Request {
    std::string id;
    std::string cmd;
    nlohmann::json args = nlohmann::json::object();

    [[nodiscard]] nlohmann::json to_json() const;
};

/// 响应：成功 { "id", "ok": true, "data": {...} }
///       失败 { "id", "ok": false, "error": { "code", "message" } }
struct Response {
    std::string id;
    bool ok{false};
    nlohmann::json data = nlohmann::json::object();
    std::string error_code;
    std::string error_message;

    [[nodiscard]] nlohmann::json to_json() const;

    [[nodiscard]] static Response
    success(std::string id, nlohmann::json data = nlohmann::json::object());
    [[nodiscard]] static Response failure(std::string id, std::string code,
                                          std::string message);
};

/// 解析一行 JSON（允许行尾 \r\n）；结构非法返回 kProtocolError。
[[nodiscard]] Result<Request> parse_request(std::string_view line);
[[nodiscard]] Result<Response> parse_response(std::string_view line);

/// 编码为 JSON Lines（含结尾 '\n'）。
[[nodiscard]] std::string encode_line(const Request &request);
[[nodiscard]] std::string encode_line(const Response &response);

/// ErrorCode ↔ IPC error.code 映射。
[[nodiscard]] std::string to_ipc_error_code(ErrorCode code) noexcept;
[[nodiscard]] ErrorCode from_ipc_error_code(std::string_view code) noexcept;

} // namespace lsw::ipc

/**
 * @file protocol.hpp
 * @brief lswd JSON Lines 请求/响应与错误码映射。
 * @details 文件位置：include/lsw/ipc/protocol.hpp（头文件）。
 *          属于 LSW IPC 与守护进程通信；公共行为以
 * docs/interfaces/daemon-ipc.md 与 agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <lsw/core/error.hpp>

namespace lsw::ipc {

/// @brief IPC 错误码字符串（docs/interfaces/daemon-ipc.md）
inline constexpr std::string_view kBadRequest = "BAD_REQUEST";
inline constexpr std::string_view kUnknownCommand = "UNKNOWN_COMMAND";
inline constexpr std::string_view kNotImplemented = "NOT_IMPLEMENTED";
inline constexpr std::string_view kPathOutsideHome = "PATH_OUTSIDE_HOME";
inline constexpr std::string_view kVMNotRunning = "VM_NOT_RUNNING";
inline constexpr std::string_view kSshFailed = "SSH_FAILED";
inline constexpr std::string_view kProgramFailed = "PROGRAM_FAILED";
inline constexpr std::string_view kMountFailed = "MOUNT_FAILED";
inline constexpr std::string_view kUnmountFailed = "UNMOUNT_FAILED";
inline constexpr std::string_view kShareSetupFailed = "SHARE_SETUP_FAILED";
inline constexpr std::string_view kPermissionDenied = "PERMISSION_DENIED";
inline constexpr std::string_view kMissingDependency = "MISSING_DEPENDENCY";
inline constexpr std::string_view kQmpError = "QMP_ERROR";
inline constexpr std::string_view kInternal = "INTERNAL";

/// @brief 请求：{ "id": "...", "cmd": "...", "args": { ... } }
struct Request {
    std::string id;
    std::string cmd;
    nlohmann::json args = nlohmann::json::object();

    /// @brief 将请求序列化为 JSON 对象。
    /// @return JSON 对象。
    [[nodiscard]] nlohmann::json to_json() const;
};

/// @brief 响应：成功 { "id", "ok": true, "data": {...} }
///       失败 { "id", "ok": false, "error": { "code", "message" } }
struct Response {
    std::string id;
    bool ok{false};
    nlohmann::json data = nlohmann::json::object();
    std::string error_code;
    std::string error_message;

    /// @brief 将响应序列化为 JSON 对象。
    /// @return JSON 对象。
    [[nodiscard]] nlohmann::json to_json() const;

    /// @brief 创建成功响应。
    /// @param id 响应 id。
    /// @param data 响应数据。
    /// @return 成功响应对象。
    [[nodiscard]] static Response
    success(std::string id, nlohmann::json data = nlohmann::json::object());
    /// @brief 创建失败响应。
    /// @param id 响应 id。
    /// @param code 错误码。
    /// @param message 错误信息。
    /// @return 失败响应对象。
    [[nodiscard]] static Response failure(std::string id, std::string code,
                                          std::string message);
};

/// @brief 解析一行 JSON（允许行尾 \r\n）；结构非法返回 kProtocolError。
/// @param line 待解析的一行文本。
/// @return 解析成功时返回 Request，否则返回错误。
[[nodiscard]] Result<Request> parse_request(std::string_view line);
/// @brief 解析一行响应 JSON（允许行尾 \r\n）。
/// @param line 待解析的一行文本。
/// @return 解析成功时返回 Response，否则返回错误。
[[nodiscard]] Result<Response> parse_response(std::string_view line);

/// @brief 编码为 JSON Lines（含结尾 '\n'）。
/// @param request 请求对象。
/// @return 编码后的 JSON Lines 文本（含结尾 '\n'）。
[[nodiscard]] std::string encode_line(const Request &request);
/// @brief 将响应编码为 JSON Lines（含结尾 '\n'）。
/// @param response 响应对象。
/// @return 编码后的 JSON Lines 文本（含结尾 '\n'）。
[[nodiscard]] std::string encode_line(const Response &response);

/// @brief ErrorCode ↔ IPC error.code 映射。
/// @param code 核心错误码。
/// @return 对应的 IPC 错误码字符串。
[[nodiscard]] std::string to_ipc_error_code(ErrorCode code) noexcept;
/// @brief 将 IPC error.code 映射为核心错误码。
/// @param code IPC 错误码字符串。
/// @return 对应的核心错误码。
[[nodiscard]] ErrorCode from_ipc_error_code(std::string_view code) noexcept;

} // namespace lsw::ipc

/**
 * @file test_protocol.cpp
 * @brief test_protocol 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_protocol.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 */

#include <catch2/catch_test_macros.hpp>

#include <lsw/ipc/protocol.hpp>

using lsw::ErrorCode;
using lsw::ipc::parse_request;
using lsw::ipc::parse_response;
using lsw::ipc::Request;
using lsw::ipc::Response;

TEST_CASE("请求 JSON Lines 往返", "[ipc][protocol]") {
    const Request request{"42", "status", nlohmann::json{{"verbose", true}}};
    const std::string line = lsw::ipc::encode_line(request);
    REQUIRE(line.back() == '\n');

    const auto parsed = parse_request(line);
    REQUIRE(parsed.has_value());
    CHECK(parsed->id == "42");
    CHECK(parsed->cmd == "status");
    CHECK(parsed->args["verbose"].get<bool>());
}

TEST_CASE("成功响应往返", "[ipc][protocol]") {
    const auto response =
        Response::success("7", nlohmann::json{{"vm_status", "running"}});
    const std::string line = lsw::ipc::encode_line(response);
    const auto parsed = parse_response(line);
    REQUIRE(parsed.has_value());
    CHECK(parsed->id == "7");
    CHECK(parsed->ok);
    CHECK((*parsed).data["vm_status"] == "running");
}

TEST_CASE("失败响应往返", "[ipc][protocol]") {
    const auto response = Response::failure(
        "8", std::string(lsw::ipc::kUnknownCommand), "未知命令: x");
    const auto parsed = parse_response(lsw::ipc::encode_line(response));
    REQUIRE(parsed.has_value());
    REQUIRE_FALSE(parsed->ok);
    CHECK(parsed->error_code == std::string(lsw::ipc::kUnknownCommand));
    CHECK(parsed->error_message == "未知命令: x");
}

TEST_CASE("非法请求被拒绝", "[ipc][protocol]") {
    CHECK(parse_request("{not json").error().code == ErrorCode::kProtocolError);
    CHECK(parse_request("{}").error().code == ErrorCode::kProtocolError);
    CHECK(parse_request(R"({"id":"1"})").error().code ==
          ErrorCode::kProtocolError);
    CHECK(parse_request(R"({"id":"1","cmd":"x","args":[]})").error().code ==
          ErrorCode::kProtocolError);
    CHECK(parse_request(R"(["not","object"])").error().code ==
          ErrorCode::kProtocolError);
}

TEST_CASE("非法响应被拒绝", "[ipc][protocol]") {
    CHECK(parse_response(R"({"id":"1"})").error().code ==
          ErrorCode::kProtocolError);
    CHECK(parse_response(R"({"id":"1","ok":true,"data":[]})").error().code ==
          ErrorCode::kProtocolError);
    CHECK(parse_response(R"({"id":"1","ok":false})").error().code ==
          ErrorCode::kProtocolError);
    CHECK(parse_response(R"({"id":"1","ok":false,"error":{"code":"X"}})")
              .error()
              .code == ErrorCode::kProtocolError);
}

TEST_CASE("IPC 错误码映射往返", "[ipc][protocol]") {
    const std::pair<ErrorCode, const char *> cases[] = {
        {ErrorCode::kInvalidArgument, "BAD_REQUEST"},
        {ErrorCode::kUnknownCommand, "UNKNOWN_COMMAND"},
        {ErrorCode::kNotImplemented, "NOT_IMPLEMENTED"},
        {ErrorCode::kPathMappingFailed, "PATH_OUTSIDE_HOME"},
        {ErrorCode::kSshFailed, "SSH_FAILED"},
        {ErrorCode::kIoError, "MOUNT_FAILED"},
        {ErrorCode::kQemuError, "QMP_ERROR"},
        {ErrorCode::kInternal, "INTERNAL"},
    };
    for (const auto &[code, text] : cases) {
        CHECK(lsw::ipc::to_ipc_error_code(code) == text);
        CHECK(lsw::ipc::from_ipc_error_code(text) == code);
    }
    CHECK(lsw::ipc::from_ipc_error_code("NO_SUCH_CODE") ==
          ErrorCode::kProtocolError);
}

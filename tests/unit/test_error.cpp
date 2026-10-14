/**
 * @file test_error.cpp
 * @brief test_error 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_error.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <catch2/catch_test_macros.hpp>

#include <lsw/core/error.hpp>

namespace {

/** @brief 返回成功的整型结果。
 * @return 值为 42 的成功 Result。 */
lsw::Result<int> succeed() { return 42; }

/** @brief 构造路径映射失败的结果。
 * @return 携带 kPathMappingFailed 错误码与消息的失败 Result。 */
lsw::Result<int> fail_result() {
    return lsw::fail(lsw::ErrorCode::kPathMappingFailed, "outside");
}

} // namespace

TEST_CASE("错误码映射到 CLI 退出码", "[core][error]") {
    using lsw::ErrorCode;
    CHECK(lsw::to_exit_code(ErrorCode::kOk) == 0);
    CHECK(lsw::to_exit_code(ErrorCode::kInvalidArgument) == 2);
    CHECK(lsw::to_exit_code(ErrorCode::kMissingDependency) == 3);
    CHECK(lsw::to_exit_code(ErrorCode::kPathMappingFailed) == 5);
    CHECK(lsw::to_exit_code(ErrorCode::kSshFailed) == 6);
    CHECK(lsw::to_exit_code(ErrorCode::kProgramFailed) == 7);
    CHECK(lsw::to_exit_code(ErrorCode::kQemuError) == 8);
    CHECK(lsw::to_exit_code(ErrorCode::kConfigInvalid) == 2);
    CHECK(lsw::to_exit_code(ErrorCode::kInternal) == 1);
}

TEST_CASE("Result 成功路径", "[core][error]") {
    const auto result = succeed();
    REQUIRE(result.has_value());
    CHECK(*result == 42);
}

TEST_CASE("Result 失败路径保留错误码与消息", "[core][error]") {
    const auto result = fail_result();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == lsw::ErrorCode::kPathMappingFailed);
    CHECK(result.error().message == "outside");
}

TEST_CASE("Status 成功与失败", "[core][error]") {
    lsw::Status ok = {};
    CHECK(ok.has_value());

    lsw::Status bad = lsw::fail(lsw::ErrorCode::kIoError, "cannot write");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == lsw::ErrorCode::kIoError);
}

TEST_CASE("错误码字符串可用于日志", "[core][error]") {
    CHECK(lsw::to_string(lsw::ErrorCode::kConfigParseError) ==
          "ConfigParseError");
    CHECK_FALSE(lsw::to_string(static_cast<lsw::ErrorCode>(999)).empty());
}

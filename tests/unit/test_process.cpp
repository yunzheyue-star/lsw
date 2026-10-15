/**
 * @file test_process.cpp
 * @brief test_process 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_process.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

#include <lsw/core/process.hpp>

#include "test_support.hpp"

using lsw::ErrorCode;
using lsw::run_process;
using lsw::RunOptions;

namespace {

/** @brief 去除字符串末尾的换行符。
 * @param text 待处理的字符串。
 * @return 去除末尾换行与回车字符后的字符串。 */
std::string trim_newline(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

} // namespace

TEST_CASE("捕获 stdout 与退出码 0", "[core][process]") {
    const auto result =
        run_process({lsw::test::binary("echo"), "hello", "lsw"});
    REQUIRE(result.has_value());
    CHECK(result->exit_code == 0);
    CHECK(result->stdout_data == "hello lsw\n");
    CHECK(result->stderr_data.empty());
}

TEST_CASE("参数不经过 shell（无注入）", "[core][process]") {
    const auto result =
        run_process({lsw::test::binary("echo"), "a; echo pwned"});
    REQUIRE(result.has_value());
    CHECK(result->stdout_data == "a; echo pwned\n");
}

TEST_CASE("捕获 stderr 与非零退出码", "[core][process]") {
    const auto result =
        run_process({lsw::test::binary("sh"), "-c", "echo boom 1>&2; exit 4"});
    REQUIRE(result.has_value());
    CHECK(result->exit_code == 4);
    CHECK(result->stderr_data == "boom\n");
}

TEST_CASE("支持指定工作目录", "[core][process]") {
    const lsw::test::TempDir temp("process-cwd");
    RunOptions options;
    options.cwd = temp.path();

    const auto result = run_process({lsw::test::binary("pwd")}, options);
    REQUIRE(result.has_value());
    const auto printed =
        std::filesystem::weakly_canonical(trim_newline(result->stdout_data));
    const auto expected = std::filesystem::weakly_canonical(temp.path());
    CHECK(printed == expected);
}

TEST_CASE("超时会被终止并返回错误", "[core][process]") {
    RunOptions options;
    options.timeout = std::chrono::milliseconds(200);

    const auto start = std::chrono::steady_clock::now();
    const auto result = run_process({lsw::test::binary("sleep"), "5"}, options);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kProcessTimeout);
    CHECK(elapsed < std::chrono::seconds(3));
}

TEST_CASE("无法执行时返回 ProcessError", "[core][process]") {
    const auto result = run_process({"/no/such/lsw-binary"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kProcessError);
    CHECK(result.error().message.find("无法执行") != std::string::npos);
}

TEST_CASE("非 UTF-8 进程输出被清洗为合法 UTF-8", "[core][process]") {
    const auto result = run_process({lsw::test::binary("printf"), "\\262\\n"});
    REQUIRE(result.has_value());
    CHECK(result->exit_code == 0);
    CHECK(result->stdout_data == "\xEF\xBF\xBD\n");
}

TEST_CASE("空 argv 返回参数错误", "[core][process]") {
    const auto result = run_process({});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kInvalidArgument);
}

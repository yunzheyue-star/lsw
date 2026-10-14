#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <lsw/core/process.hpp>

#include "test_support.hpp"

using lsw::ErrorCode;
using lsw::core::ChildProcess;
using lsw::core::SpawnOptions;

TEST_CASE("ChildProcess 启动、探测与 kill", "[core][process]") {
    auto process = ChildProcess::spawn({lsw::test::binary("sleep"), "5"});
    REQUIRE(process.has_value());
    CHECK(process->valid());
    CHECK(process->pid() > 0);
    CHECK(process->running());

    REQUIRE(process->kill().has_value());
    const auto code = process->wait();
    REQUIRE(code.has_value());
    CHECK(*code == 128 + SIGKILL);
    CHECK_FALSE(process->running());
}

TEST_CASE("ChildProcess 正常退出码", "[core][process]") {
    auto process = ChildProcess::spawn({lsw::test::binary("true")});
    REQUIRE(process.has_value());
    const auto code = process->wait();
    REQUIRE(code.has_value());
    CHECK(*code == 0);
}

TEST_CASE("ChildProcess 输出重定向到文件", "[core][process]") {
    const lsw::test::TempDir temp("child-output");
    const auto output = temp.path() / "out.txt";

    auto process = ChildProcess::spawn(
        {lsw::test::binary("echo"), "hello-child"},
        SpawnOptions{.stdout_file = output, .stderr_file = output});
    REQUIRE(process.has_value());
    REQUIRE(process->wait().has_value());

    std::ifstream input(output);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    CHECK(content == "hello-child\n");
}

TEST_CASE("ChildProcess wait_for 超时", "[core][process]") {
    auto process = ChildProcess::spawn({lsw::test::binary("sleep"), "5"});
    REQUIRE(process.has_value());
    const auto waited = process->wait_for(std::chrono::milliseconds(100));
    REQUIRE_FALSE(waited.has_value());
    CHECK(waited.error().code == ErrorCode::kProcessTimeout);
    CHECK(process->running());
    REQUIRE(process->kill().has_value());
    REQUIRE(process->wait().has_value());
}

TEST_CASE("ChildProcess 执行不存在的程序", "[core][process]") {
    auto process = ChildProcess::spawn({"/no/such/lsw-child-binary"});
    REQUIRE_FALSE(process.has_value());
    CHECK(process.error().code == ErrorCode::kProcessError);
}

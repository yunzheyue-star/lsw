/**
 * @file test_shell.cpp
 * @brief test_shell 实现：P7 shell 路径映射与 ssh 参数构造测试。
 * @details 文件位置：tests/unit/test_shell.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/shell/powershell.hpp>

#include "test_support.hpp"

using lsw::Config;
using lsw::ErrorCode;
using lsw::shell::build_powershell_script;
using lsw::shell::build_ssh_args;

namespace {

/// @brief 设置测试用配置：host SMB 地址与共享名。
/// @param config 待修改的配置。
void configure_network(Config &config) {
    config.network.guest_smb_host = "10.0.2.2";
    config.network.guest_smb_share = "qemu";
    config.network.ssh_host_port = 2222;
    config.user.windows_user = "lsw";
}

} // namespace

TEST_CASE("shell：$HOME 使用 PowerShell 默认路径", "[shell]") {
    lsw::test::TempDir temp("shell-home");
    const auto home = temp.path() / "home";
    std::filesystem::create_directories(home);
    Config config = lsw::default_config();
    configure_network(config);

    auto script = build_powershell_script(config, home, home);
    REQUIRE(script.has_value());
    CHECK(script->find("net use $drive $unc") != std::string::npos);
    CHECK(script->find("$target=''") != std::string::npos);
    CHECK(script->find("Set-Location -LiteralPath $target") !=
          std::string::npos);
    CHECK(script->find("RawUI.BufferSize") != std::string::npos);
    CHECK(script->find("global:Clear-Host") != std::string::npos);
}

TEST_CASE("shell：子目录映射为 Z: 并转义单引号", "[shell]") {
    lsw::test::TempDir temp("shell-subdir");
    const auto home = temp.path() / "home";
    const auto cwd = home / "Project" / "demo dir" / "中文'quote";
    std::filesystem::create_directories(cwd);
    Config config = lsw::default_config();
    configure_network(config);

    auto script = build_powershell_script(config, cwd, home);
    REQUIRE(script.has_value());
    CHECK(script->find("$target='Z:\\Project\\demo dir\\中文''quote'") !=
          std::string::npos);
}

TEST_CASE("shell：$HOME 外返回路径映射错误", "[shell]") {
    lsw::test::TempDir temp("shell-outside");
    const auto home = temp.path() / "home";
    std::filesystem::create_directories(home);
    Config config = lsw::default_config();
    configure_network(config);

    auto script = build_powershell_script(config, "/tmp", home);
    REQUIRE_FALSE(script.has_value());
    CHECK(script.error().code == ErrorCode::kPathMappingFailed);
}

TEST_CASE("shell：ssh argv 使用 -t 与 EncodedCommand", "[shell]") {
    lsw::test::TempDir temp("shell-argv");
    const auto home = temp.path() / "home";
    const auto cwd = home / "Project" / "demo";
    std::filesystem::create_directories(cwd);
    Config config = lsw::default_config();
    configure_network(config);

    auto args = build_ssh_args(config, cwd, home, "ssh");
    REQUIRE(args.has_value());
    REQUIRE(args->size() >= 13);
    CHECK((*args)[0] == "ssh");
    CHECK((*args)[1] == "-t");
    CHECK((*args)[2] == "-p");
    CHECK((*args)[3] == "2222");
    CHECK((*args)[5] == "StrictHostKeyChecking=accept-new");
    CHECK((*args)[6] == "lsw@127.0.0.1");
    CHECK((*args)[7] == "--");
    CHECK((*args)[8] == "powershell.exe");
    CHECK((*args)[9] == "-NoLogo");
    CHECK((*args)[10] == "-NoExit");
    CHECK((*args)[11] == "-EncodedCommand");
    CHECK_FALSE((*args)[12].empty());

    auto script = build_powershell_script(config, cwd, home);
    REQUIRE(script.has_value());
}

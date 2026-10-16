/**
 * @file test_exec.cpp
 * @brief test_exec 实现：P8 Windows 程序执行辅助测试。
 * @details 文件位置：tests/unit/test_exec.cpp（单元测试）。
 *          覆盖路径解析/越界复制、批处理脚本构造、PE GUI 检测与 ssh argv。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/exec/windows_runner.hpp>

#include "test_support.hpp"

using lsw::Config;
using lsw::ErrorCode;
using lsw::exec::build_script;
using lsw::exec::build_ssh_args;
using lsw::exec::ExecOptions;
using lsw::exec::is_gui_executable;
using lsw::exec::resolve_target;

namespace {

/// @brief 写出一个最小 PE 文件，指定子系统。
/// @param path 目标路径。
/// @param subsystem PE Subsystem 值（2=GUI，3=Console）。
void write_pe(const std::filesystem::path &path, std::uint16_t subsystem) {
    std::vector<unsigned char> data(0x40 + 4 + 20 + 0x60, 0);
    data[0] = 'M';
    data[1] = 'Z';
    const std::uint32_t pe_offset = 0x40;
    data[0x3C] = static_cast<unsigned char>(pe_offset & 0xFFU);
    data[0x3D] = static_cast<unsigned char>((pe_offset >> 8U) & 0xFFU);
    data[0x3E] = static_cast<unsigned char>((pe_offset >> 16U) & 0xFFU);
    data[0x3F] = static_cast<unsigned char>((pe_offset >> 24U) & 0xFFU);
    data[pe_offset] = 'P';
    data[pe_offset + 1] = 'E';
    const std::size_t optional = pe_offset + 4 + 20;
    data[optional] = 0x0B; // PE32 magic（低字节）
    data[optional + 1] = 0x01;
    data[optional + 0x5C] = static_cast<unsigned char>(subsystem & 0xFFU);
    data[optional + 0x5D] =
        static_cast<unsigned char>((subsystem >> 8U) & 0xFFU);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
}

/// @brief 构造测试配置。
/// @return 已设置 host SMB 的配置。
Config make_config() {
    Config config = lsw::default_config();
    config.network.guest_smb_host = "10.0.2.2";
    config.network.guest_smb_share = "qemu";
    config.network.ssh_host_port = 2222;
    config.user.windows_user = "lsw";
    return config;
}

} // namespace

TEST_CASE("exec：$HOME 下程序直接映射 Z:", "[exec]") {
    lsw::test::TempDir temp("exec-home");
    const auto home = temp.path() / "home";
    std::filesystem::create_directories(home);
    std::ofstream(home / "test.bat") << "@echo off\r\n";

    auto target = resolve_target("test.bat", home, home);
    REQUIRE(target.has_value());
    CHECK_FALSE(target->copied);
    CHECK(target->windows_path == "Z:\\test.bat");
}

TEST_CASE("exec：$HOME 外程序复制到临时目录", "[exec]") {
    lsw::test::TempDir temp("exec-copy");
    const auto home = temp.path() / "home";
    const auto outside = temp.path() / "outside";
    std::filesystem::create_directories(home);
    std::filesystem::create_directories(outside);
    std::ofstream(outside / "run.bat") << "@echo off\r\n";

    auto target = resolve_target(outside / "run.bat", home, home);
    REQUIRE(target.has_value());
    CHECK(target->copied);
    CHECK(target->host_path.filename() == "run.bat");
    CHECK(target->host_path.string().find("/.lsw/tmp/") != std::string::npos);
    CHECK(target->windows_path.find("Z:\\.lsw\\tmp\\") == 0);
}

TEST_CASE("exec：程序不存在返回路径错误", "[exec]") {
    lsw::test::TempDir temp("exec-missing");
    const auto home = temp.path() / "home";
    std::filesystem::create_directories(home);
    auto target = resolve_target("nope.bat", home, home);
    REQUIRE_FALSE(target.has_value());
    CHECK(target.error().code == ErrorCode::kPathMappingFailed);
}

TEST_CASE("exec：PE GUI 子系统被识别", "[exec]") {
    lsw::test::TempDir temp("exec-pe");
    const auto gui = temp.path() / "gui.exe";
    const auto console = temp.path() / "console.exe";
    write_pe(gui, 2);
    write_pe(console, 3);
    CHECK(is_gui_executable(gui));
    CHECK_FALSE(is_gui_executable(console));
}

TEST_CASE("exec：批处理脚本构造与参数转义", "[exec]") {
    const Config config = make_config();
    auto script = build_script(config, "Z:\\dir\\run.bat", {"a b", "x'y"});
    REQUIRE(script.has_value());
    CHECK(script->find("net use $drive $unc") != std::string::npos);
    CHECK(script->find("$source='Z:\\dir\\run.bat'") != std::string::npos);
    CHECK(script->find("Copy-Item -LiteralPath $source") != std::string::npos);
    CHECK(script->find("Unblock-File") != std::string::npos);
    CHECK(script->find("$argv=@('a b', 'x''y')") != std::string::npos);
    CHECK(script->find("& $program @argv") != std::string::npos);
}

TEST_CASE("exec：ssh argv 非交互且使用 EncodedCommand", "[exec]") {
    const Config config = make_config();
    auto args = build_ssh_args(config, "Z:\\run.bat", {"a"}, "ssh");
    REQUIRE(args.has_value());
    CHECK((*args)[0] == "ssh");
    CHECK((*args)[1] == "-p");
    CHECK((*args)[2] == "2222");
    bool has_batch_mode = false;
    bool has_no_profile = false;
    bool has_encoded = false;
    for (const auto &arg : *args) {
        has_batch_mode = has_batch_mode || arg == "BatchMode=yes";
        has_no_profile = has_no_profile || arg == "-NoProfile";
        has_encoded = has_encoded || arg == "-EncodedCommand";
    }
    CHECK(has_batch_mode);
    CHECK(has_no_profile);
    CHECK(has_encoded);
    CHECK_FALSE((args->back().empty()));
}

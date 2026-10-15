/**
 * @file test_z_mapping.cpp
 * @brief Windows `Z:` 映射单元测试（假 ssh）。
 * @details 文件位置：tests/unit/test_z_mapping.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_support.hpp"

#include <lsw/core/config.hpp>
#include <lsw/fs/z_mapping.hpp>

using lsw::Config;
using lsw::ErrorCode;
using lsw::winfs::ZDriveMapper;
using lsw::winfs::ZMappingOptions;

namespace {

/// @brief 在测试进程内临时设置环境变量，析构时恢复。
class ScopedEnv {
  public:
    /// @brief 设置环境变量并记录旧值。
    /// @param name 变量名。
    /// @param value 变量值。
    ScopedEnv(const char *name, const char *value) : name_(name) {
        const char *old = std::getenv(name);
        if (old != nullptr) {
            old_ = old;
        }
        ::setenv(name, value, 1);
    }

    /// @brief 析构时恢复旧环境变量。
    ~ScopedEnv() {
        if (old_.has_value()) {
            ::setenv(name_.c_str(), old_->c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

    /// @brief 禁止拷贝构造。
    /// @param other 待拷贝的源对象。
    ScopedEnv(const ScopedEnv &other) = delete;
    /// @brief 禁止拷贝赋值。
    /// @param other 待赋值的源对象。
    /// @return 该重载已删除。
    ScopedEnv &operator=(const ScopedEnv &other) = delete;

  private:
    std::string name_;
    std::optional<std::string> old_;
};

/// @brief 创建可执行的假 ssh 脚本。
/// @param dir 测试临时目录。
/// @return 假 ssh 路径。
std::filesystem::path make_fake_ssh(const std::filesystem::path &dir) {
    const auto path = dir / "ssh";
    std::ofstream out(path);
    out << R"(#!/usr/bin/env bash
args=("$@")
last="${args[${#args[@]}-1]}"
script="$last"
if [ "${#args[@]}" -ge 2 ] && [ "${args[${#args[@]}-2]}" = "-EncodedCommand" ]; then
    script=$(python3 -c 'import base64,sys; print(base64.b64decode(sys.argv[1]).decode("utf-16le"))' "$last")
fi
echo "$*" >> "$LSW_TEST_SSH_LOG"
echo "$script" >> "$LSW_TEST_SSH_LOG"
if [ -n "${LSW_TEST_SSH_EXIT:-}" ]; then exit "$LSW_TEST_SSH_EXIT"; fi
if [[ "$script" == *"net use"* ]]; then exit 0; fi
if [[ "$script" == *"exit 7"* ]]; then exit 7; fi
exit 0
)";
    out.close();
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);
    return path;
}

/// @brief 构造测试用配置。
/// @return 默认配置（guest SMB 地址与用户名）。
Config make_config() {
    Config config;
    config.network.ssh_host_port = 2222;
    config.network.guest_smb_host = "10.0.2.4";
    config.network.guest_smb_share = "qemu";
    config.user.windows_user = "lsw";
    config.filesystem.windows_drive_z = "Z:";
    return config;
}

} // namespace

TEST_CASE("Z: 未映射时 is_mapped 返回 false", "[fs][z]") {
    lsw::test::TempDir temp("z-check");
    auto config = make_config();
    const auto ssh = make_fake_ssh(temp.path());
    const auto log = temp.path() / "ssh.log";
    ScopedEnv log_env("LSW_TEST_SSH_LOG", log.c_str());

    ZDriveMapper mapper(config, ZMappingOptions{.ssh_binary = ssh.string()});
    const auto mapped = mapper.is_mapped();
    REQUIRE(mapped.has_value());
    CHECK_FALSE(*mapped);
}

TEST_CASE("Z: 未映射时 ensure_mapped 执行 net use", "[fs][z]") {
    lsw::test::TempDir temp("z-ensure");
    auto config = make_config();
    const auto ssh = make_fake_ssh(temp.path());
    const auto log = temp.path() / "ssh.log";
    ScopedEnv log_env("LSW_TEST_SSH_LOG", log.c_str());

    ZDriveMapper mapper(config, ZMappingOptions{.ssh_binary = ssh.string()});
    REQUIRE(mapper.ensure_mapped().has_value());
    std::ifstream input(log);
    const std::string calls((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    CHECK(calls.find("net use") != std::string::npos);
    CHECK(calls.find("10.0.2.4") != std::string::npos);
    CHECK(calls.find("StrictHostKeyChecking=accept-new") != std::string::npos);
}

TEST_CASE("net use 失败返回 ShareSetupFailed", "[fs][z]") {
    lsw::test::TempDir temp("z-fail");
    auto config = make_config();
    const auto ssh = make_fake_ssh(temp.path());
    const auto log = temp.path() / "ssh.log";
    ScopedEnv log_env("LSW_TEST_SSH_LOG", log.c_str());
    ScopedEnv exit_env("LSW_TEST_SSH_EXIT", "7");

    ZDriveMapper mapper(config, ZMappingOptions{.ssh_binary = ssh.string()});
    const auto result = mapper.ensure_mapped();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kShareSetupFailed);
}

TEST_CASE("危险配置字符被拒绝", "[fs][z]") {
    lsw::test::TempDir temp("z-invalid");
    auto config = make_config();
    config.user.windows_user = "bad'user";
    const auto ssh = make_fake_ssh(temp.path());
    ZDriveMapper mapper(config, ZMappingOptions{.ssh_binary = ssh.string()});
    const auto result = mapper.ensure_mapped();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kConfigInvalid);
}

TEST_CASE("Z: 映射可携带显式凭据（密码走 stdin）", "[fs][z]") {
    lsw::test::TempDir temp("z-credential");
    auto config = make_config();
    const auto ssh = make_fake_ssh(temp.path());
    const auto log = temp.path() / "ssh.log";
    ScopedEnv log_env("LSW_TEST_SSH_LOG", log.c_str());

    ZDriveMapper mapper(config, ZMappingOptions{.ssh_binary = ssh.string()});
    REQUIRE(mapper.ensure_mapped(lsw::winfs::WindowsCredential{"lsw", "p@ss"})
                .has_value());
    std::ifstream input(log);
    const std::string calls((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    CHECK(calls.find("/user:'lsw' $pw") != std::string::npos);
    CHECK(calls.find("[Console]::In.ReadLine") != std::string::npos);
}

TEST_CASE("ssh 缺失返回 MissingDependency", "[fs][z]") {
    auto config = make_config();
    ZDriveMapper mapper(
        config, ZMappingOptions{.ssh_binary = "/nonexistent/ssh-lsw-test"});
    const auto result = mapper.ensure_mapped();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kMissingDependency);
}

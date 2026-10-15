/**
 * @file test_credentials.cpp
 * @brief credentials 模块单元测试。
 * @details 文件位置：tests/unit/test_credentials.cpp（单元测试）。
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

#include <lsw/fs/credentials.hpp>

using lsw::ErrorCode;
using lsw::winfs::credential_from_environment;
using lsw::winfs::CredentialOptions;
using lsw::winfs::CredentialStore;
using lsw::winfs::WindowsCredential;

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

/// @brief 写入一个可执行的假 secret-tool 脚本。
/// @param dir 测试临时目录。
/// @param body shell 脚本正文。
/// @return 脚本路径。
std::filesystem::path make_fake_secret_tool(const std::filesystem::path &dir,
                                            const std::string &body) {
    const auto path = dir / "secret-tool";
    std::ofstream out(path);
    out << "#!/usr/bin/env bash\n" << body << "\n";
    out.close();
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);
    return path;
}

} // namespace

TEST_CASE("CredentialStore 能读取 keyring 中的凭据", "[fs][credentials]") {
    lsw::test::TempDir temp("credentials-lookup");
    const auto tool = make_fake_secret_tool(
        temp.path(),
        R"(if [ "$1" = "lookup" ]; then printf 's3cret\n'; exit 0; fi
exit 1)");
    CredentialStore store(
        CredentialOptions{.service = "lsw-test", .secret_tool_binary = tool});

    const auto credential = store.lookup("lsw");
    REQUIRE(credential.has_value());
    REQUIRE(credential->has_value());
    CHECK(credential->value().username == "lsw");
    CHECK(credential->value().password == "s3cret");
}

TEST_CASE("CredentialStore 对不存在凭据返回 nullopt", "[fs][credentials]") {
    lsw::test::TempDir temp("credentials-missing");
    const auto tool = make_fake_secret_tool(temp.path(), R"(exit 1)");
    CredentialStore store(
        CredentialOptions{.service = "lsw-test", .secret_tool_binary = tool});

    const auto credential = store.lookup("nobody");
    REQUIRE(credential.has_value());
    CHECK_FALSE(credential->has_value());
}

TEST_CASE("CredentialStore 通过 stdin 保存凭据", "[fs][credentials]") {
    lsw::test::TempDir temp("credentials-store");
    const auto captured = temp.path() / "captured-password";
    const auto tool = make_fake_secret_tool(
        temp.path(), R"(if [ "$1" = "store" ]; then cat > "$CAPTURE"; exit 0; fi
exit 1)");
    ScopedEnv capture("CAPTURE", captured.c_str());
    CredentialStore store(
        CredentialOptions{.service = "lsw-test", .secret_tool_binary = tool});

    REQUIRE(store.store(WindowsCredential{"lsw", "p@ss"}).has_value());
    std::ifstream input(captured);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    CHECK(content == "p@ss\n");
}

TEST_CASE("secret-tool 缺失时返回 MissingDependency", "[fs][credentials]") {
    CredentialStore store(
        CredentialOptions{.service = "lsw-test",
                          .secret_tool_binary = "/nonexistent/secret-tool"});
    const auto credential = store.lookup("lsw");
    REQUIRE_FALSE(credential.has_value());
    CHECK(credential.error().code == ErrorCode::kMissingDependency);
}

TEST_CASE("环境变量可提供一次性凭据", "[fs][credentials]") {
    ScopedEnv user("LSW_WINDOWS_USER", "alice");
    ScopedEnv password("LSW_WINDOWS_PASSWORD", "env-secret");
    const auto credential = credential_from_environment("lsw");
    REQUIRE(credential.has_value());
    CHECK(credential->username == "alice");
    CHECK(credential->password == "env-secret");
}

/**
 * @file test_mount.cpp
 * @brief CIFS 挂载管理单元测试（假 helper）。
 * @details 文件位置：tests/unit/test_mount.cpp（单元测试）。
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
#include <lsw/fs/credentials.hpp>
#include <lsw/fs/mount.hpp>

using lsw::Config;
using lsw::ErrorCode;
using lsw::winfs::CifsMountManager;
using lsw::winfs::MountOptions;
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

/// @brief 创建可执行的假 helper 脚本。
/// @param dir 测试临时目录。
/// @return 假 helper 路径。
std::filesystem::path make_fake_helper(const std::filesystem::path &dir) {
    const auto path = dir / "lsw-mount-helper";
    std::ofstream out(path);
    out << R"(#!/usr/bin/env bash
echo "$*" >> "$LSW_TEST_HELPER_LOG"
if [ "$1" = "mount" ]; then
  if [ -n "${LSW_TEST_PASSWORD_FILE:-}" ]; then cat > "$LSW_TEST_PASSWORD_FILE"; fi
  if [ "${LSW_TEST_FAIL_FIRST:-0}" = "1" ]; then
    count=$(grep -c '^mount ' "$LSW_TEST_HELPER_LOG" || true)
    if [ "$count" -le 1 ]; then exit 1; fi
  fi
  if [ -n "${LSW_TEST_MISSING_DRIVE:-}" ] && [ "$2" = "$LSW_TEST_MISSING_DRIVE" ]; then
    echo "mount error(2): No such file or directory" >&2
    exit 32
  fi
  exit 0
fi
if [ "$1" = "unmount" ]; then exit 0; fi
exit 2
)";
    out.close();
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);
    return path;
}

/// @brief 构造测试用配置与挂载选项。
/// @param root 临时目录。
/// @return pair<配置, 挂载选项>。
std::pair<Config, MountOptions>
make_fixture(const std::filesystem::path &root) {
    Config config;
    config.filesystem.mount_root = root / "mnt";
    config.network.smb_host_port = 1445;
    config.user.windows_user = "lsw";

    MountOptions options;
    options.mount_root = config.filesystem.mount_root;
    options.helper_path = make_fake_helper(root);
    options.privilege = "none";
    options.mounts_file = root / "mounts";
    options.drives = {'C', 'D'};
    options.timeout = std::chrono::seconds(3);
    return {config, options};
}

/// @brief 向假挂载表追加一条 CIFS 记录。
/// @param mounts_file 挂载表路径。
/// @param letter 盘符。
/// @param mount_root 挂载根目录。
void append_mount(const std::filesystem::path &mounts_file, char letter,
                  const std::filesystem::path &mount_root) {
    std::ofstream out(mounts_file, std::ios::app);
    out << "//127.0.0.1/" << letter << "$ "
        << (mount_root / std::string(1, letter)).string()
        << " cifs rw,relatime 0 0\n";
}

} // namespace

TEST_CASE("ensure_mounts 通过 helper 挂载 C 与 D", "[fs][mount]") {
    lsw::test::TempDir temp("mount-ensure");
    auto [config, options] = make_fixture(temp.path());
    std::ofstream(options.mounts_file).close();
    const auto log = temp.path() / "helper.log";
    const auto password_file = temp.path() / "password.txt";
    ScopedEnv log_env("LSW_TEST_HELPER_LOG", log.c_str());
    ScopedEnv password_env("LSW_TEST_PASSWORD_FILE", password_file.c_str());

    CifsMountManager manager(config, options);
    const auto mounted =
        manager.ensure_mounts(WindowsCredential{"lsw", "p@ss"});
    REQUIRE(mounted.has_value());
    REQUIRE(mounted->size() == 2);
    CHECK((*mounted)[0] == (config.filesystem.mount_root / "C").string());
    CHECK((*mounted)[1] == (config.filesystem.mount_root / "D").string());

    std::ifstream input(password_file);
    std::string password((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    CHECK(password == "p@ss\n");
    std::ifstream log_input(log);
    std::string calls((std::istreambuf_iterator<char>(log_input)),
                      std::istreambuf_iterator<char>());
    CHECK(calls.find("mount C //127.0.0.1/C$") != std::string::npos);
    CHECK(calls.find("port") == std::string::npos); // helper 协议不含密码字段
}

TEST_CASE("ensure_mounts 幂等：已挂载时不调用 helper", "[fs][mount]") {
    lsw::test::TempDir temp("mount-idempotent");
    auto [config, options] = make_fixture(temp.path());
    append_mount(options.mounts_file, 'C', config.filesystem.mount_root);
    append_mount(options.mounts_file, 'D', config.filesystem.mount_root);
    const auto log = temp.path() / "helper.log";
    ScopedEnv log_env("LSW_TEST_HELPER_LOG", log.c_str());

    CifsMountManager manager(config, options);
    const auto mounted =
        manager.ensure_mounts(WindowsCredential{"lsw", "p@ss"});
    REQUIRE(mounted.has_value());
    CHECK(mounted->size() == 2);
    CHECK_FALSE(std::filesystem::exists(log));
}

TEST_CASE("unmount_all 卸载挂载根下全部 CIFS 挂载点", "[fs][mount]") {
    lsw::test::TempDir temp("mount-unmount");
    auto [config, options] = make_fixture(temp.path());
    append_mount(options.mounts_file, 'C', config.filesystem.mount_root);
    append_mount(options.mounts_file, 'D', config.filesystem.mount_root);
    const auto log = temp.path() / "helper.log";
    ScopedEnv log_env("LSW_TEST_HELPER_LOG", log.c_str());

    CifsMountManager manager(config, options);
    REQUIRE(manager.unmount_all().has_value());
    std::ifstream input(log);
    std::string calls((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
    CHECK(calls.find("unmount") != std::string::npos);
}

TEST_CASE("挂载失败会清理残留并自动重挂一次", "[fs][mount]") {
    lsw::test::TempDir temp("mount-recover");
    auto [config, options] = make_fixture(temp.path());
    std::ofstream(options.mounts_file).close();
    const auto log = temp.path() / "helper.log";
    ScopedEnv log_env("LSW_TEST_HELPER_LOG", log.c_str());
    ScopedEnv fail_env("LSW_TEST_FAIL_FIRST", "1");

    CifsMountManager manager(config, options);
    const auto mounted =
        manager.ensure_mounts(WindowsCredential{"lsw", "p@ss"});
    REQUIRE(mounted.has_value());
    CHECK(mounted->size() == 2);
}

TEST_CASE("D: 不存在时只挂载 C 并跳过 D", "[fs][mount]") {
    lsw::test::TempDir temp("mount-missing-d");
    auto [config, options] = make_fixture(temp.path());
    std::ofstream(options.mounts_file).close();
    const auto log = temp.path() / "helper.log";
    ScopedEnv log_env("LSW_TEST_HELPER_LOG", log.c_str());
    ScopedEnv missing_env("LSW_TEST_MISSING_DRIVE", "D");

    CifsMountManager manager(config, options);
    const auto mounted =
        manager.ensure_mounts(WindowsCredential{"lsw", "p@ss"});
    REQUIRE(mounted.has_value());
    REQUIRE(mounted->size() == 1);
    CHECK((*mounted)[0] == (config.filesystem.mount_root / "C").string());
}

TEST_CASE("helper 缺失时返回 MissingDependency", "[fs][mount]") {
    lsw::test::TempDir temp("mount-helper-missing");
    auto [config, options] = make_fixture(temp.path());
    options.helper_path = temp.path() / "does-not-exist";
    std::ofstream(options.mounts_file).close();

    CifsMountManager manager(config, options);
    const auto mounted =
        manager.ensure_mounts(WindowsCredential{"lsw", "p@ss"});
    REQUIRE_FALSE(mounted.has_value());
    CHECK(mounted.error().code == ErrorCode::kMissingDependency);
}

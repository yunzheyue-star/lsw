/**
 * @file test_daemon_fs.cpp
 * @brief DaemonService 文件系统集成测试（假 QEMU / ssh / helper）。
 * @details 文件位置：tests/unit/test_daemon_fs.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <lsw/daemon/service.hpp>

#include "test_support.hpp"

#ifndef LSW_FAKE_QEMU_PATH
#error "测试需要 CMake 定义 LSW_FAKE_QEMU_PATH"
#endif

using lsw::Config;
using lsw::PathEnvironment;
using lsw::XdgPaths;
using lsw::daemon::DaemonService;
using lsw::daemon::DaemonServiceOptions;

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

/// @brief 写入可执行文件并返回路径。
/// @param path 目标路径。
/// @param body shell 脚本正文。
/// @return 已设置可执行权限的脚本路径。
std::filesystem::path write_script(const std::filesystem::path &path,
                                   const std::string &body) {
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

/// @brief 测试夹具：临时 XDG 环境与配置。
struct Fixture {
    lsw::test::TempDir temp{"daemon-fs"};
    XdgPaths paths;
    Config config;
    std::filesystem::path helper;
    std::filesystem::path ssh;
    std::filesystem::path helper_log;
    std::filesystem::path mounts_file;

    /// @brief 构造夹具并创建假 helper、假 ssh 与磁盘文件。
    Fixture() {
        PathEnvironment env;
        env.home = temp.path() / "home";
        env.xdg_config_home = temp.path() / "config";
        env.xdg_data_home = temp.path() / "data";
        env.xdg_state_home = temp.path() / "state";
        env.xdg_runtime_dir = temp.path() / "run";
        auto resolved = lsw::resolve_paths(env);
        REQUIRE(resolved.has_value());
        paths = *resolved;
        REQUIRE(lsw::ensure_directories(paths).has_value());

        config = lsw::default_config();
        config.vm.disk = temp.path() / "disk.qcow2";
        config.filesystem.mount_root = temp.path() / "mnt";
        config.network.smb_host_port = 1445;
        config.user.windows_user = "lsw";
        std::ofstream(config.vm.disk).close();

        helper_log = temp.path() / "helper.log";
        mounts_file = temp.path() / "mounts";
        std::ofstream(mounts_file).close();
        helper = write_script(temp.path() / "lsw-mount-helper",
                              R"(echo "$*" >> "$LSW_TEST_HELPER_LOG"
if [ "$1" = "mount" ]; then
  echo "//127.0.0.1/$2 $4 cifs rw 0 0" >> "$LSW_TEST_MOUNTS_FILE"
  exit 0
fi
if [ "$1" = "unmount" ]; then exit 0; fi
exit 2)");
        ssh = write_script(temp.path() / "ssh",
                           R"(args=("$@")
last="${args[${#args[@]}-1]}"
script="$last"
if [ "${#args[@]}" -ge 2 ] && [ "${args[${#args[@]}-2]}" = "-EncodedCommand" ]; then
    script=$(python3 -c 'import base64,sys; print(base64.b64decode(sys.argv[1]).decode("utf-16le"))' "$last")
fi
echo "$*" >> "$LSW_TEST_SSH_LOG"
echo "$script" >> "$LSW_TEST_SSH_LOG"
if [[ "$script" == *"net use"* ]]; then exit 0; fi
if [[ "$script" == *"exit 7"* ]]; then exit 7; fi
exit 0)");
    }

    /// @brief 构造启用文件系统集成的 daemon 选项。
    /// @return daemon 服务选项。
    DaemonServiceOptions options() const {
        DaemonServiceOptions options;
        options.qemu_options.qemu_binary = LSW_FAKE_QEMU_PATH;
        options.qemu_options.enable_kvm = false;
        options.qemu_options.enable_firmware = false;
        options.qemu_options.disk_image = config.vm.disk;
        options.qemu_options.home_dir = temp.path();
        options.qemu_options.qmp_connect_timeout = std::chrono::seconds(3);
        options.qemu_options.shutdown_timeout = std::chrono::seconds(2);
        options.wait_for_ssh = false;
        options.try_ssh_shutdown = false;
        options.enable_host_smb = false;
        options.enable_mounts = true;
        options.enable_z_mapping = true;
        options.mount_options.mount_root = config.filesystem.mount_root;
        options.mount_options.helper_path = helper;
        options.mount_options.privilege = "none";
        options.mount_options.mounts_file = mounts_file;
        options.mount_options.timeout = std::chrono::seconds(3);
        options.z_mapping_options.ssh_binary = ssh.string();
        options.z_mapping_options.port = 2222;
        return options;
    }
};

} // namespace

TEST_CASE("DaemonService start 挂载 C/D 并报告 Z:（延迟映射）",
          "[daemon][fs]") {
    Fixture fixture;
    ScopedEnv password("LSW_WINDOWS_PASSWORD", "p@ss");
    ScopedEnv helper_log("LSW_TEST_HELPER_LOG", fixture.helper_log.c_str());
    ScopedEnv mounts_file("LSW_TEST_MOUNTS_FILE", fixture.mounts_file.c_str());
    const auto ssh_log = fixture.temp.path() / "ssh.log";
    ScopedEnv ssh_log_env("LSW_TEST_SSH_LOG", ssh_log.c_str());

    DaemonService service(fixture.config, fixture.paths, fixture.options());
    const auto started = service.handle("start");
    REQUIRE(started.has_value());
    CHECK((*started)["vm_status"] == "running");
    CHECK((*started)["z_drive"] == "Z:");
    REQUIRE((*started)["mounts"].size() == 2);
    CHECK((*started)["mounts"][0] ==
          (fixture.config.filesystem.mount_root / "C").string());

    const auto shutdown = service.handle("shutdown");
    REQUIRE(shutdown.has_value());
    CHECK((*shutdown)["stopped"] == true);

    std::ifstream helper_input(fixture.helper_log);
    const std::string helper_calls(
        (std::istreambuf_iterator<char>(helper_input)),
        std::istreambuf_iterator<char>());
    CHECK(helper_calls.find("mount C") != std::string::npos);
    CHECK(helper_calls.find("unmount") != std::string::npos);

    // Z: 映射延迟到使用会话；daemon start 阶段不应调用 ssh。
    std::ifstream ssh_input(ssh_log);
    const std::string ssh_calls((std::istreambuf_iterator<char>(ssh_input)),
                                std::istreambuf_iterator<char>());
    CHECK(ssh_calls.find("net use") == std::string::npos);
}

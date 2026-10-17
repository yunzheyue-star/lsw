/**
 * @file test_install.cpp
 * @brief test_install 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_install.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <lsw/vm/install.hpp>
#include <lsw/vm/qemu.hpp>

#include "test_support.hpp"

#ifndef LSW_FAKE_QEMU_PATH
#error "测试需要 CMake 定义 LSW_FAKE_QEMU_PATH"
#endif

using lsw::Config;
using lsw::ErrorCode;
using lsw::PathEnvironment;
using lsw::XdgPaths;
using lsw::vm::Installer;
using lsw::vm::InstallOptions;

namespace {

struct Fixture {
    lsw::test::TempDir temp{"install"};
    XdgPaths paths;
    Config config;

    /**
     * @brief 构造测试夹具并初始化临时 XDG 路径与默认配置。
     */
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
        config.vm.disk = temp.path() / "vm" / "disk.qcow2";
    }

    /**
     * @brief 在指定路径创建文件并写入内容，必要时创建父目录。
     * @param path 目标文件路径。
     * @param content 写入文件的正文，默认为 "x"。
     */
    void touch(const std::filesystem::path &path,
               const std::string &content = "x") const {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }

    /**
     * @brief 创建测试用假 ISO 文件。
     * @param name 相对于临时目录的文件名。
     * @return 新建 ISO 文件的完整路径。
     */
    std::filesystem::path make_iso(const std::string &name) const {
        const auto path = temp.path() / name;
        touch(path, "fake-iso");
        return path;
    }

    /**
     * @brief 创建测试用假 OVMF 固件文件。
     * @param name 相对于临时目录的文件名。
     * @return 新建固件文件的完整路径。
     */
    std::filesystem::path make_ovmf(const std::string &name) const {
        const auto path = temp.path() / name;
        touch(path, "fake-ovmf");
        return path;
    }

    /**
     * @brief 基于当前夹具状态生成完整的安装选项。
     * @return 已填充测试资源路径与假 QEMU 参数的安装选项。
     */
    InstallOptions options() const {
        InstallOptions options;
        options.windows_iso = make_iso("win11.iso");
        options.virtio_iso = make_iso("virtio-win.iso");
        options.disk_image = config.vm.disk;
        options.ovmf_code = make_ovmf("OVMF_CODE.fd");
        options.ovmf_vars_template = make_ovmf("OVMF_VARS.fd");
        options.qemu_img_binary = lsw::test::binary("qemu-img");
        options.require_kvm = false;
        options.interactive = false;
        options.qemu_options.qemu_binary = LSW_FAKE_QEMU_PATH;
        options.qemu_options.enable_kvm = false;
        options.qemu_options.enable_firmware = true;
        options.qemu_options.home_dir = temp.path();
        options.qemu_options.shutdown_timeout = std::chrono::seconds(3);
        return options;
    }
};

struct ScopedEnv {
    /**
     * @brief 构造时设置指定的环境变量。
     * @param name 环境变量名。
     * @param value 环境变量值。
     */
    ScopedEnv(const char *name, const char *value) : name_(name) {
        ::setenv(name, value, 1);
    }
    /**
     * @brief 析构时清理由本对象设置的环境变量。
     */
    ~ScopedEnv() { ::unsetenv(name_.c_str()); }
    std::string name_;
};

} // namespace

TEST_CASE("依赖缺失时返回详细报告", "[install]") {
    Fixture fixture;
    InstallOptions options = fixture.options();
    options.require_kvm = true;
    options.kvm_device = fixture.temp.path() / "no-such-kvm";
    options.qemu_binary = "/no/such/qemu-system-x86_64";
    options.qemu_img_binary = "/no/such/qemu-img";
    options.ovmf_code = fixture.temp.path() / "missing_code.fd";
    options.ovmf_vars_template = fixture.temp.path() / "missing_vars.fd";
    options.require_swtpm = true;
    options.swtpm_binary = "/no/such/swtpm";

    Installer installer(fixture.config, fixture.paths, options);
    const auto report = installer.check_dependencies();
    REQUIRE(report.has_value());
    CHECK_FALSE(report->ok());
    CHECK(report->missing.size() >= 5);
    CHECK_FALSE(report->hints.empty());

    const auto result = installer.run();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::kMissingDependency);
}

TEST_CASE("Arch 环境依赖提示使用 pacman 与 edk2-ovmf", "[install]") {
    if (!std::filesystem::exists("/etc/arch-release")) {
        SUCCEED("非 Arch 环境，跳过");
        return;
    }
    Fixture fixture;
    InstallOptions options = fixture.options();
    options.ovmf_code = fixture.temp.path() / "missing_code.fd";
    options.ovmf_vars_template = fixture.temp.path() / "missing_vars.fd";
    Installer installer(fixture.config, fixture.paths, options);
    const auto report = installer.check_dependencies();
    REQUIRE(report.has_value());

    const auto has = [&report](const std::string &needle) {
        return std::any_of(report->hints.begin(), report->hints.end(),
                           [&](const std::string &hint) {
                               return hint.find(needle) != std::string::npos;
                           });
    };
    CHECK(has("pacman"));
    CHECK(has("edk2-ovmf"));
    // OVMF CODE 与 VARS 缺失应合并为同一条提示
    const auto ovmf_hints =
        std::count_if(report->hints.begin(), report->hints.end(),
                      [](const std::string &hint) {
                          return hint.find("UEFI") != std::string::npos;
                      });
    CHECK(ovmf_hints == 1);
}

TEST_CASE("依赖齐全时报告通过", "[install]") {
    Fixture fixture;
    Installer installer(fixture.config, fixture.paths, fixture.options());
    const auto report = installer.check_dependencies();
    REQUIRE(report.has_value());
    CHECK(report->ok());
    CHECK_FALSE(report->qemu_binary.empty());
    CHECK_FALSE(report->ovmf_code.empty());
}

TEST_CASE("依赖检查使用 run 实际启动的 QEMU 路径", "[install]") {
    Fixture fixture;
    InstallOptions options = fixture.options();
    // Fixture 只设置 qemu_options.qemu_binary；依赖检查应使用同一路径，
    // 而不是回落到宿主机 PATH 中的 qemu-system-x86_64。
    REQUIRE(options.qemu_binary.empty());
    Installer installer(fixture.config, fixture.paths, options);
    const auto report = installer.check_dependencies();
    REQUIRE(report.has_value());
    CHECK(report->qemu_binary == std::filesystem::path(LSW_FAKE_QEMU_PATH));
}

TEST_CASE("ISO 校验拒绝非法路径", "[install]") {
    Fixture fixture;
    InstallOptions options = fixture.options();
    options.windows_iso = fixture.temp.path() / "missing.iso";
    Installer installer(fixture.config, fixture.paths, options);
    CHECK(installer.validate_isos().error().code ==
          ErrorCode::kInvalidArgument);

    options.windows_iso = fixture.temp.path() / "win.txt";
    fixture.touch(options.windows_iso, "not-iso");
    Installer second(fixture.config, fixture.paths, options);
    CHECK(second.validate_isos().error().code == ErrorCode::kInvalidArgument);

    options.windows_iso = fixture.make_iso("ok.iso");
    options.virtio_iso = fixture.temp.path() / "missing-virtio.iso";
    Installer third(fixture.config, fixture.paths, options);
    CHECK(third.validate_isos().error().code == ErrorCode::kInvalidArgument);
}

TEST_CASE("创建磁盘与 UEFI 变量且幂等不覆盖", "[install]") {
    if (lsw::test::binary("qemu-img") == "qemu-img" &&
        !std::filesystem::exists("/usr/bin/qemu-img")) {
        SUCCEED("未安装 qemu-img，跳过");
        return;
    }
    Fixture fixture;
    InstallOptions options = fixture.options();
    options.allow_existing_disk = true;
    Installer installer(fixture.config, fixture.paths, options);
    REQUIRE(installer.prepare_storage().has_value());

    const auto disk = fixture.config.vm.disk;
    REQUIRE(std::filesystem::exists(disk));
    CHECK(std::filesystem::file_size(disk) > 0);

    const auto vars = fixture.paths.data_dir / "vm" / "OVMF_VARS.fd";
    REQUIRE(std::filesystem::exists(vars));

    // 第二次执行不得覆盖磁盘与 UEFI 变量
    const auto disk_time = std::filesystem::last_write_time(disk);
    {
        std::ofstream out(vars, std::ios::trunc);
        out << "user-modified";
    }
    REQUIRE(installer.prepare_storage().has_value());
    CHECK(std::filesystem::last_write_time(disk) == disk_time);
    std::ifstream in(vars);
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    CHECK(content == "user-modified");
}

TEST_CASE("默认拒绝把安装模式挂到外部已有磁盘", "[install]") {
    Fixture fixture;
    const auto external = fixture.temp.path() / "external-win10.qcow2";
    {
        std::ofstream out(external);
        out << "pretend-existing-vm-disk";
    }
    InstallOptions options = fixture.options();
    options.disk_image = external;
    options.allow_existing_disk = false;
    Installer rejecting(fixture.config, fixture.paths, options);
    const auto rejected = rejecting.prepare_storage();
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == ErrorCode::kInvalidArgument);

    options.allow_existing_disk = true;
    Installer allowed(fixture.config, fixture.paths, options);
    REQUIRE(allowed.prepare_storage().has_value());
    std::ifstream input(external);
    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    CHECK(content == "pretend-existing-vm-disk");
}

TEST_CASE("BIOS 模式不要求 OVMF 且可完成安装", "[install]") {
    Fixture fixture;
    InstallOptions options = fixture.options();
    options.qemu_options.enable_firmware = false;
    options.ovmf_code = fixture.temp.path() / "missing_code.fd";
    options.ovmf_vars_template = fixture.temp.path() / "missing_vars.fd";

    Installer installer(fixture.config, fixture.paths, options);
    const auto report = installer.check_dependencies();
    REQUIRE(report.has_value());
    CHECK(report->ok());

    ScopedEnv auto_quit("LSW_FAKE_QEMU_AUTO_QUIT_MS", "400");
    const auto result = installer.run();
    REQUIRE(result.has_value());
    CHECK(std::filesystem::exists(fixture.config.vm.disk));
    CHECK_FALSE(std::filesystem::exists(fixture.paths.data_dir / "vm" /
                                        "OVMF_VARS.fd"));
}

TEST_CASE("完整安装流程（假 QEMU 自动退出）", "[install]") {
    Fixture fixture;
    ScopedEnv auto_quit("LSW_FAKE_QEMU_AUTO_QUIT_MS", "400");
    Installer installer(fixture.config, fixture.paths, fixture.options());
    const auto result = installer.run();
    REQUIRE(result.has_value());
    CHECK(result->qemu_exit_code == 0);
    CHECK(result->guidance.size() >= 4);
    CHECK(std::filesystem::exists(fixture.config.vm.disk));
    CHECK(std::filesystem::exists(fixture.paths.data_dir / "vm" /
                                  "OVMF_VARS.fd"));
}

TEST_CASE("安装后指引覆盖关键检查项", "[install]") {
    Fixture fixture;
    Installer installer(fixture.config, fixture.paths, fixture.options());
    const auto guidance = installer.post_install_guidance();
    const std::string text = [&guidance] {
        std::string joined;
        for (const auto &line : guidance) {
            joined += line + "\n";
        }
        return joined;
    }();
    CHECK(text.find("OpenSSH") != std::string::npos);
    CHECK(text.find("QEMU Guest Agent") != std::string::npos);
    CHECK(text.find("net use Z:") != std::string::npos);
    CHECK(text.find("post-install.ps1") != std::string::npos);
}

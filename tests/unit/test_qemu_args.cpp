/**
 * @file test_qemu_args.cpp
 * @brief test_qemu_args 实现（测试与集成探针）。
 * @details 文件位置：tests/unit/test_qemu_args.cpp（单元测试）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <lsw/vm/qemu.hpp>

#include "test_support.hpp"

using lsw::Config;
using lsw::ErrorCode;
using lsw::PathEnvironment;
using lsw::XdgPaths;
using lsw::vm::build_qemu_args;
using lsw::vm::build_swtpm_args;
using lsw::vm::QemuVmOptions;

namespace {

/// @brief 根据临时目录创建测试所需的 XDG 路径。
/// @param temp 测试临时目录。
/// @return 已解析并确保存在的 XDG 路径。
XdgPaths make_paths(const lsw::test::TempDir &temp) {
    PathEnvironment env;
    env.home = temp.path();
    env.xdg_config_home = temp.path() / "config";
    env.xdg_data_home = temp.path() / "data";
    env.xdg_state_home = temp.path() / "state";
    env.xdg_runtime_dir = temp.path() / "run";
    auto paths = lsw::resolve_paths(env);
    REQUIRE(paths.has_value());
    REQUIRE(lsw::ensure_directories(*paths).has_value());
    return *paths;
}

/// @brief 构造用于 QEMU 参数构建测试的选项。
/// @param temp 测试临时目录。
/// @return 测试用 QEMU 虚拟机选项。
QemuVmOptions test_options(const lsw::test::TempDir &temp) {
    QemuVmOptions options;
    options.qemu_binary = "qemu-system-x86_64";
    options.enable_kvm = false;
    options.enable_firmware = false;
    options.disk_image = temp.path() / "disk.qcow2";
    options.home_dir = "/home/alice";
    return options;
}

/// @brief 判断参数列表中是否存在指定的键值对。
/// @param args 待搜索的参数列表。
/// @param key 键名。
/// @param value 键值。
/// @return 找到匹配键值对时返回 true，否则返回 false。
bool has_pair(const std::vector<std::string> &args, const std::string &key,
              const std::string &value) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == key && args[i + 1] == value) {
            return true;
        }
    }
    return false;
}

/// @brief 判断参数列表中是否存在包含指定片段的参数。
/// @param args 待搜索的参数列表。
/// @param fragment 要匹配的片段。
/// @return 找到包含指定片段的参数时返回 true，否则返回 false。
bool has_arg_containing(const std::vector<std::string> &args,
                        const std::string &fragment) {
    for (const auto &arg : args) {
        if (arg.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("运行模式参数符合 SKILL-02", "[vm][qemu]") {
    const lsw::test::TempDir temp("qemu-args");
    const XdgPaths paths = make_paths(temp);
    Config config = lsw::default_config();
    config.vm.name = "lsw-windows";
    config.network.ssh_host_port = 2222;
    config.network.smb_host_port = 1445;

    const auto args = build_qemu_args(config, paths, test_options(temp), false);
    REQUIRE(args.has_value());
    CHECK(has_pair(*args, "-display", "none"));
    CHECK(has_pair(*args, "-machine", "q35,accel=tcg"));
    CHECK(has_pair(*args, "-smp", "4"));
    CHECK(has_pair(*args, "-m", "8192"));
    CHECK(has_pair(*args, "-cpu", "max"));
    CHECK(has_pair(*args, "-boot", "order=c"));
    CHECK(has_arg_containing(*args, "hostfwd=tcp:127.0.0.1:2222-:22"));
    CHECK(has_arg_containing(*args, "hostfwd=tcp:127.0.0.1:1445-:445"));
    if (lsw::vm::find_executable("smbd").empty()) {
        CHECK_FALSE(has_arg_containing(*args, "smb="));
    } else {
        CHECK(has_arg_containing(*args, "smb=/home/alice"));
    }
    CHECK(has_arg_containing(
        *args, "unix:" + (paths.runtime_dir / "qmp.sock").string()));
    CHECK(has_arg_containing(*args, (temp.path() / "disk.qcow2").string()));
}

TEST_CASE("安装模式参数启用 gtk 与双 ISO", "[vm][qemu]") {
    const lsw::test::TempDir temp("qemu-install");
    const XdgPaths paths = make_paths(temp);
    Config config = lsw::default_config();
    config.vm.windows_iso = temp.path() / "win11.iso";
    config.vm.virtio_iso = temp.path() / "virtio-win.iso";

    const auto args = build_qemu_args(config, paths, test_options(temp), true);
    REQUIRE(args.has_value());
    CHECK(has_pair(*args, "-display", "gtk"));
    CHECK(has_pair(*args, "-boot", "order=d"));
    CHECK(has_arg_containing(*args, "win11.iso,media=cdrom,readonly=on"));
    CHECK(has_arg_containing(*args, "virtio-win.iso,media=cdrom,readonly=on"));
}

TEST_CASE("安装模式缺少 Windows ISO 时报错", "[vm][qemu]") {
    const lsw::test::TempDir temp("qemu-no-iso");
    const XdgPaths paths = make_paths(temp);
    const Config config = lsw::default_config();

    const auto args = build_qemu_args(config, paths, test_options(temp), true);
    REQUIRE_FALSE(args.has_value());
    CHECK(args.error().code == ErrorCode::kConfigInvalid);
}

TEST_CASE("显式 OVMF 固件不存在时报错", "[vm][qemu]") {
    const lsw::test::TempDir temp("qemu-firmware");
    const XdgPaths paths = make_paths(temp);
    const Config config = lsw::default_config();
    QemuVmOptions options = test_options(temp);
    options.enable_firmware = true;
    options.ovmf_code = temp.path() / "missing_OVMF_CODE.fd";
    options.ovmf_vars = temp.path() / "OVMF_VARS.fd";

    const auto args = build_qemu_args(config, paths, options, false);
    REQUIRE_FALSE(args.has_value());
    CHECK(args.error().code == ErrorCode::kMissingDependency);
}

TEST_CASE("无 /dev/kvm 时 KVM 模式和 TCG 模式行为正确", "[vm][qemu]") {
    const lsw::test::TempDir temp("qemu-kvm");
    const XdgPaths paths = make_paths(temp);
    const Config config = lsw::default_config();

    QemuVmOptions options = test_options(temp);
    options.enable_kvm = true;
    const auto kvm_args = build_qemu_args(config, paths, options, false);
    if (std::filesystem::exists("/dev/kvm")) {
        REQUIRE(kvm_args.has_value());
        CHECK(has_pair(*kvm_args, "-machine", "q35,accel=kvm"));
    } else {
        REQUIRE_FALSE(kvm_args.has_value());
        CHECK(kvm_args.error().code == ErrorCode::kMissingDependency);
    }

    options.enable_kvm = false;
    const auto tcg_args = build_qemu_args(config, paths, options, false);
    REQUIRE(tcg_args.has_value());
    CHECK(has_pair(*tcg_args, "-machine", "q35,accel=tcg"));
}

TEST_CASE("swtpm 检测与参数拼接", "[vm][qemu]") {
    const lsw::test::TempDir temp("qemu-swtpm");
    const XdgPaths paths = make_paths(temp);

    if (!lsw::vm::swtpm_available()) {
        SUCCEED("当前环境未安装 swtpm，跳过参数断言");
        return;
    }

    QemuVmOptions options = test_options(temp);
    options.enable_swtpm = true;
    options.swtpm_binary = lsw::vm::find_executable("swtpm");

    const auto swtpm_args = build_swtpm_args(options, paths);
    REQUIRE(swtpm_args.has_value());
    CHECK(has_arg_containing(*swtpm_args, "--tpm2"));
    CHECK(has_arg_containing(*swtpm_args, "type=unixio,path="));

    const Config config = lsw::default_config();
    const auto qemu_args = build_qemu_args(config, paths, options, false);
    REQUIRE(qemu_args.has_value());
    CHECK(has_pair(*qemu_args, "-tpmdev", "emulator,id=tpm0,chardev=chrtpm"));
    CHECK(has_arg_containing(*qemu_args, "swtpm.sock"));
}

TEST_CASE("OVMF 候选路径覆盖主流发行版", "[vm][qemu]") {
    const auto code_candidates = lsw::vm::ovmf_code_candidates();
    const auto vars_candidates = lsw::vm::ovmf_vars_candidates();
    const auto contains = [](const std::vector<std::filesystem::path> &list,
                             const std::string &value) {
        return std::any_of(list.begin(), list.end(),
                           [&](const auto &path) { return path == value; });
    };
    CHECK(contains(code_candidates, "/usr/share/OVMF/OVMF_CODE.fd"));
    CHECK(contains(code_candidates, "/usr/share/edk2/x64/OVMF_CODE.4m.fd"));
    CHECK(contains(vars_candidates, "/usr/share/OVMF/OVMF_VARS.fd"));
    CHECK(contains(vars_candidates, "/usr/share/edk2/x64/OVMF_VARS.4m.fd"));
}

TEST_CASE("工具探测函数行为合理", "[vm][qemu]") {
    CHECK_FALSE(lsw::vm::find_executable("sh").empty());
    CHECK(lsw::vm::find_executable("no-such-lsw-binary").empty());
    const auto ovmf = lsw::vm::detect_ovmf_code();
    if (ovmf.has_value()) {
        CHECK(std::filesystem::exists(*ovmf));
    }
    if (std::filesystem::exists("/usr/share/edk2/x64/OVMF_CODE.4m.fd")) {
        REQUIRE(ovmf.has_value());
        CHECK(*ovmf == "/usr/share/edk2/x64/OVMF_CODE.4m.fd");
    }
    const auto ovmf_vars = lsw::vm::detect_ovmf_vars_template();
    if (std::filesystem::exists("/usr/share/edk2/x64/OVMF_VARS.4m.fd")) {
        REQUIRE(ovmf_vars.has_value());
        CHECK(*ovmf_vars == "/usr/share/edk2/x64/OVMF_VARS.4m.fd");
    }
}

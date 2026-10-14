/**
 * @file install_probe.cpp
 * @brief install_probe 实现（测试与集成探针）。
 * @details 文件位置：tests/integration/install_probe.cpp（集成探针/测试替身）。
 *          属于 LSW 测试与集成探针；公共行为以 docs/interfaces 与 agent.md
 * 为准。
 */

// 安装流程集成探针：使用假 QEMU 走通 Installer::run()。
// 用法: lsw_install_probe <fake-qemu> <workdir>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <lsw/core/config.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/install.hpp>
#include <lsw/vm/qemu.hpp>

namespace {

void touch(const std::filesystem::path &path,
           const std::string &content = "x") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "用法: lsw_install_probe <fake-qemu> <workdir>\n";
        return 2;
    }
    const std::filesystem::path fake_qemu = argv[1];
    const std::filesystem::path workdir = argv[2];

    lsw::PathEnvironment env;
    env.home = workdir / "home";
    env.xdg_config_home = workdir / "config";
    env.xdg_data_home = workdir / "data";
    env.xdg_state_home = workdir / "state";
    env.xdg_runtime_dir = workdir / "run";
    auto paths = lsw::resolve_paths(env);
    if (!paths) {
        std::cerr << "ERROR: " << paths.error().message << "\n";
        return 1;
    }
    if (auto status = lsw::ensure_directories(*paths); !status) {
        std::cerr << "ERROR: " << status.error().message << "\n";
        return 1;
    }

    const auto iso = workdir / "win11.iso";
    const auto virtio_iso = workdir / "virtio-win.iso";
    const auto ovmf_code = workdir / "OVMF_CODE.fd";
    const auto ovmf_vars = workdir / "OVMF_VARS.fd";
    touch(iso);
    touch(virtio_iso);
    touch(ovmf_code, "code");
    touch(ovmf_vars, "vars");

    lsw::Config config = lsw::default_config();
    config.vm.disk = workdir / "vm" / "disk.qcow2";

    lsw::vm::InstallOptions options;
    options.windows_iso = iso;
    options.virtio_iso = virtio_iso;
    options.disk_image = config.vm.disk;
    options.ovmf_code = ovmf_code;
    options.ovmf_vars_template = ovmf_vars;
    options.qemu_img_binary = "qemu-img";
    options.require_kvm = false;
    options.interactive = false;
    options.qemu_options.qemu_binary = fake_qemu;
    options.qemu_options.enable_kvm = false;
    options.qemu_options.enable_firmware = true;
    options.qemu_options.home_dir = workdir;
    options.qemu_options.shutdown_timeout = std::chrono::seconds(3);

    // 假 QEMU 在握手后自动退出，模拟用户完成安装关闭窗口。
    ::setenv("LSW_FAKE_QEMU_AUTO_QUIT_MS", "400", 1);

    lsw::vm::Installer installer(std::move(config), std::move(*paths),
                                 std::move(options));
    auto result = installer.run();
    if (!result) {
        std::cerr << "ERROR " << lsw::to_string(result.error().code) << ": "
                  << result.error().message << "\n";
        return 1;
    }

    std::cout << "INSTALL_OK exit_code=" << result->qemu_exit_code << "\n";
    for (const auto &line : result->guidance) {
        std::cout << line << "\n";
    }
    return 0;
}

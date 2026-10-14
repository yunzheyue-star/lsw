#include <iostream>
#include <string>

#include <CLI/CLI.hpp>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/install.hpp>

#ifndef LSW_VERSION
#define LSW_VERSION "0.1.0"
#endif

namespace {

int report_error(const lsw::Error &error) {
    std::cerr << "lsw: " << error.message << "\n";
    return lsw::to_exit_code(error.code);
}

} // namespace

int main(int argc, char **argv) {
    CLI::App app{"lsw - Linux Subsystem for Windows"};
    app.name("lsw");
    app.set_version_flag("--version", std::string("lsw ") + LSW_VERSION);

    bool install_flag = false;
    std::string iso;
    std::string virtio_iso;
    std::string disk;

    app.add_flag("--install", install_flag,
                 "引导安装 Windows（不提供、不下载 ISO）");
    app.add_option("--iso", iso, "Windows ISO 路径");
    app.add_option("--virtio-iso", virtio_iso, "virtio-win ISO 路径");
    app.add_option("--disk", disk, "系统盘 qcow2 路径");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ExtrasError &error) {
        std::cerr << error.what() << "\n"
                  << "提示：ISO / 磁盘路径必须配合选项使用：--iso、"
                     "--virtio-iso、--disk；\n"
                  << "      只运行 `lsw --install` 可在终端中交互输入路径。\n";
        return lsw::to_exit_code(lsw::ErrorCode::kInvalidArgument);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }

    if (!install_flag) {
        std::cerr
            << "lsw: shell 功能将在 P7 实现；当前可用命令：lsw --install\n";
        std::cout << app.help();
        return lsw::to_exit_code(lsw::ErrorCode::kNotImplemented);
    }

    auto paths = lsw::resolve_paths(lsw::PathEnvironment::from_process());
    if (!paths) {
        return report_error(paths.error());
    }

    lsw::Config config;
    if (auto loaded = lsw::load_config(paths->config_file, true); loaded) {
        config = *loaded;
    } else {
        return report_error(loaded.error());
    }

    lsw::vm::InstallOptions options;
    options.windows_iso = iso;
    options.virtio_iso = virtio_iso;
    options.disk_image = disk;
    options.interactive = true;

    lsw::vm::Installer installer(std::move(config), std::move(*paths),
                                 std::move(options));
    auto result = installer.run();
    if (!result) {
        return report_error(result.error());
    }

    std::cout << "\n安装流程结束（QEMU 退出码 " << result->qemu_exit_code
              << "）。\n";
    for (const auto &line : result->guidance) {
        std::cout << "  " << line << "\n";
    }
    return 0;
}

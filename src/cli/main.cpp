/**
 * @file main.cpp
 * @brief lsw CLI 入口：install/start/shutdown 命令解析。
 * @details 文件位置：src/cli/main.cpp（源文件）。
 *          属于 LSW lsw CLI；公共行为以 docs/interfaces/cli.md 与 agent.md
 * 为准。
 */

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

#include <CLI/CLI.hpp>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/core/process.hpp>
#include <lsw/ipc/client.hpp>
#include <lsw/util/paths.hpp>
#include <lsw/vm/install.hpp>
#include <lsw/vm/qemu.hpp>

#ifndef LSW_VERSION
#define LSW_VERSION "0.1.0"
#endif

namespace {

int report_error(const lsw::Error &error) {
    std::cerr << "lsw: " << error.message << "\n";
    return lsw::to_exit_code(error.code);
}

std::filesystem::path daemon_binary_path() {
    std::error_code error;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        const auto candidate = self.parent_path() / "lswd";
        std::error_code exists_error;
        if (std::filesystem::exists(candidate, exists_error)) {
            return candidate;
        }
    }
    return lsw::vm::find_executable("lswd");
}

lsw::Status ensure_daemon(const lsw::XdgPaths &paths) {
    const auto socket = paths.runtime_dir / "lswd.sock";
    lsw::ipc::ClientOptions probe_options;
    probe_options.socket_path = socket;
    probe_options.connect_timeout = std::chrono::milliseconds(500);
    probe_options.io_timeout = std::chrono::milliseconds(1000);
    lsw::ipc::Client probe(probe_options);
    if (probe.call("status")) {
        return {};
    }

    if (auto status = lsw::ensure_directories(paths); !status) {
        return status;
    }
    const auto binary = daemon_binary_path();
    if (binary.empty()) {
        return lsw::fail(lsw::ErrorCode::kMissingDependency,
                         "找不到 lswd 可执行文件");
    }
    const auto log_file = paths.state_dir / "lswd.log";
    if (auto status = lsw::spawn_detached(
            {binary.string()},
            lsw::core::SpawnOptions{.stdout_file = log_file,
                                    .stderr_file = log_file});
        !status) {
        return status;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (probe.call("status")) {
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return lsw::fail(lsw::ErrorCode::kDaemonUnavailable,
                     "lswd 启动超时，请查看 " + log_file.string());
}

int run_daemon_command(const lsw::XdgPaths &paths, const std::string &command) {
    lsw::ipc::ClientOptions options;
    options.socket_path = paths.runtime_dir / "lswd.sock";
    options.connect_timeout = std::chrono::seconds(1);
    options.io_timeout = command == "start" ? std::chrono::seconds(180)
                                            : std::chrono::seconds(60);
    lsw::ipc::Client client(options);

    auto result = client.call(command);
    if (!result) {
        if (command == "shutdown" &&
            result.error().code == lsw::ErrorCode::kDaemonUnavailable) {
            std::cout << "lswd 未运行，Windows 已处于停止状态。\n";
            return 0;
        }
        return report_error(result.error());
    }
    std::cout << result->dump() << "\n";
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    CLI::App app{"lsw - Linux Subsystem for Windows"};
    app.name("lsw");
    app.set_version_flag("--version", std::string("lsw ") + LSW_VERSION);

    bool install_flag = false;
    bool allow_existing_disk = false;
    std::string iso;
    std::string virtio_iso;
    std::string disk;
    std::string firmware = "bios";

    app.add_flag("--install", install_flag,
                 "引导安装 Windows（不提供、不下载 ISO）");
    app.add_option("--iso", iso, "Windows ISO 路径");
    app.add_option("--virtio-iso", virtio_iso, "virtio-win ISO 路径");
    app.add_option("--disk", disk, "系统盘 qcow2 路径");
    app.add_option("--firmware", firmware, "安装固件：bios（默认）或 ovmf")
        ->check(CLI::IsMember({"bios", "ovmf"}));
    app.add_flag("--allow-existing-disk", allow_existing_disk,
                 "允许复用 LSW 数据目录之外已存在的磁盘（危险）");

    auto *start_command =
        app.add_subcommand("start", "启动 Windows 虚拟机服务");
    auto *shutdown_command =
        app.add_subcommand("shutdown", "关闭 Windows 虚拟机服务");

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

    if (install_flag) {
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
        options.allow_existing_disk = allow_existing_disk;
        options.qemu_options.enable_firmware = firmware == "ovmf";
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

    if (start_command->parsed() || shutdown_command->parsed()) {
        auto paths = lsw::resolve_paths(lsw::PathEnvironment::from_process());
        if (!paths) {
            return report_error(paths.error());
        }
        if (start_command->parsed()) {
            std::cout
                << "正在启动 Windows 虚拟机服务（等待 SSH 127.0.0.1:2222，"
                   "最长 120 秒）...\n"
                << std::flush;
            if (auto status = ensure_daemon(*paths); !status) {
                return report_error(status.error());
            }
            return run_daemon_command(*paths, "start");
        }
        std::cout << "正在关闭 Windows 虚拟机（优先 SSH，QMP/信号兜底）...\n"
                  << std::flush;
        return run_daemon_command(*paths, "shutdown");
    }

    std::cerr << "lsw: shell 功能将在 P7 实现；当前可用命令：\n"
              << "  lsw --install   lsw start   lsw shutdown\n";
    std::cout << app.help();
    return lsw::to_exit_code(lsw::ErrorCode::kNotImplemented);
}

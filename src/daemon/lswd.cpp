#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/file.h>
#include <unistd.h>

#include <CLI/CLI.hpp>

#include <lsw/core/state.hpp>
#include <lsw/ipc/server.hpp>
#include <lsw/util/log.hpp>
#include <lsw/util/paths.hpp>

#ifndef LSW_VERSION
#define LSW_VERSION "0.1.0"
#endif

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void on_signal(int) { g_stop_requested = 1; }

} // namespace

int main(int argc, char **argv) {
    CLI::App app{"lswd - LSW 守护进程（P2 骨架）"};
    std::string socket_option;
    bool show_version = false;
    app.add_option("--socket", socket_option,
                   "IPC socket 路径（覆盖默认位置）");
    app.add_flag("--version", show_version, "显示版本后退出");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }
    if (show_version) {
        std::cout << "lswd " << LSW_VERSION << "\n";
        return 0;
    }

    const auto environment = lsw::PathEnvironment::from_process();
    auto paths = lsw::resolve_paths(environment);
    if (!paths) {
        std::cerr << "lswd: " << paths.error().message << "\n";
        return lsw::to_exit_code(paths.error().code);
    }

    const std::filesystem::path socket_path =
        socket_option.empty() ? paths->runtime_dir / "lswd.sock"
                              : std::filesystem::path(socket_option);

    if (auto status = lsw::ensure_directories(*paths); !status) {
        std::cerr << "lswd: " << status.error().message << "\n";
        return lsw::to_exit_code(status.error().code);
    }
    if (auto status = lsw::log::init(paths->state_dir / "lswd.log"); !status) {
        std::cerr << "lswd: " << status.error().message << "\n";
        return lsw::to_exit_code(status.error().code);
    }

    // ===== 单实例锁 =====
    const auto pid_path = paths->runtime_dir / "lswd.pid";
    const int pid_fd =
        ::open(pid_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (pid_fd < 0) {
        std::cerr << "lswd: 无法创建 PID 文件: " << std::strerror(errno)
                  << "\n";
        return 1;
    }
    if (::flock(pid_fd, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "lswd: 已在运行（" << pid_path.string() << " 被占用）\n";
        ::close(pid_fd);
        return 1;
    }
    const std::string pid_text = std::to_string(::getpid()) + "\n";
    (void)::ftruncate(pid_fd, 0);
    (void)::write(pid_fd, pid_text.data(), pid_text.size());

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    lsw::ipc::Server server({socket_path, 0700, 16});
    lsw::core::DaemonState state(LSW_VERSION);
    for (const char *command :
         {"status", "start", "shutdown", "mount", "unmount", "shell", "exec"}) {
        server.register_handler(command,
                                [&state, command](const nlohmann::json &args) {
                                    return state.handle(command, args);
                                });
    }

    if (auto status = server.start(); !status) {
        std::cerr << "lswd: " << status.error().message << "\n";
        ::close(pid_fd);
        std::error_code ignored;
        std::filesystem::remove(pid_path, ignored);
        return lsw::to_exit_code(status.error().code);
    }

    lsw::log::logger()->info("lswd {} 已启动，监听 {}", LSW_VERSION,
                             socket_path.string());

    while (g_stop_requested == 0) {
        auto served = server.serve_once();
        if (!served) {
            lsw::log::logger()->error("lswd 服务循环退出: {}",
                                      served.error().message);
            break;
        }
    }

    server.stop();
    lsw::log::logger()->info("lswd 已停止");
    lsw::log::shutdown();

    ::close(pid_fd);
    std::error_code ignored;
    std::filesystem::remove(pid_path, ignored);
    return 0;
}

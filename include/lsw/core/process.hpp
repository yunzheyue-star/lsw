#pragma once

#include <chrono>
#include <csignal>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <lsw/core/error.hpp>

namespace lsw {

struct RunOptions {
    std::optional<std::filesystem::path> cwd; ///< 子进程工作目录
    std::chrono::milliseconds timeout{0};     ///< 0 表示不设超时
};

struct ProcessResult {
    int exit_code{0};
    std::string stdout_data;
    std::string stderr_data;
};

/// 参数化执行外部程序（fork + execvp，不经过 shell，无注入风险）。
/// - 始终捕获 stdout/stderr
/// - 超时返回 kProcessTimeout，并回收子进程
/// - 无法执行（不存在、无权限、chdir 失败）返回 kProcessError
[[nodiscard]] Result<ProcessResult>
run_process(const std::vector<std::string> &argv,
            const RunOptions &options = {});

namespace core {

struct SpawnOptions {
    std::optional<std::filesystem::path> cwd;
    std::optional<std::filesystem::path> stdout_file; ///< 空 = /dev/null
    std::optional<std::filesystem::path> stderr_file; ///< 空 = /dev/null
};

/// 后台子进程 RAII：析构时若仍存活则 SIGKILL 并回收，避免僵尸。
class ChildProcess {
  public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;
    ChildProcess(ChildProcess &&other) noexcept;
    ChildProcess &operator=(ChildProcess &&other) noexcept;

    [[nodiscard]] static Result<ChildProcess>
    spawn(const std::vector<std::string> &argv,
          const SpawnOptions &options = {});

    [[nodiscard]] bool valid() const noexcept { return pid_ > 0; }
    [[nodiscard]] int pid() const noexcept { return pid_; }
    [[nodiscard]] bool running();
    [[nodiscard]] std::optional<int> exit_code() const noexcept {
        return exit_code_;
    }

    /// 阻塞等待退出，返回退出码（信号终止为 128+signal）。
    [[nodiscard]] Result<int> wait();
    /// 限时等待；超时返回 kProcessTimeout，进程仍存活。
    [[nodiscard]] Result<int> wait_for(std::chrono::milliseconds timeout);
    [[nodiscard]] Status send_signal(int signal) noexcept;
    [[nodiscard]] Status terminate() noexcept { return send_signal(SIGTERM); }
    [[nodiscard]] Status kill() noexcept { return send_signal(SIGKILL); }

  private:
    void reap_nonblocking() noexcept;
    int pid_{-1};
    std::optional<int> exit_code_;
};

} // namespace core

} // namespace lsw

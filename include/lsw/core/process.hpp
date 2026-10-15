/**
 * @file process.hpp
 * @brief 同步进程执行与后台子进程 RAII。
 * @details 文件位置：include/lsw/core/process.hpp（头文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

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
    /// 写入子进程 stdin 的数据；空表示沿用父进程 stdin。
    /// 密码等敏感数据只允许走这里，禁止放进 argv。
    std::optional<std::string> stdin_data;
};

struct ProcessResult {
    int exit_code{0};
    std::string stdout_data;
    std::string stderr_data;
};

/// @brief 参数化执行外部程序（fork + execvp，不经过 shell，无注入风险）。
/// - 始终捕获 stdout/stderr
/// - 超时返回 kProcessTimeout，并回收子进程
/// - 无法执行（不存在、无权限、chdir 失败）返回 kProcessError
/// @param argv 参数向量，argv[0] 为可执行文件。
/// @param options 执行选项（工作目录与超时）。
/// @return 执行成功返回进程结果，失败返回对应错误。
[[nodiscard]] Result<ProcessResult>
run_process(const std::vector<std::string> &argv,
            const RunOptions &options = {});

namespace core {

struct SpawnOptions {
    std::optional<std::filesystem::path> cwd;
    std::optional<std::filesystem::path> stdout_file; ///< 空 = /dev/null
    std::optional<std::filesystem::path> stderr_file; ///< 空 = /dev/null
};

/// @brief 后台子进程 RAII：析构时若仍存活则 SIGKILL 并回收，避免僵尸。
class ChildProcess {
  public:
    /// @brief 默认构造：不持有任何子进程。
    ChildProcess() = default;
    /// @brief 析构时若子进程仍存活则 SIGKILL 并回收，避免僵尸。
    ~ChildProcess();
    /// @brief 拷贝构造已删除：ChildProcess 不可拷贝。
    ChildProcess(const ChildProcess &) = delete;
    /// @brief 拷贝赋值已删除：ChildProcess 不可拷贝。
    ChildProcess &operator=(const ChildProcess &) = delete;
    /// @brief 移动构造：接管 other 的子进程句柄。
    /// @param other 被移动的 ChildProcess。
    ChildProcess(ChildProcess &&other) noexcept;
    /// @brief 移动赋值：先释放自身句柄，再接管 other 的子进程句柄。
    /// @param other 被移动的 ChildProcess。
    /// @return 自身引用。
    ChildProcess &operator=(ChildProcess &&other) noexcept;

    /// @brief 启动后台子进程。
    /// @param argv 参数向量，argv[0] 为可执行文件。
    /// @param options 启动选项（工作目录与标准输出/错误重定向）。
    /// @return 成功返回持有子进程的 ChildProcess，失败返回错误。
    [[nodiscard]] static Result<ChildProcess>
    spawn(const std::vector<std::string> &argv,
          const SpawnOptions &options = {});

    /// @brief 是否持有有效的子进程句柄。
    /// @return 持有有效 pid 时返回 true，否则 false。
    [[nodiscard]] bool valid() const noexcept { return pid_ > 0; }
    /// @brief 获取子进程 pid。
    /// @return 子进程 pid；未持有时返回 -1。
    [[nodiscard]] int pid() const noexcept { return pid_; }
    /// @brief 查询子进程当前是否仍在运行。
    /// @return 仍在运行返回 true，已退出返回 false。
    [[nodiscard]] bool running();
    /// @brief 获取已退出子进程的退出码。
    /// @return 已有退出码时返回之，否则返回 nullopt。
    [[nodiscard]] std::optional<int> exit_code() const noexcept {
        return exit_code_;
    }

    /// @brief 阻塞等待退出，返回退出码（信号终止为 128+signal）。
    /// @return 子进程退出码，失败返回错误。
    [[nodiscard]] Result<int> wait();
    /// @brief 限时等待；超时返回 kProcessTimeout，进程仍存活。
    /// @param timeout 等待超时时间。
    /// @return 子进程退出码；超时返回 kProcessTimeout，失败返回错误。
    [[nodiscard]] Result<int> wait_for(std::chrono::milliseconds timeout);
    /// @brief 向子进程发送指定信号。
    /// @param signal 信号编号。
    /// @return 发送成功返回成功状态，失败返回错误。
    [[nodiscard]] Status send_signal(int signal) noexcept;
    /// @brief 向子进程发送 SIGTERM。
    /// @return 发送成功返回成功状态，失败返回错误。
    [[nodiscard]] Status terminate() noexcept { return send_signal(SIGTERM); }
    /// @brief 向子进程发送 SIGKILL。
    /// @return 发送成功返回成功状态，失败返回错误。
    [[nodiscard]] Status kill() noexcept { return send_signal(SIGKILL); }

  private:
    /// @brief 非阻塞回收子进程并更新退出码。
    void reap_nonblocking() noexcept;
    int pid_{-1};
    std::optional<int> exit_code_;
};

} // namespace core

/// @brief 以“脱离终端”的方式启动后台进程（双重 fork + setsid），用于 CLI 拉起
/// lswd。 父进程只等待中间子进程退出，不持有孙进程句柄。
/// @param argv 参数向量，argv[0] 为可执行文件。
/// @param options 启动选项（工作目录与标准输出/错误重定向）。
/// @return 启动成功返回成功状态，失败返回错误。
[[nodiscard]] Status spawn_detached(const std::vector<std::string> &argv,
                                    const core::SpawnOptions &options = {});

} // namespace lsw

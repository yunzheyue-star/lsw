/**
 * @file process.cpp
 * @brief process 实现（核心库）。
 * @details 文件位置：src/core/process.cpp（源文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <lsw/core/process.hpp>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lsw {
namespace {

class ScopedFd {
  public:
    /// @brief 默认构造：持有无效的文件描述符。
    ScopedFd() = default;
    /// @brief 接管一个文件描述符的 RAII 构造函数。
    /// @param fd 要接管的文件描述符。
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}
    /// @brief 析构时关闭仍持有的文件描述符。
    ~ScopedFd() { reset(); }
    /// @brief 禁止拷贝构造，避免文件描述符被重复关闭。
    /// @param other 待拷贝的源对象；该重载已删除。
    ScopedFd(const ScopedFd &) = delete;
    /// @brief 禁止拷贝赋值，避免文件描述符被重复关闭。
    /// @param other 待赋值的源对象；该重载已删除。
    /// @return 该重载已删除，不会返回。
    ScopedFd &operator=(const ScopedFd &) = delete;
    /// @brief 移动构造：从 other 转移文件描述符所有权。
    /// @param other 被移动的源对象。
    ScopedFd(ScopedFd &&other) noexcept : fd_(other.release()) {}
    /// @brief 移动赋值：先释放当前描述符，再转移 other 的所有权。
    /// @param other 被移动的源对象。
    /// @return 当前对象的引用。
    ScopedFd &operator=(ScopedFd &&other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    /// @brief 获取当前持有的文件描述符。
    /// @return 文件描述符；未持有时返回 -1。
    [[nodiscard]] int get() const noexcept { return fd_; }

    /// @brief 释放所有权并返回文件描述符，不关闭它。
    /// @return 当前持有的文件描述符。
    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    /// @brief 关闭当前文件描述符并接管新文件描述符。
    /// @param fd 要接管的新文件描述符，默认 -1 表示仅关闭旧描述符。
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

/// @brief 创建带 CLOEXEC 标记的管道。
/// @param read_end 输出参数，接收管道读端。
/// @param write_end 输出参数，接收管道写端。
/// @return 成功返回空 Status；失败返回 kProcessError。
Status make_pipe(ScopedFd &read_end, ScopedFd &write_end) {
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("创建管道失败: ") + std::strerror(errno));
    }
    for (const int fd : fds) {
        const int flags = ::fcntl(fd, F_GETFD);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }
    read_end.reset(fds[0]);
    write_end.reset(fds[1]);
    return {};
}

/// @brief 将文件描述符设置为非阻塞模式。
/// @param fd 待设置的文件描述符。
/// @return 成功返回空 Status；失败返回 kProcessError。
Status set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("设置非阻塞失败: ") + std::strerror(errno));
    }
    return {};
}

/// @brief 向进程发送 SIGKILL 并阻塞回收，避免僵尸进程。
/// @param pid 目标进程 ID。
void kill_and_reap(pid_t pid) {
    ::kill(pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

/// @brief 将 waitpid 返回的状态解码为退出码。
/// @param status waitpid 填写的状态值。
/// @return 正常退出返回退出码，被信号终止返回 128+信号，其他返回 -1。
int decode_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

struct Capture {
    ScopedFd fd;
    std::string *data;
    bool open{true};
};

/// @brief 判断字节是否为 UTF-8 续字节。
/// @param byte 待判断字节。
/// @return 是续字节返回 true。
bool is_utf8_continuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

/// @brief 将任意进程输出清洗为合法 UTF-8（非法字节替换为 U+FFFD）。
/// @param input 原始字节串。
/// @return 合法 UTF-8 字符串。
std::string sanitize_utf8(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    std::size_t index = 0;
    while (index < input.size()) {
        const auto byte = static_cast<unsigned char>(input[index]);
        if (byte < 0x80U) {
            output.push_back(static_cast<char>(byte));
            ++index;
            continue;
        }
        std::size_t length = 0;
        if (byte >= 0xF0U) {
            length = 4;
        } else if (byte >= 0xE0U) {
            length = 3;
        } else if (byte >= 0xC0U) {
            length = 2;
        }
        bool valid = length > 0 && index + length <= input.size();
        if (valid) {
            for (std::size_t offset = 1; offset < length; ++offset) {
                if (!is_utf8_continuation(
                        static_cast<unsigned char>(input[index + offset]))) {
                    valid = false;
                    break;
                }
            }
        }
        if (valid) {
            if (length == 2 && byte < 0xC2U) {
                valid = false;
            } else if (length == 3) {
                const auto second =
                    static_cast<unsigned char>(input[index + 1]);
                if ((byte == 0xE0U && second < 0xA0U) ||
                    (byte == 0xEDU && second >= 0xA0U)) {
                    valid = false;
                }
            } else if (length == 4) {
                const auto second =
                    static_cast<unsigned char>(input[index + 1]);
                if ((byte == 0xF0U && second < 0x90U) || byte > 0xF4U ||
                    (byte == 0xF4U && second > 0x8FU)) {
                    valid = false;
                }
            }
        }
        if (valid) {
            output.append(input.substr(index, length));
            index += length;
        } else {
            output.append("\xEF\xBF\xBD");
            ++index;
        }
    }
    return output;
}

} // namespace

/// @brief 同步运行外部程序并捕获标准输出与标准错误。
/// @param argv 命令与参数列表，第一项为可执行文件。
/// @param options 运行选项，包括工作目录与超时。
/// @return 运行结果；参数、执行、I/O 或超时错误时返回对应错误。
Result<ProcessResult> run_process(const std::vector<std::string> &argv,
                                  const RunOptions &options) {
    if (argv.empty() || argv.front().empty()) {
        return fail(ErrorCode::kInvalidArgument, "argv 不能为空");
    }

    ScopedFd out_read;
    ScopedFd out_write;
    ScopedFd err_read;
    ScopedFd err_write;
    ScopedFd exec_read;
    ScopedFd exec_write;
    ScopedFd in_read;
    ScopedFd in_write;
    if (options.stdin_data.has_value()) {
        if (auto status = make_pipe(in_read, in_write); !status) {
            return std::unexpected(status.error());
        }
    }
    if (auto status = make_pipe(out_read, out_write); !status) {
        return std::unexpected(status.error());
    }
    if (auto status = make_pipe(err_read, err_write); !status) {
        return std::unexpected(status.error());
    }
    if (auto status = make_pipe(exec_read, exec_write); !status) {
        return std::unexpected(status.error());
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("fork 失败: ") + std::strerror(errno));
    }

    if (pid == 0) {
        // 子进程：不执行任何 C++ 析构，失败路径一律 _exit。
        if (in_read.get() >= 0) {
            (void)::dup2(in_read.get(), STDIN_FILENO);
        }
        if (options.cwd.has_value() && ::chdir(options.cwd->c_str()) != 0) {
            const int code = errno;
            const ssize_t ignored =
                ::write(exec_write.get(), &code, sizeof(code));
            (void)ignored;
            ::_exit(126);
        }
        (void)::dup2(out_write.get(), STDOUT_FILENO);
        (void)::dup2(err_write.get(), STDERR_FILENO);

        std::vector<char *> raw;
        raw.reserve(argv.size() + 1);
        for (const auto &arg : argv) {
            raw.push_back(const_cast<char *>(arg.c_str()));
        }
        raw.push_back(nullptr);
        ::execvp(raw.front(), raw.data());

        const int code = errno;
        const ssize_t ignored = ::write(exec_write.get(), &code, sizeof(code));
        (void)ignored;
        ::_exit(127);
    }

    // 父进程：写入 stdin（若有），再关闭写端，等待子进程 exec/chdir 结果。
    if (options.stdin_data.has_value()) {
        in_read.reset();
        sigset_t pipe_set{};
        sigset_t old_set{};
        ::sigemptyset(&pipe_set);
        ::sigaddset(&pipe_set, SIGPIPE);
        const bool blocked =
            ::pthread_sigmask(SIG_BLOCK, &pipe_set, &old_set) == 0;
        const auto &data = *options.stdin_data;
        std::size_t written = 0;
        while (written < data.size()) {
            const ssize_t count = ::write(in_write.get(), data.data() + written,
                                          data.size() - written);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                break;
            }
            written += static_cast<std::size_t>(count);
        }
        if (blocked) {
            timespec zero{0, 0};
            siginfo_t pending{};
            while (::sigtimedwait(&pipe_set, &pending, &zero) > 0) {
            }
            ::pthread_sigmask(SIG_SETMASK, &old_set, nullptr);
        }
        in_write.reset();
    }
    out_write.reset();
    err_write.reset();
    // 关键：关闭父进程持有的写端，否则子进程 exec 成功后 read 等不到 EOF。
    exec_write.reset();

    int exec_errno = 0;
    ssize_t bytes = 0;
    do {
        bytes = ::read(exec_read.get(), &exec_errno, sizeof(exec_errno));
    } while (bytes < 0 && errno == EINTR);
    exec_read.reset();

    if (bytes == static_cast<ssize_t>(sizeof(exec_errno))) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return fail(ErrorCode::kProcessError,
                    "无法执行 '" + argv.front() +
                        "': " + std::strerror(exec_errno));
    }
    if (bytes < 0) {
        kill_and_reap(pid);
        return fail(ErrorCode::kProcessError,
                    std::string("读取 exec 状态失败: ") + std::strerror(errno));
    }

    if (auto status = set_nonblocking(out_read.get()); !status) {
        kill_and_reap(pid);
        return std::unexpected(status.error());
    }
    if (auto status = set_nonblocking(err_read.get()); !status) {
        kill_and_reap(pid);
        return std::unexpected(status.error());
    }

    std::string stdout_data;
    std::string stderr_data;
    std::vector<Capture> captures;
    captures.push_back(Capture{std::move(out_read), &stdout_data, true});
    captures.push_back(Capture{std::move(err_read), &stderr_data, true});

    const bool has_timeout = options.timeout.count() > 0;
    const auto deadline =
        has_timeout ? std::chrono::steady_clock::now() + options.timeout
                    : std::chrono::steady_clock::time_point::max();

    while (captures[0].open || captures[1].open) {
        std::vector<pollfd> poll_fds;
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < captures.size(); ++i) {
            if (captures[i].open) {
                poll_fds.push_back(
                    pollfd{captures[i].fd.get(), POLLIN | POLLHUP, 0});
                indices.push_back(i);
            }
        }

        int timeout_ms = -1;
        if (has_timeout) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now())
                    .count();
            if (remaining <= 0) {
                kill_and_reap(pid);
                return fail(ErrorCode::kProcessTimeout,
                            "命令执行超时: " + argv.front());
            }
            timeout_ms = static_cast<int>(remaining);
        }

        const int ready = ::poll(
            poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), timeout_ms);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            kill_and_reap(pid);
            return fail(ErrorCode::kProcessError,
                        std::string("poll 失败: ") + std::strerror(errno));
        }
        if (ready == 0) {
            kill_and_reap(pid);
            return fail(ErrorCode::kProcessTimeout,
                        "命令执行超时: " + argv.front());
        }

        for (std::size_t i = 0; i < poll_fds.size(); ++i) {
            if ((poll_fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            Capture &capture = captures[indices[i]];
            char buffer[4096];
            while (true) {
                const ssize_t count =
                    ::read(capture.fd.get(), buffer, sizeof(buffer));
                if (count > 0) {
                    capture.data->append(buffer,
                                         static_cast<std::size_t>(count));
                    continue;
                }
                if (count == 0) {
                    capture.open = false;
                    capture.fd.reset();
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                capture.open = false;
                capture.fd.reset();
                break;
            }
        }
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("waitpid 失败: ") + std::strerror(errno));
    }

    ProcessResult result;
    result.exit_code = decode_status(status);
    result.stdout_data = sanitize_utf8(stdout_data);
    result.stderr_data = sanitize_utf8(stderr_data);
    return result;
}

namespace {

/// @brief 打开子进程输出重定向目标。
/// @param file 重定向文件；为空时使用 /dev/null。
/// @param fd 输出参数，接收打开的文件描述符。
/// @return 成功返回空 Status；失败返回 kIoError。
Status open_redirect(const std::optional<std::filesystem::path> &file,
                     int &fd) {
    if (file.has_value()) {
        fd = ::open(file->c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    } else {
        fd = ::open("/dev/null", O_WRONLY);
    }
    if (fd < 0) {
        return fail(ErrorCode::kIoError,
                    std::string("打开输出重定向失败: ") + std::strerror(errno));
    }
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
    return {};
}

} // namespace

namespace core {

/// @brief 移动构造：从 other 转移子进程句柄与退出码。
/// @param other 被移动的源对象。
ChildProcess::ChildProcess(ChildProcess &&other) noexcept
    : pid_(other.pid_), exit_code_(other.exit_code_) {
    other.pid_ = -1;
    other.exit_code_.reset();
}

/// @brief 移动赋值：接管 other 的句柄，必要时先终止当前子进程。
/// @param other 被移动的源对象。
/// @return 当前对象的引用。
ChildProcess &ChildProcess::operator=(ChildProcess &&other) noexcept {
    if (this != &other) {
        if (valid() && !exit_code_.has_value()) {
            (void)kill();
            (void)wait();
        }
        pid_ = other.pid_;
        exit_code_ = other.exit_code_;
        other.pid_ = -1;
        other.exit_code_.reset();
    }
    return *this;
}

/// @brief 析构时终止并回收仍存活的子进程。
ChildProcess::~ChildProcess() {
    if (valid() && !exit_code_.has_value()) {
        (void)send_signal(SIGKILL);
        (void)wait();
    }
}

/// @brief 创建子进程并重定向标准输出与标准错误。
/// @param argv 命令与参数列表，第一项为可执行文件。
/// @param options 启动选项，包括工作目录与输出重定向。
/// @return 创建成功的 ChildProcess；失败时返回对应错误。
Result<ChildProcess> ChildProcess::spawn(const std::vector<std::string> &argv,
                                         const SpawnOptions &options) {
    if (argv.empty() || argv.front().empty()) {
        return fail(ErrorCode::kInvalidArgument, "argv 不能为空");
    }

    int exec_pipe[2] = {-1, -1};
    if (::pipe(exec_pipe) != 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("创建管道失败: ") + std::strerror(errno));
    }
    {
        const int flags = ::fcntl(exec_pipe[1], F_GETFD);
        if (flags >= 0) {
            ::fcntl(exec_pipe[1], F_SETFD, flags | FD_CLOEXEC);
        }
    }

    int stdout_fd = -1;
    int stderr_fd = -1;
    if (auto status = open_redirect(options.stdout_file, stdout_fd); !status) {
        ::close(exec_pipe[0]);
        ::close(exec_pipe[1]);
        return std::unexpected(status.error());
    }
    if (auto status = open_redirect(options.stderr_file, stderr_fd); !status) {
        ::close(exec_pipe[0]);
        ::close(exec_pipe[1]);
        ::close(stdout_fd);
        return std::unexpected(status.error());
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(exec_pipe[0]);
        ::close(exec_pipe[1]);
        ::close(stdout_fd);
        ::close(stderr_fd);
        return fail(ErrorCode::kProcessError,
                    std::string("fork 失败: ") + std::strerror(errno));
    }

    if (pid == 0) {
        if (options.cwd.has_value() && ::chdir(options.cwd->c_str()) != 0) {
            const int code = errno;
            const ssize_t ignored = ::write(exec_pipe[1], &code, sizeof(code));
            (void)ignored;
            ::_exit(126);
        }
        (void)::dup2(stdout_fd, STDOUT_FILENO);
        (void)::dup2(stderr_fd, STDERR_FILENO);
        ::close(stdout_fd);
        ::close(stderr_fd);

        std::vector<char *> raw;
        raw.reserve(argv.size() + 1);
        for (const auto &arg : argv) {
            raw.push_back(const_cast<char *>(arg.c_str()));
        }
        raw.push_back(nullptr);
        ::execvp(raw.front(), raw.data());

        const int code = errno;
        const ssize_t ignored = ::write(exec_pipe[1], &code, sizeof(code));
        (void)ignored;
        ::_exit(127);
    }

    ::close(exec_pipe[1]);
    ::close(stdout_fd);
    ::close(stderr_fd);

    int exec_errno = 0;
    ssize_t bytes = 0;
    do {
        bytes = ::read(exec_pipe[0], &exec_errno, sizeof(exec_errno));
    } while (bytes < 0 && errno == EINTR);
    ::close(exec_pipe[0]);

    if (bytes == static_cast<ssize_t>(sizeof(exec_errno))) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return fail(ErrorCode::kProcessError,
                    "无法执行 '" + argv.front() +
                        "': " + std::strerror(exec_errno));
    }
    if (bytes < 0) {
        ::kill(pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return fail(ErrorCode::kProcessError,
                    std::string("读取 exec 状态失败: ") + std::strerror(errno));
    }

    ChildProcess child;
    child.pid_ = pid;
    return child;
}

/// @brief 非阻塞回收已退出的子进程并记录退出码。
void ChildProcess::reap_nonblocking() noexcept {
    if (!valid() || exit_code_.has_value()) {
        return;
    }
    int status = 0;
    const pid_t reaped = ::waitpid(pid_, &status, WNOHANG);
    if (reaped == pid_) {
        exit_code_ = decode_status(status);
    } else if (reaped < 0 && errno == ECHILD) {
        exit_code_ = -1;
    }
}

/// @brief 判断子进程是否仍在运行，并顺带非阻塞回收。
/// @return 子进程仍在运行时返回 true，否则返回 false。
bool ChildProcess::running() {
    reap_nonblocking();
    return valid() && !exit_code_.has_value();
}

/// @brief 阻塞等待子进程退出。
/// @return 子进程退出码；等待失败时返回错误。
Result<int> ChildProcess::wait() {
    if (exit_code_.has_value()) {
        return *exit_code_;
    }
    if (!valid()) {
        return fail(ErrorCode::kProcessError, "进程无效");
    }
    int status = 0;
    pid_t reaped = -1;
    do {
        reaped = ::waitpid(pid_, &status, 0);
    } while (reaped < 0 && errno == EINTR);
    if (reaped < 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("waitpid 失败: ") + std::strerror(errno));
    }
    exit_code_ = decode_status(status);
    return *exit_code_;
}

/// @brief 在限定时间内等待子进程退出。
/// @param timeout 最大等待时长。
/// @return 已退出时返回退出码；超时返回 kProcessTimeout。
Result<int> ChildProcess::wait_for(std::chrono::milliseconds timeout) {
    if (exit_code_.has_value()) {
        return *exit_code_;
    }
    if (!valid()) {
        return fail(ErrorCode::kProcessError, "进程无效");
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!running()) {
            return *exit_code_;
        }
        ::usleep(10 * 1000);
    }
    if (!running()) {
        return *exit_code_;
    }
    return fail(ErrorCode::kProcessTimeout, "等待进程退出超时");
}

/// @brief 向子进程发送指定信号。
/// @param signal 要发送的信号编号。
/// @return 成功返回空 Status；发送失败返回 kProcessError。
Status ChildProcess::send_signal(int signal) noexcept {
    if (!valid() || exit_code_.has_value()) {
        return {};
    }
    if (::kill(pid_, signal) != 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("发送信号失败: ") + std::strerror(errno));
    }
    return {};
}

} // namespace core

/// @brief 以脱离终端的方式启动后台进程（双重 fork + setsid）。
/// @param argv 命令与参数列表，第一项为可执行文件。
/// @param options 启动选项，包括工作目录与输出重定向。
/// @return 成功返回空 Status；失败返回 kProcessError。
Status spawn_detached(const std::vector<std::string> &argv,
                      const core::SpawnOptions &options) {
    if (argv.empty() || argv.front().empty()) {
        return fail(ErrorCode::kInvalidArgument, "argv 不能为空");
    }

    const pid_t first = ::fork();
    if (first < 0) {
        return fail(ErrorCode::kProcessError,
                    std::string("fork 失败: ") + std::strerror(errno));
    }
    if (first == 0) {
        if (::setsid() < 0) {
            ::_exit(1);
        }
        const pid_t second = ::fork();
        if (second < 0) {
            ::_exit(1);
        }
        if (second > 0) {
            ::_exit(0);
        }

        if (options.cwd.has_value() && ::chdir(options.cwd->c_str()) != 0) {
            ::_exit(126);
        }
        int stdout_fd = -1;
        int stderr_fd = -1;
        if (auto status = open_redirect(options.stdout_file, stdout_fd);
            !status) {
            ::_exit(126);
        }
        if (auto status = open_redirect(options.stderr_file, stderr_fd);
            !status) {
            ::_exit(126);
        }
        (void)::dup2(stdout_fd, STDOUT_FILENO);
        (void)::dup2(stderr_fd, STDERR_FILENO);
        ::close(stdout_fd);
        ::close(stderr_fd);

        std::vector<char *> raw;
        raw.reserve(argv.size() + 1);
        for (const auto &arg : argv) {
            raw.push_back(const_cast<char *>(arg.c_str()));
        }
        raw.push_back(nullptr);
        ::execvp(raw.front(), raw.data());
        ::_exit(127);
    }

    int status = 0;
    while (::waitpid(first, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return fail(ErrorCode::kProcessError, "后台进程启动失败");
    }
    return {};
}

} // namespace lsw

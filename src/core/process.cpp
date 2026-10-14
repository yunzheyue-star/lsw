#include <lsw/core/process.hpp>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lsw {
namespace {

class ScopedFd {
 public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) noexcept : fd_(fd) {}
  ~ScopedFd() { reset(); }
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(other.release()) {}
  ScopedFd& operator=(ScopedFd&& other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }

  int release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }

  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

 private:
  int fd_{-1};
};

Status make_pipe(ScopedFd& read_end, ScopedFd& write_end) {
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

Status set_nonblocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return fail(ErrorCode::kProcessError,
                std::string("设置非阻塞失败: ") + std::strerror(errno));
  }
  return {};
}

void kill_and_reap(pid_t pid) {
  ::kill(pid, SIGKILL);
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
}

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
  std::string* data;
  bool open{true};
};

}  // namespace

Result<ProcessResult> run_process(const std::vector<std::string>& argv,
                                  const RunOptions& options) {
  if (argv.empty() || argv.front().empty()) {
    return fail(ErrorCode::kInvalidArgument, "argv 不能为空");
  }

  ScopedFd out_read;
  ScopedFd out_write;
  ScopedFd err_read;
  ScopedFd err_write;
  ScopedFd exec_read;
  ScopedFd exec_write;
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
    if (options.cwd.has_value() && ::chdir(options.cwd->c_str()) != 0) {
      const int code = errno;
      const ssize_t ignored = ::write(exec_write.get(), &code, sizeof(code));
      (void)ignored;
      ::_exit(126);
    }
    (void)::dup2(out_write.get(), STDOUT_FILENO);
    (void)::dup2(err_write.get(), STDERR_FILENO);

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
      raw.push_back(const_cast<char*>(arg.c_str()));
    }
    raw.push_back(nullptr);
    ::execvp(raw.front(), raw.data());

    const int code = errno;
    const ssize_t ignored = ::write(exec_write.get(), &code, sizeof(code));
    (void)ignored;
    ::_exit(127);
  }

  // 父进程：关闭写端，读取子进程 exec/chdir 结果（EOF = exec 成功）。
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
                "无法执行 '" + argv.front() + "': " + std::strerror(exec_errno));
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
  const auto deadline = has_timeout
                            ? std::chrono::steady_clock::now() + options.timeout
                            : std::chrono::steady_clock::time_point::max();

  while (captures[0].open || captures[1].open) {
    std::vector<pollfd> poll_fds;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < captures.size(); ++i) {
      if (captures[i].open) {
        poll_fds.push_back(pollfd{captures[i].fd.get(), POLLIN | POLLHUP, 0});
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

    const int ready =
        ::poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), timeout_ms);
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
      return fail(ErrorCode::kProcessTimeout, "命令执行超时: " + argv.front());
    }

    for (std::size_t i = 0; i < poll_fds.size(); ++i) {
      if ((poll_fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        continue;
      }
      Capture& capture = captures[indices[i]];
      char buffer[4096];
      while (true) {
        const ssize_t count = ::read(capture.fd.get(), buffer, sizeof(buffer));
        if (count > 0) {
          capture.data->append(buffer, static_cast<std::size_t>(count));
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
  result.stdout_data = std::move(stdout_data);
  result.stderr_data = std::move(stderr_data);
  return result;
}

}  // namespace lsw

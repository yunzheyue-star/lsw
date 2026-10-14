#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <lsw/core/error.hpp>

namespace lsw {

struct RunOptions {
  std::optional<std::filesystem::path> cwd;  ///< 子进程工作目录
  std::chrono::milliseconds timeout{0};      ///< 0 表示不设超时
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
[[nodiscard]] Result<ProcessResult> run_process(
    const std::vector<std::string>& argv, const RunOptions& options = {});

}  // namespace lsw

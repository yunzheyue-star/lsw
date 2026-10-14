#pragma once

#include <filesystem>
#include <memory>

#include <spdlog/spdlog.h>

#include <lsw/core/error.hpp>

namespace lsw::log {

/// 初始化日志：写入 rotating 文件，并注册为 spdlog 默认 logger。
/// 失败返回 kIoError（目录不可创建、文件不可写等）。
[[nodiscard]] Status
init(const std::filesystem::path &log_file,
     spdlog::level::level_enum level = spdlog::level::info);

/// 获取 LSW logger；未初始化时返回 spdlog 默认 logger。
[[nodiscard]] std::shared_ptr<spdlog::logger> logger();

/// 刷新并释放 logger，主要供测试与进程退出使用。
void shutdown() noexcept;

} // namespace lsw::log

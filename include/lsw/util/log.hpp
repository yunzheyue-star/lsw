/**
 * @file log.hpp
 * @brief spdlog 日志初始化、访问与关闭。
 * @details 文件位置：include/lsw/util/log.hpp（头文件）。
 *          属于 LSW 工具库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#pragma once

#include <filesystem>
#include <memory>

#include <spdlog/spdlog.h>

#include <lsw/core/error.hpp>

namespace lsw::log {

/// @brief 初始化日志：写入 rotating 文件，并注册为 spdlog 默认 logger。
/// 失败返回 kIoError（目录不可创建、文件不可写等）。
/// @param log_file 日志文件路径。
/// @param level 日志级别，默认为 info。
/// @return 初始化成功返回成功状态，失败返回 kIoError。
[[nodiscard]] Status
init(const std::filesystem::path &log_file,
     spdlog::level::level_enum level = spdlog::level::info);

/// @brief 获取 LSW logger；未初始化时返回 spdlog 默认 logger。
/// @return logger 共享指针。
[[nodiscard]] std::shared_ptr<spdlog::logger> logger();

/// @brief 刷新并释放 logger，主要供测试与进程退出使用。
void shutdown() noexcept;

} // namespace lsw::log

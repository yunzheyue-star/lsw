/**
 * @file log.cpp
 * @brief log 实现（工具库）。
 * @details 文件位置：src/util/log.cpp（源文件）。
 *          属于 LSW 工具库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <lsw/util/log.hpp>

#include <memory>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>

namespace lsw::log {
namespace {

std::shared_ptr<spdlog::logger> g_logger;

} // namespace

/// @brief 初始化日志：创建 rotating 文件 sink 并注册为默认 logger。
/// @param log_file 日志文件路径。
/// @param level 日志级别。
/// @return 成功返回空 Status；目录或文件不可用时返回 kIoError。
Status init(const std::filesystem::path &log_file,
            spdlog::level::level_enum level) {
    try {
        if (!log_file.parent_path().empty()) {
            std::filesystem::create_directories(log_file.parent_path());
        }

        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file.string(), 1024 * 1024, 3);
        auto logger = std::make_shared<spdlog::logger>("lsw", std::move(sink));
        logger->set_level(level);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger->flush_on(spdlog::level::warn);

        g_logger = std::move(logger);
        spdlog::set_default_logger(g_logger);
        return {};
    } catch (const spdlog::spdlog_ex &error) {
        return fail(ErrorCode::kIoError,
                    std::string("初始化日志失败: ") + error.what());
    } catch (const std::filesystem::filesystem_error &error) {
        return fail(ErrorCode::kIoError,
                    std::string("创建日志目录失败: ") + error.what());
    }
}

/// @brief 获取 LSW logger；未初始化时回退到 spdlog 默认 logger。
/// @return 日志记录器共享指针。
std::shared_ptr<spdlog::logger> logger() {
    if (g_logger) {
        return g_logger;
    }
    return spdlog::default_logger();
}

/// @brief 刷新并释放全局 logger，主要供测试与进程退出使用。
void shutdown() noexcept {
    if (g_logger) {
        g_logger->flush();
    }
    spdlog::shutdown();
    g_logger.reset();
}

} // namespace lsw::log

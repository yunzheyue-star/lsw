#include <lsw/util/log.hpp>

#include <memory>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>

namespace lsw::log {
namespace {

std::shared_ptr<spdlog::logger> g_logger;

}  // namespace

Status init(const std::filesystem::path& log_file, spdlog::level::level_enum level) {
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
  } catch (const spdlog::spdlog_ex& error) {
    return fail(ErrorCode::kIoError,
                std::string("初始化日志失败: ") + error.what());
  } catch (const std::filesystem::filesystem_error& error) {
    return fail(ErrorCode::kIoError,
                std::string("创建日志目录失败: ") + error.what());
  }
}

std::shared_ptr<spdlog::logger> logger() {
  if (g_logger) {
    return g_logger;
  }
  return spdlog::default_logger();
}

void shutdown() noexcept {
  if (g_logger) {
    g_logger->flush();
  }
  spdlog::shutdown();
  g_logger.reset();
}

}  // namespace lsw::log

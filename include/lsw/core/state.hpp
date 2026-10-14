#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <lsw/core/error.hpp>

namespace lsw::core {

/// lswd 的内存状态机（P2 骨架）。
/// P3 起由 QEMU/QMP 驱动 vm_status 与 mounts；本阶段保证命令幂等与线程安全。
class DaemonState {
 public:
  explicit DaemonState(std::string version = "0.1.0");

  /// 分发 IPC 命令：status/start/shutdown/mount/unmount/shell/exec。
  [[nodiscard]] Result<nlohmann::json> handle(
      const std::string& command,
      const nlohmann::json& args = nlohmann::json::object());

 private:
  [[nodiscard]] Result<nlohmann::json> on_start();
  [[nodiscard]] Result<nlohmann::json> on_shutdown();
  [[nodiscard]] Result<nlohmann::json> on_mount();
  [[nodiscard]] Result<nlohmann::json> on_unmount();
  [[nodiscard]] Result<nlohmann::json> on_status() const;

  mutable std::mutex mutex_;
  bool vm_running_{false};
  std::vector<std::string> mounts_;
  std::string version_;
  std::chrono::steady_clock::time_point started_at_{
      std::chrono::steady_clock::now()};
};

}  // namespace lsw::core

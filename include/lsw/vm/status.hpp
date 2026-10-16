/**
 * @file status.hpp
 * @brief `lsw status` 的离线检查、guest 信息缓存与运行时探测。
 * @details 文件位置：include/lsw/vm/status.hpp（头文件）。
 *          属于 LSW 虚拟机管理；公共行为以 docs/interfaces/cli.md 与
 *          docs/interfaces/daemon-ipc.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/util/paths.hpp>

namespace lsw::vm {

/// @brief 缓存的 Windows guest 信息。
struct GuestStatus {
    bool valid{false};                 ///< 是否存在有效缓存
    std::string hostname;              ///< Windows 主机名
    std::string virtio_win{"unknown"}; ///< installed / not_installed / unknown
    std::string source{"none"};        ///< runtime_probe / cache / none
    std::int64_t checked_at{0};        ///< Unix 秒
};

/// @brief `lsw --install` 完成标记。
struct InstallMarker {
    bool exists{false};
    std::string disk;
    std::string windows_iso;
    std::string virtio_iso;
    std::int64_t completed_at{0};
};

/// @brief 读取缓存的 guest 信息。
/// @param paths XDG 路径。
/// @return 成功返回缓存；不存在/损坏返回 valid=false。
[[nodiscard]] Result<GuestStatus> load_guest_status(const XdgPaths &paths);

/// @brief 写入 guest 信息缓存（`$STATE/lsw/guest.json`）。
/// @param paths XDG 路径。
/// @param status 待写入状态。
/// @return 写入结果。
[[nodiscard]] Status save_guest_status(const XdgPaths &paths,
                                       const GuestStatus &status);

/// @brief 读取安装完成标记（`$STATE/lsw/install.json`）。
/// @param paths XDG 路径。
/// @return 成功返回标记；不存在返回 exists=false。
[[nodiscard]] Result<InstallMarker> load_install_marker(const XdgPaths &paths);

/// @brief 写入安装完成标记。
/// @param paths XDG 路径。
/// @param marker 待写入标记。
/// @return 写入结果。
[[nodiscard]] Status save_install_marker(const XdgPaths &paths,
                                         const InstallMarker &marker);

/// @brief 通过 SSH 探测 Windows 主机名与 virtio 驱动服务。
/// @param config 全局配置（SSH 端口、Windows 用户名）。
/// @param ssh_binary ssh 可执行文件。
/// @param timeout SSH 执行超时。
/// @return 成功返回 guest 状态；SSH 失败返回 kSshFailed。
[[nodiscard]] Result<GuestStatus>
probe_guest_status(const Config &config, std::string_view ssh_binary,
                   std::chrono::milliseconds timeout);

/// @brief 生成离线状态 JSON（不启动 daemon、不要求 VM 运行）。
/// @param config 全局配置。
/// @param paths XDG 路径。
/// @return 包含磁盘、安装标记、guest 缓存的状态 JSON。
[[nodiscard]] nlohmann::json offline_status_json(const Config &config,
                                                 const XdgPaths &paths);

} // namespace lsw::vm

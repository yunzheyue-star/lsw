/**
 * @file host_smb.hpp
 * @brief 宿主 root Samba 共享管理：为 guest 提供 SMB2/3 的 Z: 数据源。
 * @details 文件位置：include/lsw/fs/host_smb.hpp（头文件）。
 *          属于 LSW 文件系统模块；通过 sudo/pkexec helper 在宿主机
 *          127.0.0.1:445 启动 root smbd，guest 经 QEMU user-net 网关
 *          10.0.2.2 访问，支持任意多 SSH/登录会话。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <lsw/core/error.hpp>
#include <lsw/core/process.hpp>

#ifndef LSW_INSTALL_HELPER_PATH
#define LSW_INSTALL_HELPER_PATH "/usr/local/lib/lsw/lsw-mount-helper"
#endif

namespace lsw::winfs {

/// @brief 宿主 root Samba 共享管理选项。
struct HostSmbOptions {
    std::filesystem::path helper_path{LSW_INSTALL_HELPER_PATH}; ///< 提权 helper
    std::string privilege{"sudo"};            ///< sudo / pkexec / none
    std::filesystem::path home;               ///< 共享目录；空 = 当前 $HOME
    std::string share{"qemu"};                ///< guest 侧共享名
    std::string user;                         ///< Linux 用户；空 = 当前用户
    std::chrono::milliseconds timeout{30000}; ///< helper 执行超时
};

/// @brief 宿主 root Samba 共享的 RAII 管理器。
class HostSmbManager {
  public:
    /// @brief 构造管理器。
    /// @param options 管理选项。
    HostSmbManager(HostSmbOptions options = {});

    /// @brief 析构时停止 root smbd（幂等）。
    ~HostSmbManager();

    HostSmbManager(const HostSmbManager &) = delete;
    HostSmbManager &operator=(const HostSmbManager &) = delete;
    HostSmbManager(HostSmbManager &&other) noexcept;
    HostSmbManager &operator=(HostSmbManager &&other) noexcept;

    /// @brief 调用 helper `smb_start` 启动 root smbd。
    /// @return 成功返回空结果；失败返回权限/依赖错误。
    [[nodiscard]] Status start();

    /// @brief 调用 helper `smb_stop` 停止 root smbd（幂等）。
    void stop() noexcept;

    /// @brief 查询管理器是否认为服务已启动。
    /// @return 已启动返回 true。
    [[nodiscard]] bool running() const noexcept { return started_; }

  private:
    /// @brief 调用 helper 执行子命令。
    /// @param args helper 子命令及参数。
    /// @return helper 执行结果。
    [[nodiscard]] Result<ProcessResult>
    run_helper(const std::vector<std::string> &args) const;

    HostSmbOptions options_;
    bool started_{false};
};

} // namespace lsw::winfs

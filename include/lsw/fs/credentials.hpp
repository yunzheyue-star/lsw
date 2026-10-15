/**
 * @file credentials.hpp
 * @brief Windows SMB/SSH 凭据的 keyring 存取接口。
 * @details 文件位置：include/lsw/fs/credentials.hpp（头文件）。
 *          属于 LSW 文件系统模块；公共行为以 docs/interfaces/filesystem.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <lsw/core/error.hpp>

namespace lsw::winfs {

/// @brief Windows 用户凭据（内存中短暂存在，禁止落盘或写入日志）。
struct WindowsCredential {
    std::string username; ///< Windows 登录用户名。
    std::string password; ///< Windows 登录密码，仅允许通过 stdin/内存传递。
};

/// @brief 凭据存储选项。
struct CredentialOptions {
    std::string service{"lsw"}; ///< keyring 中使用的 service 标签。
    std::string secret_tool_binary{"secret-tool"}; ///< libsecret 命令行工具。
    std::chrono::milliseconds timeout{5000};       ///< 单次 keyring 操作超时。
};

/// @brief 基于 libsecret（secret-tool）的 Windows 凭据存取。
/// - 密码只存放在系统 keyring，绝不写入配置文件或日志；
/// - secret-tool 不存在时返回 kMissingDependency，由上层引导交互输入。
class CredentialStore {
  public:
    /// @brief 构造凭据存储。
    /// @param options 存储选项；默认使用 secret-tool 与 service=lsw。
    explicit CredentialStore(CredentialOptions options = {});

    /// @brief 判断 secret-tool 是否可用。
    /// @return 可执行时返回 true，否则 false。
    [[nodiscard]] bool available() const;

    /// @brief 查询指定 Windows 用户的凭据。
    /// @param username Windows 用户名。
    /// @return keyring 中存在时返回凭据；不存在返回
    /// nullopt；工具或执行错误返回错误。
    [[nodiscard]] Result<std::optional<WindowsCredential>>
    lookup(std::string_view username) const;

    /// @brief 将凭据写入系统 keyring。
    /// @param credential 待保存的凭据；密码通过 stdin 传给 secret-tool。
    /// @return 成功返回空状态，失败返回 kMountFailed 或 kMissingDependency。
    [[nodiscard]] Status store(const WindowsCredential &credential) const;

    /// @brief 从系统 keyring 删除指定用户凭据。
    /// @param username Windows 用户名。
    /// @return 成功返回空状态，失败返回错误。
    [[nodiscard]] Status erase(std::string_view username) const;

  private:
    /// @brief 解析 secret-tool 的实际路径。
    /// @return 可执行文件路径；找不到时返回空字符串。
    [[nodiscard]] std::string resolve_binary() const;

    CredentialOptions options_; ///< 凭据存储配置。
};

/// @brief 从环境变量读取一次性凭据（LSW_WINDOWS_USER / LSW_WINDOWS_PASSWORD）。
/// @param default_user 未设置 LSW_WINDOWS_USER 时使用的默认用户名。
/// @return 设置了非空密码时返回凭据，否则返回 nullopt。
[[nodiscard]] std::optional<WindowsCredential>
credential_from_environment(std::string_view default_user);

} // namespace lsw::winfs

/**
 * @file credentials.cpp
 * @brief Windows 凭据 keyring 存取实现。
 * @details 文件位置：src/fs/credentials.cpp（源文件）。
 *          属于 LSW 文件系统模块；公共行为以 docs/interfaces/filesystem.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/fs/credentials.hpp>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <utility>

#include <lsw/core/process.hpp>
#include <lsw/util/paths.hpp>

namespace lsw::winfs {
namespace {

/// @brief 去掉字符串结尾的换行符。
/// @param text 待处理的字符串。
/// @return 去掉结尾 \n 与 \r 后的字符串。
std::string trim_newline(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

} // namespace

/// @brief 构造凭据存储。
/// @param options 存储选项；默认使用 secret-tool 与 service=lsw。
CredentialStore::CredentialStore(CredentialOptions options)
    : options_(std::move(options)) {}

/// @brief 解析 secret-tool 的实际路径。
/// @return 可执行文件路径；找不到时返回空字符串。
std::string CredentialStore::resolve_binary() const {
    if (options_.secret_tool_binary.find('/') != std::string::npos) {
        return ::access(options_.secret_tool_binary.c_str(), X_OK) == 0
                   ? options_.secret_tool_binary
                   : std::string{};
    }
    return find_executable(options_.secret_tool_binary);
}

/// @brief 判断 secret-tool 是否可用。
/// @return 可执行时返回 true，否则 false。
bool CredentialStore::available() const { return !resolve_binary().empty(); }

/// @brief 查询指定 Windows 用户的凭据。
/// @param username Windows 用户名。
/// @return keyring 中存在时返回凭据；不存在返回
/// nullopt；工具或执行错误返回错误。
Result<std::optional<WindowsCredential>>
CredentialStore::lookup(std::string_view username) const {
    const std::string binary = resolve_binary();
    if (binary.empty()) {
        return fail(ErrorCode::kMissingDependency,
                    "secret-tool 未安装（libsecret）：请安装 libsecret 后重试，"
                    "或通过环境变量 LSW_WINDOWS_PASSWORD 提供一次性密码");
    }
    const std::string user(username);
    const auto result = run_process(
        {binary, "lookup", "service", options_.service, "account", user},
        RunOptions{.timeout = options_.timeout});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code == 1) {
        return std::optional<WindowsCredential>{};
    }
    if (result->exit_code != 0) {
        return fail(ErrorCode::kMountFailed,
                    "secret-tool lookup 失败（exit " +
                        std::to_string(result->exit_code) +
                        "）: " + trim_newline(result->stderr_data));
    }
    std::string password = trim_newline(result->stdout_data);
    if (password.empty()) {
        return std::optional<WindowsCredential>{};
    }
    return std::optional<WindowsCredential>{
        WindowsCredential{user, std::move(password)}};
}

/// @brief 将凭据写入系统 keyring。
/// @param credential 待保存的凭据；密码通过 stdin 传给 secret-tool。
/// @return 成功返回空状态，失败返回 kMountFailed 或 kMissingDependency。
Status CredentialStore::store(const WindowsCredential &credential) const {
    const std::string binary = resolve_binary();
    if (binary.empty()) {
        return fail(ErrorCode::kMissingDependency,
                    "secret-tool 未安装（libsecret），无法保存凭据");
    }
    const std::string label = "LSW Windows (" + credential.username + ")";
    const auto result =
        run_process({binary, "store", "--label=" + label, "service",
                     options_.service, "account", credential.username},
                    RunOptions{.timeout = options_.timeout,
                               .stdin_data = credential.password + "\n"});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        return fail(ErrorCode::kMountFailed,
                    "secret-tool store 失败（exit " +
                        std::to_string(result->exit_code) +
                        "）: " + trim_newline(result->stderr_data));
    }
    return {};
}

/// @brief 从系统 keyring 删除指定用户凭据。
/// @param username Windows 用户名。
/// @return 成功返回空状态，失败返回错误。
Status CredentialStore::erase(std::string_view username) const {
    const std::string binary = resolve_binary();
    if (binary.empty()) {
        return fail(ErrorCode::kMissingDependency,
                    "secret-tool 未安装（libsecret），无法删除凭据");
    }
    const auto result =
        run_process({binary, "clear", "service", options_.service, "account",
                     std::string(username)},
                    RunOptions{.timeout = options_.timeout});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        return fail(ErrorCode::kMountFailed,
                    "secret-tool clear 失败（exit " +
                        std::to_string(result->exit_code) + "）");
    }
    return {};
}

/// @brief 从环境变量读取一次性凭据（LSW_WINDOWS_USER / LSW_WINDOWS_PASSWORD）。
/// @param default_user 未设置 LSW_WINDOWS_USER 时使用的默认用户名。
/// @return 设置了非空密码时返回凭据，否则返回 nullopt。
std::optional<WindowsCredential>
credential_from_environment(std::string_view default_user) {
    const char *password = std::getenv("LSW_WINDOWS_PASSWORD");
    if (password == nullptr || *password == '\0') {
        return std::nullopt;
    }
    const char *user = std::getenv("LSW_WINDOWS_USER");
    return WindowsCredential{(user != nullptr && *user != '\0')
                                 ? std::string(user)
                                 : std::string(default_user),
                             password};
}

} // namespace lsw::winfs

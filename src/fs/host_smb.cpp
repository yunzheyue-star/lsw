/**
 * @file host_smb.cpp
 * @brief 宿主 root Samba 共享管理实现。
 * @details 文件位置：src/fs/host_smb.cpp（源文件）。
 *          通过提权 helper 启动/停止监听 127.0.0.1:445 的 root smbd，
 *          共享 `$HOME`；guest 经 QEMU user-net 网关 10.0.2.2 访问。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <lsw/fs/host_smb.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#include <pwd.h>
#include <unistd.h>

#include <lsw/core/process.hpp>
#include <lsw/util/log.hpp>

namespace lsw::winfs {
namespace {

/// @brief 查询当前用户名。
/// @return 用户名；失败时返回 "nobody"。
std::string current_user() {
    const passwd *entry = ::getpwuid(::getuid());
    if (entry == nullptr || entry->pw_name == nullptr) {
        return "nobody";
    }
    return entry->pw_name;
}

/// @brief 去除字符串尾部换行。
/// @param text 输入文本。
/// @return 去除尾部 \n/\r 的文本。
std::string trim_tail(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

} // namespace

/** @brief 构造管理器。
 * @param options 管理选项。
 */
HostSmbManager::HostSmbManager(HostSmbOptions options)
    : options_(std::move(options)) {}

/** @brief 析构时停止 root smbd。 */
HostSmbManager::~HostSmbManager() { stop(); }

/** @brief 移动构造。 */
HostSmbManager::HostSmbManager(HostSmbManager &&other) noexcept = default;

/** @brief 移动赋值。 */
HostSmbManager &
HostSmbManager::operator=(HostSmbManager &&other) noexcept = default;

/** @brief 调用 helper 执行子命令。 */
Result<ProcessResult>
HostSmbManager::run_helper(const std::vector<std::string> &args) const {
    if (options_.helper_path.empty() ||
        !std::filesystem::exists(options_.helper_path)) {
        return fail(ErrorCode::kMissingDependency,
                    "找不到 host SMB helper：" + options_.helper_path.string() +
                        "；请安装 scripts/lsw-mount-helper");
    }
    if (::access(options_.helper_path.c_str(), X_OK) != 0) {
        return fail(ErrorCode::kPermissionDenied,
                    "host SMB helper 不可执行：" +
                        options_.helper_path.string());
    }
    std::vector<std::string> argv;
    if (options_.privilege == "sudo") {
        argv = {"sudo", "-n", options_.helper_path.string()};
    } else if (options_.privilege == "pkexec") {
        argv = {"pkexec", options_.helper_path.string()};
    } else if (options_.privilege.empty() || options_.privilege == "none") {
        argv = {options_.helper_path.string()};
    } else {
        return fail(ErrorCode::kInvalidArgument,
                    "未知提权方式: " + options_.privilege);
    }
    argv.insert(argv.end(), args.begin(), args.end());
    return run_process(argv, RunOptions{.timeout = options_.timeout});
}

/** @brief 启动 root smbd。 */
Status HostSmbManager::start() {
    if (started_) {
        return {};
    }
    std::filesystem::path home = options_.home;
    if (home.empty()) {
        if (const char *env = std::getenv("HOME"); env != nullptr) {
            home = env;
        }
    }
    if (home.empty()) {
        return fail(ErrorCode::kInvalidArgument, "无法确定 host SMB 共享目录");
    }
    const std::string user =
        options_.user.empty() ? current_user() : options_.user;
    auto result =
        run_helper({"smb_start", home.string(), options_.share, user});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        std::string detail = trim_tail(result->stderr_data);
        if (detail.empty()) {
            detail = trim_tail(result->stdout_data);
        }
        const ErrorCode code =
            (result->exit_code == 126 || result->exit_code == 127 ||
             detail.find("password is required") != std::string::npos)
                ? ErrorCode::kPermissionDenied
                : ErrorCode::kShareSetupFailed;
        return fail(code, "启动 host SMB 失败（exit " +
                              std::to_string(result->exit_code) +
                              "）: " + detail);
    }
    started_ = true;
    log::logger()->info("host SMB 已就绪：127.0.0.1:445 -> {} (share {})",
                        home.string(), options_.share);
    return {};
}

/** @brief 停止 root smbd（幂等）。 */
void HostSmbManager::stop() noexcept {
    if (!started_) {
        return;
    }
    auto result = run_helper({"smb_stop"});
    if (!result || result->exit_code != 0) {
        log::logger()->warn("停止 host SMB 失败（可忽略）");
    }
    started_ = false;
}

} // namespace lsw::winfs

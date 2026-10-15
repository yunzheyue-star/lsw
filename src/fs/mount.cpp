/**
 * @file mount.cpp
 * @brief CIFS 挂载/卸载与异常恢复实现。
 * @details 文件位置：src/fs/mount.cpp（源文件）。
 *          属于 LSW 文件系统模块；公共行为以 docs/interfaces/filesystem.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/fs/mount.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include <lsw/core/process.hpp>
#include <lsw/util/log.hpp>

namespace lsw::winfs {
namespace {

/// @brief 判断路径是否位于指定根目录之下。
/// @param path 待判断路径。
/// @param root 根目录。
/// @return 位于根目录之下（含根本身）返回 true，否则 false。
bool is_under(const std::filesystem::path &path,
              const std::filesystem::path &root) {
    const auto normalized_path = path.lexically_normal();
    const auto normalized_root = root.lexically_normal();
    const auto relative = normalized_path.lexically_relative(normalized_root);
    return !relative.empty() && *relative.begin() != "..";
}

/// @brief 判断 mount.cifs 错误是否表示共享/盘不存在。
/// @param message 错误消息。
/// @return 表示共享不存在返回 true。
bool is_missing_share_error(const std::string &message) {
    return message.find("mount error(2)") != std::string::npos ||
           message.find("NT_STATUS_BAD_NETWORK_NAME") != std::string::npos ||
           message.find("No such file or directory") != std::string::npos;
}

/// @brief 从挂载点/源路径推断盘符。
/// @param mount_point Linux 挂载点。
/// @param source CIFS 源路径。
/// @return 盘符字符；无法推断时返回 '\0'。
char detect_letter(const std::filesystem::path &mount_point,
                   const std::filesystem::path &source) {
    const std::string name = mount_point.filename().string();
    if (name.size() == 1) {
        return name.front();
    }
    const std::string source_text = source.string();
    const auto marker = source_text.find("$");
    if (marker != std::string::npos && marker > 0) {
        return source_text[marker - 1];
    }
    return '\0';
}

} // namespace

/// @brief 构造 CIFS 挂载管理器。
/// @param config 全局配置（网络端口、挂载根、用户名）。
/// @param options 挂载选项。
CifsMountManager::CifsMountManager(Config config, MountOptions options)
    : config_(std::move(config)), options_(std::move(options)) {}

/// @brief 构造 helper 参数并执行。
/// @param helper_args helper 子命令及其参数（不含 helper 路径与提权）。
/// @param stdin_data 需要写入 helper stdin 的数据（密码）。
/// @return 成功返回空状态，失败返回错误。
Status CifsMountManager::run_helper(
    const std::vector<std::string> &helper_args,
    const std::optional<std::string> &stdin_data) const {
    if (options_.helper_path.empty() ||
        !std::filesystem::exists(options_.helper_path)) {
        return fail(ErrorCode::kMissingDependency,
                    "找不到挂载 helper：" + options_.helper_path.string() +
                        "；请安装 scripts/lsw-mount-helper 并配置 "
                        "sudo/polkit 规则");
    }
    if (::access(options_.helper_path.c_str(), X_OK) != 0) {
        return fail(ErrorCode::kPermissionDenied,
                    "挂载 helper 不可执行：" + options_.helper_path.string());
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
    argv.insert(argv.end(), helper_args.begin(), helper_args.end());

    const auto result =
        run_process(argv, RunOptions{.timeout = options_.timeout,
                                     .stdin_data = stdin_data});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        std::string detail = result->stderr_data;
        if (detail.empty()) {
            detail = result->stdout_data;
        }
        while (!detail.empty() &&
               (detail.back() == '\n' || detail.back() == '\r')) {
            detail.pop_back();
        }
        if (result->exit_code == 126 || result->exit_code == 127 ||
            detail.find("password is required") != std::string::npos) {
            return fail(ErrorCode::kPermissionDenied,
                        "挂载 helper 提权失败（exit " +
                            std::to_string(result->exit_code) +
                            "）: " + detail);
        }
        return fail(ErrorCode::kMountFailed,
                    "挂载 helper 失败（exit " +
                        std::to_string(result->exit_code) + "）: " + detail);
    }
    return {};
}

/// @brief 列出挂载根下当前残留的 CIFS 挂载点。
/// @return 挂载点列表；读取挂载表失败返回错误。
Result<std::vector<MountedDrive>> CifsMountManager::mounted_drives() const {
    std::ifstream input(options_.mounts_file);
    if (!input) {
        return fail(ErrorCode::kMountFailed,
                    "无法读取挂载表: " + options_.mounts_file.string());
    }
    std::vector<MountedDrive> drives;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string source;
        std::string mount_point;
        std::string fs_type;
        if (!(stream >> source >> mount_point >> fs_type)) {
            continue;
        }
        if (fs_type.rfind("cifs", 0) != 0 && fs_type.rfind("smb", 0) != 0) {
            continue;
        }
        const std::filesystem::path point(mount_point);
        if (!is_under(point, options_.mount_root)) {
            continue;
        }
        drives.push_back(MountedDrive{detect_letter(point, source), source,
                                      point.lexically_normal()});
    }
    return drives;
}

/// @brief 检查挂载点是否已在挂载表中且类型为 CIFS。
/// @param mount_point 待检查的挂载点。
/// @return 已挂载返回 true，否则 false。
bool CifsMountManager::is_mounted(
    const std::filesystem::path &mount_point) const {
    const auto drives = mounted_drives();
    if (!drives) {
        return false;
    }
    const auto normalized = mount_point.lexically_normal();
    for (const auto &drive : *drives) {
        if (drive.mount_point == normalized) {
            return true;
        }
    }
    return false;
}

/// @brief 挂载单个盘符。
/// @param letter 盘符字符（C/D）。
/// @param credential Windows SMB 凭据。
/// @return 成功返回空状态，失败返回错误。
Status CifsMountManager::mount_drive(char letter,
                                     const WindowsCredential &credential) {
    const std::string drive(1, letter);
    const std::filesystem::path mount_point = options_.mount_root / drive;
    const std::string source = "//127.0.0.1/" + drive + "$";
    const std::string port = std::to_string(config_.network.smb_host_port);
    return run_helper({"mount", drive, source, mount_point.string(), port,
                       credential.username, options_.cifs_version},
                      credential.password + "\n");
}

/// @brief 卸载指定挂载点。
/// @param mount_point Linux 挂载点。
/// @return 成功返回空状态，失败返回错误。
Status
CifsMountManager::unmount_point(const std::filesystem::path &mount_point) {
    return run_helper({"unmount", mount_point.string()}, std::nullopt);
}

/// @brief 确保配置中的 Windows 盘全部挂载（幂等，失败自动重挂一次）。
/// @param credential Windows SMB 凭据；密码只通过 stdin 传给 helper。
/// @return 成功返回挂载点字符串列表；失败返回 kMountFailed /
/// kMissingDependency / kPermissionDenied。
Result<std::vector<std::string>>
CifsMountManager::ensure_mounts(const WindowsCredential &credential) {
    std::vector<std::string> result;
    for (const char letter : options_.drives) {
        const std::filesystem::path mount_point =
            options_.mount_root / std::string(1, letter);
        if (is_mounted(mount_point)) {
            result.push_back(mount_point.string());
            continue;
        }
        if (auto mounted = mount_drive(letter, credential); mounted) {
            result.push_back(mount_point.string());
            continue;
        }
        // 异常恢复：清理可能残留的挂载后再挂一次。
        (void)unmount_point(mount_point);
        auto retried = mount_drive(letter, credential);
        if (!retried) {
            // LSW 默认只有一个系统盘（C:），D: 可能不存在：
            // 非 C 盘遇到「共享不存在」时跳过，保证 C 盘可用。
            if (letter != 'C' &&
                is_missing_share_error(retried.error().message)) {
                log::logger()->warn("Windows 盘 {}: 不存在，已跳过挂载",
                                    std::string(1, letter));
                continue;
            }
            return std::unexpected(retried.error());
        }
        result.push_back(mount_point.string());
    }
    return result;
}

/// @brief 卸载挂载根下所有 CIFS 挂载点（幂等，shutdown 兜底）。
/// @return 全部成功或已无挂载返回空状态；否则返回 kUnmountFailed。
Status CifsMountManager::unmount_all() {
    const auto drives = mounted_drives();
    if (!drives) {
        return std::unexpected(drives.error());
    }
    std::optional<Error> last_error;
    for (const auto &drive : *drives) {
        if (auto status = unmount_point(drive.mount_point); !status) {
            last_error = status.error();
        }
    }
    if (last_error.has_value()) {
        Error error = *last_error;
        error.code = ErrorCode::kUnmountFailed;
        return std::unexpected(std::move(error));
    }
    return {};
}

/// @brief 异常恢复：先卸载残留挂载，再重新挂载全部盘符。
/// @param credential Windows SMB 凭据。
/// @return 成功返回挂载点字符串列表；失败返回错误。
Result<std::vector<std::string>>
CifsMountManager::recover(const WindowsCredential &credential) {
    if (auto cleaned = unmount_all(); !cleaned) {
        return std::unexpected(cleaned.error());
    }
    return ensure_mounts(credential);
}

} // namespace lsw::winfs

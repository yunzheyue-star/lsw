/**
 * @file status.cpp
 * @brief `lsw status` 状态检查与 guest 信息探测实现。
 * @details 文件位置：src/vm/status.cpp（源文件）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <lsw/vm/status.hpp>

#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>

#include <lsw/core/process.hpp>
#include <lsw/shell/powershell.hpp>

namespace lsw::vm {
namespace {

/// @brief 当前 Unix 秒。
/// @return 当前时间戳（秒）。
std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// @brief 读取 JSON 文件；不存在或损坏返回空。
/// @param path 文件路径。
/// @return JSON 对象；失败返回 null。
nlohmann::json read_json_file(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) {
        return nlohmann::json{};
    }
    nlohmann::json value;
    try {
        input >> value;
    } catch (const nlohmann::json::exception &) {
        return nlohmann::json{};
    }
    return value;
}

/// @brief 写入 JSON 文件（先建目录）。
/// @param path 文件路径。
/// @param value JSON 内容。
/// @return 写入结果。
Status write_json_file(const std::filesystem::path &path,
                       const nlohmann::json &value) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return fail(ErrorCode::kIoError,
                    "创建状态目录失败: " + error.message());
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return fail(ErrorCode::kIoError, "无法写入状态文件: " + path.string());
    }
    output << value.dump(2) << "\n";
    if (!output) {
        return fail(ErrorCode::kIoError, "写入状态文件失败: " + path.string());
    }
    return {};
}

/// @brief 解析 SSH 探测输出。
/// @param output 标准输出。
/// @return 解析出的 guest 状态。
GuestStatus parse_probe_output(std::string_view output) {
    GuestStatus status;
    status.valid = true;
    status.source = "runtime_probe";
    status.checked_at = now_seconds();
    std::istringstream stream{std::string(output)};
    std::string line;
    while (std::getline(stream, line)) {
        if (line.starts_with("LSW_HOSTNAME=")) {
            status.hostname = line.substr(std::string("LSW_HOSTNAME=").size());
        } else if (line.starts_with("LSW_VIRTIO=")) {
            const auto value = line.substr(std::string("LSW_VIRTIO=").size());
            if (value.starts_with("installed")) {
                status.virtio_win = "installed";
            } else if (value == "not_installed") {
                status.virtio_win = "not_installed";
            } else {
                status.virtio_win = "unknown";
            }
        }
    }
    return status;
}

} // namespace

/** @brief 读取缓存的 guest 信息。 */
Result<GuestStatus> load_guest_status(const XdgPaths &paths) {
    const auto path = paths.state_dir / "guest.json";
    const auto value = read_json_file(path);
    if (!value.is_object()) {
        return GuestStatus{};
    }
    GuestStatus status;
    status.valid = true;
    status.source = value.value("source", std::string{"cache"});
    if (status.source.empty()) {
        status.source = "cache";
    }
    status.hostname = value.value("hostname", std::string{});
    status.virtio_win = value.value("virtio_win", std::string{"unknown"});
    status.checked_at = value.value("checked_at", std::int64_t{0});
    return status;
}

/** @brief 写入 guest 信息缓存。 */
Status save_guest_status(const XdgPaths &paths, const GuestStatus &status) {
    nlohmann::json value{
        {"hostname", status.hostname},
        {"virtio_win", status.virtio_win},
        {"source", status.source},
        {"checked_at",
         status.checked_at == 0 ? now_seconds() : status.checked_at},
    };
    return write_json_file(paths.state_dir / "guest.json", value);
}

/** @brief 读取安装完成标记。 */
Result<InstallMarker> load_install_marker(const XdgPaths &paths) {
    const auto value = read_json_file(paths.state_dir / "install.json");
    if (!value.is_object()) {
        return InstallMarker{};
    }
    InstallMarker marker;
    marker.exists = value.value("exists", false);
    marker.disk = value.value("disk", std::string{});
    marker.windows_iso = value.value("windows_iso", std::string{});
    marker.virtio_iso = value.value("virtio_iso", std::string{});
    marker.completed_at = value.value("completed_at", std::int64_t{0});
    return marker;
}

/** @brief 写入安装完成标记。 */
Status save_install_marker(const XdgPaths &paths, const InstallMarker &marker) {
    nlohmann::json value{
        {"exists", true},
        {"disk", marker.disk},
        {"windows_iso", marker.windows_iso},
        {"virtio_iso", marker.virtio_iso},
        {"completed_at",
         marker.completed_at == 0 ? now_seconds() : marker.completed_at},
    };
    return write_json_file(paths.state_dir / "install.json", value);
}

/** @brief 通过 SSH 探测 Windows 主机名与 virtio 驱动服务。 */
Result<GuestStatus> probe_guest_status(const Config &config,
                                       std::string_view ssh_binary,
                                       std::chrono::milliseconds timeout) {
    if (ssh_binary.empty()) {
        return fail(ErrorCode::kMissingDependency, "ssh 可执行文件为空");
    }
    const std::string script =
        "Write-Output ('LSW_HOSTNAME=' + [System.Net.Dns]::GetHostName()); "
        "$names=@('vioscsi','viostor','netkvm','balloon','vioserial','qxldod');"
        " "
        "$found=@(); foreach($n in $names){ if(Test-Path "
        "('HKLM:\\SYSTEM\\CurrentControlSet\\Services\\' + $n)){ "
        "$found+=$n } }; if($found.Count -gt 0){ Write-Output "
        "('LSW_VIRTIO=installed:' + ($found -join ',')) } else { "
        "Write-Output 'LSW_VIRTIO=not_installed' }";
    const std::string encoded = shell::encode_powershell_command(script);
    std::vector<std::string> args{
        std::string(ssh_binary),
        "-p",
        std::to_string(config.network.ssh_host_port),
        "-o",
        "BatchMode=yes",
        "-o",
        "StrictHostKeyChecking=accept-new",
        "-o",
        "LogLevel=ERROR",
        "-o",
        "ConnectTimeout=5",
        config.user.windows_user + "@127.0.0.1",
        "--",
        "powershell.exe",
        "-NoLogo",
        "-NoProfile",
        "-NonInteractive",
        "-EncodedCommand",
        encoded,
    };
    auto result = run_process(args, RunOptions{.timeout = timeout});
    if (!result) {
        if (result.error().code == ErrorCode::kProcessTimeout) {
            return fail(ErrorCode::kSshFailed,
                        "探测 guest 信息超时（" +
                            std::to_string(timeout.count()) + " ms）");
        }
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        return fail(ErrorCode::kSshFailed,
                    "探测 guest 信息失败（ssh 退出码 " +
                        std::to_string(result->exit_code) + "）：" +
                        result->stderr_data);
    }
    if (result->stdout_data.empty()) {
        return fail(ErrorCode::kSshFailed, "探测 guest 信息为空");
    }
    return parse_probe_output(result->stdout_data);
}

/** @brief 生成离线状态 JSON。 */
nlohmann::json offline_status_json(const Config &config,
                                   const XdgPaths &paths) {
    auto guest = load_guest_status(paths);
    if (!guest) {
        guest = GuestStatus{};
    }
    auto marker = load_install_marker(paths);
    if (!marker) {
        marker = InstallMarker{};
    }

    std::filesystem::path disk = config.vm.disk;
    if (auto expanded = expand_tilde(disk, PathEnvironment::from_process());
        expanded) {
        disk = *expanded;
    }
    std::error_code error;
    const bool disk_exists =
        std::filesystem::is_regular_file(disk, error) && !error;

    std::string virtio_win = "unknown";
    std::string virtio_source = "none";
    if (guest->valid && guest->virtio_win != "unknown") {
        virtio_win = guest->virtio_win;
        virtio_source = guest->source.empty() ? "cache" : guest->source;
    } else if (marker->exists && !marker->virtio_iso.empty()) {
        virtio_source = "install_marker";
    }

    return nlohmann::json{
        {"daemon", "stopped"},
        {"vm_status", "stopped"},
        {"vm_installed", disk_exists},
        {"disk", disk.string()},
        {"disk_exists", disk_exists},
        {"pid", -1},
        {"mounts", nlohmann::json::array()},
        {"hostname", guest->valid ? guest->hostname : std::string{}},
        {"virtio_win", virtio_win},
        {"virtio_source", virtio_source},
        {"guest_checked_at", guest->checked_at},
        {"install_marker",
         nlohmann::json{{"exists", marker->exists},
                        {"disk", marker->disk},
                        {"windows_iso", marker->windows_iso},
                        {"virtio_iso", marker->virtio_iso},
                        {"completed_at", marker->completed_at}}},
    };
}

} // namespace lsw::vm

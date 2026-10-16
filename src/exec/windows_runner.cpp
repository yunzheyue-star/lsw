/**
 * @file windows_runner.cpp
 * @brief `lsw <program>` 的 Windows 执行辅助实现。
 * @details 文件位置：src/exec/windows_runner.cpp（源文件）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <lsw/exec/windows_runner.hpp>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>

#include <lsw/core/path_map.hpp>
#include <lsw/shell/powershell.hpp>

namespace lsw::exec {
namespace {

/// @brief 读取小端 16 位整数。
/// @param data 数据指针。
/// @return 小端无符号 16 位值。
std::uint16_t read_u16_le(const unsigned char *data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8U));
}

/// @brief 读取小端 32 位整数。
/// @param data 数据指针。
/// @return 小端无符号 32 位值。
std::uint32_t read_u32_le(const unsigned char *data) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U));
}

/// @brief 判断 SMB 主机/共享名是否安全。
/// @param value 待检查字符串。
/// @return 不含引号/反斜杠/控制字符且非空返回 true。
bool safe_smb_component(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '\'' || ch == '\\' || ch == '\n' || ch == '\r' ||
            std::iscntrl(byte) != 0) {
            return false;
        }
    }
    return true;
}

/// @brief 将路径散列为十六进制字符串。
/// @param path 路径。
/// @return 十六进制散列。
std::string hash_path(const std::filesystem::path &path) {
    const auto value = std::hash<std::string>{}(path.string());
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

/// @brief 清理临时目录中超过 7 天未使用的子目录。
/// @param temp_root 临时根目录。
void cleanup_stale_temp(const std::filesystem::path &temp_root) {
    std::error_code error;
    if (!std::filesystem::is_directory(temp_root, error)) {
        return;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    constexpr auto kMaxAge = std::chrono::hours(24 * 7);
    for (const auto &entry :
         std::filesystem::directory_iterator(temp_root, error)) {
        if (error) {
            return;
        }
        std::error_code time_error;
        const auto modified =
            std::filesystem::last_write_time(entry, time_error);
        if (time_error) {
            continue;
        }
        if (now - modified > kMaxAge) {
            std::error_code remove_error;
            std::filesystem::remove_all(entry, remove_error);
        }
    }
}

/// @brief 构造 PowerShell 字符串数组字面量。
/// @param args 参数列表。
/// @return PowerShell 数组字面量文本。
std::string powershell_array(const std::vector<std::string> &args) {
    std::string out = "@(";
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index != 0) {
            out += ", ";
        }
        out += "'" + shell::escape_powershell_single_quote(args[index]) + "'";
    }
    out += ")";
    return out;
}

} // namespace

/** @brief 判断 `.exe` 是否为 Windows GUI 子系统程序。 */
bool is_gui_executable(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    unsigned char dos_header[64]{};
    input.read(reinterpret_cast<char *>(dos_header), sizeof(dos_header));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(dos_header)) ||
        dos_header[0] != 'M' || dos_header[1] != 'Z') {
        return false;
    }
    const std::uint32_t pe_offset = read_u32_le(dos_header + 0x3C);
    input.seekg(static_cast<std::streamoff>(pe_offset));
    unsigned char signature[4]{};
    input.read(reinterpret_cast<char *>(signature), sizeof(signature));
    if (input.gcount() != 4 || signature[0] != 'P' || signature[1] != 'E' ||
        signature[2] != 0 || signature[3] != 0) {
        return false;
    }
    // COFF header 20 字节后是 optional header；Subsystem 位于其偏移 0x5C。
    constexpr std::streamoff kOptionalHeaderOffset = 4 + 20;
    input.seekg(static_cast<std::streamoff>(pe_offset + kOptionalHeaderOffset));
    unsigned char optional_header[0x60]{};
    input.read(reinterpret_cast<char *>(optional_header),
               sizeof(optional_header));
    if (input.gcount() !=
        static_cast<std::streamsize>(sizeof(optional_header))) {
        return false;
    }
    const std::uint16_t magic = read_u16_le(optional_header);
    if (magic != 0x010B && magic != 0x020B) {
        return false;
    }
    const std::uint16_t subsystem = read_u16_le(optional_header + 0x5C);
    return subsystem == 2; // IMAGE_SUBSYSTEM_WINDOWS_GUI
}

/** @brief 解析程序路径并按需复制到 `$HOME/.lsw/tmp`。 */
Result<ExecTarget> resolve_target(const std::filesystem::path &program,
                                  const std::filesystem::path &cwd,
                                  const std::filesystem::path &home,
                                  const ExecOptions &options) {
    std::error_code error;
    std::filesystem::path candidate = program;
    if (candidate.is_relative()) {
        candidate = cwd / candidate;
    }
    candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        candidate = std::filesystem::absolute(program, error);
        if (error) {
            return fail(ErrorCode::kPathMappingFailed,
                        "无法解析程序路径: " + program.string());
        }
        candidate = candidate.lexically_normal();
    }
    if (!std::filesystem::is_regular_file(candidate, error) || error) {
        return fail(ErrorCode::kPathMappingFailed,
                    "程序文件不存在或不可读: " + candidate.string());
    }
    if (candidate.extension() == ".exe" && is_gui_executable(candidate)) {
        return fail(ErrorCode::kProgramFailed,
                    "暂不支持 GUI 程序（仅支持控制台程序）: " +
                        candidate.string());
    }

    std::error_code home_error;
    const auto canonical_home =
        std::filesystem::weakly_canonical(home, home_error);
    const auto home_path = home_error ? home : canonical_home;
    if (is_within_home(candidate, home_path)) {
        auto windows = map_cwd_to_z(candidate, home_path);
        if (!windows || windows->empty()) {
            return fail(ErrorCode::kPathMappingFailed,
                        "无法映射程序到 Z: " + candidate.string());
        }
        return ExecTarget{
            .host_path = candidate, .windows_path = *windows, .copied = false};
    }

    const auto temp_root = options.temp_root.empty()
                               ? home_path / ".lsw" / "tmp"
                               : options.temp_root;
    cleanup_stale_temp(temp_root);
    const auto destination =
        temp_root / hash_path(candidate) / candidate.filename();
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        return fail(ErrorCode::kPathMappingFailed,
                    "创建临时目录失败: " + error.message());
    }
    std::filesystem::copy_file(
        candidate, destination,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return fail(ErrorCode::kPathMappingFailed,
                    "复制程序到临时目录失败: " + error.message() + "（" +
                        destination.string() + "）");
    }
    auto windows = map_cwd_to_z(destination, home_path);
    if (!windows || windows->empty()) {
        return fail(ErrorCode::kPathMappingFailed,
                    "无法映射临时程序到 Z: " + destination.string());
    }
    return ExecTarget{
        .host_path = destination, .windows_path = *windows, .copied = true};
}

/** @brief 构造远端 PowerShell 执行脚本。 */
Result<std::string> build_script(const Config &config,
                                 const std::string &windows_program,
                                 const std::vector<std::string> &args) {
    if (!safe_smb_component(config.network.guest_smb_host) ||
        !safe_smb_component(config.network.guest_smb_share)) {
        return fail(ErrorCode::kConfigInvalid,
                    "network.guest_smb_host / guest_smb_share 含非法字符");
    }
    if (windows_program.empty()) {
        return fail(ErrorCode::kInvalidArgument, "Windows 程序路径为空");
    }
    const std::string drive = config.filesystem.windows_drive_z;
    const std::string unc = "\\\\" + config.network.guest_smb_host + "\\" +
                            config.network.guest_smb_share;
    std::string script;
    script.reserve(1024);
    script += "$ProgressPreference='SilentlyContinue'; ";
    script += "$ErrorActionPreference='Continue'; ";
    script += "$drive='" + shell::escape_powershell_single_quote(drive) + "'; ";
    script += "$unc='" + shell::escape_powershell_single_quote(unc) + "'; ";
    script += "$mapped=$false; ";
    script += "for ($i=0; $i -lt 8; $i++) { ";
    script += "net use $drive /delete /y 2>$null | Out-Null; ";
    script += "net use $drive $unc /persistent:yes 2>$null | Out-Null; ";
    script += "if (Test-Path -LiteralPath ($drive + '\\')) { $mapped=$true; "
              "break }; ";
    script += "Start-Sleep -Seconds 2 }; ";
    script += "if (-not $mapped) { Write-Error 'LSW: 无法在 Windows 内映射 " +
              shell::escape_powershell_single_quote(drive) +
              "（请确认 lsw start 的 host Samba 正在运行）'; exit 6 }; ";
    script += "$source='" +
              shell::escape_powershell_single_quote(windows_program) + "'; ";
    // 从 Z:（SMB 映射盘）直接执行 .bat/.exe 可能被 SMB 租约/文件锁或
    // Defender 扫描卡住；先复制到 Windows 本地 %TEMP%，Unblock 后再执行，
    // 结束后删除临时目录。
    script += "$localDir=Join-Path $env:TEMP ('lsw-exec-' + "
              "[guid]::NewGuid().ToString('N')); ";
    script += "New-Item -ItemType Directory -Path $localDir -Force | "
              "Out-Null; ";
    script += "$program=Join-Path $localDir "
              "([IO.Path]::GetFileName($source)); ";
    script +=
        "try { Copy-Item -LiteralPath $source -Destination $program "
        "-Force -ErrorAction Stop; "
        "Unblock-File -LiteralPath $program -ErrorAction SilentlyContinue "
        "} catch { Write-Error ('LSW: 复制程序到本地临时目录失败: ' + "
        "$_.Exception.Message); exit 6 }; ";
    script += "$argv=" + powershell_array(args) + "; ";
    // PowerShell 的 & 调用 .bat/.cmd 时会交由 cmd.exe 解释；对 .exe 则直接
    // 启动控制台程序。两者都以 $LASTEXITCODE 取退出码并透传。
    script += "& $program @argv; $code=$LASTEXITCODE; ";
    script += "Remove-Item -LiteralPath $localDir -Recurse -Force "
              "-ErrorAction SilentlyContinue; ";
    script += "if ($null -eq $code) { exit 1 }; exit $code";
    return script;
}

/** @brief 构造非交互 SSH argv。 */
Result<std::vector<std::string>>
build_ssh_args(const Config &config, const std::string &windows_program,
               const std::vector<std::string> &args, std::string ssh_binary) {
    if (ssh_binary.empty()) {
        return fail(ErrorCode::kMissingDependency, "ssh 可执行文件为空");
    }
    auto script = build_script(config, windows_program, args);
    if (!script) {
        return std::unexpected(script.error());
    }
    const std::string encoded = shell::encode_powershell_command(*script);
    std::vector<std::string> ssh_args{
        std::move(ssh_binary),
        "-p",
        std::to_string(config.network.ssh_host_port),
        "-o",
        "BatchMode=yes",
        "-o",
        "StrictHostKeyChecking=accept-new",
        "-o",
        "LogLevel=ERROR",
        "-o",
        "ConnectTimeout=10",
        "-o",
        "ServerAliveInterval=5",
        "-o",
        "ServerAliveCountMax=3",
        config.user.windows_user + "@127.0.0.1",
        "--",
        "powershell.exe",
        "-NoLogo",
        "-NoProfile",
        "-NonInteractive",
        "-EncodedCommand",
        encoded,
    };
    return ssh_args;
}

} // namespace lsw::exec

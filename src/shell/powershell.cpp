/**
 * @file powershell.cpp
 * @brief `lsw shell` 的 SSH / PowerShell 命令构造实现。
 * @details 文件位置：src/shell/powershell.cpp（源文件）。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#include <lsw/shell/powershell.hpp>

#include <cctype>
#include <cstdint>
#include <string_view>

#include <lsw/core/path_map.hpp>

namespace lsw::shell {
namespace {

/// @brief 转义 PowerShell 单引号字符串。
/// @param text 原始文本。
/// @return 单引号翻倍后的文本。
std::string escape_powershell_single_quote(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\'') {
            out += "''";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

/// @brief 将 UTF-8 文本转换为 UTF-16LE（非法序列替换为 U+FFFD）。
/// @param input UTF-8 文本。
/// @return UTF-16LE 字节串。
std::string utf8_to_utf16le(std::string_view input) {
    std::string output;
    output.reserve(input.size() * 2);
    const auto append = [&output](std::uint32_t code_point) {
        if (code_point <= 0xFFFFU) {
            output.push_back(static_cast<char>(code_point & 0xFFU));
            output.push_back(static_cast<char>((code_point >> 8U) & 0xFFU));
            return;
        }
        code_point -= 0x10000U;
        const auto high = static_cast<std::uint16_t>(
            0xD800U + ((code_point >> 10U) & 0x3FFU));
        const auto low =
            static_cast<std::uint16_t>(0xDC00U + (code_point & 0x3FFU));
        output.push_back(static_cast<char>(high & 0xFFU));
        output.push_back(static_cast<char>((high >> 8U) & 0xFFU));
        output.push_back(static_cast<char>(low & 0xFFU));
        output.push_back(static_cast<char>((low >> 8U) & 0xFFU));
    };

    std::size_t index = 0;
    while (index < input.size()) {
        const auto byte = static_cast<unsigned char>(input[index]);
        std::uint32_t code_point = 0;
        std::size_t length = 0;
        if (byte < 0x80U) {
            code_point = byte;
            length = 1;
        } else if ((byte & 0xE0U) == 0xC0U) {
            code_point = byte & 0x1FU;
            length = 2;
        } else if ((byte & 0xF0U) == 0xE0U) {
            code_point = byte & 0x0FU;
            length = 3;
        } else if ((byte & 0xF8U) == 0xF0U) {
            code_point = byte & 0x07U;
            length = 4;
        } else {
            append(0xFFFDU);
            ++index;
            continue;
        }
        if (index + length > input.size()) {
            append(0xFFFDU);
            ++index;
            continue;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(input[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if (!valid || (length == 2 && code_point < 0x80U) ||
            (length == 3 && code_point < 0x800U) ||
            (length == 4 && code_point < 0x10000U) || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            append(0xFFFDU);
            ++index;
            continue;
        }
        append(code_point);
        index += length;
    }
    return output;
}

/// @brief 标准 Base64 编码。
/// @param data 原始字节。
/// @return Base64 文本。
std::string base64_encode(std::string_view data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((data.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < data.size(); index += 3) {
        std::uint32_t block =
            static_cast<std::uint32_t>(static_cast<unsigned char>(data[index]))
            << 16U;
        const std::size_t remaining = data.size() - index;
        if (remaining > 1) {
            block |= static_cast<std::uint32_t>(
                         static_cast<unsigned char>(data[index + 1]))
                     << 8U;
        }
        if (remaining > 2) {
            block |= static_cast<std::uint32_t>(
                static_cast<unsigned char>(data[index + 2]));
        }
        output.push_back(kTable[(block >> 18U) & 0x3FU]);
        output.push_back(kTable[(block >> 12U) & 0x3FU]);
        output.push_back(remaining > 1 ? kTable[(block >> 6U) & 0x3FU] : '=');
        output.push_back(remaining > 2 ? kTable[block & 0x3FU] : '=');
    }
    return output;
}

/// @brief 校验 SMB 主机/共享名不含引号或换行。
/// @param value 待校验字符串。
/// @return 合法返回 true。
bool safe_smb_component(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '\'' || ch == '\n' || ch == '\r' || ch == '\\' ||
            std::iscntrl(byte) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

/** @brief 构造远端 PowerShell 初始化脚本（未编码）。 */
Result<std::string> build_powershell_script(const Config &config,
                                            const std::filesystem::path &cwd,
                                            const std::filesystem::path &home) {
    if (!safe_smb_component(config.network.guest_smb_host) ||
        !safe_smb_component(config.network.guest_smb_share)) {
        return fail(ErrorCode::kConfigInvalid,
                    "network.guest_smb_host / guest_smb_share 含非法字符");
    }
    auto target = map_cwd_to_z(cwd, home);
    if (!target) {
        return std::unexpected(target.error());
    }

    const std::string drive = config.filesystem.windows_drive_z;
    const std::string unc = "\\\\" + config.network.guest_smb_host + "\\" +
                            config.network.guest_smb_share;
    const std::string escaped_target = escape_powershell_single_quote(*target);

    std::string script;
    script.reserve(1024);
    script += "$ProgressPreference='SilentlyContinue'; ";
    script += "$ErrorActionPreference='Continue'; ";
    script += "$drive='" + escape_powershell_single_quote(drive) + "'; ";
    script += "$unc='" + escape_powershell_single_quote(unc) + "'; ";
    script += "$mapped=$false; ";
    script += "for ($i=0; $i -lt 8; $i++) { ";
    script += "net use $drive /delete /y 2>$null | Out-Null; ";
    script += "net use $drive $unc /persistent:yes 2>$null | Out-Null; ";
    script += "if (Test-Path -LiteralPath ($drive + '\\')) { $mapped=$true; "
              "break }; ";
    script += "Start-Sleep -Seconds 2 }; ";
    script += "if (-not $mapped) { Write-Error 'LSW: 无法在 Windows 内映射 " +
              escape_powershell_single_quote(drive) +
              "（请确认 lsw start 的 host Samba 正在运行）'; exit 6 }; ";
    script += "$target='" + escaped_target + "'; ";
    script += "if ($target.Length -gt 0) { try { "
              "Set-Location -LiteralPath $target -ErrorAction Stop } catch { "
              "Write-Error ('LSW: 无法进入 ' + $target + ': ' + "
              "$_.Exception.Message); exit 6 } }";
    return script;
}

/** @brief 构造完整 ssh argv。 */
Result<std::vector<std::string>>
build_ssh_args(const Config &config, const std::filesystem::path &cwd,
               const std::filesystem::path &home, std::string ssh_binary) {
    if (ssh_binary.empty()) {
        return fail(ErrorCode::kMissingDependency, "ssh 可执行文件为空");
    }
    auto script = build_powershell_script(config, cwd, home);
    if (!script) {
        return std::unexpected(script.error());
    }
    const std::string encoded = base64_encode(utf8_to_utf16le(*script));
    const std::string target = config.user.windows_user + "@127.0.0.1";
    std::vector<std::string> args{
        std::move(ssh_binary),
        "-t",
        "-p",
        std::to_string(config.network.ssh_host_port),
        "-o",
        "StrictHostKeyChecking=accept-new",
        target,
        "--",
        "powershell.exe",
        "-NoLogo",
        "-NoExit",
        "-EncodedCommand",
        encoded,
    };
    return args;
}

} // namespace lsw::shell

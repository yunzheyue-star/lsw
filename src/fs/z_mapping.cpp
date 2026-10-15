/**
 * @file z_mapping.cpp
 * @brief Windows `Z:` 盘符映射实现。
 * @details 文件位置：src/fs/z_mapping.cpp（源文件）。
 *          属于 LSW 文件系统模块；公共行为以 docs/interfaces/filesystem.md 与
 * agent.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-15
 */

#include <lsw/fs/z_mapping.hpp>

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

#include <lsw/util/paths.hpp>

namespace lsw::winfs {
namespace {

/// @brief 将 UTF-8 文本转换为 UTF-16LE（非法序列替换为 U+FFFD）。
/// @param input UTF-8 文本。
/// @return UTF-16LE 字节串。
std::string utf8_to_utf16le(std::string_view input) {
    std::string output;
    output.reserve(input.size() * 2);
    const auto append_code_point = [&output](std::uint32_t code_point) {
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
            append_code_point(0xFFFDU);
            ++index;
            continue;
        }
        if (index + length > input.size()) {
            append_code_point(0xFFFDU);
            ++index;
            continue;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation =
                static_cast<unsigned char>(input[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if (!valid || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            append_code_point(0xFFFDU);
            ++index;
            continue;
        }
        append_code_point(code_point);
        index += length;
    }
    return output;
}

/// @brief 生成 PowerShell 片段：盘符可进入时 exit 0，否则继续后续语句。
/// @param root 盘符根路径，例如 `Z:\`。
/// @return 不含最终 exit 7 的片段，末尾带 `; ` 便于拼接。
std::string usable_drive_attempt(const std::string &root) {
    return "if (Test-Path -LiteralPath '" + root +
           "') { try { Set-Location -LiteralPath '" + root +
           "'; if ((Get-Location).Path -eq '" + root +
           "') { exit 0 } } catch {} }; ";
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

} // namespace

/// @brief 构造 `Z:` 映射器。
/// @param config 全局配置（端口、用户名、guest SMB 地址）。
/// @param options 映射选项。
ZDriveMapper::ZDriveMapper(Config config, ZMappingOptions options)
    : config_(std::move(config)), options_(std::move(options)) {}

/// @brief 校验配置字段可安全拼入 PowerShell 脚本。
/// @return 合法返回空状态，否则返回 kConfigInvalid。
Status ZDriveMapper::validate() const {
    const std::string drive = config_.filesystem.windows_drive_z;
    if (drive.size() != 2 || drive[1] != ':' ||
        !std::isalpha(static_cast<unsigned char>(drive[0]))) {
        return fail(ErrorCode::kConfigInvalid,
                    "filesystem.windows_drive_z 必须是单个盘符，例如 Z:");
    }
    for (const auto *field :
         {&config_.network.guest_smb_host, &config_.network.guest_smb_share,
          &config_.user.windows_user}) {
        if (field->find('\'') != std::string::npos ||
            field->find('\n') != std::string::npos) {
            return fail(ErrorCode::kConfigInvalid,
                        "配置字段包含非法字符（禁止引号或换行）");
        }
    }
    if (config_.network.guest_smb_host.empty() ||
        config_.network.guest_smb_share.empty() ||
        config_.user.windows_user.empty()) {
        return fail(ErrorCode::kConfigInvalid,
                    "guest SMB 地址/共享名与 Windows 用户名不能为空");
    }
    return {};
}

/// @brief 通过 SSH 执行 PowerShell 脚本。
/// @param script PowerShell 脚本文本。
/// @return 进程执行结果；SSH 不可用或超时返回错误。
Result<ProcessResult> ZDriveMapper::ssh_powershell(
    const std::string &script,
    const std::optional<std::string> &stdin_data) const {
    std::string ssh = options_.ssh_binary;
    if (ssh.find('/') != std::string::npos) {
        if (::access(ssh.c_str(), X_OK) != 0) {
            ssh.clear();
        }
    } else {
        ssh = find_executable(ssh);
    }
    if (ssh.empty()) {
        return fail(ErrorCode::kMissingDependency,
                    "找不到 ssh 可执行文件：" + options_.ssh_binary);
    }
    const int port = options_.port.value_or(config_.network.ssh_host_port);
    const std::string target = config_.user.windows_user + "@" + options_.host;
    // Windows OpenSSH 的远端默认 shell 是 cmd.exe，会把脚本里的 '|' 当管道
    // 拆开；改用 -EncodedCommand（UTF-16LE + Base64）绕过 shell 解析。
    const std::string encoded = base64_encode(utf8_to_utf16le(script));
    const std::vector<std::string> argv = {ssh,
                                           "-p",
                                           std::to_string(port),
                                           "-o",
                                           "BatchMode=yes",
                                           "-o",
                                           "StrictHostKeyChecking=accept-new",
                                           "-o",
                                           "ConnectTimeout=5",
                                           target,
                                           "powershell.exe",
                                           "-NoProfile",
                                           "-NonInteractive",
                                           "-EncodedCommand",
                                           encoded};
    return run_process(argv, RunOptions{.timeout = options_.timeout,
                                        .stdin_data = stdin_data});
}

/// @brief 检查 `Z:` 当前是否已映射。
/// @return 已映射返回 true；未映射返回 false；SSH 执行失败返回错误。
Result<bool> ZDriveMapper::is_mapped() {
    if (auto valid = validate(); !valid) {
        return std::unexpected(valid.error());
    }
    const std::string drive = config_.filesystem.windows_drive_z;
    const std::string script = "$ProgressPreference='SilentlyContinue'; " +
                               usable_drive_attempt(drive + "\\") + "exit 7";
    const auto result = ssh_powershell(script);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code == 0) {
        return true;
    }
    if (result->exit_code == 7) {
        return false;
    }
    return fail(ErrorCode::kShareSetupFailed,
                "检查 " + drive + " 映射失败（ssh exit " +
                    std::to_string(result->exit_code) +
                    "）: " + result->stderr_data);
}

/// @brief 确保 `Z:` 已映射：未映射时执行 `net use` 并持久化。
/// @return 成功返回空状态；失败返回 kShareSetupFailed / kMissingDependency。
Status ZDriveMapper::ensure_mapped(
    const std::optional<WindowsCredential> &credential) {
    auto mapped = is_mapped();
    if (!mapped) {
        return std::unexpected(mapped.error());
    }
    if (*mapped) {
        return {};
    }

    const std::string drive = config_.filesystem.windows_drive_z;
    const std::string root = drive + "\\";
    const std::string unc = "\\\\" + config_.network.guest_smb_host + "\\" +
                            config_.network.guest_smb_share;
    std::string credential_prefix;
    std::string map_command;
    std::optional<std::string> stdin_data;
    if (credential.has_value()) {
        // 无头 SSH 会话没有交互登录缓存的密码；显式 /user，密码由
        // PowerShell 从 stdin 读取，避免进入 argv/日志。
        credential_prefix = "$pw=[Console]::In.ReadLine(); ";
        map_command = "net use " + drive + " " + unc + " /user:'" +
                      credential->username +
                      "' $pw /persistent:yes | Out-Null; ";
        stdin_data = credential->password + "\n";
    } else {
        map_command =
            "net use " + drive + " " + unc + " /persistent:yes | Out-Null; ";
    }
    // Windows 刚完成引导时 SMB1 客户端/QEMU SMB 后端可能尚未就绪：
    // 首次 net use 可能“假成功”但盘符不可用。循环清理并重挂，直到
    // 盘符真正可 Set-Location。
    const std::string script =
        "$ProgressPreference='SilentlyContinue'; " + credential_prefix +
        "for ($i=0; $i -lt 8; $i++) { net use " + drive +
        " /delete /y 2>$null | Out-Null; " + map_command +
        usable_drive_attempt(root) + "Start-Sleep -Seconds 3 }; exit 7";
    const auto result = ssh_powershell(script, stdin_data);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        return fail(
            ErrorCode::kShareSetupFailed,
            "Windows 内映射 " + drive + " 到 " + unc + " 失败（ssh exit " +
                std::to_string(result->exit_code) +
                "）: " + result->stderr_data +
                "\n提示：QEMU user-mode SMB 使用 SMB1，Windows 10/11 guest "
                "需要启用 SMB1Protocol-Client 并重启 guest。");
    }
    return {};
}

} // namespace lsw::winfs

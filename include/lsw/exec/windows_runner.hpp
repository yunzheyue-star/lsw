/**
 * @file windows_runner.hpp
 * @brief `lsw <program>` 的 Windows 执行辅助：路径解析、复制与 SSH 命令构造。
 * @details 文件位置：include/lsw/exec/windows_runner.hpp（头文件）。
 *          属于 LSW 执行模块；支持 `.exe`、`.bat`、`.cmd` 等可在
 *          Windows 控制台运行的程序。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>

namespace lsw::exec {

/// @brief Windows 程序执行选项。
struct ExecOptions {
    std::string ssh_binary{"ssh"};             ///< ssh 可执行文件
    std::filesystem::path temp_root;           ///< 空 = `$HOME/.lsw/tmp`
    std::chrono::milliseconds timeout{300000}; ///< SSH + 程序执行超时
};

/// @brief 解析后的执行目标。
struct ExecTarget {
    std::filesystem::path host_path; ///< Linux 侧实际路径
    std::string windows_path;        ///< Windows `Z:` 路径
    bool copied{false};              ///< 是否复制到 `$HOME/.lsw/tmp`
};

/// @brief 解析程序路径：相对 cwd 绝对化；`$HOME` 外复制到临时目录。
/// @param program 用户传入的程序路径（可为相对路径）。
/// @param cwd 当前 Linux 工作目录。
/// @param home Linux 家目录。
/// @param options 执行选项（临时目录、清理策略）。
/// @return 成功返回执行目标；文件缺失/GUI/复制失败返回错误。
[[nodiscard]] Result<ExecTarget> resolve_target(
    const std::filesystem::path &program, const std::filesystem::path &cwd,
    const std::filesystem::path &home, const ExecOptions &options = {});

/// @brief 判断 `.exe` 是否为 Windows GUI 子系统程序。
/// @param path 文件路径。
/// @return GUI 程序返回 true；非 PE/解析失败返回 false。
[[nodiscard]] bool is_gui_executable(const std::filesystem::path &path);

/// @brief 构造远端 PowerShell 执行脚本（未编码）。
/// @param config 全局配置（host SMB、Z: 盘符）。
/// @param windows_program Windows `Z:` 程序路径。
/// @param args 程序参数。
/// @return PowerShell 脚本文本；配置非法返回错误。
[[nodiscard]] Result<std::string>
build_script(const Config &config, const std::string &windows_program,
             const std::vector<std::string> &args);

/// @brief 构造非交互 SSH argv（`ssh -p ... -- powershell.exe
/// -EncodedCommand`）。
/// @param config 全局配置。
/// @param windows_program Windows `Z:` 程序路径。
/// @param args 程序参数。
/// @param ssh_binary ssh 可执行文件。
/// @return 成功返回 argv；失败返回配置/依赖错误。
[[nodiscard]] Result<std::vector<std::string>>
build_ssh_args(const Config &config, const std::string &windows_program,
               const std::vector<std::string> &args,
               std::string ssh_binary = "ssh");

} // namespace lsw::exec

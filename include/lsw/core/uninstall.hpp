/**
 * @file uninstall.hpp
 * @brief `lsw uninstall` / `lsw --uninstall` 卸载流程。
 * @details 文件位置：include/lsw/core/uninstall.hpp（头文件）。
 *          默认只删除 VM 专属文件；选择彻底清理时再删除配置、helper、
 *          数据/状态目录与 lsw init 记录过的受管包。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/util/paths.hpp>

namespace lsw::uninstall {

/// @brief uninstall 选项。
struct Options {
    bool assume_yes{false}; ///< 跳过“确认删除 VM”
    bool purge{false};      ///< 连带清理配置/helper/受管包/数据
    bool keep{false};       ///< 只删除 VM（显式）
    bool force{false};      ///< 允许删除 LSW 数据目录之外的磁盘
    bool dry_run{false};    ///< 只打印计划，不执行
};

/// @brief 卸载计划。
struct Plan {
    std::filesystem::path disk;                     ///< VM 磁盘
    std::vector<std::filesystem::path> vm_files;    ///< VM 专属文件
    std::vector<std::filesystem::path> purge_files; ///< 彻底清理的文件
    std::vector<std::filesystem::path> purge_dirs;  ///< 彻底清理的目录
    bool remove_helper{false};                      ///< 是否删除 helper/sudoers
    std::string package_manager;                    ///< 受管包对应包管理器
    std::vector<std::string> packages;              ///< lsw init 记录过的包
};

/// @brief 生成卸载计划（只读）。
/// @param config 全局配置。
/// @param paths XDG 路径。
/// @param options 选项。
/// @return 成功返回计划；外部磁盘未加 --force 等返回错误。
[[nodiscard]] Result<Plan> plan(const Config &config, const XdgPaths &paths,
                                const Options &options = {});

/// @brief 执行卸载流程（含确认、关机、文件删除与可选包卸载）。
/// @param paths XDG 路径。
/// @param options 选项。
/// @return 成功/用户取消返回成功；权限/删除/安装错误返回对应错误。
[[nodiscard]] Status run(const XdgPaths &paths, const Options &options = {});

} // namespace lsw::uninstall

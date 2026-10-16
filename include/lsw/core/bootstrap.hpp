/**
 * @file bootstrap.hpp
 * @brief `lsw init` 宿主环境初始化：发行版识别、依赖检查与自动安装。
 * @details 文件位置：include/lsw/core/bootstrap.hpp（头文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/init.md 与
 *          docs/interfaces/cli.md 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-16
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <lsw/core/config.hpp>
#include <lsw/core/error.hpp>
#include <lsw/util/paths.hpp>

namespace lsw::bootstrap {

/// @brief 宿主机包管理器家族。
enum class PackageManager {
    kPacman,  ///< Arch Linux
    kApt,     ///< Debian / Ubuntu
    kDnf,     ///< Fedora / RHEL
    kUnknown, ///< 未知，只输出手动安装建议
};

/// @brief init 选项。
struct Options {
    bool assume_yes{false}; ///< 跳过交互确认
    bool dry_run{false};    ///< 只打印计划，不执行安装
};

/// @brief 单个依赖/组件检查结果。
struct Component {
    std::string id;                    ///< 内部标识
    std::string display;               ///< 人类可读名称
    bool required{true};               ///< 是否必需
    bool satisfied{false};             ///< 当前是否满足
    std::string detail;                ///< 状态说明
    std::vector<std::string> packages; ///< 缺失时需要安装的包
};

/// @brief init 检查计划。
struct Plan {
    std::string distro; ///< 发行版显示名
    PackageManager manager{PackageManager::kUnknown};
    std::string manager_name; ///< 包管理器命令名
    std::vector<Component> components;
    std::vector<std::string> missing_packages;
    bool helper_missing{false}; ///< 宿主 helper 是否缺失
    bool windows_hint{true};    ///< 需要提示 Windows 侧 post-install
};

/// @brief 包管理器枚举转名称。
/// @param manager 包管理器。
/// @return 命令名（pacman/apt-get/dnf）或 unknown。
[[nodiscard]] std::string to_string(PackageManager manager);

/// @brief 根据 /etc/os-release（可注入路径用于测试）识别包管理器。
/// @param os_release_path os-release 路径。
/// @return 识别出的包管理器家族。
[[nodiscard]] PackageManager detect_package_manager(
    const std::filesystem::path &os_release_path = "/etc/os-release");

/// @brief 构造包安装命令（不含 sudo）。
/// @param manager 包管理器。
/// @param packages 待安装包列表（去重后）。
/// @return argv 形式的安装命令；未知包管理器返回空。
[[nodiscard]] std::vector<std::string>
install_command(PackageManager manager,
                const std::vector<std::string> &packages);

/// @brief 扫描宿主环境，生成 init 计划（不执行安装）。
/// @param paths XDG 路径。
/// @return 成功返回计划；无法读取系统信息返回错误。
[[nodiscard]] Result<Plan> plan(const XdgPaths &paths);

/// @brief 如果配置文件不存在则写入默认 `config.toml`。
/// @param paths XDG 路径。
/// @return 初始化结果。
[[nodiscard]] Status initialize_config(const XdgPaths &paths);

/// @brief 执行完整 init 流程：配置初始化、检查、确认、安装、复检。
/// @param paths XDG 路径。
/// @param options 交互/ dry-run 选项。
/// @return 全部必需组件满足返回成功；安装失败或仍缺依赖返回错误。
[[nodiscard]] Status run(const XdgPaths &paths, const Options &options = {});

/// @brief 记录 lsw init 实际安装的包，供 uninstall 彻底清理时参考。
/// @param paths XDG 路径。
/// @param manager 包管理器。
/// @param packages 实际安装的包名。
/// @return 写入结果。
[[nodiscard]] Status
record_managed_packages(const XdgPaths &paths, PackageManager manager,
                        const std::vector<std::string> &packages);

/// @brief 读取 lsw init 记录的受管包列表。
/// @param paths XDG 路径。
/// @return JSON：{ manager, packages }；不存在返回空对象。
[[nodiscard]] Result<nlohmann::json>
load_managed_packages(const XdgPaths &paths);

} // namespace lsw::bootstrap

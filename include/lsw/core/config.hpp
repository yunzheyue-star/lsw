/**
 * @file config.hpp
 * @brief TOML 配置加载、默认值、校验与序列化。
 * @details 文件位置：include/lsw/core/config.hpp（头文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <lsw/core/error.hpp>
#include <lsw/util/paths.hpp>

namespace lsw {

struct VmConfig {
    std::string name{"lsw-windows"};
    int memory_mb{8192};
    int cpus{4};
    std::filesystem::path disk;
    std::filesystem::path windows_iso;
    std::filesystem::path virtio_iso;
};

struct NetworkConfig {
    int ssh_host_port{2222};
    int smb_host_port{1445};
    std::string guest_smb_host{"10.0.2.4"};
    std::string guest_smb_share{"qemu"};
};

struct FilesystemConfig {
    std::filesystem::path mount_root{"/mnt/Windows"};
    std::filesystem::path linux_home_share{"~"};
    std::string windows_drive_z{"Z:"};
};

struct UserConfig {
    std::string windows_user{"lsw"};
};

struct Config {
    VmConfig vm;
    NetworkConfig network;
    FilesystemConfig filesystem;
    UserConfig user;
};

/// @brief 与 docs/interfaces/config.md 一致的默认配置（disk 保留
/// ~，解析时展开）。
/// @return 默认配置对象。
[[nodiscard]] Config default_config();

/// @brief 从 TOML 文本解析配置；缺失字段使用默认值，非法值返回错误。
/// @param toml_text TOML 文本内容。
/// @param env 路径环境快照，用于展开 ~ 等路径。
/// @return 解析成功返回 Config，失败返回错误。
[[nodiscard]] Result<Config>
parse_config(std::string_view toml_text,
             const PathEnvironment &env = PathEnvironment::from_process());

/// @brief 从文件加载配置；allow_missing=true 时文件不存在返回默认配置。
/// @param file 配置文件路径。
/// @param allow_missing 文件不存在时是否返回默认配置。
/// @param env 路径环境快照，用于展开 ~ 等路径。
/// @return 加载成功返回 Config，失败返回错误。
[[nodiscard]] Result<Config>
load_config(const std::filesystem::path &file, bool allow_missing = true,
            const PathEnvironment &env = PathEnvironment::from_process());

/// @brief 校验字段范围与必填项，返回 kConfigInvalid。
/// @param config 待校验的配置。
/// @return 校验通过返回成功状态，否则返回 kConfigInvalid。
[[nodiscard]] Status validate_config(const Config &config);

/// @brief 默认配置的 TOML 文本（可作为模板写入 $CONFIG）。
/// @return 默认配置的 TOML 文本。
[[nodiscard]] std::string default_config_toml();

/// @brief 序列化为 TOML 文本。
/// @param config 待序列化的配置。
/// @return 序列化得到的 TOML 文本。
[[nodiscard]] std::string to_toml(const Config &config);

} // namespace lsw

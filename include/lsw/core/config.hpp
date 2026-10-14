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

/// 与 docs/interfaces/config.md 一致的默认配置（disk 保留 ~，解析时展开）。
[[nodiscard]] Config default_config();

/// 从 TOML 文本解析配置；缺失字段使用默认值，非法值返回错误。
[[nodiscard]] Result<Config> parse_config(
    std::string_view toml_text,
    const PathEnvironment& env = PathEnvironment::from_process());

/// 从文件加载配置；allow_missing=true 时文件不存在返回默认配置。
[[nodiscard]] Result<Config> load_config(
    const std::filesystem::path& file, bool allow_missing = true,
    const PathEnvironment& env = PathEnvironment::from_process());

/// 校验字段范围与必填项，返回 kConfigInvalid。
[[nodiscard]] Status validate_config(const Config& config);

/// 默认配置的 TOML 文本（可作为模板写入 $CONFIG）。
[[nodiscard]] std::string default_config_toml();

/// 序列化为 TOML 文本。
[[nodiscard]] std::string to_toml(const Config& config);

}  // namespace lsw

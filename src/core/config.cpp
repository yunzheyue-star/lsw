/**
 * @file config.cpp
 * @brief config 实现（核心库）。
 * @details 文件位置：src/core/config.cpp（源文件）。
 *          属于 LSW 核心库；公共行为以 docs/interfaces/core.md 与 agent.md
 * 为准。
 * @author 琳筱筱 <341178706@qq.com>
 * @date 2026-10-14
 */

#include <lsw/core/config.hpp>

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>

#include <toml++/toml.hpp>

namespace lsw {
namespace {

/// @brief 将文本转换为带引号且转义反斜杠与双引号的 TOML 字符串。
/// @param text 待引用的原始文本。
/// @return 可用于 TOML 的带引号字符串。
std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

/// @brief 从 TOML 节点读取可选字符串字段。
/// @param node 待读取的 TOML 节点；为空表示字段缺失。
/// @param label 字段名称，用于错误信息。
/// @param out 读取成功时写入的字符串。
/// @return 成功返回空 Status；类型不匹配返回 kConfigInvalid。
Status read_string(const toml::node *node, std::string_view label,
                   std::string &out) {
    if (node == nullptr) {
        return {};
    }
    if (auto value = node->value<std::string>()) {
        out = *value;
        return {};
    }
    return fail(ErrorCode::kConfigInvalid,
                std::string(label) + " 类型错误（期望字符串）");
}

/// @brief 从 TOML 节点读取可选整数字段。
/// @param node 待读取的 TOML 节点；为空表示字段缺失。
/// @param label 字段名称，用于错误信息。
/// @param out 读取成功时写入的整数。
/// @return 成功返回空 Status；类型不匹配返回 kConfigInvalid。
Status read_int(const toml::node *node, std::string_view label, int &out) {
    if (node == nullptr) {
        return {};
    }
    if (auto value = node->value<int64_t>()) {
        out = static_cast<int>(*value);
        return {};
    }
    return fail(ErrorCode::kConfigInvalid,
                std::string(label) + " 类型错误（期望整数）");
}

/// @brief 从 TOML 节点读取可选路径字段并展开开头的 ~。
/// @param node 待读取的 TOML 节点；为空表示字段缺失。
/// @param label 字段名称，用于错误信息。
/// @param out 读取成功时写入的路径。
/// @param env 路径展开所需的环境快照。
/// @return 成功返回空 Status；类型错误或路径展开失败时返回对应错误。
Status read_path(const toml::node *node, std::string_view label,
                 std::filesystem::path &out, const PathEnvironment &env) {
    if (node == nullptr) {
        return {};
    }
    if (auto value = node->value<std::string>()) {
        auto expanded = expand_tilde(std::filesystem::path(*value), env);
        if (!expanded) {
            return std::unexpected(expanded.error());
        }
        out = *expanded;
        return {};
    }
    return fail(ErrorCode::kConfigInvalid,
                std::string(label) + " 类型错误（期望路径字符串）");
}

/// @brief 在 TOML 表中按 key 查找节点。
/// @param table 待查找的表；为空表示无该表。
/// @param key 节点键名。
/// @return 找到的节点指针；表或键不存在时返回 nullptr。
const toml::node *at(const toml::table *table, std::string_view key) {
    if (table == nullptr) {
        return nullptr;
    }
    return (*table)[key].node();
}

/// @brief 获取指定名称的 TOML 子表。
/// @param root 根 TOML 表。
/// @param name 子表名称。
/// @return 子表指针；缺省时返回 nullptr，类型错误返回 kConfigInvalid。
Result<const toml::table *> section(const toml::table &root,
                                    std::string_view name) {
    const toml::node *node = root[name].node();
    if (node == nullptr) {
        return static_cast<const toml::table *>(nullptr);
    }
    if (!node->is_table()) {
        return fail(ErrorCode::kConfigInvalid,
                    std::string(name) + " 必须是 TOML 表");
    }
    return node->as_table();
}

#define LSW_TRY(expr)                                                          \
    do {                                                                       \
        auto status = (expr);                                                  \
        if (!status) {                                                         \
            return std::unexpected(status.error());                            \
        }                                                                      \
    } while (false)

} // namespace

/// @brief 创建与文档一致的默认配置。
/// @return 默认配置对象。
Config default_config() {
    Config config;
    config.vm.disk = "~/.local/share/lsw/vm/disk.qcow2";
    return config;
}

/// @brief 从 TOML 文本解析配置；缺失字段使用默认值。
/// @param toml_text 待解析的 TOML 文本。
/// @param env 路径展开所需的环境快照。
/// @return 解析并校验后的配置；语法或字段错误时返回对应错误。
Result<Config> parse_config(std::string_view toml_text,
                            const PathEnvironment &env) {
    Config config = default_config();

    toml::table root;
    try {
        root = toml::parse(toml_text);
    } catch (const toml::parse_error &error) {
        return fail(ErrorCode::kConfigParseError,
                    std::string("TOML 解析失败: ") +
                        std::string(error.description()));
    }

    auto vm_section = section(root, "vm");
    if (!vm_section) {
        return std::unexpected(vm_section.error());
    }
    auto network_section = section(root, "network");
    if (!network_section) {
        return std::unexpected(network_section.error());
    }
    auto fs_section = section(root, "filesystem");
    if (!fs_section) {
        return std::unexpected(fs_section.error());
    }
    auto user_section = section(root, "user");
    if (!user_section) {
        return std::unexpected(user_section.error());
    }

    const toml::table *vm = *vm_section;
    const toml::table *network = *network_section;
    const toml::table *filesystem = *fs_section;
    const toml::table *user = *user_section;

    LSW_TRY(read_string(at(vm, "name"), "vm.name", config.vm.name));
    LSW_TRY(read_int(at(vm, "memory_mb"), "vm.memory_mb", config.vm.memory_mb));
    LSW_TRY(read_int(at(vm, "cpus"), "vm.cpus", config.vm.cpus));
    LSW_TRY(read_path(at(vm, "disk"), "vm.disk", config.vm.disk, env));
    LSW_TRY(read_path(at(vm, "windows_iso"), "vm.windows_iso",
                      config.vm.windows_iso, env));
    LSW_TRY(read_path(at(vm, "virtio_iso"), "vm.virtio_iso",
                      config.vm.virtio_iso, env));

    LSW_TRY(read_int(at(network, "ssh_host_port"), "network.ssh_host_port",
                     config.network.ssh_host_port));
    LSW_TRY(read_int(at(network, "smb_host_port"), "network.smb_host_port",
                     config.network.smb_host_port));
    LSW_TRY(read_string(at(network, "guest_smb_host"), "network.guest_smb_host",
                        config.network.guest_smb_host));
    LSW_TRY(read_string(at(network, "guest_smb_share"),
                        "network.guest_smb_share",
                        config.network.guest_smb_share));

    LSW_TRY(read_path(at(filesystem, "mount_root"), "filesystem.mount_root",
                      config.filesystem.mount_root, env));
    LSW_TRY(read_path(at(filesystem, "linux_home_share"),
                      "filesystem.linux_home_share",
                      config.filesystem.linux_home_share, env));
    LSW_TRY(read_string(at(filesystem, "windows_drive_z"),
                        "filesystem.windows_drive_z",
                        config.filesystem.windows_drive_z));

    LSW_TRY(read_string(at(user, "windows_user"), "user.windows_user",
                        config.user.windows_user));

    if (auto status = validate_config(config); !status) {
        return std::unexpected(status.error());
    }
    return config;
}

/// @brief 从文件加载配置。
/// @param file 配置文件路径。
/// @param allow_missing 为 true 时文件不存在返回默认配置。
/// @param env 路径展开所需的环境快照。
/// @return 配置对象；读取失败或不允许缺失时返回错误。
Result<Config> load_config(const std::filesystem::path &file,
                           bool allow_missing, const PathEnvironment &env) {
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (error) {
        return fail(ErrorCode::kIoError, "检查配置文件失败: " + file.string() +
                                             ": " + error.message());
    }
    if (!exists) {
        if (allow_missing) {
            return default_config();
        }
        return fail(ErrorCode::kConfigNotFound,
                    "配置文件不存在: " + file.string());
    }

    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return fail(ErrorCode::kIoError, "无法读取配置文件: " + file.string());
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    return parse_config(text, env);
}

/// @brief 校验配置字段的范围与必填项。
/// @param config 待校验的配置。
/// @return 校验通过返回空 Status；非法字段返回 kConfigInvalid。
Status validate_config(const Config &config) {
    if (config.vm.name.empty()) {
        return fail(ErrorCode::kConfigInvalid, "vm.name 不能为空");
    }
    if (config.vm.memory_mb < 256 || config.vm.memory_mb > 1048576) {
        return fail(ErrorCode::kConfigInvalid,
                    "vm.memory_mb 必须在 256~1048576 之间");
    }
    if (config.vm.cpus < 1 || config.vm.cpus > 256) {
        return fail(ErrorCode::kConfigInvalid, "vm.cpus 必须在 1~256 之间");
    }
    if (config.vm.disk.empty()) {
        return fail(ErrorCode::kConfigInvalid, "vm.disk 不能为空");
    }
    const auto valid_port = [](int port) { return port >= 1 && port <= 65535; };
    if (!valid_port(config.network.ssh_host_port)) {
        return fail(ErrorCode::kConfigInvalid,
                    "network.ssh_host_port 必须在 1~65535 之间");
    }
    if (!valid_port(config.network.smb_host_port)) {
        return fail(ErrorCode::kConfigInvalid,
                    "network.smb_host_port 必须在 1~65535 之间");
    }
    if (config.network.guest_smb_host.empty()) {
        return fail(ErrorCode::kConfigInvalid,
                    "network.guest_smb_host 不能为空");
    }
    if (config.network.guest_smb_share.empty()) {
        return fail(ErrorCode::kConfigInvalid,
                    "network.guest_smb_share 不能为空");
    }
    if (config.filesystem.mount_root.empty() ||
        !config.filesystem.mount_root.is_absolute()) {
        return fail(ErrorCode::kConfigInvalid,
                    "filesystem.mount_root 必须是非空绝对路径");
    }
    if (config.filesystem.windows_drive_z.empty()) {
        return fail(ErrorCode::kConfigInvalid,
                    "filesystem.windows_drive_z 不能为空");
    }
    if (config.user.windows_user.empty()) {
        return fail(ErrorCode::kConfigInvalid, "user.windows_user 不能为空");
    }
    return {};
}

/// @brief 将配置序列化为 TOML 文本。
/// @param config 待序列化的配置。
/// @return TOML 文本。
std::string to_toml(const Config &config) {
    std::ostringstream out;
    out << "[vm]\n"
        << "name = " << quote(config.vm.name) << "\n"
        << "memory_mb = " << config.vm.memory_mb << "\n"
        << "cpus = " << config.vm.cpus << "\n"
        << "disk = " << quote(config.vm.disk.string()) << "\n"
        << "windows_iso = " << quote(config.vm.windows_iso.string()) << "\n"
        << "virtio_iso = " << quote(config.vm.virtio_iso.string()) << "\n\n"
        << "[network]\n"
        << "ssh_host_port = " << config.network.ssh_host_port << "\n"
        << "smb_host_port = " << config.network.smb_host_port << "\n"
        << "guest_smb_host = " << quote(config.network.guest_smb_host) << "\n"
        << "guest_smb_share = " << quote(config.network.guest_smb_share)
        << "\n\n"
        << "[filesystem]\n"
        << "mount_root = " << quote(config.filesystem.mount_root.string())
        << "\n"
        << "linux_home_share = "
        << quote(config.filesystem.linux_home_share.string()) << "\n"
        << "windows_drive_z = " << quote(config.filesystem.windows_drive_z)
        << "\n\n"
        << "[user]\n"
        << "windows_user = " << quote(config.user.windows_user) << "\n";
    return out.str();
}

/// @brief 返回默认配置的 TOML 文本。
/// @return 默认配置序列化后的 TOML 文本。
std::string default_config_toml() { return to_toml(default_config()); }

#undef LSW_TRY

} // namespace lsw

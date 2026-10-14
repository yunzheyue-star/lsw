#include <lsw/core/config.hpp>

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>

#include <toml++/toml.hpp>

namespace lsw {
namespace {

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

const toml::node *at(const toml::table *table, std::string_view key) {
    if (table == nullptr) {
        return nullptr;
    }
    return (*table)[key].node();
}

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

Config default_config() {
    Config config;
    config.vm.disk = "~/.local/share/lsw/vm/disk.qcow2";
    return config;
}

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

std::string default_config_toml() { return to_toml(default_config()); }

#undef LSW_TRY

} // namespace lsw

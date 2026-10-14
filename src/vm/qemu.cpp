/**
 * @file qemu.cpp
 * @brief qemu 实现（虚拟机管理（QEMU/QMP/安装））。
 * @details 文件位置：src/vm/qemu.cpp（源文件）。
 *          属于 LSW 虚拟机管理（QEMU/QMP/安装）；公共行为以
 * docs/interfaces/qmp.md 与 agent.md 为准。
 */

#include <lsw/vm/qemu.hpp>

#include <cstdlib>
#include <fstream>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace lsw::vm {
namespace {

std::string arg_join(const std::string &key, const std::string &value) {
    return key + value;
}

bool executable_file(const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) &&
           ::access(path.c_str(), X_OK) == 0;
}

std::string read_file_tail(const std::filesystem::path &file,
                           std::size_t max_bytes = 4096) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0) {
        return {};
    }
    const std::streamoff start =
        size > static_cast<std::streamoff>(max_bytes)
            ? size - static_cast<std::streamoff>(max_bytes)
            : 0;
    input.seekg(start);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

} // namespace

std::string_view to_string(VmStatus status) noexcept {
    switch (status) {
    case VmStatus::kStopped:
        return "stopped";
    case VmStatus::kRunning:
        return "running";
    case VmStatus::kPaused:
        return "paused";
    case VmStatus::kUnknown:
        return "unknown";
    }
    return "unknown";
}

std::filesystem::path find_executable(std::string_view name) {
    const std::filesystem::path candidate(name);
    if (candidate.has_parent_path()) {
        return executable_file(candidate) ? candidate : std::filesystem::path{};
    }
    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return {};
    }
    std::string path_text(path_env);
    std::size_t begin = 0;
    while (begin <= path_text.size()) {
        const auto end = path_text.find(':', begin);
        const std::string directory = path_text.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!directory.empty()) {
            const auto full = std::filesystem::path(directory) / candidate;
            if (executable_file(full)) {
                return full;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return {};
}

std::vector<std::filesystem::path> ovmf_code_candidates() {
    return {
        "/usr/share/OVMF/OVMF_CODE.fd",    // Debian / Ubuntu
        "/usr/share/OVMF/OVMF_CODE_4M.fd", // Debian / Ubuntu（4M）
        "/usr/share/OVMF/OVMF_CODE.4m.fd",
        "/usr/share/edk2/ovmf/OVMF_CODE.fd", // Fedora 旧布局
        "/usr/share/edk2/x64/OVMF_CODE.fd",
        "/usr/share/edk2/x64/OVMF_CODE.4m.fd", // Arch（edk2-ovmf）
        "/usr/share/ovmf/OVMF_CODE.fd",
        "/usr/share/ovmf/x64/OVMF_CODE.fd",
        "/usr/share/qemu/OVMF_CODE.fd",
    };
}

std::vector<std::filesystem::path> ovmf_vars_candidates() {
    return {
        "/usr/share/OVMF/OVMF_VARS.fd",    // Debian / Ubuntu
        "/usr/share/OVMF/OVMF_VARS_4M.fd", // Debian / Ubuntu（4M）
        "/usr/share/OVMF/OVMF_VARS.4m.fd",
        "/usr/share/edk2/ovmf/OVMF_VARS.fd", // Fedora 旧布局
        "/usr/share/edk2/x64/OVMF_VARS.fd",
        "/usr/share/edk2/x64/OVMF_VARS.4m.fd", // Arch（edk2-ovmf）
        "/usr/share/ovmf/OVMF_VARS.fd",
        "/usr/share/ovmf/x64/OVMF_VARS.fd",
        "/usr/share/qemu/OVMF_VARS.fd",
    };
}

std::optional<std::filesystem::path> detect_ovmf_code() {
    for (const auto &candidate : ovmf_code_candidates()) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> detect_ovmf_vars_template() {
    for (const auto &candidate : ovmf_vars_candidates()) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool swtpm_available(const std::filesystem::path &binary) {
    if (binary.empty()) {
        return false;
    }
    if (binary.has_parent_path()) {
        return executable_file(binary);
    }
    return !find_executable(binary.string()).empty();
}

Result<std::vector<std::string>> build_swtpm_args(const QemuVmOptions &options,
                                                  const XdgPaths &paths) {
    if (!options.enable_swtpm) {
        return std::vector<std::string>{};
    }
    if (!swtpm_available(options.swtpm_binary)) {
        return fail(ErrorCode::kMissingDependency,
                    "swtpm 不可用：" + options.swtpm_binary.string() +
                        "（Windows 11 需要，Win10 可忽略）");
    }
    const std::filesystem::path binary =
        options.swtpm_binary.has_parent_path()
            ? options.swtpm_binary
            : find_executable(options.swtpm_binary.string());
    const auto state_dir = paths.data_dir / "vm" / "tpm";
    const auto socket_path = paths.runtime_dir / "swtpm.sock";
    return std::vector<std::string>{
        binary.string(),
        "socket",
        "--tpm2",
        "--tpmstate",
        "dir=" + state_dir.string(),
        "--ctrl",
        "type=unixio,path=" + socket_path.string(),
        "--log",
        "file=" + (paths.state_dir / "swtpm.log").string(),
    };
}

Result<std::vector<std::string>> build_qemu_args(const Config &config,
                                                 const XdgPaths &paths,
                                                 const QemuVmOptions &options,
                                                 bool install_mode) {
    std::vector<std::string> args;
    args.push_back(options.qemu_binary.string());
    args.push_back("-name");
    args.push_back(config.vm.name);
    args.push_back("-smp");
    args.push_back(std::to_string(config.vm.cpus));
    args.push_back("-m");
    args.push_back(std::to_string(config.vm.memory_mb));

    if (options.enable_kvm) {
        if (::access("/dev/kvm", R_OK | W_OK) != 0) {
            return fail(
                ErrorCode::kMissingDependency,
                "/dev/kvm 不可用；无 KVM 环境可设置 enable_kvm=false 使用 TCG");
        }
        args.push_back("-enable-kvm");
        args.push_back("-machine");
        args.push_back(arg_join(options.machine, ",accel=kvm"));
        args.push_back("-cpu");
        args.push_back("host");
    } else {
        args.push_back("-machine");
        args.push_back(arg_join(options.machine, ",accel=tcg"));
        args.push_back("-cpu");
        args.push_back("max");
    }

    if (options.enable_firmware) {
        std::filesystem::path code = options.ovmf_code;
        if (code.empty()) {
            const auto detected = detect_ovmf_code();
            if (!detected) {
                return fail(ErrorCode::kMissingDependency,
                            "未找到 OVMF_CODE.fd；可安装 OVMF 或设置 "
                            "QemuVmOptions.ovmf_code");
            }
            code = *detected;
        }
        std::error_code code_error;
        if (!std::filesystem::is_regular_file(code, code_error)) {
            return fail(ErrorCode::kMissingDependency,
                        "OVMF CODE 固件不存在: " + code.string());
        }
        const auto vars = options.ovmf_vars.empty()
                              ? paths.data_dir / "vm" / "OVMF_VARS.fd"
                              : options.ovmf_vars;
        args.push_back("-drive");
        args.push_back("if=pflash,format=raw,readonly=on,file=" +
                       code.string());
        args.push_back("-drive");
        args.push_back("if=pflash,format=raw,file=" + vars.string());
    }

    const auto disk = options.disk_image.empty()
                          ? (config.vm.disk.string().starts_with("~")
                                 ? paths.data_dir / "vm" / "disk.qcow2"
                                 : config.vm.disk)
                          : options.disk_image;
    args.push_back("-drive");
    args.push_back("file=" + disk.string() + ",if=virtio,format=qcow2");

    if (install_mode) {
        // BIOS/SeaBIOS：显式优先从 CD-ROM 启动，避免落到 PXE。
        args.push_back("-boot");
        args.push_back("order=d");
        if (config.vm.windows_iso.empty()) {
            return fail(
                ErrorCode::kConfigInvalid,
                "安装模式需要 Windows ISO 路径（--iso 或配置 vm.windows_iso）");
        }
        args.push_back("-drive");
        args.push_back("file=" + config.vm.windows_iso.string() +
                       ",media=cdrom,readonly=on");
        if (!config.vm.virtio_iso.empty()) {
            args.push_back("-drive");
            args.push_back("file=" + config.vm.virtio_iso.string() +
                           ",media=cdrom,readonly=on");
        }
        args.push_back("-display");
        args.push_back("gtk");
    } else {
        if (!options.enable_firmware) {
            args.push_back("-boot");
            args.push_back("order=c");
        }
        args.push_back("-display");
        args.push_back("none");
    }

    std::string netdev = "user,id=net0,hostfwd=tcp:127.0.0.1:" +
                         std::to_string(config.network.ssh_host_port) +
                         "-:22,hostfwd=tcp:127.0.0.1:" +
                         std::to_string(config.network.smb_host_port) + "-:445";
    // QEMU user-mode SMB 需要本机 smbd；缺失时跳过，避免 QEMU 启动即退出。
    const bool smbd_available = !find_executable("smbd").empty();
    if (!options.home_dir.empty() && smbd_available) {
        netdev += ",smb=" + options.home_dir.string();
    }
    args.push_back("-netdev");
    args.push_back(netdev);
    args.push_back("-device");
    args.push_back("virtio-net-pci,netdev=net0");

    args.push_back("-qmp");
    args.push_back("unix:" + (paths.runtime_dir / "qmp.sock").string() +
                   ",server=on,wait=off");
    args.push_back("-chardev");
    args.push_back("socket,path=" + (paths.runtime_dir / "qga.sock").string() +
                   ",server=on,wait=off,id=qga0");
    args.push_back("-device");
    args.push_back("virtio-serial");
    args.push_back("-device");
    args.push_back("virtserialport,chardev=qga0,name=org.qemu.guest_agent.0");

    if (options.enable_swtpm) {
        if (auto swtpm = build_swtpm_args(options, paths); swtpm) {
            args.push_back("-chardev");
            args.push_back("socket,id=chrtpm,path=" +
                           (paths.runtime_dir / "swtpm.sock").string());
            args.push_back("-tpmdev");
            args.push_back("emulator,id=tpm0,chardev=chrtpm");
            args.push_back("-device");
            args.push_back("tpm-tis,tpmdev=tpm0");
        } else {
            return std::unexpected(swtpm.error());
        }
    }
    return args;
}

QemuVm::QemuVm(Config config, XdgPaths paths, QemuVmOptions options)
    : config_(std::move(config)), paths_(std::move(paths)),
      options_(std::move(options)) {
    disk_image_ = options_.disk_image;
    ovmf_vars_ = options_.ovmf_vars.empty()
                     ? paths_.data_dir / "vm" / "OVMF_VARS.fd"
                     : options_.ovmf_vars;
    qmp_socket_ = paths_.runtime_dir / "qmp.sock";
    qga_socket_ = paths_.runtime_dir / "qga.sock";
    swtpm_socket_ = paths_.runtime_dir / "swtpm.sock";
    if (options_.home_dir.empty()) {
        if (const char *home = std::getenv("HOME"); home != nullptr) {
            options_.home_dir = home;
        }
    }
}

Result<void> QemuVm::launch(bool install_mode) {
    if (running()) {
        return fail(ErrorCode::kQemuError, "QEMU 已在运行");
    }
    process_.reset();
    qmp_.reset();
    swtpm_process_.reset();

    std::error_code error;
    std::filesystem::create_directories(paths_.data_dir / "vm", error);
    std::filesystem::create_directories(paths_.runtime_dir, error);
    std::filesystem::create_directories(paths_.state_dir, error);
    if (error) {
        return fail(ErrorCode::kIoError,
                    "创建运行目录失败: " + error.message());
    }

    auto args_result = build_qemu_args(config_, paths_, options_, install_mode);
    if (!args_result) {
        return std::unexpected(args_result.error());
    }
    last_args_ = *args_result;

    if (options_.enable_swtpm) {
        auto swtpm_args = build_swtpm_args(options_, paths_);
        if (!swtpm_args) {
            return std::unexpected(swtpm_args.error());
        }
        auto swtpm = core::ChildProcess::spawn(
            *swtpm_args, {.stdout_file = paths_.state_dir / "swtpm.log",
                          .stderr_file = paths_.state_dir / "swtpm.log"});
        if (!swtpm) {
            return std::unexpected(swtpm.error());
        }
        swtpm_process_ = std::move(*swtpm);
        for (int i = 0; i < 40; ++i) {
            if (std::filesystem::exists(swtpm_socket_)) {
                break;
            }
            ::usleep(50 * 1000);
        }
    }

    auto process = core::ChildProcess::spawn(
        last_args_, {.stdout_file = paths_.state_dir / "qemu.log",
                     .stderr_file = paths_.state_dir / "qemu.log"});
    if (!process) {
        return std::unexpected(process.error());
    }
    process_ = std::move(*process);

    qmp_.emplace(QmpOptions{qmp_socket_, options_.qmp_connect_timeout,
                            std::chrono::milliseconds(5000)});
    if (auto connected = qmp_->connect(); !connected) {
        std::string detail = connected.error().message;
        if (!process_->running()) {
            detail += "（QEMU 已退出，退出码 " +
                      std::to_string(process_->exit_code().value_or(-1)) + "）";
        }
        const std::string log_tail =
            read_file_tail(paths_.state_dir / "qemu.log");
        if (!log_tail.empty()) {
            detail += "\nQEMU 输出:\n" + log_tail;
        }
        (void)process_->kill();
        (void)process_->wait();
        process_.reset();
        qmp_.reset();
        return fail(ErrorCode::kQemuError, detail);
    }
    return {};
}

Result<void> QemuVm::start() { return launch(false); }
Result<void> QemuVm::start_install() { return launch(true); }

bool QemuVm::running() { return process_.has_value() && process_->running(); }

int QemuVm::pid() const noexcept {
    return process_.has_value() ? process_->pid() : -1;
}

Result<VmStatus> QemuVm::status() {
    if (!running()) {
        return VmStatus::kStopped;
    }
    if (!qmp_.has_value() || !qmp_->connected()) {
        return fail(ErrorCode::kQemuError, "QMP 未连接，无法查询状态");
    }
    auto result = qmp_->execute("query-status");
    if (!result) {
        return std::unexpected(result.error());
    }
    const std::string status = result->value("status", "unknown");
    if (status == "running") {
        return VmStatus::kRunning;
    }
    if (status == "paused") {
        return VmStatus::kPaused;
    }
    if (status == "shutdown" || status == "prelaunch") {
        return VmStatus::kStopped;
    }
    return VmStatus::kUnknown;
}

Result<void> QemuVm::shutdown() {
    if (!running()) {
        qmp_.reset();
        swtpm_process_.reset();
        return {};
    }
    if (qmp_.has_value() && qmp_->connected()) {
        (void)qmp_->system_powerdown(); // 失败则走信号兜底
    }
    if (auto waited = process_->wait_for(options_.shutdown_timeout); waited) {
        qmp_.reset();
        swtpm_process_.reset();
        return {};
    }
    (void)process_->terminate();
    if (auto waited = process_->wait_for(std::chrono::seconds(5)); waited) {
        qmp_.reset();
        swtpm_process_.reset();
        return {};
    }
    (void)process_->kill();
    (void)process_->wait();
    qmp_.reset();
    swtpm_process_.reset();
    return {};
}

Result<int> QemuVm::wait() {
    if (!process_.has_value()) {
        return fail(ErrorCode::kQemuError, "QEMU 未启动");
    }
    auto result = process_->wait();
    qmp_.reset();
    swtpm_process_.reset();
    return result;
}

Status QemuVm::kill() {
    if (process_.has_value()) {
        (void)process_->kill();
        (void)process_->wait();
    }
    qmp_.reset();
    swtpm_process_.reset();
    return {};
}

} // namespace lsw::vm

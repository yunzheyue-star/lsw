#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <lsw/vm/qemu.hpp>

#include "test_support.hpp"

#ifndef LSW_FAKE_QEMU_PATH
#error "测试需要 CMake 定义 LSW_FAKE_QEMU_PATH"
#endif

using lsw::Config;
using lsw::ErrorCode;
using lsw::PathEnvironment;
using lsw::XdgPaths;
using lsw::vm::QemuVm;
using lsw::vm::QemuVmOptions;
using lsw::vm::VmStatus;

namespace {

struct VmFixture {
  lsw::test::TempDir temp{"qemu-vm"};
  XdgPaths paths;
  Config config;

  VmFixture() {
    PathEnvironment env;
    env.home = temp.path();
    env.xdg_config_home = temp.path() / "config";
    env.xdg_data_home = temp.path() / "data";
    env.xdg_state_home = temp.path() / "state";
    env.xdg_runtime_dir = temp.path() / "run";
    auto resolved = lsw::resolve_paths(env);
    REQUIRE(resolved.has_value());
    paths = *resolved;
    REQUIRE(lsw::ensure_directories(paths).has_value());

    config = lsw::default_config();
    config.vm.disk = temp.path() / "disk.qcow2";
    std::ofstream(config.vm.disk).close();
  }

  QemuVmOptions options() const {
    QemuVmOptions options;
    options.qemu_binary = LSW_FAKE_QEMU_PATH;
    options.enable_kvm = false;
    options.enable_firmware = false;
    options.disk_image = config.vm.disk;
    options.home_dir = temp.path();
    options.qmp_connect_timeout = std::chrono::milliseconds(3000);
    options.shutdown_timeout = std::chrono::milliseconds(2000);
    return options;
  }
};

}  // namespace

TEST_CASE("QemuVm 运行模式完整生命周期（假 QEMU）", "[vm]") {
  VmFixture fixture;
  QemuVm vm(fixture.config, fixture.paths, fixture.options());

  const auto started = vm.start();
  if (!started) {
    std::ifstream log_file(fixture.paths.state_dir / "qemu.log");
    const std::string log((std::istreambuf_iterator<char>(log_file)),
                          std::istreambuf_iterator<char>());
    FAIL("vm.start() 失败: " << started.error().message
                             << " | qemu.log: " << log);
  }
  CHECK(vm.pid() > 0);
  CHECK(vm.running());
  CHECK(std::filesystem::exists(fixture.paths.runtime_dir / "qmp.sock"));

  const auto args = vm.last_args();
  REQUIRE(args.size() > 1);
  bool display_none = false;
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == "-display" && args[i + 1] == "none") {
      display_none = true;
    }
  }
  CHECK(display_none);

  const auto status = vm.status();
  REQUIRE(status.has_value());
  CHECK(*status == VmStatus::kRunning);

  REQUIRE(vm.shutdown().has_value());
  const auto stopped = vm.status();
  REQUIRE(stopped.has_value());
  CHECK(*stopped == VmStatus::kStopped);
  CHECK_FALSE(std::filesystem::exists(fixture.paths.runtime_dir / "qmp.sock"));
}

TEST_CASE("QemuVm 安装模式使用 gtk", "[vm]") {
  VmFixture fixture;
  fixture.config.vm.windows_iso = fixture.temp.path() / "win11.iso";
  std::ofstream(fixture.config.vm.windows_iso).close();

  QemuVm vm(fixture.config, fixture.paths, fixture.options());
  REQUIRE(vm.start_install().has_value());

  const auto args = vm.last_args();
  bool display_gtk = false;
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == "-display" && args[i + 1] == "gtk") {
      display_gtk = true;
    }
  }
  CHECK(display_gtk);
  REQUIRE(vm.kill().has_value());
  CHECK_FALSE(vm.running());
}

TEST_CASE("KVM 不可用时不启动 QEMU", "[vm]") {
  if (std::filesystem::exists("/dev/kvm")) {
    SUCCEED("当前环境存在 /dev/kvm，跳过");
    return;
  }
  VmFixture fixture;
  QemuVmOptions options = fixture.options();
  options.enable_kvm = true;
  QemuVm vm(fixture.config, fixture.paths, options);

  const auto result = vm.start();
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code == ErrorCode::kMissingDependency);
  CHECK_FALSE(vm.running());
}

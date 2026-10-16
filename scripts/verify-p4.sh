#!/usr/bin/env bash
# ============================================================
# LSW P4 验收脚本 · lsw --install
# 用法:   bash scripts/verify-p4.sh
# 退出码: 0 = 全部通过（SKIP 不算失败）, 1 = 存在失败项
# 说明:   - 完整安装流程用假 QEMU + 临时目录验证
#         - 真机依赖检查路径按当前环境自动跳过或断言
# ============================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

PASS=0
FAIL=0
SKIP=0
ok() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
ng() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
skip() { printf '  [SKIP] %s\n' "$1"; SKIP=$((SKIP + 1)); }
section() { printf '\n== %s ==\n' "$1"; }
check_file() {
  if [ -f "$1" ]; then ok "存在 $1"; else ng "缺失 $1"; fi
}
check_grep() {
  if grep -q -- "$2" "$1" 2>/dev/null; then ok "$3"; else ng "$3"; fi
}

# ---------- P4-1 依赖检查 ----------
section "P4-1 · 依赖检查与指引"
check_file "include/lsw/vm/install.hpp"
check_file "src/vm/install.cpp"
check_grep "src/vm/install.cpp" "qemu-img" "检查 qemu-img 与 QEMU"
check_grep "src/vm/install.cpp" "usermod -aG kvm" "KVM 缺失时给出修复指引"
check_grep "src/vm/install.cpp" "OVMF_CODE" "检查 OVMF 固件"
check_grep "src/vm/install.cpp" "swtpm" "检查 swtpm（可选）"
check_grep "src/vm/qemu.cpp" "OVMF_CODE.4m.fd" "收录 Arch edk2-ovmf 路径"
check_grep "src/vm/install.cpp" "edk2-ovmf" "按发行版给出 OVMF 安装提示"
check_grep "src/vm/install.cpp" "pacman" "Arch 提示使用 pacman"
if grep -q 'smb=' src/vm/qemu.cpp 2>/dev/null; then
  ng "QEMU 运行参数不应再使用 user SMB（P6 起改 host Samba + 10.0.2.2）"
else
  ok "QEMU 运行参数不再使用 user SMB（host Samba）"
fi
check_grep "src/vm/install.cpp" 'find_executable("smbd")' "安装依赖检查包含 smbd（host Samba）"
check_grep "src/vm/qemu.cpp" "QEMU 输出" "QMP 失败时携带 QEMU 日志"

# ---------- P4-2 ISO 校验 ----------
section "P4-2 · ISO 交互与校验"
check_grep "src/vm/install.cpp" "isatty" "交互式输入保护（非 TTY 报错）"
check_grep "src/vm/install.cpp" ".iso" "ISO 扩展名校验"
check_grep "src/cli/main.cpp" "--iso" "CLI 提供 --iso / --virtio-iso"

# ---------- P4-3 磁盘与 UEFI 变量 ----------
section "P4-3 · 磁盘与 UEFI 变量"
check_grep "src/vm/install.cpp" "qcow2" "qemu-img 创建 qcow2"
check_grep "src/vm/install.cpp" "allow_existing_disk" "安装磁盘外部覆盖保护"
check_grep "src/vm/install.cpp" "OVMF_VARS" "复制 OVMF_VARS 模板"
check_grep "tests/unit/test_install.cpp" "幂等" "覆盖重复执行不覆盖测试"

# ---------- P4-4 安装执行 ----------
section "P4-4 · 安装模式执行"
check_grep "src/vm/install.cpp" "start_install" "调用 QemuVm 安装模式"
check_grep "src/vm/install.cpp" "kQemuError" "QEMU 异常退出映射错误码"

# ---------- P4-5 安装后引导 ----------
section "P4-5 · 安装后引导"
check_file "scripts/post-install.ps1"
check_grep "scripts/post-install.ps1" "sshd" "post-install 检查 OpenSSH"
check_grep "scripts/post-install.ps1" "qemu-ga" "post-install 检查 QEMU Guest Agent"
check_grep "scripts/post-install.ps1" "net use Z:" "post-install 检查 Z: 映射"

# ---------- 代码格式 ----------
section "代码格式（.clang-format）"
CPP_FILES="$(find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) | sort)"
if [ -z "$CPP_FILES" ]; then
  ng "未找到 C++ 源文件"
else
  if echo "$CPP_FILES" | xargs clang-format --style=file --dry-run --Werror \
      >/tmp/lsw-p4-format.log 2>&1; then
    ok "全部 C++ 文件符合 .clang-format（$(echo "$CPP_FILES" | wc -l) 个）"
  else
    ng "存在不符合 .clang-format 的文件"
    head -10 /tmp/lsw-p4-format.log | sed 's/^/       /'
  fi
fi

# ---------- 构建与测试 ----------
section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
  if cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      >/tmp/lsw-p4-configure.log 2>&1; then
    ok "CMake 配置成功"
  else
    ng "CMake 配置失败"
    tail -15 /tmp/lsw-p4-configure.log | sed 's/^/       /'
  fi
fi

if cmake --build build -j >/tmp/lsw-p4-build.log 2>&1; then
  ok "构建成功"
else
  ng "构建失败"
  tail -15 /tmp/lsw-p4-build.log | sed 's/^/       /'
fi

if ctest --test-dir build --output-on-failure >/tmp/lsw-p4-ctest.log 2>&1; then
  total="$(grep -oE 'out of [0-9]+' /tmp/lsw-p4-ctest.log | grep -oE '[0-9]+' | head -1)"
  if [ "${total:-0}" -ge 72 ]; then
    ok "ctest 100% 通过（${total} 项）"
  else
    ng "ctest 通过但测试数仅 ${total}（期望 ≥72）"
  fi
else
  ng "ctest 失败"
  tail -20 /tmp/lsw-p4-ctest.log | sed 's/^/       /'
fi

# ---------- CLI 行为 ----------
section "CLI 行为"
version_output="$(./build/lsw --version 2>&1)"
if printf '%s' "$version_output" | grep -q "lsw 1.0.0"; then
  ok "lsw --version 输出正确"
else
  ng "lsw --version 输出异常：$version_output"
fi
if ./build/lsw --help 2>&1 | grep -q -- "--install"; then
  ok "lsw --help 列出 --install"
else
  ng "lsw --help 缺少 --install"
fi
if ./build/lsw --help 2>&1 | grep -qE '^  install '; then
  ok "lsw --help 的 SUBCOMMANDS 列出 install"
else
  ng "lsw --help 缺少 install 子命令"
fi
if ./build/lsw install --help 2>&1 | grep -q -- "--iso"; then
  ok "lsw install --help 列出 --iso"
else
  ng "lsw install --help 缺少安装选项"
fi
alias_extra_out="$(./build/lsw install "$ROOT/not-an-option.iso" 2>&1)"
alias_extra_rc=$?
if [ "$alias_extra_rc" -eq 2 ] &&
  printf '%s' "$alias_extra_out" | grep -q "路径必须配合选项"; then
  ok "lsw install 多余位置参数给出友好提示（退出码 2）"
else
  ng "lsw install 位置参数提示异常：退出码 $alias_extra_rc"
  printf '%s\n' "$alias_extra_out" | sed 's/^/       /'
fi
extra_out="$(./build/lsw --install "$ROOT/not-an-option.iso" 2>&1)"
extra_rc=$?
if [ "$extra_rc" -eq 2 ] && printf '%s' "$extra_out" | grep -q "路径必须配合选项"; then
  ok "多余位置参数给出友好提示（退出码 2）"
else
  ng "位置参数提示异常：退出码 $extra_rc"
  printf '%s\n' "$extra_out" | sed 's/^/       /'
fi

# 依赖缺失路径：仅在确实缺 KVM 或 OVMF 的环境断言退出码 3
if [ ! -e /dev/kvm ] ||
   { [ ! -f /usr/share/OVMF/OVMF_CODE.fd ] &&
     [ ! -f /usr/share/OVMF/OVMF_CODE.4m.fd ] &&
     [ ! -f /usr/share/edk2/ovmf/OVMF_CODE.fd ] &&
     [ ! -f /usr/share/edk2/x64/OVMF_CODE.4m.fd ]; }; then
  TMP_DEPS="$(mktemp -d)"
  # 放两个空 .iso，避免 ISO 存在性校验抢先于依赖错误（安装器应先报依赖缺失）。
  : >"$TMP_DEPS/win.iso"
  : >"$TMP_DEPS/virtio.iso"
  HOME="$TMP_DEPS/home" XDG_CONFIG_HOME="$TMP_DEPS/config" \
    XDG_DATA_HOME="$TMP_DEPS/data" XDG_STATE_HOME="$TMP_DEPS/state" \
    XDG_RUNTIME_DIR="$TMP_DEPS/run" \
    timeout 20 ./build/lsw --install --iso "$TMP_DEPS/win.iso" \
      --virtio-iso "$TMP_DEPS/virtio.iso" --disk "$TMP_DEPS/disk.qcow2" \
      >"$TMP_DEPS/out.log" 2>&1
  rc=$?
  if [ "$rc" -eq 3 ] && grep -q "缺少安装依赖" "$TMP_DEPS/out.log"; then
    ok "依赖缺失时退出码 3 且含修复指引"
  else
    ng "依赖缺失路径异常：退出码 $rc"
    sed 's/^/       /' "$TMP_DEPS/out.log"
  fi
  HOME="$TMP_DEPS/home" XDG_CONFIG_HOME="$TMP_DEPS/config" \
    XDG_DATA_HOME="$TMP_DEPS/data" XDG_STATE_HOME="$TMP_DEPS/state" \
    XDG_RUNTIME_DIR="$TMP_DEPS/run" \
    timeout 20 ./build/lsw install --iso "$TMP_DEPS/win.iso" \
      --virtio-iso "$TMP_DEPS/virtio.iso" --disk "$TMP_DEPS/disk.qcow2" \
      >"$TMP_DEPS/install-alias.log" 2>&1
  alias_rc=$?
  if [ "$alias_rc" -eq 3 ] &&
    grep -q "缺少安装依赖" "$TMP_DEPS/install-alias.log"; then
    ok "lsw install 与 --install 走同一依赖检查（退出码 3）"
  else
    ng "lsw install 依赖路径异常：退出码 $alias_rc"
    sed 's/^/       /' "$TMP_DEPS/install-alias.log"
  fi
  rm -rf "$TMP_DEPS"
else
  skip "本机 KVM/OVMF 齐全，跳过依赖缺失断言"
fi

# ---------- 完整安装流程（假 QEMU） ----------
section "完整安装流程（假 QEMU）"
TMP_INSTALL="$(mktemp -d)"
if timeout 60 ./build/tests/lsw_install_probe ./build/tests/lsw_fake_qemu \
    "$TMP_INSTALL" >"$TMP_INSTALL/out.log" 2>&1; then
  ok "安装探针返回成功"
  if grep -q "INSTALL_OK" "$TMP_INSTALL/out.log"; then
    ok "输出包含 INSTALL_OK"
  else
    ng "输出缺少 INSTALL_OK"
  fi
  if [ -f "$TMP_INSTALL/vm/disk.qcow2" ]; then
    ok "系统盘 qcow2 已创建"
  else
    ng "系统盘 qcow2 未创建"
  fi
  if [ -f "$TMP_INSTALL/data/lsw/vm/OVMF_VARS.fd" ]; then
    ok "OVMF_VARS.fd 已复制"
  else
    ng "OVMF_VARS.fd 未复制"
  fi
  if grep -q "post-install.ps1" "$TMP_INSTALL/out.log"; then
    ok "输出安装后指引"
  else
    ng "输出缺少安装后指引"
  fi
else
  ng "安装探针失败"
  tail -15 "$TMP_INSTALL/out.log" | sed 's/^/       /'
fi
rm -rf "$TMP_INSTALL"

# ---------- 文档同步 ----------
section "文档同步"
check_grep "docs/interfaces/cli.md" "P4" "cli.md 记录 --install P4 行为"
check_grep "docs/interfaces/cli.md" "lsw install" "cli.md 记录 install 子命令别名"
check_grep "docs/interfaces/qmp.md" "Installer" "qmp.md 记录 Installer"
check_grep "docs/TASKS.md" "P4 验收方式" "TASKS.md 含 P4 验收方式"
check_grep "docs/CHANGELOG.md" "Installer" "CHANGELOG 记录 Installer"
check_grep "docs/README.md" "clang-format" "公开文档写入格式化规范"

# ---------- 汇总 ----------
printf '\n========================================\n'
printf 'P4 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
  printf '✅ P4 验收通过\n'
  exit 0
fi
printf '❌ P4 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

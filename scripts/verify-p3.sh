#!/usr/bin/env bash
# ============================================================
# LSW P3 验收脚本 · QEMU / QMP
# 用法:   bash scripts/verify-p3.sh
# 退出码: 0 = 全部通过（SKIP 不算失败）, 1 = 存在失败项
# 说明:   - 单元测试使用假 QEMU，无需 KVM/固件
#         - 若本机有真 QEMU，额外做一次真实 QMP 冒烟
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

# ---------- P3-1 QmpClient ----------
section "P3-1 · QmpClient"
for f in include/lsw/vm/qmp_client.hpp src/vm/qmp_client.cpp tests/unit/test_qmp_client.cpp; do
  check_file "$f"
done
check_grep "include/lsw/vm/qmp_client.hpp" "qmp_capabilities\|handshake\|connect" "实现握手/连接流程"
check_grep "src/vm/qmp_client.cpp" '"event"' "自动跳过 QMP 异步事件"
check_grep "src/vm/qmp_client.cpp" "read_buffer_" "持久接收缓冲（多行不丢包）"

# ---------- P3-2/P3-3 QemuVm 与进程 ----------
section "P3-2/P3-3 · QemuVm 生命周期与状态"
for f in include/lsw/vm/qemu.hpp src/vm/qemu.cpp tests/unit/test_qemu_vm.cpp \
         tests/unit/test_qemu_args.cpp tests/unit/test_child_process.cpp; do
  check_file "$f"
done
check_grep "include/lsw/core/process.hpp" "class ChildProcess" "ChildProcess RAII"
check_grep "src/vm/qemu.cpp" "system_powerdown" "关机走 QMP"
check_grep "src/vm/qemu.cpp" "query-status" "状态查询走 QMP"
check_grep "src/vm/qemu.cpp" "-display\|\"none\"" "运行模式 -display none"
check_grep "CMakeLists.txt" "src/vm/\*.cpp" "lsw_core 接入 src/vm"

# ---------- P3-4 安装模式 ----------
section "P3-4 · 安装模式参数"
check_grep "src/vm/qemu.cpp" "gtk" "安装模式 -display gtk"
check_grep "src/vm/qemu.cpp" "media=cdrom,readonly=on" "挂载 Windows / virtio ISO"
check_grep "src/vm/qemu.cpp" "OVMF_CODE" "OVMF pflash 支持"

# ---------- P3-5 swtpm ----------
section "P3-5 · swtpm 可选支持"
check_grep "src/vm/qemu.cpp" "tpm-tis" "拼接 TPM 设备参数"
check_grep "src/vm/qemu.cpp" "--tpm2" "启动 swtpm --tpm2"
check_grep "src/vm/qemu.cpp" "kMissingDependency" "swtpm 缺失时报错"

# ---------- 构建与测试 ----------
section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
  if cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      >/tmp/lsw-p3-configure.log 2>&1; then
    ok "CMake 配置成功"
  else
    ng "CMake 配置失败"
    tail -15 /tmp/lsw-p3-configure.log | sed 's/^/       /'
  fi
fi

if cmake --build build -j >/tmp/lsw-p3-build.log 2>&1; then
  ok "构建成功"
else
  ng "构建失败"
  tail -15 /tmp/lsw-p3-build.log | sed 's/^/       /'
fi

for bin in build/tests/lsw_fake_qemu build/tests/lsw_qmp_probe; do
  if [ -x "$bin" ]; then ok "测试工具存在：$bin"; else ng "缺少 $bin"; fi
done

if ctest --test-dir build --output-on-failure >/tmp/lsw-p3-ctest.log 2>&1; then
  total="$(grep -oE 'out of [0-9]+' /tmp/lsw-p3-ctest.log | grep -oE '[0-9]+' | head -1)"
  if [ "${total:-0}" -ge 66 ]; then
    ok "ctest 100% 通过（${total} 项：1 冒烟 + $((total - 1)) 单元/集成）"
  else
    ng "ctest 通过但测试数仅 ${total}（期望 ≥66）"
  fi
else
  ng "ctest 失败"
  tail -20 /tmp/lsw-p3-ctest.log | sed 's/^/       /'
fi

# ---------- 真 QEMU QMP 冒烟 ----------
section "真实 QEMU QMP 冒烟（可选）"
if command -v qemu-system-x86_64 >/dev/null 2>&1; then
  TMP="$(mktemp -d)"
  QEMU_PID=""
  cleanup() {
    if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
      kill -TERM "$QEMU_PID" 2>/dev/null
      wait "$QEMU_PID" 2>/dev/null
    fi
    rm -rf "$TMP"
  }
  trap cleanup EXIT
  SOCK="$TMP/qmp.sock"
  timeout 30 qemu-system-x86_64 -machine none -m 128 -display none \
    -qmp "unix:$SOCK,server=on,wait=off" >"$TMP/qemu.log" 2>&1 &
  QEMU_PID=$!
  for _ in $(seq 1 100); do [ -S "$SOCK" ] && break; sleep 0.05; done
  if [ ! -S "$SOCK" ]; then
    ng "真实 QEMU 未创建 QMP socket"
    sed 's/^/       /' "$TMP/qemu.log"
  else
    status="$(timeout 8 ./build/tests/lsw_qmp_probe "$SOCK" query-status 2>&1)"
    if printf '%s' "$status" | grep -q '"status":"running"'; then
      ok "真实 QEMU：握手 + query-status 成功"
    else
      ng "真实 QEMU query-status 异常：$status"
    fi
    if timeout 8 ./build/tests/lsw_qmp_probe "$SOCK" quit >/dev/null 2>&1; then
      ok "真实 QEMU：quit 成功"
    else
      ng "真实 QEMU quit 失败"
    fi
  fi
  kill -TERM "$QEMU_PID" 2>/dev/null || true
  wait "$QEMU_PID" 2>/dev/null || true
  QEMU_PID=""
  trap - EXIT
  rm -rf "$TMP"
else
  skip "未安装 qemu-system-x86_64，跳过真实 QMP 冒烟（单元测试已用假 QEMU 覆盖）"
fi

# ---------- 文档同步 ----------
section "文档同步"
check_grep "docs/interfaces/qmp.md" "QemuVm" "qmp.md 记录 QemuVm 生命周期"
check_grep "docs/interfaces/qmp.md" "swtpm" "qmp.md 记录 swtpm 可选支持"
check_grep "docs/TASKS.md" "P3 验收方式" "TASKS.md 含 P3 验收方式"
check_grep "docs/CHANGELOG.md" "QmpClient" "CHANGELOG 记录 QmpClient"

# ---------- 汇总 ----------
printf '\n========================================\n'
printf 'P3 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
  printf '✅ P3 验收通过\n'
  exit 0
fi
printf '❌ P3 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

#!/usr/bin/env bash
# ============================================================
# LSW P5 验收脚本 · lsw start / lsw shutdown
# 用法:   bash scripts/verify-p5.sh
# 退出码: 0 = 全部通过（SKIP 不算失败）, 1 = 存在失败项
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
check_file() { if [ -f "$1" ]; then ok "存在 $1"; else ng "缺失 $1"; fi; }
check_grep() {
    if grep -q -- "$2" "$1" 2>/dev/null; then ok "$3"; else ng "$3"; fi
}

section "P5-1/P5-2 · 启动编排与幂等"
check_file "include/lsw/daemon/service.hpp"
check_file "src/daemon/service.cpp"
check_file "include/lsw/util/net.hpp"
check_file "src/util/net.cpp"
check_grep "src/daemon/service.cpp" "wait_for_ssh_banner" "start 校验真实 SSH banner"
check_grep "src/daemon/service.cpp" "already_running" "start 幂等标记"
check_grep "src/daemon/service.cpp" "shutdown_requested_" "shutdown 触发 lswd 退出"
check_grep "CMakeLists.txt" "add_library(lsw_daemon" "拆分 lsw_daemon 服务库"

section "P5-3/P5-4 · 关闭与清理"
check_grep "src/daemon/service.cpp" "ssh" "优先尝试 SSH 关机"
check_grep "src/daemon/service.cpp" "vm_->shutdown" "QMP/信号兜底关闭"
check_grep "src/daemon/service.cpp" "cleanup_runtime" "清理运行时资源"
check_grep "src/cli/main.cpp" "start_command" "CLI 提供 start 子命令"
check_grep "src/cli/main.cpp" "shutdown_command" "CLI 提供 shutdown 子命令"

section "测试接入"
for f in tests/unit/test_net.cpp tests/unit/test_daemon_service.cpp; do
    check_file "$f"
done
check_grep "tests/CMakeLists.txt" "lsw_daemon" "测试链接守护进程服务库"

section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-p5-configure.log 2>&1 \
        && ok "CMake 配置成功" || { ng "CMake 配置失败"; tail -15 /tmp/lsw-p5-configure.log; }
fi
if cmake --build build -j >/tmp/lsw-p5-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -15 /tmp/lsw-p5-build.log | sed 's/^/       /'
fi
if ctest --test-dir build --output-on-failure >/tmp/lsw-p5-ctest.log 2>&1; then
    total="$(grep -oE 'out of [0-9]+' /tmp/lsw-p5-ctest.log | grep -oE '[0-9]+' | head -1)"
    if [ "${total:-0}" -ge 81 ]; then
        ok "ctest 100% 通过（${total} 项）"
    else
        ng "ctest 通过但测试数仅 ${total}（期望 ≥81）"
    fi
else
    ng "ctest 失败"
    tail -20 /tmp/lsw-p5-ctest.log | sed 's/^/       /'
fi

section "代码格式（.clang-format）"
CPP_FILES="$(find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) | sort)"
if echo "$CPP_FILES" | xargs clang-format --style=file --dry-run --Werror \
    >/tmp/lsw-p5-format.log 2>&1; then
    ok "全部 C++ 文件符合 .clang-format（$(echo "$CPP_FILES" | wc -l) 个）"
else
    ng "存在不符合 .clang-format 的文件"
    head -10 /tmp/lsw-p5-format.log | sed 's/^/       /'
fi

section "Doxygen 文件头"
missing_doc="$(grep -rL '@file' include src tests --include='*.hpp' --include='*.cpp' 2>/dev/null || true)"
if [ -z "$missing_doc" ]; then
    ok "全部 C++ 文件包含 Doxygen @file 文件头"
else
    ng "以下文件缺少 Doxygen 文件头:"
    printf '%s\n' "$missing_doc" | sed 's/^/       /'
fi

section "真实 lswd + CLI E2E（假 QEMU）"
TMP="$(mktemp -d)"
LSWD_PID=""
cleanup() {
    if [ -n "$LSWD_PID" ] && kill -0 "$LSWD_PID" 2>/dev/null; then
        kill -TERM "$LSWD_PID" 2>/dev/null
        wait "$LSWD_PID" 2>/dev/null
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

mkdir -p "$TMP/home" "$TMP/run"
export HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/config" \
    XDG_DATA_HOME="$TMP/data" XDG_STATE_HOME="$TMP/state" \
    XDG_RUNTIME_DIR="$TMP/run"
export LSW_QEMU_BINARY="$ROOT/build/tests/lsw_fake_qemu"
export LSW_QEMU_ENABLE_KVM=0 LSW_QEMU_ENABLE_FIRMWARE=0
export LSW_WAIT_SSH=0 LSW_TRY_SSH_SHUTDOWN=0
export LSW_QEMU_DISK="$TMP/disk.qcow2"

SOCKET="$XDG_RUNTIME_DIR/lsw/lswd.sock"
./build/lswd >"$TMP/lswd.log" 2>&1 &
LSWD_PID=$!
for _ in $(seq 1 50); do [ -S "$SOCKET" ] && break; sleep 0.1; done

start_output="$(timeout 30 ./build/lsw start 2>&1)"
if printf '%s' "$start_output" | grep -q '"vm_status":"running"'; then
    ok "lsw start 返回 running"
else
    ng "lsw start 异常：$start_output"
fi

second_output="$(timeout 30 ./build/lsw start 2>&1)"
if printf '%s' "$second_output" | grep -q '"already_running":true'; then
    ok "lsw start 幂等"
else
    ng "lsw start 幂等异常：$second_output"
fi

status_output="$(timeout 10 ./build/tests/lsw_ipc_probe "$SOCKET" status 2>&1)"
if printf '%s' "$status_output" | grep -q '"vm_status":"running"'; then
    ok "status 返回 running"
else
    ng "status 异常：$status_output"
fi

shutdown_output="$(timeout 30 ./build/lsw shutdown 2>&1)"
if printf '%s' "$shutdown_output" | grep -q '"stopped":true'; then
    ok "lsw shutdown 返回 stopped"
else
    ng "lsw shutdown 异常：$shutdown_output"
fi

for _ in $(seq 1 50); do kill -0 "$LSWD_PID" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$LSWD_PID" 2>/dev/null; then
    ng "shutdown 后 lswd 未退出"
else
    ok "shutdown 后 lswd 已退出"
fi
LSWD_PID=""
if [ ! -S "$SOCKET" ] && [ ! -f "$XDG_RUNTIME_DIR/lsw/lswd.pid" ]; then
    ok "socket / PID 已清理"
else
    ng "socket 或 PID 残留"
fi

section "文档同步"
check_grep "docs/interfaces/cli.md" "lsw start" "cli.md 记录 start"
check_grep "docs/interfaces/daemon-ipc.md" "DaemonService" "daemon-ipc.md 记录服务实现"
check_grep "docs/TASKS.md" "P5 验收方式" "TASKS.md 含 P5 验收方式"
check_grep "docs/CHANGELOG.md" "DaemonService" "CHANGELOG 记录 DaemonService"

printf '\n========================================\n'
printf 'P5 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ P5 验收通过\n'
    exit 0
fi
printf '❌ P5 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

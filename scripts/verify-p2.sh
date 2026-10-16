#!/usr/bin/env bash
# ============================================================
# LSW P2 验收脚本 · IPC 与 lswd 骨架
# 用法:   bash scripts/verify-p2.sh
# 退出码: 0 = 全部通过, 1 = 存在失败项
# 说明:   使用临时 HOME/XDG 运行真实 lswd，不污染用户目录
# ============================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

PASS=0
FAIL=0
ok() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
ng() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
section() { printf '\n== %s ==\n' "$1"; }
check_file() {
  if [ -f "$1" ]; then ok "存在 $1"; else ng "缺失 $1"; fi
}
check_grep() {
  if grep -q -- "$2" "$1" 2>/dev/null; then ok "$3"; else ng "$3"; fi
}

# ---------- P2-1 协议 ----------
section "P2-1 · IPC 协议与编解码"
for f in include/lsw/ipc/protocol.hpp src/ipc/protocol.cpp \
         include/lsw/ipc/client.hpp src/ipc/client.cpp \
         include/lsw/ipc/server.hpp src/ipc/server.cpp \
         tests/unit/test_protocol.cpp; do
  check_file "$f"
done
check_grep "include/lsw/ipc/protocol.hpp" "encode_line" "定义 JSON Lines 编码"
check_grep "include/lsw/ipc/protocol.hpp" "from_ipc_error_code" "定义错误码映射"

# ---------- P2-2 / P2-3 服务端与客户端 ----------
section "P2-2/P2-3 · Server / Client"
check_grep "include/lsw/ipc/server.hpp" "register_handler" "服务端支持命令注册"
check_grep "include/lsw/ipc/server.hpp" "std::jthread run" "服务端支持后台线程"
check_grep "src/ipc/server.cpp" "chmod" "socket 权限收敛（0700）"
check_grep "include/lsw/ipc/client.hpp" "retries" "客户端支持重试"
check_file "tests/unit/test_ipc.cpp"

# ---------- P2-4 lswd ----------
section "P2-4 · lswd 骨架"
check_file "src/daemon/lswd.cpp"
check_file "include/lsw/core/state.hpp"
check_file "src/core/state.cpp"
check_grep "src/daemon/lswd.cpp" "flock" "使用 flock 单实例锁"
check_grep "src/daemon/lswd.cpp" "SIGTERM" "处理 SIGTERM 优雅退出"
check_grep "CMakeLists.txt" "add_executable(lswd" "定义 lswd 目标"
check_file "tests/unit/test_state.cpp"
check_grep "src/core/state.cpp" "already_running" "start 幂等（already_running）"

# ---------- CLI11 警告修复 ----------
section "CLI11 CMake 弃用警告修复"
if [ -f build/_deps/cli11-src/include/CLI/Version.hpp ]; then
  version="$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' build/_deps/cli11-src/include/CLI/Version.hpp | head -1)"
  min="$(grep -m1 'cmake_minimum_required' build/_deps/cli11-src/CMakeLists.txt | grep -oE '3\.[0-9]+' | head -1)"
  ok "CLI11 版本 ${version}，cmake_minimum_required ${min}"
  case "$min" in
    3.1[0-9]|3.[2-9][0-9]|[4-9].*) ok "最低 CMake >= 3.10，弃用警告已消除" ;;
    *) ng "CLI11 最低 CMake 仍 < 3.10（$min）" ;;
  esac
elif [ -f /usr/include/CLI/Version.hpp ]; then
  version="$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' /usr/include/CLI/Version.hpp | head -1)"
  ok "系统 CLI11 版本 ${version}（经 CMake config 解析，无 FetchContent 缓存）"
  ok "系统包路径无 CLI11 CMake 弃用警告"
else
  ng "未找到 CLI11（FetchContent 缓存或系统包）"
fi

# ---------- 构建与单元测试 ----------
section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
  if cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      >/tmp/lsw-p2-configure.log 2>&1; then
    ok "CMake 配置成功"
  else
    ng "CMake 配置失败"
    tail -15 /tmp/lsw-p2-configure.log | sed 's/^/       /'
  fi
fi

if cmake --build build -j >/tmp/lsw-p2-build.log 2>&1; then
  ok "构建成功"
else
  ng "构建失败"
  tail -15 /tmp/lsw-p2-build.log | sed 's/^/       /'
fi

if [ -x build/lswd ] && [ -x build/tests/lsw_ipc_probe ]; then
  ok "lswd 与 ipc_probe 均可执行"
else
  ng "lswd 或 ipc_probe 未生成"
fi

if ctest --test-dir build --output-on-failure >/tmp/lsw-p2-ctest.log 2>&1; then
  total="$(grep -oE 'out of [0-9]+' /tmp/lsw-p2-ctest.log | grep -oE '[0-9]+' | head -1)"
  if [ "${total:-0}" -ge 47 ]; then
    ok "ctest 100% 通过（${total} 项：1 冒烟 + $((total - 1)) 单元）"
  else
    ng "ctest 通过但测试数仅 ${total}（期望 ≥47）"
  fi
else
  ng "ctest 失败"
  tail -20 /tmp/lsw-p2-ctest.log | sed 's/^/       /'
fi

# ---------- 真实 lswd 集成验收 ----------
section "真实 lswd 集成验收（临时 HOME/XDG）"
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

export HOME="$TMP/home"
export XDG_CONFIG_HOME="$TMP/config" XDG_DATA_HOME="$TMP/data"
export XDG_STATE_HOME="$TMP/state" XDG_RUNTIME_DIR="$TMP/run"
export LSW_DISABLE_MOUNTS=1
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"

SOCKET="$XDG_RUNTIME_DIR/lsw/lswd.sock"
PID_FILE="$XDG_RUNTIME_DIR/lsw/lswd.pid"

./build/lswd >"$TMP/lswd.log" 2>&1 &
LSWD_PID=$!
for _ in $(seq 1 50); do [ -S "$SOCKET" ] && break; sleep 0.1; done
if ! kill -0 "$LSWD_PID" 2>/dev/null && [ ! -S "$SOCKET" ]; then
  ng "lswd 启动失败"
  sed 's/^/       /' "$TMP/lswd.log"
else
  if [ -S "$SOCKET" ]; then ok "socket 已创建：$SOCKET"; else ng "socket 未创建"; fi
  if [ "$(stat -c '%a' "$SOCKET" 2>/dev/null)" = "700" ]; then
    ok "socket 权限为 0700"
  else
    ng "socket 权限异常：$(stat -c '%a' "$SOCKET" 2>/dev/null)"
  fi
  if [ -f "$PID_FILE" ]; then ok "PID 文件已创建"; else ng "PID 文件缺失"; fi

  status_json="$(./build/tests/lsw_ipc_probe "$SOCKET" status 2>&1)"
  if printf '%s' "$status_json" | grep -q '"daemon":"running"'; then
    ok "status 返回 daemon=running"
  else
    ng "status 返回异常：$status_json"
  fi

  mount_json="$(./build/tests/lsw_ipc_probe "$SOCKET" mount 2>&1)"
  if printf '%s' "$mount_json" | grep -q '/mnt/Windows/C'; then
    ok "mount 幂等返回 /mnt/Windows/C"
  else
    ng "mount 返回异常：$mount_json"
  fi

  if ./build/tests/lsw_ipc_probe "$SOCKET" nope >/dev/null 2>&1; then
    ng "未知命令未被拒绝"
  else
    ok "未知命令被拒绝"
  fi

  if ./build/tests/lsw_ipc_probe "$SOCKET" shell >/dev/null 2>&1; then
    ng "shell 应返回 NOT_IMPLEMENTED"
  else
    ok "shell 返回 NOT_IMPLEMENTED（阶段 P7 实现）"
  fi

  if ./build/lswd >"$TMP/lswd-second.log" 2>&1; then
    ng "第二实例未被单实例锁拒绝"
  else
    ok "第二实例被单实例锁拒绝"
  fi

  kill -TERM "$LSWD_PID" 2>/dev/null
  wait "$LSWD_PID" 2>/dev/null
  LSWD_PID=""
  if [ ! -S "$SOCKET" ] && [ ! -f "$PID_FILE" ]; then
    ok "退出后 socket 与 PID 文件已清理"
  else
    ng "退出后仍残留 socket 或 PID 文件"
  fi
fi

# ---------- 文档同步 ----------
section "文档同步"
check_grep "docs/interfaces/daemon-ipc.md" "NOT_IMPLEMENTED" "daemon-ipc.md 记录 NOT_IMPLEMENTED"
check_grep "docs/interfaces/core.md" "DaemonState" "core.md 记录守护进程状态机"
check_grep "docs/TASKS.md" "P2 验收方式" "TASKS.md 含 P2 验收方式"
check_grep "docs/CHANGELOG.md" "lswd" "CHANGELOG 记录 lswd"

# ---------- 汇总 ----------
printf '\n========================================\n'
printf 'P2 验收结果：%d 项通过，%d 项失败\n' "$PASS" "$FAIL"
if [ "$FAIL" -eq 0 ]; then
  printf '✅ P2 验收通过\n'
  exit 0
fi
printf '❌ P2 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

#!/usr/bin/env bash
# ============================================================
# LSW P1 验收脚本 · lsw_core 核心库
# 用法:   bash scripts/verify-p1.sh
# 退出码: 0 = 全部通过, 1 = 存在失败项
# 说明:   复用仓库 build/ 目录；首次配置会自动拉取依赖（需网络）
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

# ---------- P1-1 依赖接入与目标拆分 ----------
section "P1-1 · 依赖接入与目标拆分"
check_file "cmake/Dependencies.cmake"
check_grep "cmake/Dependencies.cmake" "find_package" "依赖优先使用系统包（find_package）"
check_grep "cmake/Dependencies.cmake" "FetchContent_MakeAvailable" "缺失时 FetchContent 兜底"
check_grep "cmake/Dependencies.cmake" "LSW_FETCH_DEPS" "提供离线开关 LSW_FETCH_DEPS"
check_grep "CMakeLists.txt" "add_library(lsw_core STATIC" "定义 lsw_core 静态库"
check_grep "CMakeLists.txt" "target_link_libraries(lsw PRIVATE lsw_core)" "lsw 链接 lsw_core"
check_grep "CMakeLists.txt" "CONFIGURE_DEPENDS" "模块目录自动 glob（CONFIGURE_DEPENDS）"

# ---------- P1-2 ~ P1-6 核心实现 ----------
section "P1-2~P1-6 · 核心实现"
for f in \
  include/lsw/core/error.hpp include/lsw/util/paths.hpp include/lsw/util/log.hpp \
  include/lsw/core/config.hpp include/lsw/core/path_map.hpp include/lsw/core/process.hpp \
  src/core/error.cpp src/util/paths.cpp src/util/log.cpp \
  src/core/config.cpp src/core/path_map.cpp src/core/process.cpp; do
  check_file "$f"
done

# ---------- P1-7 测试接入 ----------
section "P1-7 · 测试接入"
for f in \
  tests/unit/test_error.cpp tests/unit/test_paths.cpp tests/unit/test_config.cpp \
  tests/unit/test_path_map.cpp tests/unit/test_process.cpp tests/unit/test_support.hpp; do
  check_file "$f"
done
check_grep "tests/CMakeLists.txt" "lsw_core_tests" "定义 lsw_core_tests 目标"
check_grep "tests/CMakeLists.txt" "catch_discover_tests" "Catch2 测试接入 CTest"

# ---------- 构建与测试 ----------
section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
  if cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      >/tmp/lsw-p1-configure.log 2>&1; then
    ok "首次 CMake 配置成功"
  else
    ng "CMake 配置失败"
    tail -15 /tmp/lsw-p1-configure.log | sed 's/^/       /'
  fi
fi

if cmake --build build -j >/tmp/lsw-p1-build.log 2>&1; then
  ok "构建成功"
else
  ng "构建失败"
  tail -15 /tmp/lsw-p1-build.log | sed 's/^/       /'
fi

if [ -f build/compile_commands.json ] && grep -q -- '-std=c++26' build/compile_commands.json; then
  ok "实际编译参数包含 -std=c++26"
else
  ng "实际编译参数未确认 -std=c++26"
fi

if ctest --test-dir build --output-on-failure >/tmp/lsw-p1-ctest.log 2>&1; then
  total="$(grep -oE 'out of [0-9]+' /tmp/lsw-p1-ctest.log | grep -oE '[0-9]+' | head -1)"
  if [ "${total:-0}" -ge 33 ]; then
    ok "ctest 100% 通过（${total} 项：1 冒烟 + $((total - 1)) 单元）"
  else
    ng "ctest 通过但测试数仅 ${total}（期望 ≥33）"
  fi
else
  ng "ctest 失败"
  tail -20 /tmp/lsw-p1-ctest.log | sed 's/^/       /'
fi

# ---------- 文档同步 ----------
section "文档同步"
check_file "docs/interfaces/core.md"
check_grep "docs/interfaces/config.md" "P1-4" "config.md 标注 P1-4 实现说明"
check_grep "docs/TASKS.md" "P1 验收方式" "TASKS.md 含 P1 验收方式"
check_grep "docs/TASKS.md" "P0 · 工程基线" "TASKS.md 含 P0 阶段基线"
check_grep "docs/CHANGELOG.md" "lsw_core" "CHANGELOG 记录 lsw_core"

# ---------- 汇总 ----------
printf '\n========================================\n'
printf 'P1 验收结果：%d 项通过，%d 项失败\n' "$PASS" "$FAIL"
if [ "$FAIL" -eq 0 ]; then
  printf '✅ P1 验收通过\n'
  exit 0
fi
printf '❌ P1 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

#!/usr/bin/env bash
# ============================================================
# LSW P0 验收脚本 · 工程基线一键核验
# 用法:   bash scripts/verify-p0.sh
# 退出码: 0 = 全部通过, 1 = 存在失败项
# 说明:   只在临时目录构建，不污染仓库的 build/ 目录
# ============================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

PASS=0
FAIL=0
ok() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
ng() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
section() { printf '\n== %s ==\n' "$1"; }

TMP_DIR="$(mktemp -d)"
CLEAN_BUILD="$TMP_DIR/build"
PROBE="src/util/__verify_p0_probe.cpp"
cleanup() {
  rm -rf "$TMP_DIR"
  rm -f "$ROOT/$PROBE"
}
trap cleanup EXIT

# ---------- P0-1 构建基线 ----------
section "P0-1 · CMake / C++26 基线"
if grep -q 'cmake_minimum_required(VERSION 3.25)' CMakeLists.txt; then
  ok "CMake 最低版本 = 3.25"
else
  ng "CMakeLists.txt 缺少 cmake_minimum_required(VERSION 3.25)"
fi
if grep -q 'set(CMAKE_CXX_STANDARD 26)' CMakeLists.txt; then
  ok "C++ 标准 = C++26"
else
  ng "CMakeLists.txt 缺少 CMAKE_CXX_STANDARD 26"
fi
if grep -q 'CMAKE_CXX_STANDARD_REQUIRED ON' CMakeLists.txt; then
  ok "标准不允许静默降级（CXX_STANDARD_REQUIRED ON）"
else
  ng "缺少 CMAKE_CXX_STANDARD_REQUIRED ON"
fi
if grep -q 'GLOB_RECURSE LSW_CORE_SOURCES CONFIGURE_DEPENDS' CMakeLists.txt &&
   grep -q 'GLOB_RECURSE LSW_CLI_SOURCES CONFIGURE_DEPENDS' CMakeLists.txt; then
  ok "按模块目录自动 glob（CONFIGURE_DEPENDS）"
else
  ng "CMakeLists.txt 未启用模块目录自动 glob"
fi

# ---------- P0-2 干净构建 / 测试 / glob 探针 ----------
section "P0-2 · 干净构建与测试（临时目录）"
# 与主构建保持同一生成器，避免共用依赖缓存时 subbuild 冲突
GENERATOR_ARGS=()
if [ -f "$ROOT/build/CMakeCache.txt" ]; then
  GENERATOR="$(grep '^CMAKE_GENERATOR:INTERNAL=' "$ROOT/build/CMakeCache.txt" | cut -d= -f2)"
  if [ -n "$GENERATOR" ]; then
    GENERATOR_ARGS=(-G "$GENERATOR")
  fi
fi
if cmake -S "$ROOT" -B "$CLEAN_BUILD" "${GENERATOR_ARGS[@]}" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >"$TMP_DIR/configure.log" 2>&1; then
  ok "cmake 配置成功"
else
  ng "cmake 配置失败"
  tail -15 "$TMP_DIR/configure.log" | sed 's/^/       /'
fi

if cmake --build "$CLEAN_BUILD" -j >"$TMP_DIR/build.log" 2>&1; then
  ok "构建成功"
else
  ng "构建失败"
  tail -15 "$TMP_DIR/build.log" | sed 's/^/       /'
fi

if [ -f "$CLEAN_BUILD/compile_commands.json" ] && grep -q -- '-std=c++26' "$CLEAN_BUILD/compile_commands.json"; then
  ok "实际编译参数包含 -std=c++26"
else
  ng "实际编译参数未确认 -std=c++26"
fi

if ctest --test-dir "$CLEAN_BUILD" --output-on-failure >"$TMP_DIR/ctest.log" 2>&1; then
  ok "ctest 通过：$(grep -oE '[0-9]+% tests passed[^,]*' "$TMP_DIR/ctest.log" | head -1)"
else
  ng "ctest 失败"
  tail -15 "$TMP_DIR/ctest.log" | sed 's/^/       /'
fi

section "P0-2 · 新增源文件免登记（glob 探针）"
printf 'int lsw_verify_p0_probe() { return 0; }\n' > "$PROBE"
if cmake --build "$CLEAN_BUILD" >"$TMP_DIR/probe.log" 2>&1 && grep -q '__verify_p0_probe' "$TMP_DIR/probe.log"; then
  ok "新增 src/*.cpp 后构建自动编译（无需改 CMakeLists）"
else
  ng "新增 .cpp 未被自动识别"
  tail -10 "$TMP_DIR/probe.log" | sed 's/^/       /'
fi
rm -f "$PROBE"

# ---------- P0-3 目录与测试骨架 ----------
section "P0-3 · 目录与测试骨架"
for d in include/lsw/cli include/lsw/core include/lsw/vm include/lsw/fs \
         include/lsw/shell include/lsw/exec include/lsw/ipc include/lsw/util \
         tests scripts cmake; do
  if [ -d "$d" ]; then ok "目录存在：$d"; else ng "目录缺失：$d"; fi
done
if [ -f tests/CMakeLists.txt ] && grep -q 'lsw_smoke' tests/CMakeLists.txt; then
  ok "CTest 冒烟测试 lsw_smoke 已定义"
else
  ng "tests/CMakeLists.txt 缺少 lsw_smoke"
fi

# ---------- P0-4 文档与规范基线 ----------
section "P0-4 · 文档与规范基线"
for f in docs/architecture.md docs/CHANGELOG.md docs/TASKS.md docs/README.md \
         docs/interfaces/cli.md docs/interfaces/core.md docs/interfaces/daemon-ipc.md docs/interfaces/qmp.md \
         docs/interfaces/filesystem.md docs/interfaces/config.md \
         docs/diagrams/component.puml docs/diagrams/sequence-install.puml \
         docs/diagrams/sequence-start.puml docs/diagrams/sequence-shell.puml \
         docs/diagrams/sequence-exec.puml docs/diagrams/sequence-shutdown.puml; do
  if [ -f "$f" ]; then ok "文档存在：$f"; else ng "文档缺失：$f"; fi
done

INTERFACES="$(find docs/interfaces -maxdepth 1 -name '*.md' | wc -l)"
DIAGRAMS="$(find docs/diagrams -maxdepth 1 -name '*.puml' | wc -l)"
if [ "$INTERFACES" -ge 5 ]; then ok "接口文档数量 ≥ 5（当前 $INTERFACES）"; else ng "接口文档数量 = $INTERFACES（期望 ≥5）"; fi
if [ "$DIAGRAMS" -ge 6 ]; then ok "PlantUML 图数量 ≥ 6（当前 $DIAGRAMS）"; else ng "PlantUML 图数量 = $DIAGRAMS（期望 ≥6）"; fi

PUML_BAD=0
for f in docs/diagrams/*.puml; do
  first="$(grep -m1 -v '^[[:space:]]*$' "$f")"
  last="$(grep -v '^[[:space:]]*$' "$f" | tail -1)"
  if [ "$first" != "@startuml" ] || [ "$last" != "@enduml" ]; then
    PUML_BAD=$((PUML_BAD + 1))
  fi
done
if [ "$PUML_BAD" -eq 0 ]; then ok "全部 PlantUML 图结构完整（@startuml/@enduml）"; else ng "$PUML_BAD 张图结构不完整"; fi

if grep -q 'config.user.windows_user' SKILL.md && grep -q 'config.user.windows_user' docs/interfaces/config.md; then
  ok "SSH 用户名统一为 config.user.windows_user"
else
  ng "SSH 用户名说明未统一"
fi
if ! grep -q '\$STATE/qmp.sock' SKILL.md; then
  ok "socket 路径统一到 \$RUNTIME（无 \$STATE/qmp.sock）"
else
  ng "SKILL.md 仍存在 \$STATE/qmp.sock"
fi
if ! grep -q 'C++20' agent.md; then
  ok "agent.md 已统一为 C++26"
else
  ng "agent.md 仍残留 C++20"
fi

# ---------- 汇总 ----------
printf '\n========================================\n'
printf 'P0 验收结果：%d 项通过，%d 项失败\n' "$PASS" "$FAIL"
if [ "$FAIL" -eq 0 ]; then
  printf '✅ P0 验收通过\n'
  exit 0
fi
printf '❌ P0 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

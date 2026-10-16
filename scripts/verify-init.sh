#!/usr/bin/env bash
# ============================================================
# LSW `lsw init` 扩展功能验收脚本
# 用法:   bash scripts/verify-init.sh
# 退出码: 0 = 全部通过（SKIP 不算失败）, 1 = 存在失败项
# 说明:   只执行 dry-run 与只读检查，不调用包管理器、不改系统
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

TMP_DIR=""
cleanup() { [ -n "$TMP_DIR" ] && rm -rf "$TMP_DIR"; }
trap cleanup EXIT

section "init 模块"
check_file "include/lsw/core/bootstrap.hpp"
check_file "src/core/bootstrap.cpp"
check_grep "src/core/bootstrap.cpp" "detect_package_manager" "发行版/包管理器检测"
check_grep "src/core/bootstrap.cpp" "install_command" "安装命令构造"
check_grep "src/core/bootstrap.cpp" "QEMU" "QEMU 检查"
check_grep "src/core/bootstrap.cpp" "smbd" "Samba 检查"
check_grep "src/core/bootstrap.cpp" "mount.cifs" "cifs-utils 检查"
check_grep "src/core/bootstrap.cpp" "secret-tool" "libsecret 检查"
check_grep "src/core/bootstrap.cpp" "install_helper" "helper 安装"
check_grep "src/core/bootstrap.cpp" "initialize_config" "配置初始化"

section "CLI"
check_grep "src/cli/main.cpp" "init_command" "CLI init 子命令"
check_grep "src/cli/main.cpp" "--yes" "init --yes"
check_grep "src/cli/main.cpp" "--dry-run" "init --dry-run"

section "文档同步"
check_file "docs/interfaces/init.md"
check_grep "docs/interfaces/cli.md" "lsw init" "cli.md init 章节"
check_grep "docs/interfaces/init.md" "qemu-full" "init.md 包名映射"
check_grep "docs/architecture.md" "sequence-init" "architecture init 行"
check_grep "docs/diagrams/sequence-init.puml" "包管理器" "init 时序图"
check_grep "docs/CHANGELOG.md" "lsw init" "CHANGELOG 记录 init"
check_grep "docs/TASKS.md" "P12" "TASKS P12"

section "构建与测试"
if [ ! -f build/CMakeCache.txt ] || [ ! -f build/compile_commands.json ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-init-configure.log 2>&1 \
        && ok "CMake 配置成功" || { ng "CMake 配置失败"; tail -15 /tmp/lsw-init-configure.log; }
fi
BUILD_TMP="$(mktemp -d)"
if TMPDIR="$BUILD_TMP" XDG_RUNTIME_DIR="$BUILD_TMP" cmake --build build -j >/tmp/lsw-init-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -20 /tmp/lsw-init-build.log
fi
rm -rf "$BUILD_TMP"
if ctest --test-dir build >/tmp/lsw-init-ctest.log 2>&1; then
    ok "ctest 全部通过"
else
    ng "ctest 存在失败"
    tail -30 /tmp/lsw-init-ctest.log
fi
if ./build/tests/lsw_core_tests "[init]" >/dev/null 2>&1; then
    ok "init 单测通过"
else
    ng "init 单测失败"
fi

section "dry-run E2E（不产生变更）"
if [ ! -x build/lsw ]; then
    skip "缺少 build/lsw，跳过 dry-run"
else
    TMP_DIR="$(mktemp -d /tmp/lsw-init-e2e.XXXXXX)"
    mkdir -p "$TMP_DIR/home" "$TMP_DIR/config" "$TMP_DIR/data" \
        "$TMP_DIR/state" "$TMP_DIR/run"
    dry_output="$(HOME="$TMP_DIR/home" XDG_CONFIG_HOME="$TMP_DIR/config" \
        XDG_DATA_HOME="$TMP_DIR/data" XDG_STATE_HOME="$TMP_DIR/state" \
        XDG_RUNTIME_DIR="$TMP_DIR/run" \
        timeout 60 "$ROOT/build/lsw" init --dry-run 2>&1)"
    dry_rc=$?
    if [ "$dry_rc" -eq 0 ] &&
        printf '%s' "$dry_output" | grep -q '发行版:' &&
        printf '%s' "$dry_output" | grep -q '包管理器:' &&
        printf '%s' "$dry_output" | grep -q '组件检查:' &&
        printf '%s' "$dry_output" | grep -q 'dry-run'; then
        ok "init --dry-run 输出计划且退出 0"
    else
        ng "init --dry-run 异常：rc=$dry_rc $dry_output"
    fi
    if [ -f "$TMP_DIR/config/lsw/config.toml" ]; then
        ng "dry-run 不应写入 config.toml"
    else
        ok "dry-run 未产生配置变更"
    fi
    extra_output="$(HOME="$TMP_DIR/home" XDG_CONFIG_HOME="$TMP_DIR/config" \
        XDG_DATA_HOME="$TMP_DIR/data" XDG_STATE_HOME="$TMP_DIR/state" \
        XDG_RUNTIME_DIR="$TMP_DIR/run" \
        "$ROOT/build/lsw" init extra 2>&1)"
    extra_rc=$?
    if [ "$extra_rc" -eq 2 ] &&
        printf '%s' "$extra_output" | grep -q '不接受额外位置参数'; then
        ok "init 多余位置参数返回 2"
    else
        ng "init 多余参数异常：rc=$extra_rc $extra_output"
    fi
fi

section "总结"
printf '\n========================================\n'
printf 'init 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ lsw init 验收通过\n'
    exit 0
fi
printf '❌ lsw init 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

#!/usr/bin/env bash
# ============================================================
# LSW `lsw uninstall` 扩展功能验收脚本
# 用法:   bash scripts/verify-uninstall.sh
# 退出码: 0 = 全部通过（SKIP 不算失败）, 1 = 存在失败项
# 说明:   keep 路径在临时 XDG 下真实删除测试文件；purge 只跑 dry-run
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

section "uninstall 模块"
check_file "include/lsw/core/uninstall.hpp"
check_file "src/core/uninstall.cpp"
check_grep "src/core/uninstall.cpp" "vm_files" "VM 专属文件计划"
check_grep "src/core/uninstall.cpp" "purge_files" "彻底清理文件计划"
check_grep "src/core/uninstall.cpp" "load_managed_packages" "受管包"
check_grep "src/core/uninstall.cpp" "process_using" "磁盘占用检测"
check_grep "src/core/uninstall.cpp" "--force" "外部磁盘保护"

section "init 受管包记录"
check_grep "src/core/bootstrap.cpp" "managed-packages.json" "init 记录受管包"
check_grep "include/lsw/core/bootstrap.hpp" "record_managed_packages" "受管包接口"

section "CLI"
check_grep "src/cli/main.cpp" "uninstall_command" "uninstall 子命令"
check_grep "src/cli/main.cpp" "--uninstall" "--uninstall 别名"
check_grep "src/cli/main.cpp" "--purge" "--purge"
check_grep "src/cli/main.cpp" "--keep" "--keep"

section "文档同步"
check_file "docs/interfaces/uninstall.md"
check_grep "docs/interfaces/cli.md" "lsw uninstall" "cli.md uninstall 章节"
check_grep "docs/interfaces/uninstall.md" "managed-packages" "uninstall.md 受管包"
check_grep "docs/architecture.md" "sequence-uninstall" "architecture uninstall 行"
check_grep "docs/diagrams/sequence-uninstall.puml" "卸载" "uninstall 时序图"
check_grep "docs/CHANGELOG.md" "lsw uninstall" "CHANGELOG 记录 uninstall"
check_grep "docs/TASKS.md" "P13" "TASKS P13"

section "构建与测试"
if [ ! -f build/CMakeCache.txt ] || [ ! -f build/compile_commands.json ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-uninstall-configure.log 2>&1 \
        && ok "CMake 配置成功" || { ng "CMake 配置失败"; tail -15 /tmp/lsw-uninstall-configure.log; }
fi
BUILD_TMP="$(mktemp -d)"
if TMPDIR="$BUILD_TMP" XDG_RUNTIME_DIR="$BUILD_TMP" cmake --build build -j >/tmp/lsw-uninstall-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -20 /tmp/lsw-uninstall-build.log
fi
rm -rf "$BUILD_TMP"
if ctest --test-dir build >/tmp/lsw-uninstall-ctest.log 2>&1; then
    ok "ctest 全部通过"
else
    ng "ctest 存在失败"
    tail -30 /tmp/lsw-uninstall-ctest.log
fi
if ./build/tests/lsw_core_tests "[uninstall]" >/dev/null 2>&1; then
    ok "uninstall 单测通过"
else
    ng "uninstall 单测失败"
fi

section "keep / purge dry-run E2E"
if [ ! -x build/lsw ]; then
    skip "缺少 build/lsw，跳过 E2E"
else
    TMP_DIR="$(mktemp -d /tmp/lsw-uninstall-e2e.XXXXXX)"
    HOME_DIR="$TMP_DIR/home"
    DATA_DIR="$TMP_DIR/data/lsw"
    STATE_DIR="$TMP_DIR/state/lsw"
    CONFIG_DIR="$TMP_DIR/config"
    DISK="$DATA_DIR/vm/disk.qcow2"
    mkdir -p "$DATA_DIR/vm" "$STATE_DIR" "$CONFIG_DIR/lsw" "$TMP_DIR/run"
    : >"$DISK"
    : >"$DATA_DIR/vm/OVMF_VARS.fd"
    echo '{}' >"$STATE_DIR/install.json"
    echo '{}' >"$STATE_DIR/guest.json"
    cat >"$CONFIG_DIR/lsw/config.toml" <<EOF
[vm]
name = "lsw-windows"
memory_mb = 8192
cpus = 4
disk = "$DISK"
windows_iso = ""
virtio_iso = ""

[network]
ssh_host_port = 2222
smb_host_port = 1445
guest_smb_host = "10.0.2.2"
guest_smb_share = "qemu"

[filesystem]
mount_root = "/mnt/Windows"
linux_home_share = "~"
windows_drive_z = "Z:"

[user]
windows_user = "lsw"
EOF
    export HOME="$HOME_DIR"
    export XDG_CONFIG_HOME="$CONFIG_DIR" XDG_DATA_HOME="$TMP_DIR/data" \
        XDG_STATE_HOME="$TMP_DIR/state" XDG_RUNTIME_DIR="$TMP_DIR/run"

    dry_output="$("$ROOT/build/lsw" uninstall --dry-run --keep --yes 2>&1)"
    dry_rc=$?
    if [ "$dry_rc" -eq 0 ] &&
        printf '%s' "$dry_output" | grep -q '计划删除' &&
        [ -f "$DISK" ] && [ -f "$CONFIG_DIR/lsw/config.toml" ]; then
        ok "uninstall --dry-run --keep 只打印计划、不删文件"
    else
        ng "uninstall dry-run 异常：rc=$dry_rc $dry_output"
    fi

    keep_output="$("$ROOT/build/lsw" uninstall --keep --yes 2>&1)"
    keep_rc=$?
    if [ "$keep_rc" -eq 0 ] &&
        [ ! -f "$DISK" ] &&
        [ ! -f "$DATA_DIR/vm/OVMF_VARS.fd" ] &&
        [ ! -f "$STATE_DIR/install.json" ] &&
        [ ! -f "$STATE_DIR/guest.json" ] &&
        [ -f "$CONFIG_DIR/lsw/config.toml" ]; then
        ok "--keep 只删 VM 专属文件并保留配置"
    else
        ng "--keep 行为异常：rc=$keep_rc $keep_output"
    fi

    # 重新生成 VM 文件，验证 --uninstall 别名与 purge dry-run。
    : >"$DISK"
    : >"$DATA_DIR/vm/OVMF_VARS.fd"
    echo '{}' >"$STATE_DIR/install.json"
    echo '{}' >"$STATE_DIR/guest.json"
    alias_output="$("$ROOT/build/lsw" --uninstall --dry-run --purge --yes 2>&1)"
    alias_rc=$?
    if [ "$alias_rc" -eq 0 ] &&
        printf '%s' "$alias_output" | grep -q '彻底清理模式: 是' &&
        [ -f "$DISK" ] && [ -f "$CONFIG_DIR/lsw/config.toml" ]; then
        ok "--uninstall --purge --dry-run 打印彻底清理计划"
    else
        ng "--uninstall purge dry-run 异常：rc=$alias_rc $alias_output"
    fi

    conflict_output="$("$ROOT/build/lsw" uninstall --purge --keep 2>&1)"
    conflict_rc=$?
    if [ "$conflict_rc" -eq 2 ] &&
        printf '%s' "$conflict_output" | grep -q '不能同时使用'; then
        ok "--purge 与 --keep 互斥返回 2"
    else
        ng "互斥参数异常：rc=$conflict_rc $conflict_output"
    fi

    extra_output="$("$ROOT/build/lsw" uninstall extra 2>&1)"
    extra_rc=$?
    if [ "$extra_rc" -eq 2 ] &&
        printf '%s' "$extra_output" | grep -q '不接受额外位置参数'; then
        ok "uninstall 多余位置参数返回 2"
    else
        ng "uninstall 多余参数异常：rc=$extra_rc $extra_output"
    fi
fi

section "总结"
printf '\n========================================\n'
printf 'uninstall 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ lsw uninstall 验收通过\n'
    exit 0
fi
printf '❌ lsw uninstall 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

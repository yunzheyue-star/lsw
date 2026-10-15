#!/usr/bin/env bash
# ============================================================
# LSW P6 验收脚本 · 文件系统（Z: / CIFS）
# 用法:   bash scripts/verify-p6.sh
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
check_exec() { if [ -x "$1" ]; then ok "可执行 $1"; else ng "不可执行 $1"; fi; }
check_grep() {
    if grep -q -- "$2" "$1" 2>/dev/null; then ok "$3"; else ng "$3"; fi
}

section "P6-1 · Windows 映射 Z:"
check_file "include/lsw/fs/z_mapping.hpp"
check_file "src/fs/z_mapping.cpp"
check_grep "src/fs/z_mapping.cpp" "net use" "SSH 执行 net use"
check_grep "src/daemon/service.cpp" "windows_drive_z" "start 报告 Z: 配置"
check_grep "src/daemon/service.cpp" "smb_manager_" "start 启动/停止 host Samba"

section "P6-2 · Linux 挂载 C\$/D\$"
check_file "include/lsw/fs/mount.hpp"
check_file "src/fs/mount.cpp"
check_grep "src/fs/mount.cpp" "mount_drive" "按盘符挂载"
check_grep "include/lsw/fs/mount.hpp" "/proc/mounts" "解析挂载表"
check_grep "src/daemon/service.cpp" "ensure_mounts" "start 集成 CIFS 挂载"

section "P6-3 · 凭据安全"
check_file "include/lsw/fs/credentials.hpp"
check_file "src/fs/credentials.cpp"
check_grep "src/fs/credentials.cpp" "secret-tool" "keyring 读写"
check_grep "src/fs/credentials.cpp" "stdin_data" "密码仅走 stdin"
check_grep "src/cli/main.cpp" "prompt_password" "TTY 隐藏输入"
check_grep "src/cli/main.cpp" "save-password" "可选保存到 keyring"

section "P6-4 · 挂载 helper"
check_file "scripts/lsw-mount-helper"
check_exec "scripts/lsw-mount-helper"
if bash -n scripts/lsw-mount-helper 2>/dev/null; then
    ok "helper 语法检查"
else
    ng "helper 语法错误"
fi
check_grep "scripts/lsw-mount-helper" "mktemp" "临时凭据文件"
check_grep "scripts/lsw-mount-helper" "trap" "退出清理凭据"
check_grep "scripts/lsw-mount-helper" "mount -t cifs" "最小权限挂载"

section "P6-5 · 异常恢复与清理"
check_grep "src/fs/mount.cpp" "unmount_all" "卸载兜底"
check_grep "src/fs/mount.cpp" "重挂" "失败重挂"
check_grep "src/daemon/service.cpp" "unmount_all" "shutdown 先卸载"
check_grep "include/lsw/core/error.hpp" "kMountFailed" "MOUNT_FAILED 错误码"
check_grep "src/core/error.cpp" "MountFailed" "错误码字符串"

section "文档同步"
check_file "docs/interfaces/filesystem.md"
check_grep "docs/interfaces/filesystem.md" "lsw-mount-helper" "helper 协议"
check_grep "docs/interfaces/filesystem.md" "libsecret" "keyring 说明"
check_grep "docs/interfaces/cli.md" "lsw mount" "CLI mount 文档"
check_grep "docs/CHANGELOG.md" "P6" "CHANGELOG 记录 P6"
check_grep "docs/diagrams/sequence-start.puml" "net use" "start 时序图"

section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-p6-configure.log 2>&1 \
        && ok "CMake 配置成功" || { ng "CMake 配置失败"; tail -15 /tmp/lsw-p6-configure.log; }
fi
if cmake --build build -j >/tmp/lsw-p6-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -15 /tmp/lsw-p6-build.log | sed 's/^/       /'
fi
if ctest --test-dir build --output-on-failure >/tmp/lsw-p6-ctest.log 2>&1; then
    total="$(grep -oE 'out of [0-9]+' /tmp/lsw-p6-ctest.log | grep -oE '[0-9]+' | head -1)"
    if [ "${total:-0}" -ge 100 ]; then
        ok "ctest 100% 通过（${total} 项）"
    else
        ng "ctest 通过但测试数仅 ${total}（期望 ≥100）"
    fi
else
    ng "ctest 失败"
    tail -20 /tmp/lsw-p6-ctest.log | sed 's/^/       /'
fi

section "代码格式与 Doxygen"
CPP_FILES="$(find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) | sort)"
if echo "$CPP_FILES" | xargs clang-format --style=file --dry-run --Werror \
    >/tmp/lsw-p6-format.log 2>&1; then
    ok "全部 C++ 文件符合 .clang-format（$(echo "$CPP_FILES" | wc -l) 个）"
else
    ng "存在不符合 .clang-format 的文件"
    head -10 /tmp/lsw-p6-format.log | sed 's/^/       /'
fi
missing_doc="$(grep -rL '@file' include src tests --include='*.hpp' --include='*.cpp' 2>/dev/null || true)"
if [ -z "$missing_doc" ]; then
    ok "全部 C++ 文件包含 Doxygen @file 文件头"
else
    ng "以下文件缺少 Doxygen 文件头:"
    printf '%s\n' "$missing_doc" | sed 's/^/       /'
fi

section "真实 lswd + CLI E2E（假 QEMU / ssh / helper）"
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

mkdir -p "$TMP/home" "$TMP/run" "$TMP/mnt"
printf '' > "$TMP/mounts"
cat > "$TMP/lsw-mount-helper" <<'HELPER'
#!/usr/bin/env bash
echo "$*" >> "$LSW_TEST_HELPER_LOG"
if [ "$1" = "mount" ]; then
    echo "//127.0.0.1/$2 $4 cifs rw 0 0" >> "$LSW_MOUNTS_FILE"
    exit 0
fi
if [ "$1" = "unmount" ]; then
    grep -v " $2 cifs " "$LSW_MOUNTS_FILE" > "$LSW_MOUNTS_FILE.tmp" 2>/dev/null || true
    mv "$LSW_MOUNTS_FILE.tmp" "$LSW_MOUNTS_FILE"
    exit 0
fi
exit 2
HELPER
chmod 0755 "$TMP/lsw-mount-helper"
cat > "$TMP/ssh" <<'SSH'
#!/usr/bin/env bash
args=("$@")
last="${args[${#args[@]}-1]}"
script="$last"
if [ "${#args[@]}" -ge 2 ] && [ "${args[${#args[@]}-2]}" = "-EncodedCommand" ]; then
    script=$(python3 -c 'import base64,sys; print(base64.b64decode(sys.argv[1]).decode("utf-16le"))' "$last")
fi
echo "$*" >> "$LSW_TEST_SSH_LOG"
echo "$script" >> "$LSW_TEST_SSH_LOG"
if [[ "$script" == *"net use"* ]]; then exit 0; fi
if [[ "$script" == *"exit 7"* ]]; then exit 7; fi
exit 0
SSH
chmod 0755 "$TMP/ssh"

export HOME="$TMP/home" XDG_CONFIG_HOME="$TMP/config" \
    XDG_DATA_HOME="$TMP/data" XDG_STATE_HOME="$TMP/state" \
    XDG_RUNTIME_DIR="$TMP/run"
unset LSW_DISABLE_MOUNTS LSW_DISABLE_Z_MAPPING
export LSW_DISABLE_HOST_SMB=1
export LSW_QEMU_BINARY="$ROOT/build/tests/lsw_fake_qemu"
export LSW_QEMU_ENABLE_KVM=0 LSW_QEMU_ENABLE_FIRMWARE=0
export LSW_WAIT_SSH=0 LSW_TRY_SSH_SHUTDOWN=0
export LSW_QEMU_DISK="$TMP/disk.qcow2"
export LSW_MOUNT_HELPER="$TMP/lsw-mount-helper" LSW_MOUNT_PRIVILEGE=none
export LSW_MOUNT_ROOT="$TMP/mnt" LSW_MOUNTS_FILE="$TMP/mounts"
export LSW_SSH_BINARY="$TMP/ssh"
export LSW_WINDOWS_PASSWORD="p@ss-no-log"
export LSW_TEST_HELPER_LOG="$TMP/helper.log"
export LSW_TEST_SSH_LOG="$TMP/ssh.log"

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
if printf '%s' "$start_output" | grep -q '"z_drive":"Z:"'; then
    ok "start 返回 z_drive=Z:"
else
    ng "start 未返回 z_drive：$start_output"
fi
if printf '%s' "$start_output" | grep -q "$TMP/mnt/C" &&
    printf '%s' "$start_output" | grep -q "$TMP/mnt/D"; then
    ok "start 返回 C/D 挂载点"
else
    ng "start 挂载点异常：$start_output"
fi
if grep -q 'mount C' "$TMP/helper.log" 2>/dev/null; then
    ok "helper CIFS 挂载被调用（Z: 延迟到使用会话）"
else
    ng "helper 挂载日志缺失"
fi
shutdown_output="$(timeout 30 ./build/lsw shutdown 2>&1)"
if printf '%s' "$shutdown_output" | grep -q '"stopped":true'; then
    ok "lsw shutdown 返回 stopped"
else
    ng "lsw shutdown 异常：$shutdown_output"
fi
for _ in $(seq 1 50); do kill -0 "$LSWD_PID" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$LSWD_PID" 2>/dev/null; then
    ng "lswd 未在 shutdown 后退出"
else
    ok "lswd 已退出"
    LSWD_PID=""
fi
if grep -q 'unmount' "$TMP/helper.log" 2>/dev/null; then
    ok "shutdown 调用 helper 卸载"
else
    ng "shutdown 未调用 helper 卸载"
fi
if grep -q 'p@ss-no-log' "$TMP/lswd.log" "$TMP/helper.log" 2>/dev/null ||
    printf '%s' "$start_output" | grep -q 'p@ss-no-log'; then
    ng "检测到密码出现在日志/输出中"
else
    ok "密码未出现在日志/输出中"
fi

section "总结"
printf '\n========================================\n'
printf 'P6 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ P6 验收通过\n'
    exit 0
fi
printf '❌ P6 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

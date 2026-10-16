#!/usr/bin/env bash
# ============================================================
# LSW P8 验收脚本 · lsw <program>（.exe / .bat / .cmd）
# 用法:   bash scripts/verify-p8.sh
# 退出码: 0 = 全部通过（SKIP 不算失败）, 1 = 存在失败项
# 说明:   - .bat 为一等公民；假 QEMU + 假 ssh 验证 stdout/stderr/退出码
#         - 真机 .bat 验收由使用者在已安装 Windows 的环境执行
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

E2E_DIR=""
LSWD_PID=""
cleanup() {
    set +e
    if [ -n "$LSWD_PID" ]; then
        timeout 20 "$ROOT/build/lsw" shutdown >/dev/null 2>&1
        kill "$LSWD_PID" >/dev/null 2>&1
    fi
    [ -n "$E2E_DIR" ] && rm -rf "$E2E_DIR"
}
trap cleanup EXIT

# ---------- P8-1 路径解析与临时复制 ----------
section "P8-1 · 路径解析与临时复制"
check_file "include/lsw/exec/windows_runner.hpp"
check_file "src/exec/windows_runner.cpp"
check_grep "CMakeLists.txt" "src/exec" "lsw_core 收录 exec 模块"
check_grep "src/exec/windows_runner.cpp" "resolve_target" "路径解析"
check_grep "src/exec/windows_runner.cpp" "\\.lsw" "越界复制到 ~/.lsw/tmp"
check_grep "src/exec/windows_runner.cpp" "cleanup_stale_temp" "临时目录清理策略"
check_grep "src/exec/windows_runner.cpp" "is_gui_executable" "PE GUI 子系统检测"

# ---------- P8-2 SSH 执行与透传 ----------
section "P8-2 · SSH 执行与透传"
check_grep "src/exec/windows_runner.cpp" "BatchMode=yes" "非交互 SSH"
check_grep "src/exec/windows_runner.cpp" "EncodedCommand" "EncodedCommand"
check_grep "src/exec/windows_runner.cpp" 'LASTEXITCODE' "退出码透传"
check_grep "src/exec/windows_runner.cpp" 'Copy-Item' "Windows 本地副本执行"
check_grep "src/exec/windows_runner.cpp" "\\.bat" "支持 .bat"
check_grep "src/daemon/service.cpp" "on_exec" "daemon exec 处理"
check_grep "include/lsw/ipc/protocol.hpp" "PROGRAM_FAILED" "PROGRAM_FAILED 错误码"

# ---------- P8-3 CLI 与 GUI 提示 ----------
section "P8-3 · CLI 与 GUI 提示"
check_grep "src/cli/main.cpp" "run_exec_command" "CLI exec 入口"
check_grep "src/cli/main.cpp" "app.remaining" "解析位置参数"
check_grep "src/cli/main.cpp" "exit_code & 0xFF" "退出码透传"
check_grep "src/cli/main.cpp" "looks_like_program" "拼写错误按未知命令拒绝"
check_grep "src/exec/windows_runner.cpp" "暂不支持 GUI" "GUI 明确提示"

# ---------- 文档同步 ----------
section "文档同步"
check_grep "docs/interfaces/cli.md" "\\.bat" "cli.md 说明 .bat"
check_grep "docs/interfaces/daemon-ipc.md" "PROGRAM_FAILED" "daemon-ipc PROGRAM_FAILED"
check_grep "docs/diagrams/sequence-exec.puml" "exec" "exec 时序图"
check_grep "docs/CHANGELOG.md" "P8" "CHANGELOG 记录 P8"

# ---------- 构建与测试 ----------
section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-p8-configure.log 2>&1 \
        && ok "CMake 配置成功" || { ng "CMake 配置失败"; tail -15 /tmp/lsw-p8-configure.log; }
fi
BUILD_TMP="$(mktemp -d)"
if TMPDIR="$BUILD_TMP" XDG_RUNTIME_DIR="$BUILD_TMP" cmake --build build -j >/tmp/lsw-p8-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -20 /tmp/lsw-p8-build.log
fi
rm -rf "$BUILD_TMP"
if ctest --test-dir build >/tmp/lsw-p8-ctest.log 2>&1; then
    ok "ctest 全部通过"
else
    ng "ctest 存在失败"
    tail -30 /tmp/lsw-p8-ctest.log
fi
if ./build/tests/lsw_core_tests "[exec]" >/dev/null 2>&1; then
    ok "exec 单测通过"
else
    ng "exec 单测失败"
fi
if ./build/tests/lsw_core_tests "[daemon][exec]" >/dev/null 2>&1; then
    ok "daemon exec 单测通过"
else
    ng "daemon exec 单测失败"
fi

# ---------- 假 QEMU / 假 ssh E2E ----------
section "假 QEMU / 假 ssh E2E"
if [ ! -x build/tests/lsw_fake_qemu ] || [ ! -x build/lswd ]; then
    skip "缺少测试替身，跳过 E2E"
else
    E2E_DIR="$(mktemp -d /tmp/lsw-p8-e2e.XXXXXX)"
    mkdir -p "$E2E_DIR/home" "$E2E_DIR/config" "$E2E_DIR/data" \
        "$E2E_DIR/state" "$E2E_DIR/run" "$E2E_DIR/bin" "$E2E_DIR/outside"
    : >"$E2E_DIR/disk.qcow2"
    printf '@echo off\r\nexit /b 0\r\n' >"$E2E_DIR/home/test.bat"
    printf '@echo off\r\nexit /b 0\r\n' >"$E2E_DIR/outside/run.bat"
    echo 0 >"$E2E_DIR/exit_code"
    python3 - "$E2E_DIR/home/gui.exe" <<'PY'
import sys
path = sys.argv[1]
data = bytearray(0x40 + 4 + 20 + 0x60)
data[0] = ord('M'); data[1] = ord('Z')
pe = 0x40
data[0x3C:0x40] = pe.to_bytes(4, 'little')
data[pe:pe + 4] = b'PE\0\0'
opt = pe + 24
data[opt] = 0x0B; data[opt + 1] = 0x01
data[opt + 0x5C:opt + 0x5E] = (2).to_bytes(2, 'little')
open(path, 'wb').write(data)
PY
    cat >"$E2E_DIR/bin/fake-ssh" <<'SSH'
#!/usr/bin/env bash
printf 'FAKE_STDOUT\n'
printf 'FAKE_STDERR\n' >&2
exit "$(cat "${LSW_FAKE_EXIT_FILE:?}" 2>/dev/null || echo 0)"
SSH
    chmod 0755 "$E2E_DIR/bin/fake-ssh"

    export HOME="$E2E_DIR/home"
    export XDG_CONFIG_HOME="$E2E_DIR/config" XDG_DATA_HOME="$E2E_DIR/data" \
        XDG_STATE_HOME="$E2E_DIR/state" XDG_RUNTIME_DIR="$E2E_DIR/run"
    export LSW_QEMU_BINARY="$ROOT/build/tests/lsw_fake_qemu"
    export LSW_QEMU_ENABLE_KVM=0 LSW_QEMU_ENABLE_FIRMWARE=0
    export LSW_WAIT_SSH=0 LSW_TRY_SSH_SHUTDOWN=0
    export LSW_DISABLE_MOUNTS=1 LSW_DISABLE_HOST_SMB=1
    export LSW_QEMU_DISK="$E2E_DIR/disk.qcow2"
    export LSW_SSH_BINARY="$E2E_DIR/bin/fake-ssh"
    export LSW_FAKE_EXIT_FILE="$E2E_DIR/exit_code"
    export LSW_EXEC_TIMEOUT_MS=10000

    "$ROOT/build/lswd" >"$E2E_DIR/lswd.log" 2>&1 &
    LSWD_PID=$!
    for _ in $(seq 1 50); do
        [ -S "$E2E_DIR/run/lsw/lswd.sock" ] && break
        sleep 0.1
    done
    start_output="$("$ROOT/build/lsw" start 2>&1)"
    if printf '%s' "$start_output" | grep -q '"vm_status":"running"'; then
        ok "假 QEMU 下 lsw start 成功"
    else
        ng "假 QEMU 下 lsw start 异常：$start_output"
    fi

    if (cd "$E2E_DIR/home" &&
        "$ROOT/build/lsw" test.bat "a b" >"$E2E_DIR/out1" 2>"$E2E_DIR/err1"); then
        if grep -q 'FAKE_STDOUT' "$E2E_DIR/out1" &&
            grep -q 'FAKE_STDERR' "$E2E_DIR/err1"; then
            ok ".bat 执行并透传 stdout/stderr（退出码 0）"
        else
            ng ".bat 输出异常：$(cat "$E2E_DIR/out1" "$E2E_DIR/err1")"
        fi
    else
        ng ".bat 执行失败：$(cat "$E2E_DIR/out1" "$E2E_DIR/err1" 2>/dev/null)"
    fi

    echo 7 >"$E2E_DIR/exit_code"
    (cd "$E2E_DIR/home" && "$ROOT/build/lsw" test.bat >/dev/null 2>&1)
    if [ "$?" -eq 7 ]; then
        ok "程序非零退出码透传（7）"
    else
        ng "程序非零退出码未透传：$?"
    fi

    if (cd "$E2E_DIR/home" &&
        "$ROOT/build/lsw" gui.exe >"$E2E_DIR/gui.out" 2>&1); then
        ng "GUI exe 未被拒绝"
    else
        gui_rc=$?
        if [ "$gui_rc" -eq 7 ] &&
            grep -q '暂不支持 GUI' "$E2E_DIR/gui.out"; then
            ok "GUI exe 返回 PROGRAM_FAILED（退出码 7）"
        else
            ng "GUI exe 错误异常：rc=$gui_rc $(cat "$E2E_DIR/gui.out")"
        fi
    fi

    echo 0 >"$E2E_DIR/exit_code"
    if (cd "$E2E_DIR/outside" && "$ROOT/build/lsw" run.bat >/dev/null 2>&1); then
        if find "$E2E_DIR/home/.lsw/tmp" -name run.bat | grep -q .; then
            ok "\$HOME 外程序复制到 ~/.lsw/tmp 并执行"
        else
            ng "越界程序未复制到 ~/.lsw/tmp"
        fi
    else
        ng "越界程序执行失败"
    fi

    timeout 20 "$ROOT/build/lsw" shutdown >/dev/null 2>&1 || true
    for _ in $(seq 1 50); do kill -0 "$LSWD_PID" 2>/dev/null || break; sleep 0.1; done
    if kill -0 "$LSWD_PID" 2>/dev/null; then
        ng "E2E lswd 未退出"
    else
        ok "E2E lswd 已退出"
        LSWD_PID=""
    fi
fi

section "总结"
printf '\n========================================\n'
printf 'P8 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ P8 验收通过\n'
    exit 0
fi
printf '❌ P8 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

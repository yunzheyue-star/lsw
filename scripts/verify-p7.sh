#!/usr/bin/env bash
# ============================================================
# LSW P7 验收脚本 · lsw / lsw shell
# 用法:   bash scripts/verify-p7.sh
# 退出码: 0 = 全部通过（SKIP 不算失败）, 1 = 存在失败项
# 说明:   - 构建 + ctest + shell 单测 + 假 QEMU/假 ssh E2E
#         - 真机交互验收由使用者在已安装 Windows 的环境执行
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

# ---------- P7-1 cwd 映射与 IPC ----------
section "P7-1 · cwd 映射与 shell IPC"
check_file "include/lsw/shell/powershell.hpp"
check_file "src/shell/powershell.cpp"
check_grep "CMakeLists.txt" "src/shell" "lsw_core 收录 shell 模块"
check_grep "src/shell/powershell.cpp" "map_cwd_to_z" "cwd → Z: 映射"
check_grep "src/shell/powershell.cpp" "-EncodedCommand" "PowerShell EncodedCommand"
check_grep "src/daemon/service.cpp" "on_shell" "daemon 处理 shell"
check_grep "src/daemon/service.cpp" "kVMNotRunning" "VM 未运行返回错误"
check_grep "include/lsw/ipc/protocol.hpp" "VM_NOT_RUNNING" "IPC VM_NOT_RUNNING"
check_grep "include/lsw/ipc/protocol.hpp" "PATH_OUTSIDE_HOME" "IPC 路径越界码"

# ---------- P7-2 SSH 命令构造 ----------
section "P7-2 · SSH 命令构造"
check_grep "include/lsw/shell/powershell.hpp" '"ssh"' "默认 ssh 可执行文件"
check_grep "src/shell/powershell.cpp" '"-t"' "ssh -t 分配终端"
check_grep "src/shell/powershell.cpp" '"powershell.exe"' "远端 PowerShell"
check_grep "src/shell/powershell.cpp" '"-NoExit"' "交互式不退出"
check_grep "src/shell/powershell.cpp" '"-NoLogo"' "PowerShell -NoLogo"

# ---------- P7-3 execvp 接管终端 ----------
section "P7-3 · execvp 接管终端"
check_grep "src/cli/main.cpp" "run_shell_command" "CLI shell 入口"
check_grep "src/cli/main.cpp" "execvp" "execvp 替换当前进程"
check_grep "src/cli/main.cpp" "shell_command" "lsw shell 子命令"
check_grep "src/cli/main.cpp" "argc == 1" "裸 lsw 等价 shell"

# ---------- P7-4 边界场景 ----------
section "P7-4 · 边界场景"
check_grep "src/shell/powershell.cpp" "escape_powershell_single_quote" "单引号转义"
check_grep "src/shell/powershell.cpp" "target.Length" "空路径使用默认目录"
check_grep "src/daemon/service.cpp" "expand_tilde" "~ 展开为 $HOME"

# ---------- 文档同步 ----------
section "文档同步"
check_grep "docs/interfaces/cli.md" "execvp" "cli.md shell 执行方式"
check_grep "docs/interfaces/daemon-ipc.md" "ssh_args" "daemon-ipc shell 响应"
check_grep "docs/interfaces/daemon-ipc.md" "VM_NOT_RUNNING" "daemon-ipc 错误码"
check_grep "docs/diagrams/sequence-shell.puml" "execvp" "shell 时序图"
check_grep "docs/CHANGELOG.md" "P7" "CHANGELOG 记录 P7"

# ---------- 构建与测试 ----------
section "构建与测试"
if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-p7-configure.log 2>&1 \
        && ok "CMake 配置成功" || { ng "CMake 配置失败"; tail -15 /tmp/lsw-p7-configure.log; }
fi
BUILD_TMP="$(mktemp -d)"
if TMPDIR="$BUILD_TMP" XDG_RUNTIME_DIR="$BUILD_TMP" cmake --build build -j >/tmp/lsw-p7-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -20 /tmp/lsw-p7-build.log
fi
rm -rf "$BUILD_TMP"
if ctest --test-dir build >/tmp/lsw-p7-ctest.log 2>&1; then
    ok "ctest 全部通过"
else
    ng "ctest 存在失败"
    tail -30 /tmp/lsw-p7-ctest.log
fi
if ./build/tests/lsw_core_tests "[shell]" >/dev/null 2>&1; then
    ok "shell 单测通过"
else
    ng "shell 单测失败"
fi
if ./build/tests/lsw_core_tests "[daemon][shell]" >/dev/null 2>&1; then
    ok "daemon shell 单测通过"
else
    ng "daemon shell 单测失败"
fi

# ---------- 假 QEMU / 假 ssh E2E ----------
section "假 QEMU / 假 ssh E2E"
if [ ! -x build/tests/lsw_fake_qemu ] || [ ! -x build/lswd ]; then
    skip "缺少测试替身，跳过 E2E"
else
    E2E_DIR="$(mktemp -d /tmp/lsw-p7-e2e.XXXXXX)"
    mkdir -p "$E2E_DIR/home/Project/demo" "$E2E_DIR/config" \
        "$E2E_DIR/data" "$E2E_DIR/state" "$E2E_DIR/run" "$E2E_DIR/bin"
    : >"$E2E_DIR/disk.qcow2"
    cat >"$E2E_DIR/bin/fake-ssh" <<'SSH'
#!/usr/bin/env bash
printf '%s\n' "$@" > "${LSW_TEST_SSH_LOG:?}"
exit 0
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
    export LSW_TEST_SSH_LOG="$E2E_DIR/ssh.log"

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

    if (cd "$E2E_DIR/home/Project/demo" &&
        "$ROOT/build/lsw" shell >"$E2E_DIR/shell.out" 2>&1); then
        ok "lsw shell 在 \$HOME 内返回 0"
    else
        ng "lsw shell 在 \$HOME 内失败：$(cat "$E2E_DIR/shell.out")"
    fi
    if [ -s "$E2E_DIR/ssh.log" ]; then
        encoded="$(tail -1 "$E2E_DIR/ssh.log")"
        decoded="$(python3 -c \
            'import base64,sys; print(base64.b64decode(sys.argv[1]).decode("utf-16le"))' \
            "$encoded" 2>/dev/null || true)"
        if printf '%s' "$decoded" | grep -q 'net use' &&
            printf '%s' "$decoded" | grep -q 'Set-Location -LiteralPath' &&
            printf '%s' "$decoded" | grep -q 'Z:\\Project\\demo'; then
            ok "EncodedCommand 含 Z: 映射 + Set-Location Z:\\Project\\demo"
        else
            ng "EncodedCommand 内容异常：$decoded"
        fi
    else
        ng "假 ssh 未收到参数（execvp 未生效？）"
    fi

    : >"$E2E_DIR/ssh.log"
    if (cd "$E2E_DIR/home/Project/demo" &&
        "$ROOT/build/lsw" >"$E2E_DIR/bare.out" 2>&1); then
        ok "裸 lsw 等价 shell"
    else
        ng "裸 lsw 失败：$(cat "$E2E_DIR/bare.out")"
    fi

    if (cd /tmp && "$ROOT/build/lsw" shell >"$E2E_DIR/outside.out" 2>&1); then
        ng "$HOME 外目录未被拒绝"
    else
        outside_rc=$?
        if [ "$outside_rc" -eq 5 ] &&
            grep -q 'PATH_OUTSIDE_HOME' "$E2E_DIR/outside.out"; then
            ok "\$HOME 外返回 PATH_OUTSIDE_HOME（退出码 5）"
        else
            ng "$HOME 外错误异常：rc=$outside_rc $(cat "$E2E_DIR/outside.out")"
        fi
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
printf 'P7 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ P7 验收通过\n'
    exit 0
fi
printf '❌ P7 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

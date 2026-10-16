#!/usr/bin/env bash
# ============================================================
# LSW `lsw status` 扩展功能验收脚本
# 用法:   bash scripts/verify-status.sh
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

section "状态模块"
check_file "include/lsw/vm/status.hpp"
check_file "src/vm/status.cpp"
check_grep "src/vm/status.cpp" "offline_status_json" "离线状态 JSON"
check_grep "src/vm/status.cpp" "probe_guest_status" "SSH guest 探测"
check_grep "src/vm/status.cpp" "guest.json" "guest 缓存"
check_grep "src/vm/status.cpp" "install.json" "安装标记"
check_grep "src/vm/install.cpp" "save_install_marker" "安装成功写标记"
check_grep "src/daemon/service.cpp" "vm_installed" "daemon 上报安装状态"
check_grep "src/daemon/service.cpp" "virtio_win" "daemon 上报 virtio"
check_grep "src/daemon/service.cpp" "hostname" "daemon 上报主机名"

section "CLI 与 JSON"
check_grep "src/cli/main.cpp" "run_status_command" "CLI status 入口"
check_grep "src/cli/main.cpp" "status_command" "status 子命令"
check_grep "src/cli/main.cpp" "--json" "status --json"
check_grep "src/cli/main.cpp" "Windows 主机名" "人类可读主机名输出"
check_grep "src/cli/main.cpp" "virtio-win" "人类可读 virtio 输出"

section "文档同步"
check_grep "docs/interfaces/cli.md" "lsw status" "cli.md status 章节"
check_grep "docs/interfaces/daemon-ipc.md" "vm_installed" "daemon-ipc status 字段"
check_grep "docs/architecture.md" "sequence-status" "architecture status 行"
check_grep "docs/diagrams/sequence-status.puml" "ssh 探测" "status 时序图"
check_grep "docs/CHANGELOG.md" "lsw status" "CHANGELOG 记录 status"
check_grep "docs/TASKS.md" "P10" "TASKS 扩展功能"

section "构建与测试"
if [ ! -f build/CMakeCache.txt ] || [ ! -f build/compile_commands.json ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-status-configure.log 2>&1 \
        && ok "CMake 配置成功" || { ng "CMake 配置失败"; tail -15 /tmp/lsw-status-configure.log; }
fi
BUILD_TMP="$(mktemp -d)"
if TMPDIR="$BUILD_TMP" XDG_RUNTIME_DIR="$BUILD_TMP" cmake --build build -j >/tmp/lsw-status-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -20 /tmp/lsw-status-build.log
fi
rm -rf "$BUILD_TMP"
if ctest --test-dir build >/tmp/lsw-status-ctest.log 2>&1; then
    ok "ctest 全部通过"
else
    ng "ctest 存在失败"
    tail -30 /tmp/lsw-status-ctest.log
fi
if ./build/tests/lsw_core_tests "[status]" >/dev/null 2>&1; then
    ok "status 单测通过"
else
    ng "status 单测失败"
fi
if ./build/tests/lsw_core_tests "[daemon][status]" >/dev/null 2>&1; then
    ok "daemon status 单测通过"
else
    ng "daemon status 单测失败"
fi

section "离线 / 假 QEMU E2E"
if [ ! -x build/lsw ] || [ ! -x build/lswd ] || [ ! -x build/tests/lsw_fake_qemu ]; then
    skip "缺少测试替身，跳过 E2E"
else
    E2E_DIR="$(mktemp -d /tmp/lsw-status-e2e.XXXXXX)"
    mkdir -p "$E2E_DIR/home/.local/share/lsw/vm" "$E2E_DIR/config" \
        "$E2E_DIR/data" "$E2E_DIR/state" "$E2E_DIR/run" "$E2E_DIR/bin"
    : >"$E2E_DIR/home/.local/share/lsw/vm/disk.qcow2"
    cat >"$E2E_DIR/bin/fake-ssh" <<'SSH'
#!/usr/bin/env bash
echo LSW_HOSTNAME=FAKE-STATUS-HOST
echo LSW_VIRTIO=installed:vioscsi,netkvm
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
    export LSW_QEMU_DISK="$E2E_DIR/home/.local/share/lsw/vm/disk.qcow2"
    export LSW_SSH_BINARY="$E2E_DIR/bin/fake-ssh"
    export LSW_STATUS_PROBE_TIMEOUT_MS=3000

    if (cd "$E2E_DIR/home" && "$ROOT/build/lsw" statu >"$E2E_DIR/typo.out" 2>&1); then
        ng "拼写错误未被拒绝"
    else
        typo_rc=$?
        if [ "$typo_rc" -eq 2 ] &&
            grep -q '未知命令' "$E2E_DIR/typo.out"; then
            ok "拼写错误按未知命令处理（退出码 2）"
        else
            ng "拼写错误处理异常：rc=$typo_rc $(cat "$E2E_DIR/typo.out")"
        fi
    fi

    offline_json="$("$ROOT/build/lsw" status --json 2>/dev/null)"
    if printf '%s' "$offline_json" | grep -q '"daemon": "stopped"' &&
        printf '%s' "$offline_json" | grep -q '"vm_installed": true' &&
        printf '%s' "$offline_json" | grep -q '"disk_exists": true'; then
        ok "离线 status --json 报告已安装/已停止"
    else
        ng "离线 status --json 异常：$offline_json"
    fi

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

    running_json="$("$ROOT/build/lsw" status --json 2>/dev/null)"
    if printf '%s' "$running_json" | grep -q '"daemon": "running"' &&
        printf '%s' "$running_json" | grep -q '"vm_status": "running"' &&
        printf '%s' "$running_json" | grep -q '"hostname": "FAKE-STATUS-HOST"' &&
        printf '%s' "$running_json" | grep -q '"virtio_win": "installed"' &&
        printf '%s' "$running_json" | grep -q '"virtio_source": "runtime_probe"'; then
        ok "运行时 status 返回 hostname/virtio"
    else
        ng "运行时 status 异常：$running_json"
    fi

    human_output="$("$ROOT/build/lsw" status 2>&1)"
    if printf '%s' "$human_output" | grep -q 'Windows 主机名' &&
        printf '%s' "$human_output" | grep -q 'FAKE-STATUS-HOST'; then
        ok "人类可读 status 输出"
    else
        ng "人类可读 status 异常：$human_output"
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
printf 'status 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ lsw status 验收通过\n'
    exit 0
fi
printf '❌ lsw status 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

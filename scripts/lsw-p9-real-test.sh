#!/usr/bin/env bash
# ============================================================
# LSW P9 真机 5 命令验收（start / status / shell / exec / shutdown）
# 用法:   bash scripts/lsw-p9-real-test.sh
# 退出码: 0 = 通过或环境不具备时跳过；1 = 存在失败项
# 前置:   Linux + KVM + 已安装 Windows 的 VM
#         `lsw start` 需要 keyring 凭据或导出 LSW_WINDOWS_PASSWORD
# ============================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LSW_BIN="${LSW_BIN:-$ROOT/build/lsw}"
cd "$ROOT" || exit 1

PASS=0
FAIL=0
SKIP=0
ok() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
ng() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
skip() { printf '  [SKIP] %s\n' "$1"; SKIP=$((SKIP + 1)); }
section() { printf '\n== %s ==\n' "$1"; }

if [ ! -x "$LSW_BIN" ]; then
    skip "找不到 $LSW_BIN，跳过真机测试"
    printf 'P9 真机测试：0 项通过，0 项失败，1 项跳过\n'
    exit 0
fi
if [ ! -e /dev/kvm ]; then
    skip "无 /dev/kvm，跳过真机测试"
    printf 'P9 真机测试：0 项通过，0 项失败，1 项跳过\n'
    exit 0
fi
if ! command -v ssh >/dev/null 2>&1; then
    skip "缺少 ssh 客户端，跳过真机测试"
    printf 'P9 真机测试：0 项通过，0 项失败，1 项跳过\n'
    exit 0
fi

status_json="$("$LSW_BIN" status --json 2>/dev/null || true)"
if ! printf '%s' "$status_json" | grep -q '"disk_exists": true'; then
    skip "未检测到已安装的 Windows VM（disk_exists=false），跳过真机测试"
    cat <<'EOF'

--install 人工步骤清单（如需重新安装）：
  1. lsw init                        # 准备宿主依赖与配置
  2. lsw install --iso <Windows.iso> --virtio-iso <virtio-win.iso>
  3. GUI 中加载 virtio 驱动并安装 Windows，创建用户 lsw
  4. Windows 内启用 OpenSSH Server、QEMU Guest Agent、SMB，写入 Linux 公钥
  5. lsw start && lsw status && lsw shell
EOF
    printf 'P9 真机测试：0 项通过，0 项失败，1 项跳过\n'
    exit 0
fi

section "1 / 5 · lsw start"
start_output="$(timeout 240 "$LSW_BIN" start 2>&1)"
start_rc=$?
if [ "$start_rc" -eq 0 ] &&
    printf '%s' "$start_output" | grep -q '"vm_status":"running"'; then
    ok "lsw start 返回 running"
else
    ng "lsw start 失败（退出码 $start_rc）：$start_output"
fi

section "2 / 5 · lsw status"
status_output="$(timeout 30 "$LSW_BIN" status 2>&1)"
if printf '%s' "$status_output" | grep -q '运行中' &&
    printf '%s' "$status_output" | grep -q 'Windows 主机名'; then
    ok "lsw status 显示运行中与主机名"
else
    ng "lsw status 异常：$status_output"
fi

section "3 / 5 · lsw shell"
if command -v script >/dev/null 2>&1; then
    shell_output="$(
        printf 'Write-Output LSW_P9_SHELL_OK\nexit\n' \
            | timeout 90 script -qefc "$LSW_BIN shell" /dev/null 2>&1
    )"
    if printf '%s' "$shell_output" | grep -q 'LSW_P9_SHELL_OK'; then
        ok "lsw shell 进入 PowerShell 并执行命令"
    else
        ng "lsw shell 异常：$shell_output"
    fi
else
    skip "缺少 script 命令，跳过 lsw shell 交互测试"
fi

section "4 / 5 · lsw <program>"
bat="$HOME/lsw-p9-test.bat"
printf '@echo off\r\necho LSW_P9_EXEC_OK\r\nexit /b 0\r\n' >"$bat"
exec_output="$(cd "$HOME" && timeout 90 "$LSW_BIN" lsw-p9-test.bat 2>&1)"
exec_rc=$?
if [ "$exec_rc" -eq 0 ] &&
    printf '%s' "$exec_output" | grep -q 'LSW_P9_EXEC_OK'; then
    ok "lsw 执行 .bat 透传输出与退出码 0"
else
    ng "lsw .bat 执行异常（退出码 $exec_rc）：$exec_output"
fi

printf '@echo off\r\nexit /b 7\r\n' >"$bat"
(cd "$HOME" && timeout 90 "$LSW_BIN" lsw-p9-test.bat >/dev/null 2>&1)
nonzero_rc=$?
if [ "$nonzero_rc" -eq 7 ]; then
    ok "lsw 执行 .bat 非零退出码透传（7）"
else
    ng "非零退出码未透传：$nonzero_rc"
fi
rm -f "$bat"

section "5 / 5 · lsw shutdown"
shutdown_output="$(timeout 180 "$LSW_BIN" shutdown 2>&1)"
shutdown_rc=$?

# shutdown 返回后 daemon/VM 可能还要几秒完成退出；轮询 status。
stopped=false
for _ in $(seq 1 60); do
    stop_status="$("$LSW_BIN" status 2>&1 || true)"
    if ! printf '%s' "$stop_status" | grep -q '运行中'; then
        stopped=true
        break
    fi
    sleep 1
done

if [ "$shutdown_rc" -eq 0 ] &&
    printf '%s' "$shutdown_output" | grep -qE '"stopped"[: ]+true' &&
    [ "$stopped" = true ]; then
    ok "lsw shutdown 停止 VM"
else
    ng "lsw shutdown 异常（退出码 $shutdown_rc，stopped=$stopped）：$shutdown_output"
fi

section "总结"
printf '\n========================================\n'
printf 'P9 真机验收：%d 项通过，%d 项失败，%d 项跳过\n' \
    "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ P9 真机 5 命令验收通过\n'
    exit 0
fi
printf '❌ P9 真机验收未通过，请检查上方 [FAIL] 项\n'
exit 1

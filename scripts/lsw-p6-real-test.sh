#!/usr/bin/env bash
# ============================================================
# lsw-p6-real-test.sh · P6 真机一次性自动化测试（临时脚本，不提交）
#
# 适用环境：
#   - LSW Windows 已安装，Windows 用户名 admin，sshd 已运行
#   - 宿主机已安装 mount.cifs / secret-tool / ssh / ssh-copy-id / smbd
#   - libvirt 等其他 QEMU VM 已关闭
#
# 用法（必须在交互终端里运行，过程中会要求输入 sudo 密码和 Windows 密码）：
#   bash scripts/lsw-p6-real-test.sh
# ============================================================
set -uo pipefail

ROOT="${LSW_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
WIN_USER="${LSW_WINDOWS_USER:-}"
# 未显式指定时，复用 config.toml 中的 windows_user；再否则按文档默认 lsw。
if [ -z "$WIN_USER" ] && [ -f "$HOME/.config/lsw/config.toml" ]; then
    WIN_USER="$(sed -n 's/^[[:space:]]*windows_user[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p'         "$HOME/.config/lsw/config.toml" | head -1)"
fi
WIN_USER="${WIN_USER:-lsw}"
SSH_PORT="${LSW_SSH_PORT:-2222}"
CFG="$HOME/.config/lsw/config.toml"
GUEST_SMB_HOST="${LSW_GUEST_SMB_HOST:-}"
GUEST_SMB_SHARE="${LSW_GUEST_SMB_SHARE:-}"
if [ -z "$GUEST_SMB_HOST" ] && [ -f "$CFG" ]; then
    GUEST_SMB_HOST="$(sed -n 's/^[[:space:]]*guest_smb_host[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$CFG" | head -1)"
fi
if [ -z "$GUEST_SMB_SHARE" ] && [ -f "$CFG" ]; then
    GUEST_SMB_SHARE="$(sed -n 's/^[[:space:]]*guest_smb_share[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$CFG" | head -1)"
fi
GUEST_SMB_HOST="${GUEST_SMB_HOST:-10.0.2.2}"
GUEST_SMB_SHARE="${GUEST_SMB_SHARE:-qemu}"
KEY="$HOME/.ssh/id_ed25519"
HELPER_SRC="$ROOT/scripts/lsw-mount-helper"
HELPER_DST="/usr/local/lib/lsw/lsw-mount-helper"
LOG_DIR="$(mktemp -d /tmp/lsw-p6-real.XXXXXX)"

PASS=0
FAIL=0
SKIP=0
ok() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
ng() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
skip() { printf '  [SKIP] %s\n' "$1"; SKIP=$((SKIP + 1)); }
section() { printf '\n== %s ==\n' "$1"; }
die() {
    printf '\n[FATAL] %s\n' "$1" >&2
    exit 1
}

cleanup() {
    unset LSW_WINDOWS_PASSWORD WIN_PASS 2>/dev/null || true
    if [ -n "${STARTED_VM:-}" ] && [ -x "$ROOT/build/lsw" ]; then
        timeout 60 "$ROOT/build/lsw" shutdown >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

[ -t 0 ] || die "请在交互终端中运行本脚本（需要输入 sudo/Windows 密码）"
[ -x "$ROOT/build/lsw" ] || die "找不到 $ROOT/build/lsw，请先 cmake --build build"
[ -x "$ROOT/build/lswd" ] || die "找不到 $ROOT/build/lswd"
[ "$(id -u)" -ne 0 ] || die "请用普通用户运行，不要直接用 root"

# 清理早期 P5/P6 测试可能导出的临时覆盖，确保使用真实 QEMU/磁盘/HOME。
unset LSW_QEMU_HOME LSW_QEMU_DISK LSW_QEMU_BINARY LSW_QEMU_MACHINE \
      LSW_QEMU_ENABLE_KVM LSW_QEMU_ENABLE_FIRMWARE \
      LSW_DISABLE_MOUNTS LSW_DISABLE_Z_MAPPING \
      LSW_MOUNT_HELPER LSW_MOUNT_PRIVILEGE LSW_MOUNT_ROOT LSW_MOUNTS_FILE \
      LSW_SSH_BINARY LSW_WINDOWS_PASSWORD LSW_CIFS_VERSION \
      LSW_DISABLE_HOST_SMB LSW_HOST_SMB_HELPER LSW_HOST_SMB_PRIVILEGE \
      LSW_HOST_SMB_HOME LSW_HOST_SMB_SHARE 2>/dev/null || true

qemu_pids() { pgrep -f '[q]emu-system-x86_64' 2>/dev/null || true; }
port_listeners() {
    ss -ltnp 2>/dev/null | grep -E ":${SSH_PORT}\b|:1445\b" || true
}

section "0 · 环境与冲突检查"
# 清理旧的 lswd（否则 CLI 会连到旧二进制，修复不会生效）
if pgrep -x lswd >/dev/null 2>&1; then
    echo "  检测到旧 lswd，正在停止..."
    timeout 60 "$ROOT/build/lsw" shutdown >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do
        pgrep -x lswd >/dev/null 2>&1 || break
        sleep 0.2
    done
    pkill -x lswd 2>/dev/null || true
    sleep 1
fi
rm -f "$XDG_RUNTIME_DIR/lsw/lswd.sock" "$XDG_RUNTIME_DIR/lsw/lswd.pid" \
      "$XDG_RUNTIME_DIR/lsw/qmp.sock" "$XDG_RUNTIME_DIR/lsw/qga.sock"

if [ -n "$(qemu_pids)" ] || [ -n "$(port_listeners)" ]; then
    echo "  检测到残留 QEMU/端口占用，先尝试通过 lswd 正常关闭..."
    timeout 60 "$ROOT/build/lsw" shutdown >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do
        [ -z "$(qemu_pids)" ] && [ -z "$(port_listeners)" ] && break
        sleep 1
    done
fi
if [ -n "$(qemu_pids)" ]; then
    ng "仍有 QEMU 进程，请手动处理后再跑："
    qemu_pids | while read -r pid; do
        printf '       kill %s\n' "$pid"
    done
    exit 1
fi
if [ -n "$(port_listeners)" ]; then
    ng "端口 ${SSH_PORT}/1445 仍被占用："
    printf '%s\n' "$(port_listeners)" | sed 's/^/       /'
    exit 1
fi
ok "没有残留 QEMU 进程，端口 ${SSH_PORT}/1445 空闲"
for tool in mount.cifs secret-tool ssh ssh-keygen ssh-copy-id smbd; do
    if command -v "$tool" >/dev/null 2>&1; then
        ok "依赖存在：$tool"
    else
        ng "依赖缺失：$tool"
    fi
done
[ "$FAIL" -eq 0 ] || die "前置依赖不满足，请先安装缺失项"

# 记录 libvirt VM 基线（若可访问）
if command -v virsh >/dev/null 2>&1; then
    if virsh -c qemu:///system dumpxml win10 >"$LOG_DIR/win10.before.xml" 2>/dev/null; then
        ok "已记录 libvirt win10 基线"
    else
        skip "无法读取 qemu:///system 的 win10，跳过基线对比"
    fi
fi

section "1 · 配置 Windows 用户名 = ${WIN_USER}"
mkdir -p "$(dirname "$CFG")"
[ -f "$CFG" ] && cp "$CFG" "$CFG.p6-test.bak"
python3 - "$CFG" "$WIN_USER" <<'PY'
import re
import sys
from pathlib import Path

cfg = Path(sys.argv[1])
user = sys.argv[2]
lines = cfg.read_text(encoding="utf-8").splitlines() if cfg.exists() else []
lines = [line for line in lines if not re.match(r"^\s*windows_user\s*=", line)]
index = next((i for i, line in enumerate(lines) if line.strip() == "[user]"),
             None)
if index is None:
    if lines and lines[-1].strip():
        lines.append("")
    lines.extend(["[user]", f'windows_user = "{user}"'])
else:
    lines.insert(index + 1, f'windows_user = "{user}"')
cfg.write_text("\n".join(lines) + "\n", encoding="utf-8")
PY
if grep -q "windows_user = \"${WIN_USER}\"" "$CFG"; then
    ok "配置已写入 $CFG"
else
    ng "配置写入失败"
    exit 1
fi

section "2 · 安装最小权限 helper（需要 sudo）"
sudo install -d -o root -g root -m 0755 "$(dirname "$HELPER_DST")" ||
    die "创建 helper 目录失败"
sudo install -o root -g root -m 0755 "$HELPER_SRC" "$HELPER_DST" ||
    die "安装 helper 失败"
echo "$USER ALL=(root) NOPASSWD: $HELPER_DST" |
    sudo tee /etc/sudoers.d/lsw >/dev/null || die "写入 sudoers 失败"
sudo chmod 0440 /etc/sudoers.d/lsw || die "设置 sudoers 权限失败"
if sudo visudo -cf /etc/sudoers.d/lsw >/dev/null 2>&1; then
    ok "helper 安装且 sudoers 校验通过"
else
    ng "sudoers 校验失败"
    exit 1
fi
if sudo -n "$HELPER_DST" >/dev/null 2>&1; then
    : # 返回 2（用法错误）也说明 NOPASSWD 生效
fi
if sudo -n test -x "$HELPER_DST"; then
    ok "sudo -n 免密提权可用"
else
    ng "sudo -n 不可用，请检查 /etc/sudoers.d/lsw"
    exit 1
fi

section "3 · 准备 SSH 密钥"
mkdir -p "$HOME/.ssh"
chmod 700 "$HOME/.ssh"
if [ -f "$KEY" ]; then
    ok "已有密钥 $KEY"
else
    ssh-keygen -t ed25519 -f "$KEY" -N "" >/dev/null || die "ssh-keygen 失败"
    ok "已生成密钥 $KEY"
fi
# 清理可能残留/变更的 guest host key，避免 BatchMode 下 "Host key verification failed"
ssh-keygen -R "[127.0.0.1]:${SSH_PORT}" >/dev/null 2>&1 || true
ok "已清理 known_hosts 中旧的 [127.0.0.1]:${SSH_PORT} 条目"

# 给 Windows 重启/自检留更长时间（默认 240s，可覆盖）
export LSW_SSH_READY_TIMEOUT_MS="${LSW_SSH_READY_TIMEOUT_MS:-240000}"

section "4 · 以禁用文件系统模式启动 VM，安装 SSH 公钥"
echo "  （等待 SSH banner，最长 120 秒）..."
set +e
START1="$(timeout 180 env LSW_DISABLE_MOUNTS=1 LSW_DISABLE_Z_MAPPING=1 \
    "$ROOT/build/lsw" start 2>&1)"
RC1=$?
set +e
printf '%s\n' "$START1" | sed 's/^/       /'
if [ "$RC1" -ne 0 ] || ! printf '%s' "$START1" | grep -q '"vm_status":"running"'; then
    ng "禁用文件系统模式启动失败（退出码 $RC1）"
    exit 1
fi
STARTED_VM=1
ok "VM 已启动（文件系统步骤已跳过）"

if ssh -p "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=5 \
    "$WIN_USER@127.0.0.1" "powershell -NoProfile -Command hostname" \
    >/dev/null 2>&1; then
    ok "SSH 免密登录已可用"
else
    echo "  需要安装公钥到 Windows（会提示输入 Windows 密码）..."
    ssh-copy-id -o StrictHostKeyChecking=accept-new -i "$KEY.pub" \
        -p "$SSH_PORT" "$WIN_USER@127.0.0.1" || true

    # Windows 管理员账号只读 administrators_authorized_keys，且 ssh-copy-id
    # 的 POSIX 命令在 cmd.exe 下会失败；这里用 EncodedCommand 兜底。
    PUB="$(cat "$KEY.pub")"
    PS_SCRIPT="\$pub = '${PUB}'; New-Item -ItemType Directory -Force -Path 'C:\\ProgramData\\ssh' | Out-Null; Add-Content -LiteralPath 'C:\\ProgramData\\ssh\\administrators_authorized_keys' -Value \$pub; icacls 'C:\\ProgramData\\ssh\\administrators_authorized_keys' /inheritance:r /grant 'SYSTEM:F' /grant 'Administrators:F'; Restart-Service sshd"
    ENC="$(python3 -c 'import base64,sys; print(base64.b64encode(sys.argv[1].encode("utf-16le")).decode())' "$PS_SCRIPT")"
    echo "  尝试通过 SSH 写入管理员 authorized_keys（再输入一次 Windows 密码）..."
    ssh -p "$SSH_PORT" -o StrictHostKeyChecking=accept-new \
        "$WIN_USER@127.0.0.1" \
        "powershell -NoProfile -EncodedCommand $ENC" || true
fi
if ssh -p "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=5 \
    "$WIN_USER@127.0.0.1" "powershell -NoProfile -Command hostname" \
    >/dev/null 2>&1; then
    ok "SSH 免密登录验证通过"
else
    ng "SSH 免密登录仍失败（Windows 管理员账号需用 administrators_authorized_keys）"
    pub="$(cat "$KEY.pub")"
    echo "  请在 Windows 管理员 PowerShell 中执行下面三行（GUI 启动 Windows 后）："
    printf '    Add-Content C:\\ProgramData\\ssh\\administrators_authorized_keys "%s"\n' "$pub"
    echo "    icacls C:\\ProgramData\\ssh\\administrators_authorized_keys /inheritance:r /grant \"SYSTEM:F\" /grant \"Administrators:F\""
    echo "    Restart-Service sshd"
    echo "  然后关机，重新运行本脚本。"
    exit 1
fi

section "5 · 关闭临时 VM"
timeout 90 "$ROOT/build/lsw" shutdown >"$LOG_DIR/shutdown1.log" 2>&1 ||
    true
sleep 2
if [ -n "$(qemu_pids)" ]; then
    ng "shutdown 后仍有 QEMU 进程"
else
    ok "临时 VM 已关闭"
    STARTED_VM=""
fi

unset LSW_DISABLE_MOUNTS LSW_DISABLE_Z_MAPPING
section "6 · 全链路启动（Z: + CIFS）"
read -rsp "请输入 Windows 用户 ${WIN_USER} 的密码（仅本次使用，不显示）: " WIN_PASS
echo
[ -n "$WIN_PASS" ] || die "密码为空"
export LSW_WINDOWS_PASSWORD="$WIN_PASS"
set +e
START2="$(timeout 180 "$ROOT/build/lsw" start 2>&1)"
RC2=$?
set +e
printf '%s\n' "$START2" | sed 's/^/       /'
if [ "$RC2" -ne 0 ]; then
    ng "全链路 start 失败（退出码 $RC2）"
    exit 1
fi
STARTED_VM=1
if python3 - "$START2" <<'PY'
import json
import sys

lines = [line for line in sys.argv[1].splitlines() if line.strip().startswith("{")]
data = json.loads(lines[-1])
assert data.get("vm_status") == "running", data
assert data.get("z_drive") == "Z:", data
mounts = data.get("mounts", [])
assert "/mnt/Windows/C" in mounts, data
# D: 为可选盘：单盘 LSW Windows 只有 C:，D$ 不存在时应被自动跳过
assert all(m.startswith("/mnt/Windows/") for m in mounts), data
PY
then
    ok "start 返回 running + Z: + C 挂载点（D 可选）"
else
    ng "start 返回内容不符合预期"
    exit 1
fi

section "7 · Linux 侧 CIFS 验证"
if findmnt -t cifs -n -o TARGET | grep -qx "/mnt/Windows/C"; then
    if findmnt -t cifs -n -o TARGET | grep -qx "/mnt/Windows/D"; then
        ok "findmnt 显示 C 与 D 已挂载"
    else
        ok "findmnt 显示 C 已挂载（guest 无 D:，符合预期）"
    fi
else
    ng "findmnt 未找到 /mnt/Windows/C"
fi
if touch /mnt/Windows/C/.lsw-p6-real-test 2>/dev/null; then
    rm -f /mnt/Windows/C/.lsw-p6-real-test
    ok "CIFS 可写（touch/rm 成功）"
else
    ng "CIFS 不可写（检查 Windows 账号是否为管理员/密码）"
fi
if ls /mnt/Windows/C >/dev/null 2>&1; then
    ok "可读取 Windows C 盘目录"
else
    ng "无法读取 /mnt/Windows/C"
fi

section "8 · Windows 侧 Z: 验证"
# Windows 盘符映射按登录会话隔离：必须在同一个 SSH 会话里
# net use + Set-Location，才能复现 LSW 实际使用场景。
printf '  [diag] host smbd: %s\n' "$(pgrep -af smbd 2>/dev/null | head -1 || echo none)"
printf '  [diag] host 445: %s\n' "$(ss -ltn 2>/dev/null | grep ':445 ' | head -1 || echo none)"
Z_SCRIPT="\$ProgressPreference='SilentlyContinue'; net use * /delete /y 2>&1 | Out-Null; \$last=''; for (\$i=0; \$i -lt 6; \$i++) { \$del=(net use * /delete /y 2>&1 | Out-String).Trim(); \$map=(net use Z: \\\\$GUEST_SMB_HOST\\$GUEST_SMB_SHARE 2>&1 | Out-String).Trim(); \$last=('attempt ' + \$i + ' delete=[' + \$del + '] map=[' + \$map + ']'); if (Test-Path -LiteralPath 'Z:\\') { try { Set-Location -LiteralPath 'Z:\\'; if ((Get-Location).Path -eq 'Z:\\') { Write-Output ('OK ' + \$last); exit 0 } } catch { \$last += (' setloc=[' + \$_.Exception.Message + ']') } }; Start-Sleep -Seconds 3 }; Write-Output \$last; exit 7"
Z_ENC="$(printf '%s' "$Z_SCRIPT" | python3 -c 'import base64,sys; print(base64.b64encode(sys.stdin.read().encode("utf-16le")).decode())')"
if Z_OUT="$(timeout 90 ssh -p "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=5 \
    "$WIN_USER@127.0.0.1" \
    "powershell -NoProfile -EncodedCommand $Z_ENC" 2>&1)"; then
    ok "Windows 同一 SSH 会话内可映射并 Set-Location 到 Z:\\"
    printf '       %s\n' "$(printf '%s' "$Z_OUT" | tr '\n' ' ' | cut -c1-400)"
else
    Z_RC=$?
    ng "Windows 内无法映射/进入 Z:\\（exit $Z_RC）: $Z_OUT"
fi

section "9 · 幂等 / 手动卸载 / 重新挂载"
IDEM="$(timeout 60 "$ROOT/build/lsw" start 2>&1)"
if printf '%s' "$IDEM" | grep -q '"already_running":true'; then
    ok "重复 start 幂等"
else
    ng "重复 start 未返回 already_running=true"
fi
timeout 60 "$ROOT/build/lsw" unmount >/dev/null 2>&1
if findmnt -t cifs -n -o TARGET | grep -q "/mnt/Windows"; then
    ng "unmount 后仍有 CIFS 挂载"
else
    ok "unmount 后 CIFS 已清空"
fi
timeout 60 "$ROOT/build/lsw" mount >/dev/null 2>&1
if findmnt -t cifs -n -o TARGET | grep -qx "/mnt/Windows/C"; then
    ok "mount 重新挂载成功"
else
    ng "mount 重新挂载失败"
fi

section "10 · 关闭、清理与安全验证"
timeout 90 "$ROOT/build/lsw" shutdown >"$LOG_DIR/shutdown2.log" 2>&1 || true
sleep 2
if findmnt -t cifs -n -o TARGET | grep -q "/mnt/Windows"; then
    ng "shutdown 后仍有 CIFS 残留"
else
    ok "shutdown 后 CIFS 已卸载"
fi
if [ -n "$(qemu_pids)" ]; then
    ng "shutdown 后仍有 QEMU 进程"
else
    ok "shutdown 后 QEMU 已退出"
    STARTED_VM=""
fi
if pgrep -x lswd >/dev/null 2>&1; then
    ng "lswd 未退出"
else
    ok "lswd 已退出"
fi
if ss -ltn 2>/dev/null | grep -qE ":${SSH_PORT}\b|:1445\b"; then
    ng "端口未释放"
else
    ok "2222/1445 已释放"
fi

if grep -R -F -- "$WIN_PASS" "$HOME/.local/state/lsw" "$HOME/.config/lsw" \
    >/dev/null 2>&1; then
    ng "密码出现在日志/配置中"
else
    ok "密码未出现在日志/配置中"
fi

if command -v virsh >/dev/null 2>&1 && [ -f "$LOG_DIR/win10.before.xml" ]; then
    if virsh -c qemu:///system dumpxml win10 \
        >"$LOG_DIR/win10.after.xml" 2>/dev/null; then
        if diff -q "$LOG_DIR/win10.before.xml" \
            "$LOG_DIR/win10.after.xml" >/dev/null; then
            ok "libvirt win10 配置未被修改"
        else
            ng "libvirt win10 配置发生变化"
        fi
    fi
fi

unset LSW_WINDOWS_PASSWORD WIN_PASS 2>/dev/null || true

section "总结"
printf '\n========================================\n'
printf 'P6 真机测试结果：%d 项通过，%d 项失败，%d 项跳过\n' \
    "$PASS" "$FAIL" "$SKIP"
printf '日志目录：%s\n' "$LOG_DIR"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ P6 真机验收通过\n'
    exit 0
fi
printf '❌ P6 真机验收未通过，请检查上方 [FAIL]\n'
exit 1

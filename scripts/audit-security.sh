#!/usr/bin/env bash
# ============================================================
# LSW 安全与合规审计（P9-4）
# 用法:   bash scripts/audit-security.sh
# 退出码: 0 = 通过, 1 = 存在失败项
# ============================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

PASS=0
FAIL=0
ok() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
ng() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
section() { printf '\n== %s ==\n' "$1"; }

section "不内置/下载 Windows ISO"
if find "$ROOT" -path "$ROOT/.git" -prune -o -iname '*.iso' -print \
    | grep -q .; then
    ng "仓库内发现 ISO 文件"
else
    ok "仓库内无 ISO 文件"
fi
if grep -RInE 'https?://[^ ]+\.iso' src include >/dev/null 2>&1; then
    ng "源码中存在 ISO 下载链接"
else
    ok "源码未自动下载 Windows ISO"
fi

section "密码与凭据"
if grep -RInE '(password|passwd)[[:space:]]*=[[:space:]]*"[^"]+"' \
    src include >/dev/null 2>&1; then
    ng "源码中存在硬编码密码"
else
    ok "源码中无硬编码密码"
fi
if grep -RIn --exclude='audit-security.sh' 'sshpass' src include scripts >/dev/null 2>&1; then
    ng "使用了 sshpass 明文密码工具"
else
    ok "未使用 sshpass"
fi
if grep -q 'secret-tool' src/fs/credentials.cpp; then
    ok "凭据优先使用 keyring（secret-tool）"
else
    ng "credentials.cpp 未使用 secret-tool"
fi
if grep -q 'read -r password' scripts/lsw-mount-helper; then
    ok "helper 密码仅从 stdin 读取一次"
else
    ng "helper 未使用 stdin 读取密码"
fi
if grep -q 'mktemp' scripts/lsw-mount-helper &&
    grep -q 'trap' scripts/lsw-mount-helper; then
    ok "helper 临时凭据文件退出清理"
else
    ng "helper 缺少 mktemp/trap 清理"
fi

section "命令执行与注入"
if grep -RInE '\bsystem[[:space:]]*\(|\bpopen[[:space:]]*\(|/bin/sh[[:space:]]+-c|sh[[:space:]]+-c' \
    src include >/dev/null 2>&1; then
    ng "源码中存在 shell 字符串执行"
else
    ok "未使用 system/popen/sh -c 拼接命令"
fi
if grep -RInE 'execvp|run_process' src/cli src/core src/shell src/exec \
    >/dev/null 2>&1; then
    ok "外部命令使用 argv 形式执行"
else
    ng "未找到参数化执行路径"
fi

section "KVM 与块设备权限"
if grep -RInE --exclude='audit-security.sh' 'chmod[[:space:]]+(777|666)|chmod.*kvm|chown.*kvm' \
    src include scripts >/dev/null 2>&1; then
    ng "发现放宽 KVM/设备权限的操作"
else
    ok "未放宽 /dev/kvm 权限"
fi
if grep -RInE --exclude='audit-security.sh' 'ntfs-3g|mount[[:space:]].*/dev/(sd|nvme)|blockdev' \
    src include scripts >/dev/null 2>&1; then
    ng "发现直接读写 NTFS 块设备的路径"
else
    ok "未直接挂载运行中的 Windows NTFS 块设备"
fi

section "helper 最小权限"
if grep -q 'NOPASSWD' docs/interfaces/filesystem.md; then
    ok "sudoers 仅 NOPASSWD 到 helper"
else
    ng "未找到 NOPASSWD helper 说明"
fi
if grep -q 'mountpoint.*mnt/Windows' scripts/lsw-mount-helper ||
    grep -q '/mnt/Windows' scripts/lsw-mount-helper; then
    ok "helper 校验挂载点范围"
else
    ng "helper 未校验挂载点"
fi
if grep -q '127.0.0.1' scripts/lsw-mount-helper; then
    ok "helper 限制 CIFS 来源为 127.0.0.1"
else
    ng "helper 未限制 CIFS 来源"
fi

section "QEMU 运行模式"
if grep -q '"-display"' src/vm/qemu.cpp &&
    grep -q '"none"' src/vm/qemu.cpp; then
    ok "运行模式使用 -display none"
else
    ng "未确认运行模式显示策略"
fi

section "总结"
printf '\n========================================\n'
printf '安全审计结果：%d 项通过，%d 项失败\n' "$PASS" "$FAIL"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ 安全与合规审计通过\n'
    exit 0
fi
printf '❌ 存在安全审计失败项\n'
exit 1

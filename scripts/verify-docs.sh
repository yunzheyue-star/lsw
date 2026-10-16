#!/usr/bin/env bash
# ============================================================
# LSW 文档全量对齐检查（P9-3）
# 用法:   bash scripts/verify-docs.sh
# 退出码: 0 = 通过, 1 = 存在失败项
# 说明:   检查命令/端口/配置/图/README/TASKS 与代码保持一致；
#         安装了 plantuml 时额外执行语法渲染检查。
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
check() {
    local file="$1" pattern="$2" label="$3"
    if grep -q -- "$pattern" "$file" 2>/dev/null; then
        ok "$label"
    else
        ng "$label"
    fi
}

section "CLI 命令与文档"
for cmd in init install start shell status mount unmount shutdown uninstall; do
    if grep -q "add_subcommand(\"$cmd\"" src/cli/main.cpp; then
        ok "CLI 代码包含子命令 $cmd"
    else
        ng "CLI 代码缺少子命令 $cmd"
    fi
    check "docs/interfaces/cli.md" "$cmd" "cli.md 记录 $cmd"
done
for flag in --install --uninstall --iso --virtio-iso --disk --firmware \
    --allow-existing-disk --yes --dry-run --purge --keep --force --json; do
    if grep -q -- "$flag" src/cli/main.cpp; then
        ok "CLI 代码包含选项 $flag"
    else
        ng "CLI 代码缺少选项 $flag"
    fi
done

section "端口 / 挂载点 / 默认值"
for pattern in 2222 1445 10.0.2.2 /mnt/Windows linux_home_share \
    windows_drive_z windows_user; do
    check "docs/interfaces/config.md" "$pattern" "config.md 记录 $pattern"
done
check "docs/interfaces/filesystem.md" "127.0.0.1:445" "filesystem.md 记录 host Samba 445"
check "docs/interfaces/daemon-ipc.md" "ssh_args" "daemon-ipc.md 记录 shell 响应"
check "docs/interfaces/daemon-ipc.md" "PROGRAM_FAILED" "daemon-ipc.md 记录 exec 错误码"

section "PlantUML 图"
for file in docs/diagrams/*.puml; do
    if head -1 "$file" | grep -q '@startuml' &&
        tail -1 "$file" | grep -q '@enduml'; then
        ok "$(basename "$file") 结构完整"
    else
        ng "$(basename "$file") 结构不完整"
    fi
done
if command -v plantuml >/dev/null 2>&1; then
    if plantuml -checkonly docs/diagrams/*.puml >/tmp/lsw-plantuml-check.log 2>&1; then
        ok "plantuml -checkonly 渲染检查通过"
    else
        ng "plantuml 渲染检查失败"
        tail -10 /tmp/lsw-plantuml-check.log
    fi
else
    skip "未安装 plantuml，跳过实际渲染检查（sudo pacman -S plantuml / apt install plantuml）"
fi

section "README / TASKS / CHANGELOG"
check "README.md" "lsw init" "README 记录 init"
check "README.md" "lsw install" "README 记录 install"
check "README.md" "lsw start" "README 记录 start"
check "README.md" "lsw shell" "README 记录 shell"
check "README.md" "lsw status" "README 记录 status"
check "README.md" "lsw uninstall" "README 记录 uninstall"
check "README.md" "make install" "README 记录 make install"
check "README.md" "package_source" "README 记录源码打包"
check "README.md" "MIT License" "README 记录 MIT 许可证"
check "docs/TASKS.md" "P0" "TASKS 含 P0"
check "docs/TASKS.md" "P9" "TASKS 含 P9"
check "docs/TASKS.md" "P14" "TASKS 含 P14"
check "docs/CHANGELOG.md" "1.0.0" "CHANGELOG 含 1.0.0"
check "LICENSE" "MIT License" "LICENSE 为 MIT"

if [ -f docs/README.md ]; then
    while IFS= read -r target; do
        case "$target" in
        http*|\#*|"") continue ;;
        esac
        if [ -e "$target" ]; then
            ok "docs/README 引用存在：$target"
        else
            ng "docs/README 引用缺失：$target"
        fi
    done < <(grep -oE '\[[^]]+\]\([^)]+\)' docs/README.md \
        | sed -E 's/.*\]\(([^)]+)\).*/\1/' | head -40)
fi

section "总结"
printf '\n========================================\n'
printf '文档对齐结果：%d 项通过，%d 项失败，%d 项跳过\n' \
    "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ 文档全量对齐检查通过\n'
    exit 0
fi
printf '❌ 文档对齐存在失败项\n'
exit 1

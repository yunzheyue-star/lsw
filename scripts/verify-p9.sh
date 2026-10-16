#!/usr/bin/env bash
# ============================================================
# LSW P9 系统测试与 1.0 收口验收
# 用法:   bash scripts/verify-p9.sh
# 真机:   LSW_P9_REAL=1 bash scripts/verify-p9.sh
# 退出码: 0 = 通过（SKIP 不算失败）, 1 = 存在失败项
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
check_grep() {
    if grep -q -- "$2" "$1" 2>/dev/null; then ok "$3"; else ng "$3"; fi
}

export TMPDIR="${TMPDIR:-/tmp}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/lsw-p9-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"

section "1.0.0 版本与许可证"
check_grep "CMakeLists.txt" "VERSION 1.0.0" "工程版本为 1.0.0"
check_grep "LICENSE" "MIT License" "LICENSE 为 MIT"
check_grep "docs/CHANGELOG.md" "1.0.0" "CHANGELOG 已归档 1.0.0"
check_grep "README.md" "MIT License" "README 记录 MIT"
check_grep "README.md" "make install" "README 记录 make install"
check_grep "README.md" "package_source" "README 记录源码打包"
check_grep "docs/TASKS.md" "P9" "TASKS 含 P9"

section "构建与 ctest"
if [ ! -f build/CMakeCache.txt ] || [ ! -f build/compile_commands.json ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-p9-configure.log 2>&1 \
        && ok "CMake 配置成功" || {
        ng "CMake 配置失败"
        tail -20 /tmp/lsw-p9-configure.log
    }
fi
if cmake --build build -j"$(nproc)" >/tmp/lsw-p9-build.log 2>&1; then
    ok "构建成功"
else
    ng "构建失败"
    tail -20 /tmp/lsw-p9-build.log
fi
if ctest --test-dir build --output-on-failure >/tmp/lsw-p9-ctest.log 2>&1; then
    ok "ctest 全部通过"
else
    ng "ctest 存在失败"
    tail -30 /tmp/lsw-p9-ctest.log
fi

section "覆盖率（P9-1）"
bash scripts/coverage.sh >/tmp/lsw-p9-coverage.log 2>&1
coverage_rc=$?
if [ "$coverage_rc" -eq 0 ]; then
    ok "覆盖率达标"
    grep '^\[coverage\]' /tmp/lsw-p9-coverage.log | sed 's/^/       /'
elif [ "$coverage_rc" -eq 2 ]; then
    skip "缺少 gcov/gcovr，跳过覆盖率（安装 gcovr 后重试）"
else
    ng "覆盖率低于阈值"
    tail -20 /tmp/lsw-p9-coverage.log
fi

section "文档全量对齐（P9-3）"
if bash scripts/verify-docs.sh >/tmp/lsw-p9-docs.log 2>&1; then
    ok "文档对齐检查通过"
else
    ng "文档对齐检查失败"
    tail -30 /tmp/lsw-p9-docs.log
fi

section "安全与合规审计（P9-4）"
if bash scripts/audit-security.sh >/tmp/lsw-p9-security.log 2>&1; then
    ok "安全与合规审计通过"
else
    ng "安全与合规审计失败"
    tail -30 /tmp/lsw-p9-security.log
fi

section "分阶段回归（P0~P8 + 扩展）"
for script in \
    scripts/verify-p0.sh scripts/verify-p1.sh scripts/verify-p2.sh \
    scripts/verify-p3.sh scripts/verify-p4.sh scripts/verify-p5.sh \
    scripts/verify-p6.sh scripts/verify-p7.sh scripts/verify-p8.sh \
    scripts/verify-status.sh scripts/verify-init.sh \
    scripts/verify-uninstall.sh; do
    if timeout 600 bash "$script" >/tmp/lsw-p9-$(basename "$script").log 2>&1; then
        ok "$(basename "$script") 通过"
    else
        ng "$(basename "$script") 失败"
        tail -15 /tmp/lsw-p9-$(basename "$script").log
    fi
done

section "真机 5 命令 E2E（P9-2）"
if [ "${LSW_P9_REAL:-0}" = "1" ]; then
    if bash scripts/lsw-p9-real-test.sh >/tmp/lsw-p9-real.log 2>&1; then
        ok "真机 5 命令验收通过"
    else
        ng "真机 5 命令验收失败"
        tail -40 /tmp/lsw-p9-real.log
    fi
else
    skip "未设置 LSW_P9_REAL=1，跳过真机验收"
    printf '       真机执行：LSW_P9_REAL=1 bash scripts/verify-p9.sh\n'
fi

section "总结"
printf '\n========================================\n'
printf 'P9 验收结果：%d 项通过，%d 项失败，%d 项跳过\n' \
    "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -eq 0 ]; then
    printf '✅ P9 自动验收通过'
    if [ "${LSW_P9_REAL:-0}" != "1" ]; then
        printf '（真机 E2E 待执行）'
    fi
    printf '\n'
    exit 0
fi
printf '❌ P9 验收未通过，请检查上方 [FAIL] 项\n'
exit 1

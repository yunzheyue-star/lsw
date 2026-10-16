#!/usr/bin/env bash
# ============================================================
# LSW 覆盖率报告（gcov；有 gcovr 时优先用 gcovr）
# 用法:   bash scripts/coverage.sh [构建目录]
# 退出码: 0 通过；1 低于阈值；其它为构建/测试失败
# 环境变量:
#   COVERAGE_MIN_OVERALL  总体行覆盖率下限（默认 55）
#   COVERAGE_MIN_CORE     src/core 行覆盖率下限（默认 50）
#   COVERAGE_MIN_IPC      src/ipc 行覆盖率下限（默认 70）
#   COVERAGE_MIN_VM       src/vm 行覆盖率下限（默认 70）
# ============================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-build-coverage}"
cd "$ROOT" || exit 1

if ! command -v gcov >/dev/null 2>&1; then
    echo "[coverage] 找不到 gcov；请安装 GCC" >&2
    exit 2
fi

runtime="$(mktemp -d /tmp/lsw-coverage.XXXXXX)"
cleanup() { rm -rf "$runtime"; }
trap cleanup EXIT
export TMPDIR="$runtime"
export XDG_RUNTIME_DIR="$runtime"

echo "[coverage] 配置 $BUILD（LSW_ENABLE_COVERAGE=ON）"
cmake -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLSW_ENABLE_COVERAGE=ON \
    -DBUILD_TESTING=ON \
    -DLSW_FETCH_DEPS=OFF \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/tmp/lsw-coverage-configure.log 2>&1 || {
    echo "[coverage] CMake 配置失败" >&2
    tail -20 /tmp/lsw-coverage-configure.log >&2
    exit 1
}

echo "[coverage] 构建"
cmake --build "$BUILD" -j"$(nproc)" >/tmp/lsw-coverage-build.log 2>&1 || {
    echo "[coverage] 构建失败" >&2
    tail -30 /tmp/lsw-coverage-build.log >&2
    exit 1
}

echo "[coverage] 运行 ctest"
ctest --test-dir "$BUILD" --output-on-failure >/tmp/lsw-coverage-ctest.log 2>&1 || {
    echo "[coverage] ctest 失败" >&2
    tail -30 /tmp/lsw-coverage-ctest.log >&2
    exit 1
}

run_fallback_python() {
    python3 - "$ROOT" "$BUILD" \
        "${COVERAGE_MIN_OVERALL:-55}" \
        "${COVERAGE_MIN_CORE:-50}" \
        "${COVERAGE_MIN_IPC:-70}" \
        "${COVERAGE_MIN_VM:-70}" <<'PY'
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(sys.argv[1]).resolve()
build = Path(sys.argv[2]).resolve()
thresholds = {
    "overall": float(sys.argv[3]),
    "src/core/": float(sys.argv[4]),
    "src/ipc/": float(sys.argv[5]),
    "src/vm/": float(sys.argv[6]),
}

gcov = shutil.which("gcov")
if gcov is None:
    print("[coverage] 找不到 gcov", file=sys.stderr)
    sys.exit(2)

totals = {key: [0, 0] for key in thresholds}
processed = 0
for gcda in sorted(build.rglob("*.gcda")):
    with tempfile.TemporaryDirectory() as temp:
        result = subprocess.run(
            [gcov, "-j", "-o", str(gcda.parent), str(gcda)],
            cwd=temp,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode != 0:
            continue
        for report in Path(temp).glob("*.json.gz"):
            with gzip.open(report, "rt", encoding="utf-8") as handle:
                data = json.load(handle)
            for entry in data.get("files", []):
                filename = entry.get("file", "")
                path = Path(filename)
                if path.is_absolute():
                    try:
                        relative = path.relative_to(root)
                    except ValueError:
                        continue
                    filename = str(relative)
                else:
                    filename = str(path)
                    if not filename.startswith("src/"):
                        if (root / filename).exists():
                            pass
                        else:
                            continue
                if not filename.startswith("src/"):
                    continue
                lines = entry.get("lines", [])
                for line in lines:
                    count = line.get("count")
                    if count is None:
                        continue
                    totals["overall"][0] += 1
                    if count > 0:
                        totals["overall"][1] += 1
                    for key in thresholds:
                        if key != "overall" and filename.startswith(key):
                            totals[key][0] += 1
                            if count > 0:
                                totals[key][1] += 1
                processed += 1

if processed == 0:
    print("[coverage] 未找到可解析的 .gcov.json.gz 报告", file=sys.stderr)
    sys.exit(1)

failed = False
for key, threshold in thresholds.items():
    total, covered = totals[key]
    percent = (covered / total * 100.0) if total else 100.0
    status = "OK" if percent >= threshold else "FAIL"
    print(f"[coverage] {key:<12} {percent:6.2f}% "
          f"（{covered}/{total}，阈值 {threshold:.0f}%）{status}")
    if percent < threshold:
        failed = True

sys.exit(1 if failed else 0)
PY
}

if command -v gcovr >/dev/null 2>&1 ||
    python3 -m gcovr --version >/dev/null 2>&1; then
    GCOVR_CMD=()
    if command -v gcovr >/dev/null 2>&1; then
        GCOVR_CMD=(gcovr)
    else
        GCOVR_CMD=(python3 -m gcovr)
    fi
    echo "[coverage] 使用 gcovr 生成报告"
    "${GCOVR_CMD[@]}" \
        --root "$ROOT" \
        --object-directory "$BUILD" \
        --filter "$ROOT/src/" \
        --exclude "$ROOT/tests/" \
        --exclude "$ROOT/build" \
        --csv "$BUILD/coverage.csv" \
        --txt "$BUILD/coverage.txt" \
        --xml-pretty --output "$BUILD/coverage.xml" \
        >/tmp/lsw-coverage-gcovr.log 2>&1 || {
        echo "[coverage] gcovr 生成报告失败" >&2
        tail -20 /tmp/lsw-coverage-gcovr.log >&2
        exit 1
    }
    python3 - "$BUILD/coverage.csv" \
        "${COVERAGE_MIN_OVERALL:-55}" \
        "${COVERAGE_MIN_CORE:-50}" \
        "${COVERAGE_MIN_IPC:-70}" \
        "${COVERAGE_MIN_VM:-70}" <<'PY'
import csv
import sys

csv_path = sys.argv[1]
thresholds = {
    "overall": float(sys.argv[2]),
    "src/core/": float(sys.argv[3]),
    "src/ipc/": float(sys.argv[4]),
    "src/vm/": float(sys.argv[5]),
}

totals = {key: [0, 0] for key in thresholds}
with open(csv_path, newline="", encoding="utf-8") as handle:
    reader = csv.DictReader(handle)
    for row in reader:
        filename = row.get("filename", "")
        try:
            total = int(float(row.get("line_total", 0)))
            covered = int(float(row.get("line_covered", 0)))
        except ValueError:
            continue
        totals["overall"][0] += total
        totals["overall"][1] += covered
        for key in thresholds:
            if key != "overall" and key in filename:
                totals[key][0] += total
                totals[key][1] += covered

failed = False
for key, threshold in thresholds.items():
    total, covered = totals[key]
    percent = (covered / total * 100.0) if total else 100.0
    status = "OK" if percent >= threshold else "FAIL"
    print(f"[coverage] {key:<12} {percent:6.2f}% "
          f"（{covered}/{total}，阈值 {threshold:.0f}%）{status}")
    if percent < threshold:
        failed = True

sys.exit(1 if failed else 0)
PY
else
    echo "[coverage] 未安装 gcovr，使用 gcov JSON 回退解析"
    run_fallback_python
fi

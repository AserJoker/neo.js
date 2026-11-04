#!/usr/bin/env python3
"""covgen.py - 覆盖率报告生成与门禁检查（llvm-cov source-based）

用法:
    python cmake/covgen.py --build-dir build/linux-coverage \\
        --binary bin/core_unit_test [--binary bin/neo_hello] \\
        --threshold-line 90 --threshold-branch 80 --threshold-func 90

流程:
    1. 设置 LLVM_PROFILE_FILE 环境变量
    2. 运行所有指定的测试可执行（生成 .profraw）
    3. llvm-profdata merge 合并
    4. llvm-cov export 生成 lcov + summary
    5. 解析 summary，对比阈值，不达标 exit 1

关联 ADR: 027（覆盖率门禁）
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def run(cmd, **kw):
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, check=True, **kw)


def main():
    ap = argparse.ArgumentParser(description="neo.js coverage report generator")
    ap.add_argument("--build-dir", required=True, help="build directory")
    ap.add_argument("--binary", action="append", required=True,
                    help="test binary to run (repeatable)")
    ap.add_argument("--threshold-line", type=float, default=90.0)
    ap.add_argument("--threshold-branch", type=float, default=80.0)
    ap.add_argument("--threshold-func", type=float, default=90.0)
    ap.add_argument("--out-dir", default=None,
                    help="report output dir (default: <build-dir>/coverage)")
    ap.add_argument("--ignore-regex", default=None,
                    help="regex of filenames to exclude from gate (e.g. tests|third_party)")
    args = ap.parse_args()

    build_dir = Path(args.build_dir).resolve()
    out_dir = Path(args.out_dir) if args.out_dir else build_dir / "coverage"
    out_dir.mkdir(parents=True, exist_ok=True)

    profraw_dir = build_dir / "profraw"
    profraw_dir.mkdir(exist_ok=True)
    profdata = out_dir / "coverage.profdata"

    # 1. 运行测试，收集 .profraw（同时解析真实二进制路径，Windows 自动补 .exe）
    resolved_bins = []
    for i, binary in enumerate(args.binary):
        bin_path = (build_dir / binary).resolve()
        if not bin_path.exists() and sys.platform == "win32":
            bin_path = bin_path.with_suffix(bin_path.suffix + ".exe")
        if not bin_path.exists():
            print(f"ERROR: binary not found: {bin_path}", file=sys.stderr)
            return 1
        resolved_bins.append(bin_path)
        profraw = profraw_dir / f"{bin_path.stem}_{i}.profraw"
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = str(profraw)
        run([str(bin_path)], env=env)

    # 2. 合并 profraw
    profraws = list(profraw_dir.glob("*.profraw"))
    if not profraws:
        print("ERROR: no .profraw files generated", file=sys.stderr)
        return 1
    run(["llvm-profdata", "merge", "-o", str(profdata), *map(str, profraws)])

    primary_bin = resolved_bins[0]
    # 附加其余 binary 给 llvm-cov（-object），确保各测试二进制的覆盖率都能导出
    extra_obj_args = [f"-object={str(b)}" for b in resolved_bins[1:]]

    # 门禁只统计生产代码：排除测试目录、第三方、覆盖率工具自身。
    # llvm-cov 的 -ignore-filename-regex 同时作用于 export 和门禁计算。
    ignore_args = []
    if args.ignore_regex:
        ignore_args = [f"-ignore-filename-regex={args.ignore_regex}"]

    # 3. 导出 summary (json)
    summary_path = out_dir / "summary.json"
    with open(summary_path, "w", encoding="utf-8") as f:
        subprocess.run(
            ["llvm-cov", "export", "-summary-only", "-format=text",
             str(primary_bin),
             f"-instr-profile={profdata}",
             *extra_obj_args,
             *ignore_args],
            check=True, stdout=f, text=True)

    # 4. 导出 lcov（供 codecov/coveralls 等）
    lcov_path = out_dir / "coverage.lcov"
    with open(lcov_path, "w", encoding="utf-8") as f:
        subprocess.run(
            ["llvm-cov", "export", "-format=lcov",
             str(primary_bin),
             f"-instr-profile={profdata}",
             *extra_obj_args,
             *ignore_args],
            check=True, stdout=f, text=True)

    # 5. 解析 summary，门禁检查
    with open(summary_path, encoding="utf-8") as f:
        summary = json.load(f)
    totals = summary.get("data", [{}])[0].get("totals", {})

    line_pct = totals.get("lines", {}).get("percent", 0)
    branch_pct = totals.get("branches", {}).get("percent", 0)
    func_pct = totals.get("functions", {}).get("percent", 0)

    print(f"\n=== Coverage Summary ===")
    print(f"Lines:     {line_pct:.2f}% (threshold {args.threshold_line}%)")
    print(f"Branches:  {branch_pct:.2f}% (threshold {args.threshold_branch}%)")
    print(f"Functions: {func_pct:.2f}% (threshold {args.threshold_func}%)")

    failed = []
    if line_pct < args.threshold_line:
        failed.append(f"lines {line_pct:.2f}% < {args.threshold_line}%")
    if branch_pct < args.threshold_branch:
        failed.append(f"branches {branch_pct:.2f}% < {args.threshold_branch}%")
    if func_pct < args.threshold_func:
        failed.append(f"functions {func_pct:.2f}% < {args.threshold_func}%")

    if failed:
        print(f"\nFAILED: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("\nPASSED: all coverage thresholds met")
    return 0


if __name__ == "__main__":
    sys.exit(main())

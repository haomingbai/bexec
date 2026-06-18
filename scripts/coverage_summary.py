#!/usr/bin/env python3
"""Generate a non-gating line-coverage summary from GCC gcov JSON output."""

from __future__ import annotations

import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def run_gcov(build_dir: Path, output_dir: Path) -> list[Path]:
    gcov = shutil.which("gcov")
    if gcov is None:
        raise RuntimeError("gcov was not found in PATH")

    data_files = sorted(build_dir.rglob("*.gcda"))
    if not data_files:
        raise RuntimeError(f"no .gcda files found under {build_dir}")

    raw_dir = output_dir / "gcov-json"
    raw_dir.mkdir(parents=True, exist_ok=True)
    for data_file in data_files:
        subprocess.run(
            [gcov, "-j", "-p", str(data_file.resolve())],
            cwd=raw_dir,
            check=True,
            stdout=subprocess.DEVNULL,
        )
    return sorted(raw_dir.glob("*.gcov.json.gz"))


def collect_coverage(
    json_files: list[Path], source_root: Path
) -> dict[str, dict[int, int]]:
    source_root = source_root.resolve()
    coverage: dict[str, dict[int, int]] = {}
    for json_file in json_files:
        with gzip.open(json_file, "rt", encoding="utf-8") as stream:
            document = json.load(stream)

        for file_data in document.get("files", []):
            source = Path(file_data["file"]).resolve()
            try:
                relative = source.relative_to(source_root)
            except ValueError:
                continue

            line_counts = coverage.setdefault(relative.as_posix(), {})
            for line in file_data.get("lines", []):
                line_number = int(line["line_number"])
                line_counts[line_number] = line_counts.get(
                    line_number, 0
                ) + int(line.get("count", 0))
    return coverage


def write_reports(
    coverage: dict[str, dict[int, int]], output_dir: Path
) -> None:
    if not coverage:
        raise RuntimeError("gcov output contained no project source lines")

    rows = []
    total_lines = 0
    covered_lines = 0
    for source, line_counts in sorted(coverage.items()):
        source_total = len(line_counts)
        source_covered = sum(count > 0 for count in line_counts.values())
        rows.append(
            {
                "file": source,
                "covered_lines": source_covered,
                "total_lines": source_total,
                "line_coverage": source_covered / source_total,
            }
        )
        total_lines += source_total
        covered_lines += source_covered

    report = {
        "covered_lines": covered_lines,
        "total_lines": total_lines,
        "line_coverage": covered_lines / total_lines,
        "files": rows,
    }
    (output_dir / "coverage-summary.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )

    markdown = [
        "## Coverage report",
        "",
        (
            f"Line coverage: **{covered_lines}/{total_lines} "
            f"({report['line_coverage']:.2%})**"
        ),
        "",
        "> Coverage is informational and has no pass/fail threshold.",
        "",
        "| File | Covered | Total | Coverage |",
        "| --- | ---: | ---: | ---: |",
    ]
    for row in rows:
        markdown.append(
            f"| `{row['file']}` | {row['covered_lines']} | "
            f"{row['total_lines']} | {row['line_coverage']:.2%} |"
        )
    markdown.append("")
    (output_dir / "coverage-summary.md").write_text(
        "\n".join(markdown), encoding="utf-8"
    )


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    source_root = args.source_root.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        json_files = run_gcov(build_dir, output_dir)
        coverage = collect_coverage(json_files, source_root)
        write_reports(coverage, output_dir)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"coverage generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

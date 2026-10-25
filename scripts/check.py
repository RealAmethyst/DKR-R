#!/usr/bin/env python3
"""Run fast contributor checks without a ROM, C++ build, or dependency checkout."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "runtime-recomp/tests"
# Every runtime-recomp/tests/*_tests.py runs in the tooling suite unless it is
# listed here with the reason. Keep this list short: it is the visible diff.
TOOLING_TESTS_SKIP = {
    "finish_presentation_pipeline_tests.py": "needs generated v77/v80 sources and both ELFs",
    "hud_pipeline_tests.py": "needs generated v77/v80 sources, both ELFs and the baseline source ZIP",
    "hud_reference_source_tests.py": "needs a read-only Golden Balloon checkout",
    "magic_code_pipeline_tests.py": "needs generated v77/v80 sources (CTest passes them)",
    "water_profile_policy_tests.py": "needs both ELFs and their water policies",
}
TOOLING_TESTS = tuple(sorted(path.name for path in TESTS.glob("*_tests.py") if path.name not in TOOLING_TESTS_SKIP))
SUITES = ("guard", "tooling", "metadata", "assets", "track-lab")
DEFAULT_SUITES = ("guard", "tooling", "metadata", "assets")


def suite_list(value: str) -> list[str]:
    """One --suite value: a suite name, "all", or a comma-separated list of them."""
    names = [name.strip() for name in value.split(",") if name.strip()]
    names = [suite for name in names for suite in (SUITES if name == "all" else (name,))]
    unknown = [name for name in names if name not in SUITES]
    if not names or unknown:
        raise argparse.ArgumentTypeError(
            f"unknown suite {', '.join(unknown) or repr(value)} (choose from {', '.join(SUITES)} or all)"
        )
    return names


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--suite",
        action="append",
        type=suite_list,
        metavar="NAME[,NAME...]",
        help=f"select checks (repeatable, comma lists allowed: {', '.join(SUITES)} or all); "
        f"defaults to {', '.join(DEFAULT_SUITES)}",
    )
    parser.add_argument("--list", action="store_true", help="print selected checks without running them")
    args = parser.parse_args(argv)
    suites = [name for names in args.suite for name in names] if args.suite else list(DEFAULT_SUITES)
    commands = {
        "guard": [[sys.executable, "scripts/pattern_guard.py", "--tree", "--baseline", "check"]],
        "tooling": [[sys.executable, f"runtime-recomp/tests/{test}"] for test in TOOLING_TESTS],
        "metadata": [[sys.executable, "scripts/validate_release_metadata.py"]],
        "assets": [[sys.executable, "scripts/scan_for_game_assets.py"]],
        "track-lab": [[sys.executable, "tools/blender/run_tests.py", "--skip-blender"]],
    }
    for suite in dict.fromkeys(suites):
        for command in commands[suite]:
            print(f"[{suite}] {' '.join(command[1:])}", flush=True)
            if args.list:
                continue
            try:
                result = subprocess.run(command, cwd=ROOT)
            except OSError as error:
                print(f"Could not run {command[1]}: {error}", file=sys.stderr)
                return 1
            if result.returncode:
                print(f"Failed: {command[1]} (exit {result.returncode})", file=sys.stderr)
                return result.returncode if result.returncode > 0 else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build and run the ac_checks_A blackstart tests on the host.

Usage (from this directory):  python run_tests.py
Uses $CC if set, otherwise gcc. Needs python3 + PyYAML for header generation.

ac_checks_A keeps its state in function-level statics, so each scenario runs
in its own process.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIB = HERE.parent.parent
REPO = LIB.parent
MODULE_DIR = LIB / "companion_algorithms" / "ac_checks_A"
SCHEMA = LIB / "companion_algorithms" / "ac_checks_A_schema.yaml"


def main() -> int:
    subprocess.run(
        [sys.executable, "generate_header.py", str(SCHEMA), str(MODULE_DIR)],
        cwd=REPO / "schema_tools",
        check=True,
    )

    cc = os.environ.get("CC", "gcc")
    build_dir = Path(tempfile.mkdtemp(prefix="ac_checks_A_tests_"))
    exe = build_dir / ("test_blackstart.exe" if os.name == "nt" else "test_blackstart")
    try:
        subprocess.run(
            [
                cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                "-o", str(exe),
                str(HERE / "test_blackstart.c"),
                str(MODULE_DIR / "ac_checks_A.c"),
                str(LIB / "helper_lib" / "timers.c"),
                str(LIB / "helper_lib" / "cmplx.c"),
                "-lm",
            ],
            check=True,
        )

        names = subprocess.run([str(exe)], check=True, capture_output=True,
                               text=True).stdout.split()
        failed = []
        for name in names:
            result = subprocess.run([str(exe), name], capture_output=True, text=True)
            sys.stdout.write(result.stdout)
            if result.returncode != 0:
                failed.append(name)

        print()
        print(f"{len(names) - len(failed)}/{len(names)} scenarios passed")
        return 1 if failed else 0
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

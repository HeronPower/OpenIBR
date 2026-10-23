#!/usr/bin/env python3
"""Compile and compare isolated dispatch statements from the actual harness."""

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


ORIGINAL_COMMIT = "5e2396cef8e57765c6a8fefe34fc379c3a60c163"
ORIGINAL_SHA256 = "1c77cb40a95db9300000d781b7c02d8a4c974a347cc514f1c62f8b3d22a5c5e0"
SOURCE = Path("ieee_cigre_dll/test_harness/test_harness.c")
HELPER = Path("ieee_cigre_dll/test_harness/model_status.h")
HEADERS = (
    Path("ieee_cigre_dll/IEEE_Cigre_DLLInterface.h"),
    Path("ieee_cigre_dll/IEEE_Cigre_DLLInterface_types.h"),
)
ENTRY_POINTS = (
    "Model_FirstCall",
    "Model_CheckParameters",
    "Model_Initialize",
    "Model_Outputs",
    "Model_Terminate",
)
STATUSES = (0, 1, 2, -1, 3)
EXTRA_CASES = (
    (0, "ok_poison"),
    (1, "message_poison_error"),
    (2, "error_poison_general"),
    (1, "message_null"),
    (2, "error_null"),
)
UNLOAD_STUB = """static void FreeLibrary(void *handle)
{
    if (handle != &instance)
    {
        fixture_error = 1;
    }
    unload_calls++;
}"""


def sha256(content):
    return hashlib.sha256(content).hexdigest()


def code_mask(source):
    """Blank comments and literals without moving source positions."""
    result = list(source)
    i = 0
    while i < len(source):
        end = None
        if source.startswith("//", i):
            end = source.find("\n", i)
            if end == -1:
                end = len(source)
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            if end == -1:
                raise ValueError("Unterminated source comment")
            end += 2
        elif source[i] in "\"'":
            quote = source[i]
            end = i + 1
            while end < len(source):
                if source[end] == "\\":
                    end += 2
                elif source[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            else:
                raise ValueError("Unterminated source literal")
        if end is None:
            i += 1
            continue
        for position in range(i, end):
            if source[position] != "\n":
                result[position] = " "
        i = end
    return "".join(result)


def close_delimiter(mask, start, opening, closing):
    depth = 0
    for position in range(start, len(mask)):
        if mask[position] == opening:
            depth += 1
        elif mask[position] == closing:
            depth -= 1
            if depth == 0:
                return position
    raise ValueError(f"Unclosed {opening} at source offset {start}")


def extract_dispatch(content, entry_point, original):
    source = content.decode("utf-8")
    mask = code_mask(source)
    calls = list(re.finditer(rf"\b{entry_point}_p\s*\(\s*&instance\s*\)", mask))
    if len(calls) != 1:
        raise ValueError(f"Expected one actual {entry_point} call, found {len(calls)}")
    call = calls[0]
    found = []
    for match in re.finditer(r"\bif\s*\(", mask):
        opening = mask.find("(", match.start())
        closing = close_delimiter(mask, opening, "(", ")")
        if opening < call.start() < closing:
            body = closing + 1
            while body < len(mask) and mask[body].isspace():
                body += 1
            if mask[body:body + 1] != "{":
                raise ValueError(f"Expected a braced {entry_point} failure branch")
            end = close_delimiter(mask, body, "{", "}") + 1
            if re.match(r"\s*else\b", mask[end:]):
                raise ValueError(f"Unsupported else branch for {entry_point}")
            found.append((match.start(), end))
    if len(found) == 1:
        start, end = found[0]
    elif not found and original and entry_point == "Model_Terminate":
        start = call.start()
        end = call.end()
        while end < len(mask) and mask[end].isspace():
            end += 1
        if mask[end:end + 1] != ";":
            raise ValueError("Original Terminate call is not a bare statement")
        end += 1
    else:
        raise ValueError(f"Ambiguous dispatch statement for {entry_point}")
    start = source.rfind("\n", 0, start) + 1
    statement = source[start:end]
    metadata = {
        "start_line": source.count("\n", 0, start) + 1,
        "end_line": source.count("\n", 0, end) + 1,
        "start_byte": len(source[:start].encode("utf-8")),
        "end_byte": len(source[:end].encode("utf-8")),
        "sha256": sha256(statement.encode("utf-8")),
    }
    return statement, metadata


def render_fixture(template, statement, entry_point, fixed):
    first_call = entry_point == "Model_FirstCall"
    local_declarations = []
    if first_call:
        local_declarations.append("    void *hDll = &instance;")
    if entry_point == "Model_Outputs":
        local_declarations.append("    int step = 42;")
    replacements = {
        "@STATUS_INCLUDE@": '#include "model_status.h"' if fixed else "",
        "@ENTRY_POINT@": entry_point,
        "@UNLOAD_STUB@": UNLOAD_STUB if first_call else "",
        "@LOCALS@": "\n".join(local_declarations),
        "@DISPATCH@": statement,
        "@FAIL_LABEL@": "cleanup_fail:\n    return 1;" if "goto cleanup_fail;" in statement else "",
    }
    for marker, value in replacements.items():
        if marker not in template:
            raise ValueError(f"Missing fixture marker {marker}")
        template = template.replace(marker, value)
    if re.search(r"@[A-Z_]+@", template):
        raise ValueError("Unexpanded fixture marker")
    return template.encode("utf-8")


def expected_stderr(entry_point, status, mode, fixed):
    if fixed:
        if status == 0:
            message = ""
        elif status == 1:
            text = "(no message)" if mode == "message_null" else "general diagnostic"
            message = f"MESSAGE: {entry_point}: {text}\n"
        elif status == 2:
            text = "(no message)" if mode == "error_null" else "error diagnostic"
            message = f"FAIL: {entry_point}: {text}\n"
        else:
            message = f"FAIL: {entry_point} returned unknown status {status}\n"
        if status not in (0, 1) and entry_point == "Model_Outputs":
            message += "  at step 42\n"
        return message.encode("utf-8")
    if status == 0 or entry_point == "Model_Terminate":
        return b""
    if entry_point == "Model_CheckParameters":
        return b"FAIL: Model_CheckParameters rejected default parameters: error diagnostic\n"
    if entry_point == "Model_Outputs":
        return b"FAIL: Model_Outputs returned nonzero at step 42\n"
    return f"FAIL: {entry_point} returned nonzero\n".encode("utf-8")


class Regression:
    def __init__(self, repository, output, compiler):
        self.repository = repository
        self.output = output
        self.compiler = compiler
        self.manifest = {
            "format": "openibr-model-return-status-regression-1",
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": sys.version,
            "original_commit": ORIGINAL_COMMIT,
            "original_source_expected_sha256": ORIGINAL_SHA256,
            "limits_seconds": {"commands": 30, "test_cases": 5},
            "plan": {
                "entry_points": ENTRY_POINTS,
                "statuses": STATUSES,
                "valid_pointer_cases_per_version": 25,
                "extra_fixed_cases": EXTRA_CASES,
                "total_cases": 75,
                "expected_changed_continuation_outcomes": 7,
                "execution": "extracted dispatch statements and controlled callbacks only",
                "full_harness_executed": False,
                "real_model_executed": False,
            },
            "commands": [],
            "sources": {},
            "spans": {},
            "cases": [],
        }

    def save(self, relative, content):
        path = self.output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(content)
        return path

    def command(self, arguments, label, timeout=30):
        index = len(self.manifest["commands"]) + 1
        record = {
            "label": label,
            "argv": [str(argument) for argument in arguments],
            "cwd": str(self.repository),
            "timeout_seconds": timeout,
        }
        self.manifest["commands"].append(record)
        started = time.monotonic()
        try:
            result = subprocess.run(
                record["argv"], cwd=self.repository, capture_output=True, timeout=timeout,
                env=dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_TERMINAL_PROMPT="0"),
            )
            stdout, stderr = result.stdout, result.stderr
            record["returncode"] = result.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or b"", error.stderr or b""
            record["timed_out"] = True
            record["returncode"] = None
        record["elapsed_seconds"] = time.monotonic() - started
        for channel, content in (("stdout", stdout), ("stderr", stderr)):
            relative = Path("logs") / f"{index:03d}-{label}.{channel}"
            self.save(relative, content)
            record[channel] = {"path": relative.as_posix(), "sha256": sha256(content)}
        if record.get("timed_out"):
            raise RuntimeError(f"{label} exceeded its {timeout}-second deadline")
        return result

    def require_command(self, arguments, label):
        result = self.command(arguments, label)
        if result.returncode:
            raise RuntimeError(f"{label} failed with exit {result.returncode}; see logs")
        return result.stdout

    def snapshot(self):
        commit = self.require_command(
            ["git", "rev-parse", ORIGINAL_COMMIT], "original-commit",
        ).decode().strip()
        if commit != ORIGINAL_COMMIT:
            raise ValueError("Original commit does not match the pinned revision")
        originals = {}
        for relative in (SOURCE, *HEADERS):
            original = self.require_command(
                ["git", "show", f"{ORIGINAL_COMMIT}:{relative.as_posix()}"],
                f"original-{relative.stem}",
            )
            originals[relative] = original
            self.save(Path("sources/original") / relative, original)
        if sha256(originals[SOURCE]) != ORIGINAL_SHA256:
            raise ValueError("Original harness source SHA256 does not match the pin")
        fixed = {}
        for relative in (SOURCE, *HEADERS, HELPER):
            content = (self.repository / relative).read_bytes()
            if relative in HEADERS and content != originals[relative]:
                raise ValueError(f"Interface header changed from the original: {relative}")
            fixed[relative] = content
            self.save(Path("sources/fixed") / relative, content)
        for version, contents in (("original", originals), ("fixed", fixed)):
            self.manifest["sources"][version] = {
                relative.as_posix(): sha256(content) for relative, content in contents.items()
            }
        here = Path(__file__).resolve().parent
        for filename in ("harness.c", "run_regression.py", "README.md"):
            content = (here / filename).read_bytes()
            self.save(Path("sources/regression") / filename, content)
        return originals[SOURCE], fixed[SOURCE]

    def run(self):
        original, fixed = self.snapshot()
        compiler_version = self.require_command([self.compiler, "--version"], "compiler-version")
        compiler_text = compiler_version.decode("utf-8", errors="replace")
        if not re.search(r"clang|gcc|Free Software Foundation", compiler_text, re.IGNORECASE):
            raise ValueError("This regression supports GCC and Clang C11 compilers")
        self.manifest["compiler"] = {
            "path": self.compiler, "version": compiler_text,
            "sha256": sha256(Path(self.compiler).read_bytes()),
        }
        template = (self.output / "sources/regression/harness.c").read_text()
        binaries = {}
        for version, content in (("original", original), ("fixed", fixed)):
            self.manifest["spans"][version] = {}
            for entry_point in ENTRY_POINTS:
                statement, metadata = extract_dispatch(content, entry_point, version == "original")
                relative = Path("spans") / version / f"{entry_point}.c"
                self.save(relative, statement.encode("utf-8"))
                metadata["path"] = relative.as_posix()
                self.manifest["spans"][version][entry_point] = metadata
                fixture = render_fixture(template, statement, entry_point, version == "fixed")
                source_path = self.save(Path("build") / version / f"{entry_point}.c", fixture)
                binary = source_path.with_suffix(".exe" if sys.platform == "win32" else "")
                header_root = self.output / "sources" / version / "ieee_cigre_dll"
                self.require_command(
                    [self.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                     "-O0", "-I", header_root, "-I", header_root / "test_harness",
                     source_path, "-o", binary],
                    f"compile-{version}-{entry_point}",
                )
                binaries[(version, entry_point)] = binary
        outcomes = {}
        for version in ("original", "fixed"):
            for entry_point in ENTRY_POINTS:
                cases = [(status, "valid") for status in STATUSES]
                if version == "fixed":
                    cases.extend(EXTRA_CASES)
                for status, mode in cases:
                    result = self.command(
                        [binaries[(version, entry_point)], str(status), mode],
                        f"case-{version}-{entry_point}-{status}-{mode}", timeout=5,
                    )
                    continued = status in (0, 1) if version == "fixed" else (
                        status == 0 or entry_point == "Model_Terminate"
                    )
                    expected_exit = 0 if continued else 1
                    unloads = int(entry_point == "Model_FirstCall" and not continued)
                    stdout = (
                        f"RESULT continued={int(continued)} calls=1 unchanged=1 unloads={unloads}\n"
                    ).encode("utf-8")
                    stderr = expected_stderr(entry_point, status, mode, version == "fixed")
                    case = {
                        "version": version, "entry_point": entry_point, "status": status,
                        "diagnostics": mode, "expected_continued": continued,
                        "expected_exit": expected_exit, "actual_exit": result.returncode,
                        "expected_stdout": stdout.decode(), "expected_stderr": stderr.decode(),
                        "command_index": len(self.manifest["commands"]),
                        "passed": (
                            result.returncode == expected_exit
                            and result.stdout == stdout and result.stderr == stderr
                        ),
                    }
                    self.manifest["cases"].append(case)
                    if not case["passed"]:
                        raise AssertionError(f"Unexpected {version} {entry_point} {status} {mode}; see logs")
                    if mode == "valid":
                        outcomes[(version, entry_point, status)] = continued
        changed = [
            {"entry_point": entry_point, "status": status,
             "original_continued": outcomes[("original", entry_point, status)],
             "fixed_continued": outcomes[("fixed", entry_point, status)]}
            for entry_point in ENTRY_POINTS for status in STATUSES
            if outcomes[("original", entry_point, status)] != outcomes[("fixed", entry_point, status)]
        ]
        expected_changes = {
            (entry_point, 1) for entry_point in ENTRY_POINTS if entry_point != "Model_Terminate"
        } | {("Model_Terminate", status) for status in (2, -1, 3)}
        if {(row["entry_point"], row["status"]) for row in changed} != expected_changes:
            raise AssertionError("Changed continuation outcomes differ from the seven declared cases")
        if len(self.manifest["cases"]) != 75:
            raise AssertionError("Expected exactly 75 cases")
        self.manifest["changed_continuation_outcomes"] = changed
        self.manifest["result"] = {"passed": True, "cases": 75, "changed_outcomes": len(changed)}

    def finish(self, error=None):
        if error is not None:
            self.manifest["result"] = {"passed": False, "error": f"{type(error).__name__}: {error}"}
        self.manifest["artifacts"] = {
            path.relative_to(self.output).as_posix(): {
                "sha256": sha256(path.read_bytes()), "bytes": path.stat().st_size,
            }
            for path in sorted(self.output.rglob("*")) if path.is_file()
        }
        content = json.dumps(self.manifest, indent=2, sort_keys=True) + "\n"
        self.save(Path("manifest.json"), content.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path, help="Fresh receipt directory; must not exist")
    parser.add_argument("--cc", default=shutil.which("cc"), help="GCC or Clang compiler")
    arguments = parser.parse_args()
    if not arguments.cc:
        parser.error("No C compiler found; provide --cc")
    compiler = shutil.which(arguments.cc)
    if compiler is None:
        parser.error(f"Compiler not found: {arguments.cc}")
    repository = Path(__file__).resolve().parents[3]
    output = arguments.out.resolve()
    try:
        output.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        parser.error(f"Output directory already exists: {output}")
    regression = Regression(repository, output, compiler)
    try:
        regression.run()
    except (OSError, ValueError, RuntimeError, AssertionError) as error:
        regression.finish(error)
        print(f"FAIL: {error}\nReceipts: {output / 'manifest.json'}", file=sys.stderr)
        return 1
    regression.finish()
    print(f"PASS: 75 cases; 7 changed continuation outcomes\nReceipts: {output / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

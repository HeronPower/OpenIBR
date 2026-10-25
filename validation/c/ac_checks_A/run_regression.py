#!/usr/bin/env python3
"""Compile and compare original and corrected AC-check component regressions."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = Path("c_language_library/companion_algorithms/ac_checks_A/ac_checks_A.c")
SCHEMA = Path("c_language_library/companion_algorithms/ac_checks_A_schema.yaml")
GENERATOR = Path("schema_tools/generate_header.py")
HELPERS = [Path("c_language_library/helper_lib") / name for name in (
    "timers.c", "timers.h", "cmplx.c", "cmplx.h", "helper_macros.h", "common_types.h",
)]
BASELINE = "5e2396cef8e57765c6a8fefe34fc379c3a60c163"
ORIGINAL_SHA256 = "ee5ea0b383de265ab67289bca4a9b125545ecc2f050d31f604ef5733c5624525"
COMMAND_TIMEOUT = 30
UNCHANGED = {
    "nominal_unequal", "zero_all", "zero_one_phase", "signed_zero",
    "nominal_35kV", "rating_floor", "frequency_under1", "frequency_under2",
    "frequency_over1", "frequency_over2", "imbalance_reject", "imbalance_inclusive",
    "qualification_bypass", "frequency_pulse_clear_memory", "timer_helpers",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def original_variant(fixed, run, git, out):
    result = run(
        [git, "-C", str(ROOT), "show", f"{BASELINE}:{COMPONENT.as_posix()}"],
        "baseline-source", env=dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_TERMINAL_PROMPT="0"),
    )
    require(result.returncode == 0,
            "Public v1.1 baseline unavailable; use a Git clone containing " + BASELINE)
    original = (out / "baseline-source.stdout").read_bytes()
    require(digest(original) == ORIGINAL_SHA256, "Public v1.1 source hash mismatch")
    require(fixed != original, "Current source is unchanged from the original baseline")
    return original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Compiler command; defaults to CC or cc")
    parser.add_argument("--output-dir", type=Path, help="New directory for build artifacts and logs")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("This runner requires POSIX process groups; Windows/MSVC is unsupported")
    try:
        compiler = shlex.split(args.cc or os.environ.get("CC", "cc"))
    except ValueError as error:
        parser.error(f"Invalid compiler command: {error}")
    if not compiler or shutil.which(compiler[0]) is None:
        parser.error("A GCC/Clang-compatible C11 compiler is required")
    compiler[0] = str(Path(shutil.which(compiler[0])).absolute())
    git = shutil.which("git")
    if git is None:
        parser.error("Git and the public v1.1 baseline are required")
    git = str(Path(git).absolute())

    requested = args.output_dir.expanduser() if args.output_dir else None
    if requested is not None and (requested.exists() or requested.is_symlink()):
        parser.error("Output already exists; preserve it and choose a new directory")
    out = requested.resolve() if requested is not None else None
    if out is not None:
        if out == ROOT or out in ROOT.parents:
            parser.error("Output must not replace the source checkout or its ancestors")
        if ROOT in out.parents and out.relative_to(ROOT).parts[0] != ".context":
            parser.error("Use a new output outside the source checkout or under its .context directory")
    if out is None:
        out = Path(tempfile.mkdtemp(prefix="openibr-ac-checks-"))
    else:
        out.mkdir(parents=True, exist_ok=False)
    record = {"status": "preparing", "baseline_commit": BASELINE,
              "fresh_process_per_scenario": True, "inputs": [], "commands": [], "cases": [],
              "tools": [], "binaries": {}}

    def save():
        (out / "manifest.json").write_text(json.dumps(record, indent=2, allow_nan=False) + "\n")

    def run(argv, label, env=None):
        argv = [str(arg) for arg in argv]
        command = {"argv": argv, "label": label, "cwd": str(out), "status": "starting",
                   "started_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                   "timeout_seconds": COMMAND_TIMEOUT, "timed_out": False, "interrupted": False,
                   "exit_code": None,
                   **{stream: f"{label}.{stream}" for stream in ("stdout", "stderr")}}
        if env is not None:
            command["environment_overrides"] = {key: env[key] for key in ("GIT_NO_LAZY_FETCH", "GIT_TERMINAL_PROMPT")}
        for binary in record["binaries"].values():
            if argv[0] == binary["path"]:
                require(digest(Path(argv[0]).read_bytes()) == binary["sha256"], "Executable changed")
                command["executable_sha256"] = binary["sha256"]
        record["commands"].append(command)
        save()
        process = None

        def stop_owned_group():
            if process is not None:
                command["owned_group_cleanup_attempted"] = True
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()

        try:
            with (out / command["stdout"]).open("wb") as stdout, (out / command["stderr"]).open("wb") as stderr:
                try:
                    # Defer cancellation until Popen has returned the owned process ID.
                    pending = []
                    handlers = {sig: signal.signal(sig, lambda signum, frame: pending.append(signum))
                                for sig in (signal.SIGINT, signal.SIGTERM)}
                    try:
                        process = subprocess.Popen(argv, cwd=out, stdout=stdout, stderr=stderr,
                                                   env=env, start_new_session=True)
                    finally:
                        for sig, handler in handlers.items():
                            signal.signal(sig, handler)
                    command.update(status="running", pid=process.pid, owned_process_group=process.pid)
                    save()
                    if pending:
                        raise KeyboardInterrupt(f"Signal {pending[0]}")
                    process.wait(timeout=COMMAND_TIMEOUT)
                except subprocess.TimeoutExpired:
                    command["timed_out"] = True
                except KeyboardInterrupt:
                    command["interrupted"] = True
                    raise
                finally:
                    # Seal logs even if a wrapper leaves descendants after exiting.
                    stop_owned_group()
        except (Exception, KeyboardInterrupt) as error:
            command["error"] = str(error) or type(error).__name__
            raise
        finally:
            command.update(status="finished" if process is not None else "launch_failed",
                           exit_code=process.returncode if process is not None else None,
                           ended_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
            for stream in ("stdout", "stderr"):
                path = out / command[stream]
                if path.exists():
                    command[stream + "_sha256"] = digest(path.read_bytes())
            save()
        require(not command["timed_out"], f"Command timed out; inspect {label} logs")
        return subprocess.CompletedProcess(argv, process.returncode,
                                           (out / command["stdout"]).read_text(errors="replace"),
                                           (out / command["stderr"]).read_text(errors="replace"))

    def terminate(signum, frame):
        raise KeyboardInterrupt(f"Signal {signum}")

    previous_sigterm = signal.signal(signal.SIGTERM, terminate)

    try:
        save()
        record["tools"] = [{"path": str(Path(path).resolve()), "sha256": digest(Path(path).read_bytes())}
                           for path in (sys.executable, compiler[0], git)]
        record["runner_sha256"] = digest(Path(__file__).read_bytes())
        ready = run([sys.executable, "-I", "-B", "-c",
                     "import json, sys, yaml; print(json.dumps({'python': sys.version, 'pyyaml': yaml.__version__}))"],
                    "python-pyyaml")
        require(ready.returncode == 0, "Isolated Python requires PyYAML; inspect python-pyyaml logs")
        record["python"] = json.loads(ready.stdout)
        fixed = (ROOT / COMPONENT).read_bytes()
        original = original_variant(fixed, run, git, out)
        record["original_source_sha256"] = digest(original)
        record["fixed_source_sha256"] = digest(fixed)
        require(run([*compiler, "--version"], "compiler-version").returncode == 0,
                "Compiler version command failed")
        shared = out / "shared"
        for path in [*HELPERS, SCHEMA, GENERATOR]:
            content = (ROOT / path).read_bytes()
            target = shared / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
            record["inputs"].append({"path": path.as_posix(), "sha256": digest(content)})
        harness = out / "harness.c"
        harness.write_bytes(Path(__file__).with_name("harness.c").read_bytes())
        record["harness_sha256"] = digest(harness.read_bytes())
        headers = shared / COMPONENT.parent
        generated = run([sys.executable, "-I", "-B", str(shared / GENERATOR),
                         str(shared / SCHEMA), str(headers)], "generate-headers")
        require(generated.returncode == 0, "Header generation failed; Python requires PyYAML")
        header_names = ("acChecks_A_autogen.h", "acChecks_A_private_autogen.h")
        record["headers"] = [{"name": name, "sha256": digest((headers / name).read_bytes())}
                             for name in header_names]

        binaries = {}
        for variant, content in (("original", original), ("fixed", fixed)):
            directory = out / variant
            source = directory / COMPONENT
            source.parent.mkdir(parents=True)
            source.write_bytes(content)
            for path in HELPERS:
                target = directory / path
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(shared / path, target)
            for name in header_names:
                shutil.copyfile(headers / name, source.parent / name)
            binary = directory / "ac_checks_test"
            command = [*compiler, "-std=c11", "-O0", "-g", "-Wall", "-Wextra", "-Wconversion",
                       "-I", str(source.parent), "-I", str(directory / "c_language_library/helper_lib"),
                       str(harness), str(source), str(directory / HELPERS[0]),
                       str(directory / HELPERS[2]), "-lm", "-o", str(binary)]
            require(run(command, "compile-" + variant).returncode == 0,
                    f"{variant} compilation failed")
            binaries[variant] = binary
            record["binaries"][variant] = {"path": str(binary), "sha256": digest(binary.read_bytes()),
                                            "source_sha256": digest(content), "compile_label": "compile-" + variant}
            save()

        listed = run([str(binaries["fixed"]), "--list"], "scenario-list")
        require(listed.returncode == 0, "Scenario listing failed")
        scenarios = [json.loads(line) for line in listed.stdout.splitlines()]
        names = {row["name"] for row in scenarios}
        require(len(names) == len(scenarios) == 50, "Expected 50 unique scenarios")
        require(UNCHANGED <= names, "Observable controls are missing")
        record["status"] = "testing"
        for scenario in scenarios:
            name = scenario["name"]
            results = {}
            for variant in ("original", "fixed"):
                result = run([str(binaries[variant]), name], variant + "-" + name)
                events = [json.loads(line) for line in result.stdout.splitlines()]
                require(bool(events), f"{variant}/{name}: missing result")
                final = events[-1]
                require(final["event"] == "result" and final["name"] == name and final["checks"] > 0,
                        f"{variant}/{name}: malformed result")
                expected_failure = variant == "original" and scenario["original_should_fail"]
                require(result.returncode == (1 if expected_failure else 0),
                        f"{variant}/{name}: unexpected exit code")
                require((final["failures"] > 0) == expected_failure,
                        f"{variant}/{name}: unexpected check failures")
                results[variant] = {"exit_code": result.returncode, "checks": final["checks"],
                                    "failures": final["failures"],
                                    "stdout_sha256": record["commands"][-1]["stdout_sha256"]}
            if name in UNCHANGED:
                require(results["original"]["stdout_sha256"] == results["fixed"]["stdout_sha256"],
                        f"Observable control changed: {name}")
            record["cases"].append({**scenario, **results})

        summary = {"scenarios": len(scenarios),
                   "fixed_checks": sum(row["fixed"]["checks"] for row in record["cases"]),
                   "original_fail_fixed_pass": sum(row["original_should_fail"] for row in scenarios),
                   "unchanged_observable_controls": len(UNCHANGED)}
        require(summary["fixed_checks"] == 199 and summary["original_fail_fixed_pass"] == 26,
                "Unexpected regression coverage")
        require(digest((ROOT / COMPONENT).read_bytes()) == record["fixed_source_sha256"], "Current source changed")
        require(digest(Path(__file__).read_bytes()) == record["runner_sha256"], "Runner changed")
        require(digest(Path(__file__).with_name("harness.c").read_bytes()) == record["harness_sha256"], "Harness changed")
        for row in record["inputs"]:
            require(digest((ROOT / row["path"]).read_bytes()) == row["sha256"], "Shared input changed: " + row["path"])
            require(digest((shared / row["path"]).read_bytes()) == row["sha256"], "Copied input changed: " + row["path"])
        for variant, expected in (("original", record["original_source_sha256"]), ("fixed", record["fixed_source_sha256"])):
            directory = out / variant
            require(digest((directory / COMPONENT).read_bytes()) == expected, "Copied component changed: " + variant)
            for path in HELPERS:
                require((directory / path).read_bytes() == (shared / path).read_bytes(), "Copied helper changed")
            for row in record["headers"]:
                require(digest((headers / row["name"]).read_bytes()) == row["sha256"], "Generated header changed")
                require(digest((directory / COMPONENT.parent / row["name"]).read_bytes()) == row["sha256"], "Copied header changed")
        for binary in record["binaries"].values():
            require(digest(Path(binary["path"]).read_bytes()) == binary["sha256"], "Executable changed")
        record.update(status="passed", summary=summary)
        save()
        print(json.dumps(summary, sort_keys=True))
        print(f"Artifacts: {out}")
        return 0
    except (Exception, KeyboardInterrupt) as error:
        record.update(status="failed", error=str(error) or type(error).__name__)
        save()
        print(f"Regression failed: {error}\nArtifacts: {out}", file=sys.stderr)
        return 1
    finally:
        signal.signal(signal.SIGTERM, previous_sigterm)


if __name__ == "__main__":
    sys.exit(main())

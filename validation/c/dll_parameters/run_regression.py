"""Compare the actual DLL parameter checker against its pre-fix source."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
BASELINE = "5e2396cef8e57765c6a8fefe34fc379c3a60c163"
BASELINE_WRAPPER_SHA256 = "09f4f721e685d722692396b9f0e02c7a9372248446efa9bebdbf9bcd7df85e47"
WRAPPER = Path("ieee_cigre_dll/gfm_battery_dll.c")
MODULES = [Path(name) for name in (
    "c_language_library/gfm_essentials/machine_model_A",
    "c_language_library/gfm_essentials/current_limiting_A",
    "c_language_library/companion_algorithms/ac_monitor_A",
    "c_language_library/companion_algorithms/ac_checks_A",
    "c_language_library/companion_algorithms/current_source_A",
    "c_language_library/companion_algorithms/power_limiter_A",
    "c_language_library/design_specific_examples/circuit_controls_B",
)]
SOURCES = [Path("ieee_cigre_dll/gfm_battery_app.c")]
SOURCES += [module / (module.name + ".c") for module in MODULES]
SOURCES += [Path("c_language_library/helper_lib") / name for name in ("cmplx.c", "timers.c")]
SCHEMAS = [module.parent / (module.name + "_schema.yaml") for module in MODULES]
SUPPORT = [Path(name) for name in (
    "ieee_cigre_dll/gfm_battery_app.h",
    "ieee_cigre_dll/IEEE_Cigre_DLLInterface.h",
    "ieee_cigre_dll/IEEE_Cigre_DLLInterface_types.h",
    "ieee_cigre_dll/logging/signal_catalog.h",
    "ieee_cigre_dll/dll_config.yaml",
    "ieee_cigre_dll/debug_signals_config.yaml",
    "ieee_cigre_dll/generate_dll_config.py",
    "ieee_cigre_dll/logging/generate_signal_catalog.py",
    "ieee_cigre_dll/logging/generate_extra_outputs.py",
    "schema_tools/generate_header.py",
    "schema_tools/schema_signal_utils.py",
    "c_language_library/helper_lib/cmplx.h",
    "c_language_library/helper_lib/timers.h",
    "c_language_library/helper_lib/helper_macros.h",
    "c_language_library/helper_lib/common_types.h",
)]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(content):
    return hashlib.sha256(content).hexdigest()


def baseline(path):
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "show", f"{BASELINE}:{path.as_posix()}"],
            stderr=subprocess.PIPE, timeout=30,
            env=dict(os.environ, GIT_NO_LAZY_FETCH="1", GIT_TERMINAL_PROMPT="0"),
        )
    except subprocess.CalledProcessError as error:
        raise RuntimeError(
            f"Cannot read baseline {BASELINE} locally. Use a Git clone containing that commit; "
            "for a shallow clone, fetch history manually with git fetch --unshallow."
        ) from error
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Baseline Git read exceeded 30 seconds; check the local repository") from error


def metadata(events):
    return [event for event in events if event["event"] not in ("case", "result")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Compiler command; defaults to CC or cc")
    parser.add_argument("--output-dir", "--output", type=Path, help="New directory for build files and logs")
    args = parser.parse_args()
    out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix="openibr-parameters-"))
    if args.output_dir:
        out.mkdir(parents=True, exist_ok=False)
    record = {"status": "preparing", "baseline": BASELINE, "inputs": [],
              "dependency_snapshot": "current source tree",
              "generated": [], "commands": [], "executables": {},
              "called_entry_points": ["Model_GetInfo", "Model_CheckParameters"],
              "runner_sha256": digest(Path(__file__).read_bytes()),
              "harness_sha256": digest(Path(__file__).with_name("harness.c").read_bytes())}

    def save():
        (out / "manifest.json").write_text(json.dumps(record, indent=2, allow_nan=False) + "\n")

    def run(argv, label, timeout=120):
        result = subprocess.run(argv, cwd=out, capture_output=True, timeout=timeout)
        for stream in ("stdout", "stderr"):
            (out / (label + "." + stream)).write_bytes(getattr(result, stream))
        record["commands"].append({"argv": argv, "exit_code": result.returncode,
                                   **{s: label + "." + s for s in ("stdout", "stderr")},
                                   **{s + "_sha256": digest(getattr(result, s))
                                      for s in ("stdout", "stderr")}})
        save()
        return result

    try:
        compiler = shlex.split(args.cc or os.environ.get("CC", "cc"))
        require(compiler and shutil.which(compiler[0]), "C compiler unavailable; set CC or --cc")
        require(run([*compiler, "--version"], "compiler-version").returncode == 0,
                "Compiler version check failed")
        require(run([sys.executable, "-I", "-B", "-c", "import yaml; print(yaml.__version__)"],
                    "pyyaml-version").returncode == 0, "PyYAML is required by the header generators")

        original = baseline(WRAPPER)
        require(digest(original) == BASELINE_WRAPPER_SHA256, "Baseline wrapper SHA256 differs from upstream")
        fixed = (ROOT / WRAPPER).read_bytes()
        require(fixed != original, "Current wrapper is unchanged from the pre-fix baseline")
        record["original_wrapper_sha256"] = digest(original)
        record["fixed_wrapper_sha256"] = digest(fixed)

        shared = out / "shared"
        for path in [*SOURCES, *SCHEMAS, *SUPPORT]:
            content = (ROOT / path).read_bytes()
            target = shared / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
            record["inputs"].append({"path": path.as_posix(), "sha256": digest(content)})

        python = [sys.executable, "-I", "-B"]
        for module, schema in zip(MODULES, SCHEMAS):
            require(run([*python, str(shared / "schema_tools/generate_header.py"),
                         str(shared / schema), str(shared / module)],
                        "generate-" + module.name).returncode == 0, "Schema generation failed")
        dll = shared / "ieee_cigre_dll"
        catalog = dll / "logging"
        commands = (
            ("generate-dll-config", [str(dll / "generate_dll_config.py"),
                                     str(dll / "dll_config.yaml"), str(dll / "dll_config_generated.h")]),
            ("generate-signal-catalog", [str(catalog / "generate_signal_catalog.py"),
                                         str(dll / "gfm_battery_app.c"), str(shared / "c_language_library"),
                                         str(catalog / "signal_catalog_autogen.h"), str(catalog)]),
            ("generate-extra-outputs", [str(catalog / "generate_extra_outputs.py"),
                                       str(dll / "debug_signals_config.yaml"), str(dll / "gfm_battery_app.c"),
                                       str(shared / "c_language_library"), str(catalog / "extra_outputs_autogen.h")]),
        )
        for label, command in commands:
            require(run([*python, *command], label).returncode == 0, "DLL header generation failed")
        generated = sorted(shared.rglob("*_autogen.h"))
        generated += [dll / "dll_config_generated.h", catalog / "available_signals_autogen.txt"]
        require(len(generated) == 18, "Unexpected generated-header set")
        record["generated"] = [{"path": path.relative_to(shared).as_posix(),
                                "sha256": digest(path.read_bytes())} for path in generated]

        harness = out / "harness.c"
        harness.write_bytes(Path(__file__).with_name("harness.c").read_bytes())
        variants = {}
        for variant, content in (("original", original), ("fixed", fixed)):
            tree = out / variant
            shutil.copytree(shared, tree)
            (tree / WRAPPER).write_bytes(content)
            binary = tree / ("parameter_test.exe" if os.name == "nt" else "parameter_test")
            command = [*compiler, "-std=c99", "-O2", "-g", "-Wall", "-Wextra", "-I", str(tree),
                       str(harness), *[str(tree / path) for path in SOURCES], "-lm", "-o", str(binary)]
            require(run(command, "compile-" + variant).returncode == 0, f"{variant} compilation failed")
            record["executables"][variant] = {"path": str(binary.relative_to(out)),
                                              "sha256": digest(binary.read_bytes())}
            executed = run([str(binary)], "test-" + variant)
            events = [json.loads(line) for line in executed.stdout.splitlines()]
            require(events and events[-1]["event"] == "result", "Missing harness result")
            cases = [event for event in events if event["event"] == "case"]
            require(len(cases) == len({event["name"] for event in cases}) == events[-1]["cases"] == 65,
                    "Unexpected case set")
            require(events[-1]["checks"] == 445, "Unexpected check count")
            require(executed.returncode == (1 if variant == "original" else 0),
                    f"{variant}: unexpected exit code")
            for event in cases:
                expected_failure = variant == "original" and event["original_should_fail"]
                require((event["failures"] > 0) == expected_failure,
                        f"{variant}/{event['name']}: unexpected outcome")
            require(events[-1]["failures"] == (20 if variant == "original" else 0),
                    f"{variant}: unexpected failure count")
            variants[variant] = events

        before = {event["name"]: event for event in variants["original"] if event["event"] == "case"}
        after = {event["name"]: event for event in variants["fixed"] if event["event"] == "case"}
        require(before.keys() == after.keys(), "Variant case names differ")
        for name, event in after.items():
            if not event["original_should_fail"]:
                require(before[name] == event, f"Control behavior changed: {name}")
        require(metadata(variants["original"]) == metadata(variants["fixed"]), "Interface metadata changed")
        record["summary"] = {"cases": 65, "fixed_checks": 445, "original_fail_fixed_pass": 10,
                             "unchanged_controls": 55, "metadata_identical": True}
        require(sum(event["original_should_fail"] for event in after.values()) == 10,
                "Unexpected NaN regression count")
        record["status"] = "passed"
        save()
        print(json.dumps(record["summary"], sort_keys=True))
        print(f"Build files and logs: {out}")
        return 0
    except Exception as error:
        record.update(status="failed", error=str(error))
        save()
        print(f"{error}\nBuild files and logs: {out}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

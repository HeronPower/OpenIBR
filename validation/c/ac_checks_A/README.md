# AC-check voltage regressions

The two phase-voltage extrema must remain numeric. Storing `fminf`/`fmaxf`
results in `bool` turns nonzero values into 1, masking voltage excursions.

From a Git clone containing public v1.1 (`5e2396c`), using Python 3 with PyYAML
and a GCC/Clang-compatible C11 compiler:

```sh
python3 validation/c/ac_checks_A/run_regression.py
```

Use `CC` or `--cc` to select a compiler. Build artifacts, individual scenario
logs and a source-hash manifest remain in a new temporary directory; use
`--output-dir` to choose a new directory explicitly. Source archives are not
supported. The runner does not fetch missing Git history, access the network,
generate files in the source tree or install packages.
Explicit outputs must be new directories outside the source checkout or under
its `.context/` directory; existing artifacts and implementation subtrees are
refused. PyYAML is checked in the same isolated Python environment used for
header generation before any copying or compilation.

The runner reads and verifies the original component from public v1.1 Git
history, then compares it with the current working-tree source. It snapshots
and hashes the current helpers, schema and generator for both variants,
generates headers, and compiles the real component with `timers.c` and `cmplx.c`.
Current source comments and unrelated edits are allowed; changed behavior must
still satisfy the regression checks. Every scenario runs in a fresh process
because component timers include function-static state.
Before reporting success, the runner rechecks the retained source copies,
generated headers and executables against their recorded hashes.

The manifest records the baseline lookup, compiler version, command arguments,
working directories, process IDs, timings and raw log hashes, plus hashes of
the compiler command's first executable, Python, Git and both test binaries.
Compiler wrappers remain supported; the first-executable hash identifies the
wrapper, and the version/compilation logs describe its selected toolchain.
Commands run in separate POSIX process groups. A 30-second command timeout,
Ctrl-C or SIGTERM retains the logs and a failed manifest, and stops the owned
command group, including compiler descendants. Windows/MSVC is unsupported.
The group is also stopped after ordinary completion to seal logs from any
descendants left by compiler wrappers.
Output-path and missing-executable errors reported before artifact creation
leave no manifest.

Expected result: **50 corrected scenarios / 199 checks; 26 original failures
corrected; 15 controls with identical observed state logs**. Coverage includes
unequal phases and permutations, both voltage-trip tiers, inclusive
qualification and strict trip boundaries, assertion/clearing/recovery,
frequency and negative-sequence controls, signed-zero and arithmetic probes.
Crashes, missing results and unexpected original passes fail the runner.

The fixtures use positive 3.5-ms assertion/qualification and 2.5-ms clearing
delays at a 1-ms call interval. These are test timings, not protection settings.
Negative phase values test signed arithmetic, not valid RMS measurements.
Timer checks allow 1e-7 s for float rounding. Windows DLL loading, Simulink,
blackstart permission and full electrical behavior are outside these tests.
Validated on macOS/arm64 with Apple Clang 21.0.0, and Ubuntu 24.04.5/x86_64
with GNU GCC 13.3.0 in a [fork-only Linux run](https://github.com/wattness/OpenIBR/actions/runs/40224300793).
The Linux run used preinstalled Python 3.12.3/PyYAML 6.0.1 and the same
regression criteria; no dependencies were installed. The 15 observed-state
controls compare original and fixed builds within each platform. Binary or
trace byte identity between platforms is not an acceptance criterion.
Other platforms remain unverified.

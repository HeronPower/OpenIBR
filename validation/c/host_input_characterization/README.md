# Battery host-input characterization

Run the unchanged v1.1 battery wrapper from a clean checkout and retain actual
host inputs, outputs, return codes and exposed controller fields. This is a
direct-C, open-loop API experiment. It does not simulate an electrical network.

## Run

Use Python 3.10 or later, existing PyYAML 6 or later, and an existing C99-capable
Clang/GCC compiler. Dependency checks install or activate nothing. The runner
uses the same isolated Python interpreter for checking PyYAML and generating
headers. Local validation used Python 3.12, PyYAML 6.0.2 and Apple clang 21 on
macOS arm64. The same default smoke passed on Ubuntu 24.04.5/x86_64 with GNU
GCC 13.3.0, preinstalled Python 3.12.3 and PyYAML 6.0.1 in a
[fork-only Linux run](https://github.com/wattness/OpenIBR/actions/runs/40227077985).
That run retained 14 trajectories and 171,570 actual calls: ten finite controls
passed and four malformed-input cases remained observations. Its thin launcher
maps SIGTERM to Python interruption for the runner's existing cleanup path;
the runner, controller, case design and source/header pins were unchanged.
Windows/MSVC is unsupported; this version targets POSIX Clang/GCC.

From the checkout root:

```sh
python3 validation/c/host_input_characterization/run.py --check-inputs
python3 validation/c/host_input_characterization/run.py --preflight
python3 validation/c/host_input_characterization/run.py --output-dir /tmp/openibr-input-smoke
```

The default smoke selection runs 14 trajectories: ten finite controls and four
NaN observations. A directory must be new, outside the source checkout or under
its `.context` artifact directory; the runner refuses to overwrite one.
Use a different path for each run. `--cc /path/to/clang` selects an existing
compiler; macOS discovers its installed SDK with `xcrun`, with an optional
`--sdk /path/to/SDK`. `--source-dir /path/to/OpenIBR` accepts a separate checkout
or extracted source tree. Git, MATLAB, Simulink and a Windows DLL loader are not
build dependencies.

Select a smaller experiment or the complete retained 36-case design explicitly:

```sh
python3 validation/c/host_input_characterization/run.py --list-cases
python3 validation/c/host_input_characterization/run.py --cases nan_P_request --output-dir /tmp/openibr-input-request
python3 validation/c/host_input_characterization/run.py --suite all --output-dir /tmp/openibr-input-full
```

Case selection includes matched references and paired finite controls
automatically; `manifest.json` records requested and included cases. The
single-request example includes two independent finite reference trajectories.
The full design contains 19 finite/context/saturation controls and 17
behavior-only cases. Cases retain their original histories and targets.

## Method and evidence

`SOURCE-PINS.json` pins 33 upstream files and 18 generated artifacts against
`dev`/v1.1, commit `5e2396cef8e57765c6a8fefe34fc379c3a60c163`. It includes the
wrapper, active controller modules, schemas, configuration and generator
sources. Source changes are refused before execution. Pins reproduce this
revision; reviewing a later source version requires a separate pin update.

The runner copies these inputs into the new output directory. It invokes only
the seven single-schema generators and DLL configuration/catalog/extra-output
generators, with explicit copied-tree paths, then checks all eighteen generated
hashes. It does not invoke a setup script, the upstream Makefile or the
all-module generator, or generate headers alongside original sources. LF source bytes are
required by the pins; CRLF-converted checkouts fail clearly.

Each trajectory starts a new executable process. Its only initialization is
the actual `Model_FirstCall` and `Model_Initialize` calls recorded in metadata.
A warm trajectory receives 12,004 finite host calls at 200 µs, one selected
scalar perturbation, and 250 finite recovery calls on a continuous clock.
Recovery happens in that same process. Reinitialization is not used as a cold
restart. The injection index is 12,004; the recovery window is 50 ms. The cold
case, when selected, retains its separate zero-prefix history.

The harness includes the actual wrapper and links the actual application,
seven modules and helpers. It supplies rotating voltage API stimuli, zero
measured-current stimuli and fixed 1,000 V DC in the reference history. It
records commanded bridge voltage and PWM enable; these are not measured PCC
or POI power or electrical-network response.

Outputs include:

- `manifest.json`: selection, source/package/generated hashes, compiler/SDK and
  numeric preflight, exact command arguments, process IDs, exit status and scope.
- Per-case `inputs.bin`, `outputs.bin` and `returns.bin`: every actual call,
  with native byte order declared in that case's metadata.
- Per-case `.stdout`: JSON lines with metadata, six exposed-catalog snapshots,
  and the five-call prefix tail, injection and recovery traces. Scalars preserve
  hexadecimal values, classifications and native bytes, including nonfinite data.
- `comparisons.json`: matched-input checks, output differences, exposed-field
  differences and recovery observations. Stderr and failed-run manifests are
  retained alongside the copied inputs and executable.

Commands recorded in the manifest run in their own process groups. Timeout or interruption stops that
owned group, drains its output, and records a failed manifest; it does not send
signals to another session's processes.

The numeric preflight requires IEEE binary32/binary64 sizes and records the
target's NaN conversion, nonzero comparison and clamp behavior. Build flags
disable fast math and floating-point contraction. Exact byte comparisons are
used only between matched trajectories in the same build; no cross-platform
NaN payload, transient value or nonfinite-count equality is required.

Finite controls check output finiteness, actual return codes, source-defined
disable/saturation behavior and same-build repeatability. Perturbed cases must
match their reference input bytes before injection and throughout recovery,
with only the declared scalar changed on injection. Behavior-only cases report
actual returns and propagation; their outcomes have no safety-pass denominator.
`completed_characterization` means capture/integrity and selected finite
controls completed, not that malformed inputs are acceptable.

## Interpretation limits

The baseline retains the original Boolean voltage-extrema defect in
`ac_checks_A.c`. It is not a corrected-controller result. A finite bridge command
or OK status does not establish finite intermediate state, input acceptance or
recovered private history. The catalog exposes declared fields, not every
function-local filter or divider. Continued effects are limited to the recorded
50-ms window; this kit adds no rejection, shutdown or reset policy.

There is no SOC or thermal trajectory, real-site/owner/QSE binding, delivery
limit, revenue result or grid-code conformance claim. The battery example's
fixed-DC and example-PPC limitations remain. Windows DLL loading, unload/reload,
snapshots and hardware behavior are outside this direct-C experiment.

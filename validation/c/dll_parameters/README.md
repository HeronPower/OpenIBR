# DLL parameter-validation regression

`Model_CheckParameters` accepted NaN for H, D and kDroop: both ordered bounds
comparisons are false for NaN. The patch adds `isfinite` and reads the named
parameter fields into a local array. Inclusive bounds, metadata, return codes,
error text and null handling stay unchanged.

Run from a Git clone containing upstream commit
`5e2396cef8e57765c6a8fefe34fc379c3a60c163` and the parameter-validation patch:

```sh
python3 -I -S -B validation/c/dll_parameters/run_regression.py
```

If a shallow clone omits that commit, fetch its history first with
`git fetch --unshallow`. The runner makes no network requests.

Requirements: Git, Python 3.9+, PyYAML and a GCC-compatible C99 compiler. Set `CC`
or pass `--cc clang` to select an installed compiler. Use
`--output-dir <new-directory>` (`--output` is an alias) to retain the build at a
chosen path; otherwise it is retained in a temporary directory. The runner
installs nothing and leaves source files unchanged.

Both variants link the real application, seven controller modules and two helpers
from one snapshot of the current source tree, including any preceding voltage
correction. The original wrapper comes from the pinned upstream commit; the fixed
wrapper comes from the current tree. Headers are generated in copied trees using
the same current schemas and DLL generators. The harness includes each wrapper
to use its private parameter type, and calls only `Model_GetInfo` and
`Model_CheckParameters`. No controller functions are stubbed.

Expected result: **65 cases, 445 checks; 10 original-fail/fixed-pass NaN cases;
55 unchanged controls and identical interface metadata**. The original executable
is expected to fail; the runner succeeds only when it reproduces those failures
and the fixed executable passes. Cases cover signed NaNs, infinities, exact and
adjacent bounds, finite extremes, signed zero, mixed invalid parameters, null
pointers, unchanged host buffers and valid rechecks after rejection. The manifest
records source, generated-header and executable hashes, commands and test logs.

Validated on macOS with Apple Clang 21.0.0. GCC-style compiler selection is
supported; other platforms are unverified. Direct C linking does not test Windows
DLL loading or ABI compatibility. Initialization, electrical stepping and grid
compliance are outside this regression. A host must call and honor
`Model_CheckParameters`; this patch leaves `Model_Initialize` unchanged.

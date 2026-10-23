# Model return status regression

This regression extracts the actual dispatch statements for `Model_FirstCall`,
`Model_CheckParameters`, `Model_Initialize`, `Model_Outputs` and `Model_Terminate`
from the working harness and OpenIBR v1.1 at
`5e2396cef8e57765c6a8fefe34fc379c3a60c163`. It verifies the original harness SHA256
before compiling each statement with a controlled callback.

Run from the repository root with Python 3 and GCC or Clang supporting C11:

```sh
python3 validation/c/model_return_status/run_regression.py --out /tmp/openibr-status-run-1
```

The output directory must be new. Use `--cc clang` or `--cc gcc` to select a
compiler. Commands have a 30-second deadline; individual cases have five seconds.

The 75 cases cover:

- Both versions: five call sites returning `0`, `1`, `2`, `-1` and `3`, with
  distinct valid error and general diagnostics, for 25 cases per version.
- Fixed version: five additional diagnostic configurations at each site. Status
  `0` receives two poisoned pointers; status `1` receives a poisoned irrelevant
  error pointer; status `2` receives a poisoned irrelevant general pointer; the
  selected diagnostic is also tested as null with the other pointer poisoned.

Assertions require exact stderr, continuation and process exit, one callback,
unchanged complete Instance bytes and the expected FirstCall library-unload stub
count. Exactly seven continuation outcomes must change: nonfatal status `1` at
the first four sites, and status `2`, `-1` or `3` at Terminate.

The fixture compiles only an extracted statement, the interface header and the
status helper. It neither loads a DLL nor executes the full Windows harness,
controller, network or plant. These results test host dispatch, not numerical
behavior or simulator compatibility. Native Windows harness execution remains a
separate check; this runner uses GCC/Clang C11 flags.

`manifest.json` records the declared case matrix, compiler, platform, source
hashes, extracted source spans, exact commands, deadlines, exits and assertions.
The fresh receipt directory retains source snapshots, generated C, binaries and
all stdout/stderr logs with SHA256 hashes.

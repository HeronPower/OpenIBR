## Unreleased

### Elements modified
#### Companion Algorithms
- ac_checks_A: added a blackstart permissive (new input `bsCmd`, config group `BLACKSTART_CFG`, outputs `enable`, state group `BLACKSTART_STATE`).  `acOk` is unchanged; `enable` = `acOk` OR an open blackstart window.
- ac_checks_A: phase voltage extrema are now `float` (they were `bool`, which kept voltage trips from ever asserting).
- Added host-side tests in `c_language_library/tests/ac_checks_A` (`python run_tests.py`).

## Release 1.1

Added infrastructure for IEEE/CIGRE-compliant DLLs.  This generates a code-based model portable to different commercial modeling softwares including PSCAD.

### Elements added
#### Design-Specific Examples
- circuit_controls_B

### Elements modified

- None

## Release 1.0

First public release.  Includes:
- Simulink and PSCAD native models for battery, PV, and data-center applications.
- C-language implementation and associated code auto-generation tools
- Demonstration app for CHiL
- Documentation and guides

### Elements added
#### GFM Essentials
- machine_model_A
- current_limiting_A
- current_limiting_B

#### Companion Algorithms
- ac_checks_A
- ac_monitor_A
- current_source_A
- dc_voltage_control_A
- power_limiter_A

#### Design-Specific Examples
- circuit_controls_A
- modes_and_protection_A

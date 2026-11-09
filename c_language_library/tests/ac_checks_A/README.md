# ac_checks_A host tests

Scenario tests for the blackstart permissive in `ac_checks_A` (issue #11), plus a regression test for the voltage-extrema types (issue #10).

```
cd c_language_library/tests/ac_checks_A
python run_tests.py            # uses $CC, defaults to gcc
```

`run_tests.py` regenerates the `ac_checks_A` headers from the schema, builds `test_blackstart.c` with `ac_checks_A.c` and the helper library, and runs each scenario in its own process (the module keeps its timers in function-level statics).

Scenarios step `acChecks_1kHz()` at 1 ms on a 480 V / 60 Hz system and check:

- a live or partially energized bus never latches blackstart
- the bus must be dead for `tDeadBus` before a command is accepted, and the command edge must arrive after that
- the window lasts `tBlackstart` and then closes, and a held command does not restart it
- a formed, healthy voltage lets `acOk` qualify inside the window with no gap in `enable`
- an over-voltage trip releases the latch and a cleared OV does not restart it (the failure mode of the AND-gate design discussed in #11)
- `tBlackstart = 0` disables blackstart
- `random_invariants`: 600 s of randomized bus voltage and commands, checking on every step that the latch only rises on a command edge on a dead bus, never stays open longer than `tBlackstart` or through an OV trip, only releases on timeout or OV, and that `enable == acOk || active`. Set `BLACKSTART_SEED` to change the seed.

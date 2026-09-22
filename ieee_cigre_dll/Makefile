# Makefile -- MinGW/GCC build for GFM_Battery_OpenIBR.dll
#
# Run `make` (or `make test` to also run the harness) from this directory.
# Requires: gcc (MinGW-w64), python3 + PyYAML (for header generation).

REPO_ROOT := ..
BUILD_DIR := build

CC      := gcc
CFLAGS  := -std=c99 -Wall -O2 -shared -DNDEBUG
LDFLAGS := -Wl,--out-implib,$(BUILD_DIR)/libGFM_Battery_OpenIBR.a

# Git Bash puts its own bundled, older MinGW (Program Files\Git\mingw64\bin)
# near the front of PATH, ahead of a full standalone MinGW-w64 install
# elsewhere. `gcc` itself still resolves to the right install (its own
# bin dir usually isn't on PATH at all), but cc1.exe's DLL search falls
# through to PATH for libwinpthread-1.dll and picks up Git's older,
# mismatched copy, failing with "Entry Point Not Found: clock_gettime64".
# Force the resolved gcc's own bin directory to the front of PATH on every
# compiler invocation below (a plain Make `export PATH := ...` does NOT
# reliably win here: this build uses GnuWin32's native-Win32 `make`, which
# writes an all-caps PATH into the child environment alongside Windows'
# own case-insensitive "Path", and the wrong one can end up taking effect).
GCC_BINDIR := $(shell dirname "$$(command -v $(CC))")
RUN_CC := PATH="$(GCC_BINDIR):$$PATH" $(CC)

SOURCES := \
	gfm_battery_dll.c \
	gfm_battery_app.c \
	$(REPO_ROOT)/c_language_library/gfm_essentials/machine_model_A/machine_model_A.c \
	$(REPO_ROOT)/c_language_library/gfm_essentials/current_limiting_A/current_limiting_A.c \
	$(REPO_ROOT)/c_language_library/companion_algorithms/ac_monitor_A/ac_monitor_A.c \
	$(REPO_ROOT)/c_language_library/companion_algorithms/ac_checks_A/ac_checks_A.c \
	$(REPO_ROOT)/c_language_library/companion_algorithms/current_source_A/current_source_A.c \
	$(REPO_ROOT)/c_language_library/companion_algorithms/power_limiter_A/power_limiter_A.c \
	$(REPO_ROOT)/c_language_library/design_specific_examples/circuit_controls_B/circuit_controls_B.c \
	$(REPO_ROOT)/c_language_library/helper_lib/cmplx.c \
	$(REPO_ROOT)/c_language_library/helper_lib/timers.c

# Everything under test_harness/ is host-side test scaffolding, not part
# of the DLL itself -- the DLL performs no file I/O of any kind. See
# documentation/9_using_the_ieee_cigre_dll.md, sections 2.1 and 3.2.
TEST_SOURCES     := test_harness/test_harness.c test_harness/ac_network.c test_harness/converter_sim.c test_harness/inverter_requests.c test_harness/comtrade_writer.c
SNAPSHOT_SOURCES := test_harness/test_snapshot.c test_harness/converter_sim.c test_harness/dll_snapshot.c test_harness/comtrade_writer.c

TARGET          := $(BUILD_DIR)/GFM_Battery_OpenIBR.dll
TEST_TARGET     := $(BUILD_DIR)/test_harness.exe
SNAPSHOT_TARGET := $(BUILD_DIR)/test_snapshot.exe

.PHONY: all headers clean test test-snapshot

all: headers $(TARGET) $(TEST_TARGET)

test: all
	$(TEST_TARGET) $(TARGET)

test-snapshot: headers $(TARGET) $(SNAPSHOT_TARGET)
	$(SNAPSHOT_TARGET) $(TARGET)

headers:
	cd $(REPO_ROOT)/schema_tools && python generate_all_headers.py
	cd logging && python generate_signal_catalog.py
	cd logging && python generate_extra_outputs.py
	python generate_dll_config.py

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# `headers` is a prerequisite here (not just of `all`) because it's the
# only thing regenerating the *_autogen.h headers these sources #include --
# without it listed here, Make has no way to know $(TARGET)/$(TEST_TARGET)
# are stale after a schema or debug_signals_config.yaml edit that doesn't
# also touch a .c file, and would silently keep the old build. Since
# `headers` is .PHONY (always considered out of date), this makes every
# `make`/`make test` invocation relink -- cheap, and removes that footgun.
$(TARGET): headers $(SOURCES) GFM_Battery_OpenIBR.def | $(BUILD_DIR)
	$(RUN_CC) $(CFLAGS) -o $(TARGET) $(SOURCES) GFM_Battery_OpenIBR.def $(LDFLAGS)

$(TEST_TARGET): headers $(TEST_SOURCES) | $(BUILD_DIR)
	$(RUN_CC) -std=c99 -Wall -O2 -o $(TEST_TARGET) $(TEST_SOURCES) -lm

$(SNAPSHOT_TARGET): headers $(SNAPSHOT_SOURCES) | $(BUILD_DIR)
	$(RUN_CC) -std=c99 -Wall -O2 -o $(SNAPSHOT_TARGET) $(SNAPSHOT_SOURCES) -lm

clean:
	rm -rf $(BUILD_DIR)

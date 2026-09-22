#!/usr/bin/env python3
"""
Top-level autogen driver for the application project. Runs all three
generation tasks, in dependency order:

1. Module headers -- schema_tools/generate_header.py regenerates each active
   module's *_autogen.h from its *_schema.yaml. "Active" means app.c
   actually #includes it (get_active_schema_files, imported below, figures
   this out by reading app.c's own #include list - not every module in
   c_language_library; see schema_tools/generate_all_headers.py for that).
   Some modules exist there only for another consumer (e.g. circuit_controls_B,
   used solely by the ieee_cigre_dll offline DLL build) and must be skipped
   here rather than regenerated for this app.
2. Signal ID list -- debug/signal_ids_autogen.h, one SIGNAL_ID_* define per
   scalar signal (or array element), across those same active-only modules.
3. Configurable DAC switch -- debug/configurable_dac_switch_autogen.h, the
   signal_id -> live value lookup used by debug/configurable_dac.c.

Tasks 2 and 3 live in debug/generate_configurable_dac.py, which calls
get_active_schema_files() itself too - app.c's own #include list is the one
source of truth for "active" everywhere here, nothing to keep in sync by hand.

Usage: python generate_project_files.py
"""

import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, '..'))
SCHEMA_TOOLS_DIR = os.path.join(REPO_ROOT, 'schema_tools')
APPLICATION_DIR = os.path.join(SCRIPT_DIR, 'c_code', 'application')
DEBUG_DIR = os.path.join(APPLICATION_DIR, 'debug')
sys.path.insert(0, DEBUG_DIR)
sys.path.insert(0, SCHEMA_TOOLS_DIR)

import generate_configurable_dac
from schema_signal_utils import get_active_schema_files


def module_dir(schema_path: str) -> str:
    """Directory generate_header.py should write a schema's headers into -
    its own basename with '_schema.yaml' stripped, sitting right next to it
    (e.g. ac_checks_A_schema.yaml -> ac_checks_A/). True for every module
    app.c currently includes; a module whose folder name doesn't match its
    schema filename (e.g. dc_voltage_cntl_A_schema.yaml -> dc_voltage_control_A/)
    would need special-casing here if app.c ever started including it."""
    return os.path.join(os.path.dirname(schema_path), os.path.basename(schema_path)[:-len('_schema.yaml')])


def main():
    app_c_path = os.path.join(APPLICATION_DIR, 'app.c')
    schema_root = os.path.join(REPO_ROOT, 'c_language_library')

    # 1. Regenerate the active module *_autogen.h headers first.
    for schema_path in get_active_schema_files(app_c_path, schema_root):
        subprocess.run([sys.executable, 'generate_header.py', schema_path, module_dir(schema_path)],
                        cwd=SCHEMA_TOOLS_DIR, check=True)

    # 2 & 3. Signal ID list, then the configurable DAC switch built on it.
    generate_configurable_dac.generate(
        app_c_path=app_c_path,
        schema_root=schema_root,
        output_header=os.path.join(DEBUG_DIR, 'configurable_dac_switch_autogen.h'),
        output_root=DEBUG_DIR,
    )


if __name__ == "__main__":
    main()

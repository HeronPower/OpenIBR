#!/usr/bin/env python3
"""
Top-level autogen driver for the application project. Runs all three
generation tasks, in dependency order:

1. Module headers -- schema_tools/generate_all_headers.py regenerates every
   module's *_autogen.h from its *_schema.yaml.
2. Signal ID list -- debug/signal_ids_autogen.h, one SIGNAL_ID_* define per
   scalar signal (or array element) across all schemas.
3. Configurable DAC switch -- debug/configurable_dac_switch_autogen.h, the
   signal_id -> live value lookup used by debug/configurable_dac.c.

Tasks 2 and 3 live in debug/generate_configurable_dac.py.

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

import generate_configurable_dac


def main():
    # 1. Regenerate the module *_autogen.h headers first (paths inside
    #    generate_all_headers.py are relative to schema_tools, hence cwd).
    subprocess.run([sys.executable, 'generate_all_headers.py'], cwd=SCHEMA_TOOLS_DIR, check=True)

    # 2 & 3. Signal ID list, then the configurable DAC switch built on it.
    generate_configurable_dac.generate(
        app_c_path=os.path.join(APPLICATION_DIR, 'app.c'),
        schema_root=os.path.join(REPO_ROOT, 'c_language_library'),
        output_header=os.path.join(DEBUG_DIR, 'configurable_dac_switch_autogen.h'),
        output_root=DEBUG_DIR,
    )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Simple script to generate headers for all YAML schema files
"""

import os
import subprocess

# Run the command for each YAML file
commands = [

    # run the generator for companion_algorithms modules
    "python generate_header.py ../c_language_library/companion_algorithms/ac_checks_A_schema.yaml ../c_language_library/companion_algorithms/ac_checks_A",
    "python generate_header.py ../c_language_library/companion_algorithms/ac_monitor_A_schema.yaml ../c_language_library/companion_algorithms/ac_monitor_A", 
    "python generate_header.py ../c_language_library/companion_algorithms/current_source_A_schema.yaml ../c_language_library/companion_algorithms/current_source_A",
    "python generate_header.py ../c_language_library/companion_algorithms/dc_voltage_cntl_A_schema.yaml ../c_language_library/companion_algorithms/dc_voltage_control_A",
    "python generate_header.py ../c_language_library/companion_algorithms/power_limiter_A_schema.yaml ../c_language_library/companion_algorithms/power_limiter_A",
    
    # run the generator for gfm_essentials modules
    "python generate_header.py ../c_language_library/gfm_essentials/current_limiting_A_schema.yaml ../c_language_library/gfm_essentials/current_limiting_A",
    "python generate_header.py ../c_language_library/gfm_essentials/current_limiting_B_schema.yaml ../c_language_library/gfm_essentials/current_limiting_B",
    "python generate_header.py ../c_language_library/gfm_essentials/machine_model_A_schema.yaml ../c_language_library/gfm_essentials/machine_model_A",

    # run the generator for design_specific_examples modules
    "python generate_header.py ../c_language_library/design_specific_examples/modes_and_protection_A_schema.yaml ../c_language_library/design_specific_examples/modes_and_protection_A",
    "python generate_header.py ../c_language_library/design_specific_examples/circuit_controls_A_schema.yaml ../c_language_library/design_specific_examples/circuit_controls_A",
    "python generate_header.py ../c_language_library/design_specific_examples/circuit_controls_B_schema.yaml ../c_language_library/design_specific_examples/circuit_controls_B",

    # NOTE: signal ID / configurable-DAC generation lives in
    # demo_projects/c_code/application/debug/generate_configurable_dac.py; the
    # top-level driver demo_projects/generate_project_files.py
    # runs this script first and then chains that generator.
]
print()

for cmd in commands:
    print(f"Running: {cmd}")
    os.system(cmd)

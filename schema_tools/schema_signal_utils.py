#!/usr/bin/env python3
"""
Shared helpers for tools that enumerate every signal declared across the
YAML schemas in c_language_library/ and need to resolve which schema
variant is actually compiled into a given target C file.

Extracted from demo_projects/c_code/application/debug/generate_configurable_dac.py
so that both it and logging/generate_signal_catalog.py can share one
implementation instead of drifting apart. Behavior is unchanged from the
original inline versions in that file.
"""

import glob
import os
import re

from generate_header import SchemaParser

CMPLX_COMPONENTS = (('REAL', 'real'), ('IMAG', 'imag'))


def camel_to_snake_upper(name: str) -> str:
    """Convert camelCase / PascalCase / snake_case to UPPER_SNAKE_CASE."""
    s = re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', name)
    s = s.replace('-', '_')
    return s.upper()


def signal_to_upper(name: str) -> str:
    """Convert a signal name (e.g. 'i_L', 'v_ac') to a compact upper token (e.g. 'IL', 'VAC')."""
    return name.replace('_', '').upper()


def expand_signal(signal_name: str, signal_def: dict) -> list:
    """Expand one signal definition into (id_suffix, access_expr) pairs.

    Scalars produce one pair; arrays produce one pair per element.
    cmplx_t signals are split into REAL/IMAG pairs (per element, if an array).
    """
    signal_token = signal_to_upper(signal_name)
    array_size = signal_def.get('array')
    is_cmplx = signal_def.get('type') == 'cmplx_t'

    indices = range(array_size) if array_size else [None]
    index_suffix = (lambda i: str(i) if i is not None else '')

    pairs = []
    for i in indices:
        base_access = f"{signal_name}[{i}]" if i is not None else signal_name

        if is_cmplx:
            for comp_suffix, field in CMPLX_COMPONENTS:
                pairs.append((f"{signal_token}_{comp_suffix}{index_suffix(i)}", f"{base_access}.{field}"))
        else:
            pairs.append((f"{signal_token}{index_suffix(i)}", base_access))

    return pairs


def get_active_schema_files(target_c_path: str, schema_root: str) -> list:
    """Find the schema files behind every '..._autogen.h' target_c_path includes.

    Only modules a target .c file actually #includes are considered (rather
    than every schema under c_language_library) because some modules share a
    schema 'metadata.name' across mutually-exclusive variants -- e.g.
    current_limiting_A and current_limiting_B both define a `currentLimiting_IN`
    struct, but with different members. Only one variant is ever linked into
    a given target, so including both here would produce conflicting struct
    redefinitions.
    """
    target_c_dir = os.path.dirname(os.path.normpath(target_c_path))
    with open(target_c_path, 'r') as f:
        include_paths = re.findall(r'#include\s+"([^"]+_autogen\.h)"', f.read())

    all_schema_files = [os.path.normpath(p) for p in
                        glob.glob(os.path.join(schema_root, '**', '*_schema.yaml'), recursive=True)]

    schema_files = []
    for include_path in include_paths:
        autogen_header = os.path.normpath(os.path.join(target_c_dir, include_path))
        module_dir = os.path.dirname(autogen_header)
        parent_dir = os.path.dirname(module_dir)
        header_name = os.path.basename(autogen_header)

        match = next(
            (s for s in all_schema_files
             if os.path.dirname(s) == parent_dir and f"{SchemaParser(s).get_filename()}_autogen.h" == header_name),
            None
        )
        if match is None:
            raise FileNotFoundError(f"Could not find schema file backing '{autogen_header}' (included by {target_c_path})")

        schema_files.append(match)

    return schema_files


def get_autogen_include_path(schema_path: str, output_root: str) -> str:
    """Path to a schema's generated public header, relative to output_root."""
    parser = SchemaParser(schema_path)
    module_dir = os.path.join(os.path.dirname(schema_path), os.path.basename(schema_path)[:-len('_schema.yaml')])
    header_name = f"{parser.get_filename()}_autogen.h"

    # The folder name isn't always derivable from the schema filename (e.g.
    # dc_voltage_cntl_A_schema.yaml -> dc_voltage_control_A/), so fall back to
    # searching next to the schema if the guessed folder doesn't exist.
    guessed_path = os.path.join(module_dir, header_name)
    if os.path.exists(guessed_path):
        return os.path.relpath(guessed_path, output_root)

    matches = glob.glob(os.path.join(os.path.dirname(schema_path), '**', header_name), recursive=True)
    if not matches:
        raise FileNotFoundError(
            f"Could not find generated header '{header_name}' for schema '{schema_path}'. "
            f"Run generate_header.py (or generate_all_headers.py) first."
        )

    return os.path.relpath(matches[0], output_root)

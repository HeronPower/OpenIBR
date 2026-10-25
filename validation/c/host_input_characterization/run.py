"""Reproduce pinned battery host-input observations with isolated builds/processes."""
import argparse
import datetime
import hashlib
import json
import math
import os
import platform
import re
import shutil
import signal
from pathlib import Path
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
MODULES = (
    'c_language_library/gfm_essentials/machine_model_A',
    'c_language_library/gfm_essentials/current_limiting_A',
    'c_language_library/companion_algorithms/ac_monitor_A',
    'c_language_library/companion_algorithms/ac_checks_A',
    'c_language_library/companion_algorithms/current_source_A',
    'c_language_library/companion_algorithms/power_limiter_A',
    'c_language_library/design_specific_examples/circuit_controls_B',
)
UNITS = ['ieee_cigre_dll/gfm_battery_app.c']
UNITS += [p + '/' + p.rsplit('/', 1)[-1] + '.c' for p in MODULES]
UNITS += ['c_language_library/helper_lib/' + p for p in ('cmplx.c', 'timers.c')]
FLAGS = ['-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-fno-fast-math', '-ffp-contract=off']


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def loads(text):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=unique)


def load(path):
    return loads(path.read_text())


def scalar(row):
    value = float.fromhex(row['value'])
    classification = ('nan' if math.isnan(value) else
                      ('negative_infinity' if value < 0 else 'positive_infinity')
                      if math.isinf(value) else 'finite')
    require(classification == row['classification'], 'Native numeric classification')
    require(len(bytes.fromhex(row['bytes'])) == row['byte_count'], 'Native numeric byte count')
    return value


def inspect_case(out, case, stdout):
    events = [loads(line) for line in stdout.splitlines()]
    require(events[0]['event'] == 'metadata' and events[-1]['event'] == 'result', 'Complete event stream')
    meta, footer = events[0], events[-1]
    n = case['history'] + 251
    expected_events = [('metadata', None), ('snapshot', 'after_initialization')]
    for k in range(n):
        if k == case['history']:
            expected_events.append(('snapshot', 'before_injection'))
        if k >= max(0, case['history'] - 5):
            expected_events.append(('step', k))
        for offset, name in ((0, 'after_injection'), (1, 'after_first_recovery'), (5, 'after_fifth_recovery')):
            if k == case['history'] + offset:
                expected_events.append(('snapshot', name))
    expected_events.extend((('snapshot', 'after_final_recovery'), ('result', None)))
    actual_events = [(e['event'], e.get('index') if e['event'] == 'step' else e.get('id')) for e in events]
    require(actual_events == expected_events, 'Exact native event sequence, without duplicates or unknown events')
    require(meta['case'] == case['id'] and meta['history_calls'] == case['history'] and
            meta['recovery_calls'] == 250 and meta['scalar'] == case['scalar'] and
            meta['context'] == case['context'] and meta['token'] == case['token'], 'Frozen case identity')
    require(meta['open_loop'] and not meta['electrical_network'] and meta['single_instance'], 'Experiment scope')
    require(footer['controller_calls'] == n and footer['harness_integrity_failures'] == 0, 'Recorded call coverage')
    require(len(meta['input_names']) == 12 and len(meta['output_names']) == 15 and
            len(meta['catalog_definitions']) == 231, 'Metadata counts')
    require(meta['byte_order'] in ['little', 'big'], 'Recorded native byte order')
    require(float.fromhex(meta['dt']) == 0.0002 and
            meta['initialization_returns'] == {'Model_FirstCall': 0, 'Model_Initialize': 0},
            'Recorded host cadence and actual initialization returns')
    endian = '<' if meta['byte_order'] == 'little' else '>'
    inputs = (out / (case['id'] + '.inputs.bin')).read_bytes()
    outputs = (out / (case['id'] + '.outputs.bin')).read_bytes()
    returns = (out / (case['id'] + '.returns.bin')).read_bytes()
    require((len(inputs), len(outputs), len(returns)) == (n * 96, n * 120, n * 4), 'Complete raw series sizes')
    snapshots = {e['id']: e for e in events if e['event'] == 'snapshot'}
    require(len([e for e in events if e['event'] == 'snapshot']) == 6, 'Snapshot count/duplicates')
    require(list(snapshots) == ['after_initialization', 'before_injection', 'after_injection',
                              'after_first_recovery', 'after_fifth_recovery', 'after_final_recovery'], 'Snapshot sequence')
    for snap in snapshots.values():
        require(list(snap['fields']) == list(meta['catalog_definitions']), 'Snapshot catalog names/order')
        for value in snap['fields'].values():
            scalar(value)
    steps = [e for e in events if e['event'] == 'step']
    start = max(0, case['history'] - 5)
    require([r['index'] for r in steps] == list(range(start, n)), 'Logged injection/recovery coverage')
    for row in steps:
        k = row['index']
        require(float.fromhex(row['time']) == k * 0.0002, 'Continuous actual host clock')
        require(row['phase'] == ('prefix_tail' if k < case['history'] else
                                'injection' if k == case['history'] else 'recovery'), 'Recorded history phase')
        require(len(row['inputs']) == 12 and len(row['outputs']) == 15, 'Logged host shape')
        for values, binary, width in [(row['inputs'], inputs, 12), (row['outputs'], outputs, 15)]:
            for j, value in enumerate(values):
                scalar(value)
                require(bytes.fromhex(value['bytes']) == binary[(k * width + j) * 8:(k * width + j + 1) * 8],
                        'Logged host bytes differ from complete raw series')
        for value in row['selected'].values():
            scalar(value)
        require(row['return_code'] == struct.unpack_from(endian + 'i', returns, k * 4)[0], 'Return series identity')
    actual_nonfinite = sum(not math.isfinite(v[0]) for v in struct.iter_unpack(endian + 'd', outputs))
    require(actual_nonfinite == footer['nonfinite_output_observations'], 'Raw output classification count')
    if case['category'] != 'behavior_only':
        require(all(v[0] == 0 for v in struct.iter_unpack(endian + 'i', returns)), 'Finite control return code')
        require(actual_nonfinite == 0, 'Finite control output classification')
    return {'case': case, 'metadata': meta, 'snapshots': snapshots, 'steps': steps,
            'inputs': inputs, 'outputs': outputs, 'returns': returns, 'endian': endian}


def compare(case, reference):
    spec = case['case']; k = spec['history']
    require(k == reference['case']['history'], 'Matched history length')
    require(case['inputs'][:k * 96] == reference['inputs'][:k * 96] and
            case['inputs'][(k + 1) * 96:] == reference['inputs'][(k + 1) * 96:], 'Matched prefix and recovery input bytes')
    changes = [i for i in range(12) if
               case['inputs'][k * 96 + i * 8:k * 96 + (i + 1) * 8] !=
               reference['inputs'][k * 96 + i * 8:k * 96 + (i + 1) * 8]]
    require(changes == ([] if spec['scalar'] == -1 else [spec['scalar']]), 'Only frozen scalar changed')
    require(case['outputs'][:k * 120] == reference['outputs'][:k * 120], 'Matched prefix output bytes')
    require(case['snapshots']['before_injection']['fields'] == reference['snapshots']['before_injection']['fields'],
            'Matched exposed state before injection')
    n = len(case['outputs']) // 120
    metrics = {}
    for j, name in enumerate(case['metadata']['output_names']):
        different, maxima, nonfinite = [], [], []
        for i in range(k, n):
            a = case['outputs'][i * 120 + j * 8:i * 120 + (j + 1) * 8]
            b = reference['outputs'][i * 120 + j * 8:i * 120 + (j + 1) * 8]
            if a != b:
                different.append(i)
            av = struct.unpack(case['endian'] + 'd', a)[0]
            bv = struct.unpack(case['endian'] + 'd', b)[0]
            if math.isfinite(av) and math.isfinite(bv):
                maxima.append(abs(av - bv))
            else:
                nonfinite.append(i)
        metrics[name] = {'different_samples_including_injection': len(different),
                         'first_different_call_index': different[0] if different else None,
                         'different_at_final_call': n - 1 in different,
                         'maximum_finite_absolute_difference': max(maxima) if maxima else None,
                         'nonfinite_comparison_samples': len(nonfinite)}
    injection = next(row for row in case['steps'] if row['index'] == k)
    final = case['steps'][-1]
    changed_catalog = {}
    for name in case['snapshots']:
        a, b = case['snapshots'][name]['fields'], reference['snapshots'][name]['fields']
        changed_catalog[name] = [key for key in a if a[key] != b[key]]
    return {'case': spec['id'], 'reference': reference['case']['id'], 'changed_input_scalars': changes,
            'exact_full_output_bytes': case['outputs'] == reference['outputs'],
            'exact_return_bytes': case['returns'] == reference['returns'],
            'injection_return_code': injection['return_code'],
            'injection_error_seed_retained': injection['error_seed_retained'],
            'injection_pwm': injection['outputs'][3], 'injection_selected': injection['selected'],
            'final_pwm': final['outputs'][3], 'final_selected': final['selected'],
            'changed_catalog_fields': changed_catalog, 'metrics': metrics,
            'recovery_window_seconds': 0.05, 'full_reset_or_safety_verdict': False}


SMOKE = (
    'reference_b', 'enable_zero', 'enable_negative_zero', 'enable_two', 'enable_negative_one',
    'p_at_positive_clamp', 'p_over_positive_clamp', 'q_at_positive_clamp', 'q_over_positive_clamp',
    'nan_v_ac_0', 'nan_i_L_0', 'nan_P_request', 'nan_enable',
)
CONTROL_PAIRS = (
    ('reference_a', 'reference_b'), ('enable_zero', 'enable_negative_zero'),
    ('p_at_positive_clamp', 'p_over_positive_clamp'),
    ('q_at_positive_clamp', 'q_over_positive_clamp'),
)
PACKAGE_FILES = ('run.py', 'harness.c', 'numeric_preflight.c', 'CASES.json', 'SOURCE-PINS.json', 'README.md')


def selected_cases(frozen, requested):
    rows = frozen['cases']
    by_id = {c['id']: c for c in rows}
    require(len(rows) == len(by_id) == 36, 'Frozen case count and unique identities')
    for case in rows:
        require(re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', case['id']) is not None, 'Safe case identity')
        require(type(case['history']) is int and case['history'] in (0, 12004), 'Frozen history length')
        require(type(case['scalar']) is int and -1 <= case['scalar'] < 12, 'Frozen input index')
        require(case['context'] in ('enabled', 'disabled') and isinstance(case['token'], str), 'Case context/token')
        require(case['category'] in ('finite_control', 'finite_context_control', 'saturation_control', 'behavior_only'),
                'Case category')
    require(len(requested) == len(set(requested)), 'Duplicate requested case')
    require(set(requested) <= set(by_id), 'Unknown case: ' + ', '.join(sorted(set(requested) - set(by_id))))
    included = set(requested)
    while True:
        previous = included.copy()
        for name in list(included):
            reference = by_id[name]['reference']
            if reference:
                require(reference in by_id, 'Missing reference: ' + reference)
                included.add(reference)
        for a, b in CONTROL_PAIRS:
            if a in included or b in included:
                included.update((a, b))
        if previous == included:
            return [c for c in rows if c['id'] in included]


def source_inputs(source, pin):
    require(len(pin['upstream_source_files']) == 33 and len(pin['generated_files']) == 18,
            'Frozen source and generated-file counts')
    for collection in ('upstream_source_files', 'generated_files'):
        rows = pin[collection]
        require(len(rows) == len({r['path'] for r in rows}), 'Duplicate pinned path')
        for row in rows:
            relative = Path(row['path'])
            require(not relative.is_absolute() and '..' not in relative.parts, 'Safe relative pinned path')
            require(len(row['sha256']) == 64 and all(c in '0123456789abcdef' for c in row['sha256']),
                    'Pinned SHA256 shape')
    for row in pin['upstream_source_files']:
        path = (source / row['path']).resolve()
        require(source in path.parents and path.is_file(), 'Missing or external source: ' + row['path'])
        require(sha(path) == row['sha256'], 'Pinned source differs: ' + row['path'])


def readiness(args):
    require(os.name == 'posix', 'This version targets POSIX Clang/GCC; Windows/MSVC is not supported')
    compiler = shutil.which(args.cc or 'cc')
    require(compiler is not None, 'Missing existing C compiler; install nothing. Select one with --cc')

    def capture(argv, dependency):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
        require(result.returncode == 0, 'Missing/unusable ' + dependency + ': ' + result.stderr.strip())
        return result.stdout.strip()

    version = capture([compiler, '--version'], 'Clang/GCC compiler')
    require(any(s in version.lower() for s in ('clang', 'gcc', 'free software foundation')),
            'This version requires a Clang/GCC compiler')
    yaml_version = capture([sys.executable, '-I', '-B', '-c', 'import yaml; print(yaml.__version__)'],
                           'PyYAML in the selected isolated Python interpreter; install nothing')
    require(int(yaml_version.split('.')[0]) >= 6, 'PyYAML >= 6 is required; install nothing')
    sdk = None
    require(not args.sdk or platform.system() == 'Darwin', '--sdk is only used on macOS')
    if platform.system() == 'Darwin':
        sdk = str(args.sdk.resolve()) if args.sdk else capture(['xcrun', '--show-sdk-path'], 'installed macOS SDK')
        require(Path(sdk).is_dir(), 'Missing existing macOS SDK; install nothing')
    return {'compiler': compiler, 'compiler_version': version, 'sdk': sdk,
            'python': sys.executable, 'python_version': sys.version, 'PyYAML_version': yaml_version,
            'platform': platform.platform(), 'machine': platform.machine()}


def control_checks(observed, comparisons):
    checked = []
    by_id = {x['case']: x for x in comparisons}

    def check(ok, name):
        require(ok, 'Finite control failed: ' + name)
        checked.append(name)

    if 'reference_b' in by_id:
        c = by_id['reference_b']
        check(c['exact_full_output_bytes'] and c['exact_return_bytes'] and
              all(not fields for fields in c['changed_catalog_fields'].values()), 'fresh-reference-repeatability')
    for name in ('enable_two', 'enable_negative_one'):
        if name in by_id:
            c = by_id[name]
            check(c['exact_full_output_bytes'] and
                  all(not fields for fields in c['changed_catalog_fields'].values()), name + '-nonzero-encoding')
    if 'enable_zero' in observed and 'enable_negative_zero' in observed:
        check(observed['enable_zero']['outputs'] == observed['enable_negative_zero']['outputs'],
              'signed-zero-disable-equivalence')
    for name in ('enable_zero', 'enable_negative_zero', 'dc_zero_disabled'):
        if name in by_id:
            check(scalar(by_id[name]['injection_pwm']) == 0.0, name + '-disabled-final-gate')
    for prefix, field in (('p', 'currentSource_STATE.P_cmd'), ('q', 'currentSource_STATE.Q_cmd')):
        at, over = prefix + '_at_positive_clamp', prefix + '_over_positive_clamp'
        for name in (at, over):
            if name in by_id:
                cfg = observed[name]['snapshots']['after_initialization']['fields']
                bound = scalar(cfg['currentSource_GENERAL_CFG.SRated']) * scalar(
                    cfg['currentSource_GENERAL_CFG.' + ('Plimit' if prefix == 'p' else 'Qlimit')])
                check(scalar(by_id[name]['injection_selected'][field]) == bound, name + '-configured-bound')
        if at in observed and over in observed:
            check(observed[at]['outputs'] == observed[over]['outputs'], prefix + '-at-over-clamp-equivalence')
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=HERE.parents[2], help='Checkout containing the pinned sources')
    parser.add_argument('--output-dir', type=Path, help='New directory for copied inputs, binaries, logs and traces')
    parser.add_argument('--cc', help='Existing Clang/GCC executable (default: cc on PATH)')
    parser.add_argument('--sdk', type=Path, help='Existing macOS SDK (default: xcrun --show-sdk-path)')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--suite', choices=('smoke', 'all'), default='smoke')
    selection.add_argument('--cases', nargs='+', metavar='ID', help='Frozen case IDs; matching controls included automatically')
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--list-cases', action='store_true')
    action.add_argument('--check-inputs', action='store_true', help='Check source pins without executing generators or C')
    action.add_argument('--preflight', action='store_true', help='Check existing dependencies without creating outputs')
    args = parser.parse_args()
    record = None
    out = None
    try:
        pin, frozen = load(HERE / 'SOURCE-PINS.json'), load(HERE / 'CASES.json')
        require(pin['source_commit'] == frozen['source_commit'], 'Source and case revision identity')
        require(frozen['sample_time_seconds'] == 0.0002 and frozen['warm_prefix_calls'] == 12004 and
                frozen['recovery_calls'] == 250 and frozen['recovery_duration_seconds'] == 0.05,
                'Frozen experiment cadence/history/window')
        if args.list_cases:
            for case in frozen['cases']:
                print(json.dumps(case, sort_keys=True))
            return 0
        source = args.source_dir.resolve()
        source_inputs(source, pin)
        requested = args.cases or (list(SMOKE) if args.suite == 'smoke' else [c['id'] for c in frozen['cases']])
        cases = selected_cases(frozen, requested)
        if args.check_inputs:
            print(json.dumps({'source_commit': pin['source_commit'], 'source_pins_verified': 33,
                              'frozen_cases': 36, 'selected_cases': [c['id'] for c in cases]}))
            return 0
        require(args.preflight or args.output_dir is not None, 'Execution requires a NEW --output-dir')
        if args.output_dir:
            out = args.output_dir.resolve()
            require(not out.exists(), 'Output directory already exists; preserve it and choose a new path')
            require(out != source and out not in source.parents, 'Output must not replace the source or its ancestors')
            if source in out.parents:
                require(out.relative_to(source).parts[0] == '.context',
                        'Use an output outside the source checkout, or under its .context artifact directory')
        ready = readiness(args)
        if args.preflight:
            print(json.dumps(ready, sort_keys=True))
            return 0
        package_hashes = {name: sha(HERE / name) for name in PACKAGE_FILES}
        out.mkdir(parents=True, exist_ok=False)
        record = {'status': 'preparing', 'source_commit': pin['source_commit'], 'source_inputs': pin,
                  'package_hashes': package_hashes, 'frozen_design': frozen, 'readiness': ready,
                  'selection': {'requested': requested, 'included': [c['id'] for c in cases],
                                'added_controls': [c['id'] for c in cases if c['id'] not in requested]},
                  'commands': [], 'cases': [], 'flags': FLAGS, 'fresh_process_per_trajectory': True,
                  'recovery_is_same_process': True, 'production_source_modified': False,
                  'header_generation': 'Inspected pinned generators in copied tree only',
                  'electrical_network_or_MATLAB': False,
                  'numeric_policy_or_safety_acceptance': False}

        def save():
            (out / 'manifest.json').write_text(json.dumps(record, indent=2, allow_nan=False) + '\n')

        def run(argv, label, case=None):
            argv = [str(x) for x in argv]
            started = datetime.datetime.now(datetime.timezone.utc).isoformat()
            process = subprocess.Popen(argv, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       start_new_session=True)

            def stop_owned_group():
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            timed_out = False
            interrupted = False
            try:
                stdout, stderr = process.communicate(timeout=60)
            except subprocess.TimeoutExpired:
                timed_out = True
                stop_owned_group()
                stdout, stderr = process.communicate()
            except KeyboardInterrupt:
                interrupted = True
                stop_owned_group()
                stdout, stderr = process.communicate()
            streams = {}
            for suffix, data in (('stdout', stdout), ('stderr', stderr)):
                path = out / (label + '.' + suffix)
                path.write_bytes(data)
                streams[suffix] = {'path': path.name, 'sha256': sha(path)}
            command = {'argv': argv, 'cwd': str(out), 'label': label, 'pid': process.pid,
                       'owned_process_group': process.pid,
                       'started_at_utc': started, 'ended_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                       'exit_code': process.returncode, 'timed_out': timed_out,
                       'interrupted': interrupted, 'streams': streams}
            if case:
                command.update(case_id=case['id'], fresh_process=True, executable_sha256=record['executable_sha256'])
            record['commands'].append(command)
            save()
            if interrupted:
                raise KeyboardInterrupt
            require(not timed_out and process.returncode == 0, 'Command failed; inspect retained logs: ' + label)
            return stdout

        save()
        tree, kit = out / 'source', out / 'kit'
        kit.mkdir()
        for name in PACKAGE_FILES:
            (kit / name).write_bytes((HERE / name).read_bytes())
        for row in pin['upstream_source_files']:
            target = tree / row['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((source / row['path']).read_bytes())
            require(sha(target) == row['sha256'], 'Copied source differs: ' + row['path'])
        py = [sys.executable, '-I', '-B']
        for module in MODULES:
            schema = tree / (module + '_schema.yaml')
            run([*py, tree / 'schema_tools/generate_header.py', schema, tree / module],
                'generate-' + Path(module).name)
        dll, logging = tree / 'ieee_cigre_dll', tree / 'ieee_cigre_dll/logging'
        run([*py, dll / 'generate_dll_config.py', dll / 'dll_config.yaml', dll / 'dll_config_generated.h'],
            'generate-dll-config')
        run([*py, logging / 'generate_signal_catalog.py', dll / 'gfm_battery_app.c',
             tree / 'c_language_library', logging / 'signal_catalog_autogen.h', logging], 'generate-catalog')
        run([*py, logging / 'generate_extra_outputs.py', dll / 'debug_signals_config.yaml',
             dll / 'gfm_battery_app.c', tree / 'c_language_library', logging / 'extra_outputs_autogen.h'],
            'generate-extra-outputs')
        for row in pin['generated_files']:
            require(sha(tree / row['path']) == row['sha256'], 'Generated bytes differ: ' + row['path'])
        record['generated_files_verified'] = 18
        sdk_flags = ['-isysroot', ready['sdk']] if ready['sdk'] else []
        preflight = out / 'numeric_preflight'
        run([ready['compiler'], *sdk_flags, *FLAGS, kit / 'numeric_preflight.c', '-lm', '-o', preflight],
            'compile-numeric-preflight')
        record['numeric_preflight'] = loads(run([preflight], 'numeric-preflight').decode())
        require(record['numeric_preflight']['target_supported'], 'Unsupported target numeric semantics')
        binary = out / 'input_characterization'
        run([ready['compiler'], *sdk_flags, *FLAGS, '-I', tree, kit / 'harness.c',
             *[tree / unit for unit in UNITS], '-lm', '-o', binary], 'compile-controller-harness')
        record['executable_sha256'] = sha(binary)
        save()
        observed = {}
        for case in cases:
            stdout = run([binary, case['id'], case['history'], case['context'], case['scalar'],
                          case['token'], out / case['id']], case['id'], case)
            observed[case['id']] = inspect_case(out, case, stdout)
            record['cases'].append({'id': case['id'], 'category': case['category'],
                                    'controller_calls': case['history'] + 251,
                                    'artifacts': {s: {'path': case['id'] + '.' + s,
                                                      'sha256': sha(out / (case['id'] + '.' + s))}
                                                  for s in ('inputs.bin', 'outputs.bin', 'returns.bin')}})
            save()
        comparisons = [compare(observed[c['id']], observed[c['reference']]) for c in cases if c['reference']]
        record['finite_control_checks'] = control_checks(observed, comparisons)
        comparison_path = out / 'comparisons.json'
        comparison_path.write_text(json.dumps(comparisons, indent=2, allow_nan=False) + '\n')
        record['comparisons_sha256'] = sha(comparison_path)
        source_inputs(source, pin)
        for row in pin['upstream_source_files'] + pin['generated_files']:
            require(sha(tree / row['path']) == row['sha256'], 'Copied execution input changed: ' + row['path'])
        for name, expected in package_hashes.items():
            require(sha(HERE / name) == sha(kit / name) == expected, 'Experiment package changed: ' + name)
        require(sha(binary) == record['executable_sha256'], 'Executable changed during execution')
        record['summary'] = {'trajectory_processes': len(cases),
                             'controller_calls': sum(c['history'] + 251 for c in cases),
                             'finite_control_cases_passed': sum(c['category'] != 'behavior_only' for c in cases),
                             'behavior_only_cases_observed': sum(c['category'] == 'behavior_only' for c in cases),
                             'matched_comparisons': len(comparisons), 'recovery_window_seconds': 0.05,
                             'full_36_case_campaign': len(cases) == 36,
                             'numeric_policy_or_safety_verdict': False}
        record['status'] = 'completed_characterization'
        save()
        print(out)
        print(json.dumps(record['summary'], sort_keys=True))
        return 0
    except (Exception, KeyboardInterrupt) as error:
        if record is not None:
            record.update(status='failed', error=str(error) or type(error).__name__)
            (out / 'manifest.json').write_text(json.dumps(record, indent=2, allow_nan=False) + '\n')
        print(str(error) or type(error).__name__, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""Check the real full-runtime model/settings flow and preserve per-stage frames.

Requires a graphical desktop and exclusive use of BongoCat. Inspect saved frames
and native settings-window captures as well; pixel counts do not prove texture
correctness. Output must be a fresh directory. The app's existing CLI is the seam.
"""
import argparse
import csv
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

STAGES = ['startup', 'idle', 'scale-up', 'scale-down', 'opacity-50',
          'opacity-100', 'model-keyboard', 'model-standard', 'model-gamepad',
          'settings-first-model-select', 'settings-idle',
          'settings-closed-model-restored', 'settings-reopen-model-select',
          'recovery', 'gamepad-repeat', 'final-recovery']
EXPECTED = {0: 'standard', 1: 'standard', 2: 'standard', 3: 'standard',
            4: 'standard', 5: 'standard', 6: 'keyboard', 7: 'standard',
            8: 'gamepad', 11: 'standard', 13: 'standard', 14: 'gamepad',
            15: 'standard'}


def read_rows(path):
    try:
        # Drop an incomplete final line while the app appends its next frame.
        data = path.read_text().rsplit('\n', 1)[0] + '\n'
        return list(csv.DictReader(io.StringIO(data)))
    except (OSError, csv.Error):
        return []


def healthy(row):
    return (row.get('model_state_consistent') == '1'
            and row.get('loaded_model') == row.get('active_model')
            and row.get('context_current') == '1'
            and row.get('gl_error_before') == row.get('gl_error_after') == '0'
            and row.get('window_config_visible') == row.get('window_os_visible') == '1'
            and int(row.get('visible_pixels') or 0) > 100
            and int(row.get('alpha_pixels') or 0) > 100)


def snapshot(state, destination, row):
    try:
        data = (state / 'frame.bmp').read_bytes()
        if len(data) < 54 or data[:2] != b'BM' or struct.unpack_from('<I', data, 2)[0] != len(data):
            return False
        destination.mkdir(exist_ok=True)
        (destination / 'frame.bmp').write_bytes(data)
        (destination / 'frame.json').write_text(json.dumps(row, indent=2) + '\n')
        for name in ('ui-frame.txt', 'preferences-window.txt'):
            if (state / name).is_file():
                (destination / name).write_bytes((state / name).read_bytes())
        return True
    except OSError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--cwd', type=Path)
    parser.add_argument('--deadline', type=float, default=90)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    state = output / 'storage/state'
    command = [str(binary), '--ci-smoke', '--ci-model=standard', '--ci-runtime-flow',
               '--ci-frame-series', '--ci-preference-page=1', '--ci-ignore-global-input',
               '--ci-exit-ms=45000', f'--storage-root={output / "storage"}']
    env = dict(os.environ, BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN='1')
    report = {'command': command, 'started_at': datetime.datetime.now().astimezone().isoformat(),
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'working_directory': str(args.cwd.resolve()) if args.cwd else os.getcwd(),
              'environment_overrides': {'BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN': '1'},
              'deadline_seconds': args.deadline, 'timeout': False}
    events, captured = [], {}
    start = time.monotonic()
    with (output / 'stdout.log').open('wb') as out, (output / 'stderr.log').open('wb') as err:
        process = subprocess.Popen(command, cwd=args.cwd, env=env, stdout=out, stderr=err)
        report['pid'] = process.pid
        (output / 'launch.json').write_text(json.dumps(report, indent=2) + '\n')
        previous = None
        while process.poll() is None:
            elapsed = time.monotonic() - start
            if elapsed > args.deadline:
                report['timeout'] = True
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                break
            try:
                stage = (state / 'runtime-flow-stage.txt').read_text()
            except OSError:
                stage = ''
            if stage in STAGES and stage != previous:
                events.append({'stage': stage, 'elapsed_seconds': elapsed})
                (output / 'events.json').write_text(json.dumps(events, indent=2) + '\n')
                previous = stage
            rows = read_rows(state / 'frame-series.csv')
            if rows and stage in STAGES:
                row, index = rows[-1], STAGES.index(stage)
                if (row.get('runtime_stage') == str(index) and healthy(row)
                        and elapsed - captured.get(stage, 0) > .3):
                    if snapshot(state, output / f'{index:02}-{stage}', row):
                        captured[stage] = elapsed
            time.sleep(.04)
        report['returncode'] = process.wait()
    report['duration_seconds'] = time.monotonic() - start
    rows = read_rows(state / 'frame-series.csv')
    stdout = (output / 'stdout.log').read_text(errors='replace')
    stderr = (output / 'stderr.log').read_text(errors='replace')
    settings_context = re.search(r'Preferences OpenGL context: shared=1 dedicated=1 .*?main_context=(0x[0-9a-f]+) settings_context=(0x[0-9a-f]+)', stderr)
    checks = {
        'normal_exit': report['returncode'] == 0 and not report['timeout'],
        'full_runtime': 'Live2D Cubism SDK Core Version' in stdout and 'diagnostic backend' not in stdout + stderr,
        'startup_ready': 'Startup ready' in stderr and 'Model load completed: id=standard' in stderr,
        'all_stages_in_order': [e['stage'] for e in events] == STAGES,
        'normal_shutdown': 'Shutdown started: stage=shutdown:normal exit_code=0' in stderr and 'Shutdown complete: exit_code=0' in stderr,
        'no_render_errors': '[CSM][E]' not in stdout + stderr and '[ERROR:' not in stderr and 'stage=failed' not in stderr
            and not re.search(r'(?:gl_|state_|restore_)?error(?:_before|_after)?=0x0*[1-9a-fA-F][0-9a-fA-F]*', stdout + stderr),
        'frame_context_and_gl': bool(rows) and all(row.get('context_current') == '1' and row.get('gl_error_before') == row.get('gl_error_after') == '0' for row in rows),
        'dedicated_shared_settings_context': bool(settings_context) and settings_context[1] != settings_context[2]
            and int(settings_context[1], 16) != 0 and int(settings_context[2], 16) != 0,
        'two_settings_selections': stderr.count('Preferences smoke selecting model ') == 2,
    }
    summary = {}
    for index, stage in enumerate(STAGES):
        valid = [row for row in rows if row.get('runtime_stage') == str(index) and healthy(row)]
        expected = EXPECTED.get(index)
        if expected:
            valid = [row for row in valid if row.get('loaded_model') == expected and row.get('model_mode') == expected]
        elif index in (9, 10):
            valid = [row for row in valid if row.get('loaded_model') != 'gamepad']
        elif index == 12:
            valid = [row for row in valid if row.get('loaded_model') != 'standard']
        checks[f'{stage}_visible_frames'] = len(valid) >= 2
        checks[f'{stage}_frame_saved'] = (output / f'{index:02}-{stage}/frame.bmp').is_file()
        summary[stage] = {'fresh_valid_frames': len(valid), 'settled_frame': valid[-1] if valid else None}
        if index in (9, 12):
            ui = output / f'{index:02}-{stage}/ui-frame.txt'
            text = ui.read_text() if ui.exists() else ''
            fields = dict(re.findall(r'(\w+)=([^\s]+)', text))
            checks[f'{stage}_ui'] = all(fields.get(k) == v for k, v in
                (('valid', '1'), ('valid_assets', '1'), ('gl_error', '0'), ('page', '1')))
            checks[f'{stage}_ui'] &= int(fields.get('vertices', 0)) > 0 and int(fields.get('draw_elements', 0)) > 0
    for index, value in ((2, 125), (3, 75)):
        selected = [r for r in rows if r.get('runtime_stage') == str(index) and healthy(r)]
        checks[f'scale_{value}'] = len(selected) >= 2 and all(float(r['scale_percent']) == value for r in selected)
    for index, value in ((4, 50), (5, 100)):
        selected = [r for r in rows if r.get('runtime_stage') == str(index) and healthy(r)]
        checks[f'opacity_{value}'] = len(selected) >= 2 and all(float(r['opacity_percent']) == value and abs(float(r['window_opacity']) - value / 100) < .01 for r in selected)
    up, down = summary['scale-up']['settled_frame'], summary['scale-down']['settled_frame']
    checks['scale_changes_dimensions'] = bool(up and down) and int(up['width']) > int(down['width']) and int(up['height']) > int(down['height'])
    for before, after in ((5, 6), (6, 7), (7, 8), (8, 9), (11, 12), (12, 13), (13, 14), (14, 15)):
        old, new = summary[STAGES[before]]['settled_frame'], summary[STAGES[after]]['settled_frame']
        checks[f'{STAGES[after]}_selection_transaction'] = bool(old and new) and old['loaded_model'] != new['loaded_model'] and int(new['selection_serial']) > int(old['selection_serial'])
    report.update(checks=checks, stages=summary, frame_count=len(rows), passed=all(checks.values()))
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': report['passed'], 'returncode': report['returncode'],
                      'duration_seconds': round(report['duration_seconds'], 3),
                      'failed_checks': [k for k, v in checks.items() if not v]}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())

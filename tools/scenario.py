#!/usr/bin/env python3
"""Validate signal configurations and compile autonomous firmware scenarios."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

FIELDS = ['spn', 'start_ms', 'period_ms', 'duration_ms', 'pattern', 'value',
          'min', 'max', 'pattern_period_ms', 'step_count']
PATTERNS = {'constant': 0, 'ramp': 1, 'sine': 2, 'triangle': 3, 'step': 4, 'square': 5}
# Keep active-window sums within the signed deadline comparison horizon.
MAX_TIME = 2**31 - 1


def read_scenario(path, metadata):
    database = {s['spn']: s for s in metadata['signals']}
    signals, seen, pgn_periods = [], set(), {}
    with Path(path).open(newline='', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != FIELDS:
            raise ValueError(f'{path}: row 1: expected header {",".join(FIELDS)}')
        for line, source in enumerate(reader, 2):
            try:
                if None in source or any(v is None for v in source.values()):
                    raise ValueError('incorrect number of columns')
                row = {k: v.strip() for k, v in source.items()}

                def integer(key, low=0, high=MAX_TIME):
                    if not re.fullmatch(r'[0-9]+', row[key]):
                        raise ValueError(f'{key} must be an integer')
                    value = int(row[key])
                    if not low <= value <= high:
                        raise ValueError(f'{key} must be in [{low}, {high}]')
                    return value

                def number(key):
                    value = float(row[key])
                    if not math.isfinite(value):
                        raise ValueError(f'{key} must be finite')
                    return value

                spn = integer('spn', 1, 2**32 - 1)
                if spn not in database:
                    raise ValueError(f'unknown SPN {spn}')
                if spn in seen:
                    raise ValueError(f'duplicate SPN {spn}')
                s = dict(database[spn])
                s.update(spn=spn, start_ms=integer('start_ms'),
                         period_ms=integer('period_ms', 1), duration_ms=integer('duration_ms'))
                if s['start_ms'] + s['duration_ms'] > MAX_TIME:
                    raise ValueError('start_ms + duration_ms exceeds supported horizon')
                pattern = row['pattern']
                if pattern not in PATTERNS:
                    raise ValueError(f'unsupported pattern {pattern!r}')
                used = {'value'} if pattern == 'constant' else {'min', 'max', 'pattern_period_ms'}
                if pattern == 'step':
                    used.add('step_count')
                for key in {'value', 'min', 'max', 'pattern_period_ms', 'step_count'} - used:
                    if row[key]:
                        raise ValueError(f'{key} must be blank for {pattern}')
                if pattern == 'constant':
                    value = number('value')
                    lo = hi = value
                    waveform_ms, steps = 0, 0
                else:
                    lo, hi = number('min'), number('max')
                    value = lo
                    waveform_ms = integer('pattern_period_ms', 1)
                    steps = integer('step_count', 2, 50) if pattern == 'step' else 0
                if not s['min'] <= lo <= hi <= s['max']:
                    raise ValueError(f'values outside SPN range [{s["min"]}, {s["max"]}]')
                pgn = s['pgn']
                if pgn in pgn_periods and pgn_periods[pgn] != s['period_ms']:
                    raise ValueError(f'conflicting period_ms for PGN {pgn}')
                pgn_periods[pgn] = s['period_ms']
                s.update(pattern=pattern, value=value, min_value=lo, max_value=hi,
                         pattern_period_ms=waveform_ms, step_count=steps)
                # All currently supported PGNs are broadcasts (PDU2).
                pf = (pgn >> 8) & 255
                ps = (pgn & 255) if pf >= 240 else 255
                s['can_id'] = (s['priority'] << 26) | (pgn & 0x3ff00) << 8 | ps << 8 | s['source_address']
                signals.append(s)
                seen.add(spn)
                if len(signals) > metadata['max_signals'] or len(pgn_periods) > metadata['max_pgns']:
                    raise ValueError('firmware signal/PGN capacity exceeded')
            except (ValueError, OverflowError) as exc:
                raise ValueError(f'{path}: row {line}: {exc}') from exc
    if not signals:
        raise ValueError(f'{path}: no signals configured')
    return signals


def write_changed(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        temporary = path.with_suffix(path.suffix + '.tmp')
        temporary.write_text(text)
        temporary.replace(path)


def c_float(value):
    text = format(value, '.9g')
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'f'


def compile_header(signals, identity):
    lines = ['/* Generated from validated CSV; do not edit. */',
             '#ifndef GENERATED_SCENARIO_H', '#define GENERATED_SCENARIO_H',
             '#include "j1939_pattern_generator.h"',
             f'static const char k_scenario_id[] = "{identity}";',
             'static const Pattern_Config_t k_scenario_signals[] = {']
    for s in signals:
        param = s['value'] if s['pattern'] == 'constant' else s['step_count']
        lines.append('    { ' + ', '.join([
            f'.spn = {s["spn"]}u', f'.pattern_type = {PATTERNS[s["pattern"]]}',
            f'.min_value = {c_float(s["min_value"])}', f'.max_value = {c_float(s["max_value"])}',
            f'.initial_value = {c_float(s["value"])}', f'.param1 = {c_float(param)}',
            f'.waveform_period_ms = {s["pattern_period_ms"]}u',
            f'.timeframe_ms = {s["period_ms"]}u', f'.start_ms = {s["start_ms"]}u',
            f'.duration_ms = {s["duration_ms"]}u']) + ' },')
    lines.extend(['};', '#endif', ''])
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--metadata', required=True, type=Path, help='host metadata executable')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        metadata = json.loads(subprocess.check_output([str(args.metadata.resolve())], text=True))
        signals = read_scenario(args.input, metadata)
        content = json.dumps(signals, sort_keys=True, separators=(',', ':'))
        identity = hashlib.sha256(content.encode()).hexdigest()[:16]
        manifest = {'schema_version': 1, 'scenario_id': identity,
                    'input': str(args.input), 'signals': signals}
        write_changed(args.output / 'scenario.h', compile_header(signals, identity))
        write_changed(args.output / 'scenario.json', json.dumps(manifest, indent=2) + '\n')
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(2, f'{exc}\n')


if __name__ == '__main__':
    main()

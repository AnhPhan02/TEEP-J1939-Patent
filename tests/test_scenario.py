import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import scenario


class ScenarioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(['make', 'build/export_metadata'], cwd=ROOT, check=True)
        cls.metadata = json.loads(subprocess.check_output([str(ROOT / 'build/export_metadata')]))

    def load_rows(self, rows, metadata=None):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'test.csv'
            with path.open('w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(scenario.FIELDS)
                writer.writerows(rows)
            return scenario.read_scenario(path, metadata or self.metadata)

    def test_default_and_id(self):
        signals = scenario.read_scenario(ROOT / 'input/continuous_signals.csv', self.metadata)
        self.assertEqual([s['spn'] for s in signals], [190, 84, 91])
        self.assertEqual(signals[0]['can_id'], 0x0CF00400)
        self.assertEqual(signals[2]['can_id'], 0x0CF00300)
        self.assertEqual(signals[1]['start_ms'], 2000)

    def test_bad_rows(self):
        base = ['190', '0', '20', '0', 'constant', '1500', '', '', '', '']
        changes = [(0, '999999'), (1, '-1'), (2, '0'), (3, '1.5'),
                   (4, 'random'), (5, 'nan'), (5, '9000'), (8, '1000')]
        for index, value in changes:
            with self.subTest(index=index, value=value):
                row = base.copy()
                row[index] = value
                with self.assertRaisesRegex(ValueError, 'row 2:'):
                    self.load_rows([row])
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.load_rows([base, base])
        with self.assertRaisesRegex(ValueError, 'columns'):
            self.load_rows([base[:-1]])
        with self.assertRaisesRegex(ValueError, 'no signals'):
            self.load_rows([])

    def test_shared_pgn_and_capacity(self):
        rows = [[91, 0, 50, 0, 'constant', 40, '', '', '', ''],
                [92, 0, 100, 0, 'constant', 50, '', '', '', '']]
        with self.assertRaisesRegex(ValueError, 'conflicting'):
            self.load_rows(rows)
        rows[1][2] = 50
        self.assertEqual(len(self.load_rows(rows)), 2)
        with self.assertRaisesRegex(ValueError, 'capacity'):
            self.load_rows(rows, dict(self.metadata, max_signals=1))
        with self.assertRaisesRegex(ValueError, 'horizon'):
            self.load_rows([[190, scenario.MAX_TIME, 20, 1, 'constant', 1500, '', '', '', '']])

    def test_patterns_and_unchanged_output(self):
        for name in scenario.PATTERNS:
            if name == 'constant':
                continue
            row = [190, 3, 20, 1000, name, '', 800, 1800, 200, 8 if name == 'step' else '']
            signal = self.load_rows([row])[0]
            self.assertEqual(signal['pattern_period_ms'], 200)
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'header.h'
            scenario.write_changed(path, 'test')
            first = path.stat().st_mtime_ns
            scenario.write_changed(path, 'test')
            self.assertEqual(first, path.stat().st_mtime_ns)

    def test_expected_values_and_timing(self):
        signals = scenario.read_scenario(ROOT / 'input/continuous_signals.csv', self.metadata)
        rows = list(scenario.expected_rows(signals, 2201))
        first = rows[0]
        self.assertEqual(first[:4], [0, '0CF00400', 61444, 190])
        self.assertEqual(first[5], 12000)
        speed = [row for row in rows if row[3] == 84]
        self.assertEqual([row[0] for row in speed], [2000, 2100, 2200])
        self.assertEqual(float(speed[1][4]), 0.75)
        pedal = [row for row in rows if row[3] == 91]
        self.assertTrue(all(row[5] == 100 for row in pedal))
        finite = self.load_rows([[190, 7, 20, 40, 'constant', 1500, '', '', '', '']])
        self.assertEqual([row[0] for row in scenario.expected_rows(finite, 100)], [7, 27])

    def test_reference_waveforms(self):
        quarter = {'ramp': 25, 'sine': 100, 'triangle': 50,
                   'square': 100, 'step': 100 / 3}
        for pattern, value in quarter.items():
            signals = self.load_rows([[190, 7, 20, 0, pattern, '', 0, 100, 1000,
                                       4 if pattern == 'step' else '']])
            self.assertAlmostEqual(scenario.physical_value(signals[0], 257), value)

    def test_dbc_bit_layout(self):
        signals = scenario.read_scenario(ROOT / 'input/continuous_signals.csv', self.metadata)
        dbc = scenario.dbc_text(signals)
        self.assertIn('BO_ 2364539904 PGN_61444: 8 STM32', dbc)
        self.assertIn('SG_ SPN_190 : 24|16@1+ (0.125,0)', dbc)
        self.assertIn(f'BO_ {0x80000000 | 0x0CF00300} PGN_61443', dbc)
        self.assertIn('"VFrameFormat"', dbc)

    def test_cli_exports_and_scenario_switch(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d) / 'generated'
            command = [sys.executable, str(ROOT / 'tools/scenario.py'),
                       '--input', str(ROOT / 'input/continuous_signals.csv'),
                       '--metadata', str(ROOT / 'build/export_metadata'), '--output', str(output)]
            subprocess.run(command + ['--horizon-ms', '100'], check=True)
            first = (output / 'scenario.h').read_bytes()
            subprocess.run(command, check=True)
            self.assertEqual(first, (output / 'scenario.h').read_bytes())
            with (output / 'expected.csv').open() as f:
                expected = list(csv.DictReader(f))
            manifest = json.loads((output / 'scenario.json').read_text())
            self.assertTrue(all(row['scenario_id'] == manifest['scenario_id'] for row in expected))
            self.assertEqual(len(expected), 5)
            alternative = Path(d) / 'other.csv'
            alternative.write_text(','.join(scenario.FIELDS) + '\n190,0,20,0,constant,1600,,,,\n')
            changed = command.copy()
            changed[changed.index('--input') + 1] = str(alternative)
            subprocess.run(changed, check=True)
            self.assertNotEqual(first, (output / 'scenario.h').read_bytes())
            self.assertFalse((output / 'expected.csv').exists())
            self.assertFalse((output / 'expected.json').exists())
            result = subprocess.run(changed + ['--horizon-ms', '0'], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('horizon-ms', result.stderr)



if __name__ == '__main__':
    unittest.main()

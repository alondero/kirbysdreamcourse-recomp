import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from input_recording import host_script


class CompareRecordingTest(unittest.TestCase):
    def test_valid_scenario_and_tampered_host_script(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scenario = {'frames': 2, 'events': [{'frame': 1, 'count': 1, 'button': 'a'}]}
            recording = root / 'input.json'
            recording.write_text(json.dumps(scenario), encoding='utf-8')
            input_hash = hashlib.sha256(recording.read_bytes()).hexdigest()
            rom = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
            core = '72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0'
            wram_hash = hashlib.sha256(bytes(131072)).hexdigest()
            reference = root / 'ref.json'
            reference.write_text(json.dumps({
                'rom_sha256': rom, 'core_sha256': core, 'initial_sram': 'zero',
                'initial_wram': 'zero', 'input': scenario['events'],
                'input_recording_sha256': input_hash,
                'frames': [{'frame': i, 'wram_sha256': wram_hash} for i in (1, 2)],
            }), encoding='utf-8')
            for name in ('a', 'b'):
                host = root / name
                frames = host / 'frames'
                frames.mkdir(parents=True)
                for i in range(2):
                    (frames / f'frame_{i:06}_wram.bin').write_bytes(bytes(131072))
                script = host / 'input.script'
                script.write_text(host_script(scenario), encoding='ascii')
                executable = host / 'game.exe'
                executable.write_bytes(b'synthetic executable identity')
                (host / 'run.json').write_text(json.dumps({
                    'completed': True, 'frames': 2, 'rom_sha256': rom,
                    'initial_sram_sha256': hashlib.sha256(bytes(8192)).hexdigest(),
                    'input_recording_sha256': input_hash,
                    'input_script_sha256': hashlib.sha256(script.read_bytes()).hexdigest(),
                    'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
                }), encoding='utf-8')
            command = [sys.executable, str(ROOT / 'tools/compare_wram.py'),
                       '--reference', str(reference), '--host-a', str(root / 'a/frames'),
                       '--host-b', str(root / 'b/frames'), '--recording', str(recording)]
            good = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(good.returncode, 0, good.stderr)
            # Reset and state-loaded runs need their own alignment and provenance.
            ref_data = json.loads(reference.read_text(encoding='utf-8'))
            ref_data['reset_before_frame'] = 2
            reference.write_text(json.dumps(ref_data), encoding='utf-8')
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('Reset scenarios', rejected.stderr)
            del ref_data['reset_before_frame']
            reference.write_text(json.dumps(ref_data), encoding='utf-8')
            manifest_path = root / 'a/run.json'
            manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
            manifest['initial_state_sha256'] = 'a' * 64
            manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('initial save state', rejected.stderr)
            del manifest['initial_state_sha256']
            manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
            # Explicit same-number alignment selects reference frame 1 for
            # host frame 1, while the default selects reference frame 2.
            ref_data = json.loads(reference.read_text(encoding='utf-8'))
            ref_data['frames'][0]['wram_sha256'] = '0' * 64
            reference.write_text(json.dumps(ref_data), encoding='utf-8')
            same_number = subprocess.run(command + ['--start-frame', '1', '--end-frame', '1',
                                                     '--reference-offset', '0'],
                                         capture_output=True, text=True)
            self.assertEqual(same_number.returncode, 1)
            self.assertIn('host frame 1, reference frame 1', same_number.stdout)
            self.assertIn('not established as a global alignment', same_number.stdout)
            next_number = subprocess.run(command + ['--start-frame', '1', '--end-frame', '1'],
                                         capture_output=True, text=True)
            self.assertEqual(next_number.returncode, 0, next_number.stderr)
            invalid_zero = subprocess.run(command + ['--reference-offset', '0'],
                                          capture_output=True, text=True)
            self.assertNotEqual(invalid_zero.returncode, 0)
            self.assertIn('chosen offset', invalid_zero.stderr)
            invalid_high = subprocess.run(command + ['--reference-offset', '2',
                                                      '--start-frame', '1', '--end-frame', '1'],
                                          capture_output=True, text=True)
            self.assertNotEqual(invalid_high.returncode, 0)
            ref_data['frames'][0]['wram_sha256'] = wram_hash
            reference.write_text(json.dumps(ref_data), encoding='utf-8')
            # Sparse dumps may cover only an explicit interval; no missing
            # frames outside that interval are interpreted as a match.
            for name in ('a', 'b'):
                (root / name / 'frames/frame_000000_wram.bin').unlink()
            bounded = subprocess.run(command + ['--start-frame', '1', '--end-frame', '1'],
                                     capture_output=True, text=True)
            self.assertEqual(bounded.returncode, 0, bounded.stderr)
            self.assertIn('1 selected frames', bounded.stdout)
            ref_data = json.loads(reference.read_text(encoding='utf-8'))
            ref_data['frames'][1]['wram_sha256'] = '0' * 64
            reference.write_text(json.dumps(ref_data), encoding='utf-8')
            different = subprocess.run(command + ['--start-frame', '1', '--end-frame', '1'],
                                       capture_output=True, text=True)
            self.assertEqual(different.returncode, 1)
            self.assertIn('1 of 1 selected frames differ', different.stdout)
            ref_data['frames'][1]['wram_sha256'] = wram_hash
            reference.write_text(json.dumps(ref_data), encoding='utf-8')
            invalid = subprocess.run(command + ['--start-frame', '1', '--end-frame', '2'],
                                     capture_output=True, text=True)
            self.assertNotEqual(invalid.returncode, 0)
            self.assertIn('range must be within', invalid.stderr)
            for name in ('a', 'b'):
                (root / name / 'frames/frame_000000_wram.bin').write_bytes(bytes(131072))
            for name, bounce in (('a', '1'), ('b', '0')):
                manifest = root / name / 'run.json'
                data = json.loads(manifest.read_text(encoding='utf-8'))
                data['runtime_options'] = {'SNESRECOMP_LLE_BOUNCE': bounce}
                manifest.write_text(json.dumps(data), encoding='utf-8')
            mismatch = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(mismatch.returncode, 0)
            self.assertIn('different recorded runtime options', mismatch.stderr)
            self.assertEqual(subprocess.run(command + ['--allow-bounce-difference'], capture_output=True).returncode, 0)
            differing = root / 'b/run.json'
            different_data = json.loads(differing.read_text(encoding='utf-8'))
            different_data['runtime_options']['SNESRECOMP_APU_ABSOLUTE_PREANCHOR'] = '1'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            unrelated = subprocess.run(command + ['--allow-bounce-difference'], capture_output=True, text=True)
            self.assertNotEqual(unrelated.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', unrelated.stderr)
            different_data['runtime_options'].pop('SNESRECOMP_APU_ABSOLUTE_PREANCHOR')
            different_data['runtime_options']['KDC_EXPERIMENTAL_REFRESH_PARITY'] = '1'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            refresh_mismatch = subprocess.run(command + ['--allow-bounce-difference'],
                                              capture_output=True, text=True)
            self.assertNotEqual(refresh_mismatch.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', refresh_mismatch.stderr)
            different_data['runtime_options'].pop('KDC_EXPERIMENTAL_REFRESH_PARITY')
            different_data['runtime_options']['KDC_EXPERIMENTAL_AUTOJOY_LATCH'] = '1'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            latch_mismatch = subprocess.run(command + ['--allow-bounce-difference'],
                                            capture_output=True, text=True)
            self.assertNotEqual(latch_mismatch.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', latch_mismatch.stderr)
            different_data['runtime_options'].pop('KDC_EXPERIMENTAL_AUTOJOY_LATCH')
            different_data['runtime_options']['KDC_EXPERIMENTAL_AUTOJOY_PHASE'] = '1'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            phase_mismatch = subprocess.run(command + ['--allow-bounce-difference'],
                                            capture_output=True, text=True)
            self.assertNotEqual(phase_mismatch.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', phase_mismatch.stderr)
            different_data['runtime_options'].pop('KDC_EXPERIMENTAL_AUTOJOY_PHASE')
            different_data['runtime_options']['KDC_EXPERIMENTAL_APU_BYTE_PHASE'] = '1'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            bus_mismatch = subprocess.run(command + ['--allow-bounce-difference'],
                                          capture_output=True, text=True)
            self.assertNotEqual(bus_mismatch.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', bus_mismatch.stderr)
            different_data['runtime_options'].pop('KDC_EXPERIMENTAL_APU_BYTE_PHASE')
            different_data['runtime_options']['KDC_EXPERIMENTAL_APU_DRAIN'] = '1'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            drain_mismatch = subprocess.run(command + ['--allow-bounce-difference'],
                                            capture_output=True, text=True)
            self.assertNotEqual(drain_mismatch.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', drain_mismatch.stderr)
            different_data['runtime_options'].pop('KDC_EXPERIMENTAL_APU_DRAIN')
            different_data['runtime_options']['KDC_EARLY_DELAY'] = '120'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            delay_mismatch = subprocess.run(command + ['--allow-bounce-difference'],
                                            capture_output=True, text=True)
            self.assertNotEqual(delay_mismatch.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', delay_mismatch.stderr)
            different_data['runtime_options'].pop('KDC_EARLY_DELAY')
            different_data['runtime_options']['KDC_EXPERIMENTAL_SPC_D4_PHASE'] = '1'
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            indexed_write_mismatch = subprocess.run(command + ['--allow-bounce-difference'],
                                                    capture_output=True, text=True)
            self.assertNotEqual(indexed_write_mismatch.returncode, 0)
            self.assertIn('more than the explicitly selected bounce mode', indexed_write_mismatch.stderr)
            different_data['runtime_options'].pop('KDC_EXPERIMENTAL_SPC_D4_PHASE')
            differing.write_text(json.dumps(different_data), encoding='utf-8')
            manifest = root / 'b/run.json'
            data = json.loads(manifest.read_text(encoding='utf-8'))
            data['runtime_options']['SNESRECOMP_LLE_BOUNCE'] = '1'
            manifest.write_text(json.dumps(data), encoding='utf-8')
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            for name in ('a', 'b'):
                host = root / name
                config = host / 'config.initial.ini'
                config.write_bytes(b'[Controller]\nSourceP1 = 0\n')
                manifest = host / 'run.json'
                data = json.loads(manifest.read_text(encoding='utf-8'))
                data['initial_config_sha256'] = hashlib.sha256(config.read_bytes()).hexdigest()
                manifest.write_text(json.dumps(data), encoding='utf-8')
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            (root / 'b/config.initial.ini').write_bytes(b'tampered')
            bad_config = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(bad_config.returncode, 0)
            self.assertIn('initial config does not match manifest', bad_config.stderr)
            config = root / 'b/config.initial.ini'
            manifest = root / 'b/run.json'
            data = json.loads(manifest.read_text(encoding='utf-8'))
            data['initial_config_sha256'] = hashlib.sha256(config.read_bytes()).hexdigest()
            manifest.write_text(json.dumps(data), encoding='utf-8')
            bad_config = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(bad_config.returncode, 0)
            self.assertIn('different initial configs', bad_config.stderr)
            config.write_bytes(b'[Controller]\nSourceP1 = 0\n')
            data['initial_config_sha256'] = hashlib.sha256(config.read_bytes()).hexdigest()
            manifest.write_text(json.dumps(data), encoding='utf-8')
            (root / 'b/input.script').write_text('wait 2\n', encoding='ascii')
            bad = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('Host input script does not match', bad.stderr)


if __name__ == '__main__':
    unittest.main()

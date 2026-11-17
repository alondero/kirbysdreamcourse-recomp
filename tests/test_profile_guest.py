"""Check that profiling reports reject incomplete evidence and avoid double counts."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import json
import subprocess

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location('profile_guest', TOOLS / 'profile_guest.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class GuestProfileTest(unittest.TestCase):
    def test_checkpoint_preserves_fixed_shot_inputs_and_uses_idle_tail(self):
        scenario = profile.SCENARIOS['shot']
        script = profile.checkpoint_script(scenario)
        self.assertTrue(script.startswith(scenario['script']))
        self.assertTrue(script.endswith('wait 400\nsavestate 0\n'))
        self.assertEqual(profile.checkpoint_script(profile.SCENARIOS['still']), 'wait 595\nsavestate 0\n')

    def test_full_state_checks_raw_bytes_frame_offset_and_missing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory) / 'a', Path(directory) / 'b'
            for case in (a, b):
                (case / 'frames').mkdir(parents=True)
                (case / 'checkpoint.sav').write_bytes(b'full state')
                (case / 'frames/frame_000349_wram.bin').write_bytes(b'WRAM')
            profile.compare_full_state(a, b, 350, 350)
            (b / 'frames/frame_000349_wram.bin').write_bytes(b'WRAm')
            with self.assertRaisesRegex(AssertionError, 'frame_000349_wram.bin, byte 3'):
                profile.compare_full_state(a, b, 350, 350)
            with self.assertRaises(FileNotFoundError):
                profile.compare_full_state(a, a, 350, 351)
            (b / 'frames/frame_000349_wram.bin').write_bytes(b'WRAM')
            (b / 'checkpoint.sav').write_bytes(b'other execution policy')
            with self.assertRaisesRegex(AssertionError, 'checkpoint.sav, byte 0'):
                profile.compare_full_state(a, b, 350, 350)
            profile.compare_full_state(a, b, 350, 350, checkpoint_reference=b)

    def test_build_identity_never_labels_harness_revision_as_binary_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            host = Path(directory) / 'host.exe'
            host.write_bytes(b'synthetic executable')
            identity = profile.build_identity(host)
            self.assertIn('unknown', identity['provenance'])
            sidecar = host.with_suffix('.build.json')
            for invalid in ({}, None, [], dict(sha256='wrong')):
                sidecar.write_text(json.dumps(invalid))
                with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, 'does not match executable'):
                    profile.build_identity(host)
            sidecar.write_text(json.dumps(dict(sha256=identity['sha256'], source_revision='pinned')))
            self.assertEqual(profile.build_identity(host)['build_info']['source_revision'], 'pinned')

    @unittest.skipUnless(sys.platform == 'win32', 'Windows handle accounting')
    def test_process_cpu_is_read_after_process_exit(self):
        with subprocess.Popen([sys.executable, '-c', 'sum(range(1000000))']) as process:
            process.wait(timeout=10)
            self.assertGreaterEqual(profile.process_cpu_seconds(process), 0)

    def test_source_attribution_keeps_inline_chain_and_biases_only_callers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.csv'
            path.write_text('frame,stack_rvas\n350,101;301\n'
                            '350,external:getenv;201;301\n350,external:wait;401\n')
            with patch.object(profile.subprocess, 'check_output', side_effect=[
                    'ImageBase 0000000140000000\n',
                    '0x0000000140000101\ninline_read\nbus.c:10\nbridge_bus_read\nbridge.c:20\n'
                    '0x0000000140000200\n_interp_run_core\nbridge.c:30\n']) as command:
                result = profile.source_samples(path, Path('host.exe'), 'addr2line', 'objdump',
                                                [(0x100, 'bridge_bus_read'), (0x200, '_interp_run_core'),
                                                 (0x300, 'RtlRunFrame'), (0x400, 'host_main')])
                self.assertEqual(command.call_args.kwargs['input'], '140000101\n140000200\n')
            self.assertEqual(sum(count for _, count in result), 2)
            self.assertIn('inline_read at bus.c:10 > bridge_bus_read at bridge.c:20', dict(result))
            self.assertIn('external:getenv called from _interp_run_core at bridge.c:30', dict(result))

    def test_timing_rejects_wrong_window_and_missing_frames(self):
        log = ('video profile window: first=350 last=351 seconds=0.01 presentations=2\n'
               'video profile: stage=guest count=2 total_ms=12.0 mean_ms=6.0 max_ms=7.0 max_frame=351\n')
        self.assertEqual(profile.parse_timing(log, 350, 351)['guest']['mean_ms'], 6)
        with self.assertRaises(AssertionError):
            profile.parse_timing(log, 350, 352)
        with self.assertRaises(AssertionError):
            profile.parse_timing(log.replace('count=2', 'count=1'), 350, 351)
        with self.assertRaises(AssertionError):
            profile.parse_timing(log.replace('stage=guest', 'stage=compose'), 350, 351)

    def test_samples_include_apu_tail_and_partition_nested_interpreter_work(self):
        functions = [(0x100, 'dsp_cycle'), (0x200, 'apu_cycle'),
                     (0x300, 'interp_bridge_run_ex2'), (0x400, 'cpu_runOpcode'),
                     (0x500, 'bank_80_8B09_M0X0'), (0x600, 'snes_beam_frame_driver_run_frame'),
                     (0x700, 'RtlRunFrame'), (0x800, 'host_main'),
                     (0x900, 'snes_advance_beam.constprop.0')]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.csv'
            path.write_text('frame,stack_rvas\n'
                            '350,101;201;301;601;701;801\n'  # APU nested in interpreter
                            '350,201;701;801\n'  # Frame-boundary APU after driver returns
                            '350,401;301;601;701;801\n'
                            '350,501;301;601;701;801\n'
                            '350,301;301;601;701;801\n'  # Recursion counted once inclusive
                            '350,801\n'  # Outside stage=guest
                            '351,901;301;601;701;801\n')  # Beam called by interpreter
            result = profile.summarize_samples(path, functions)
        self.assertEqual(result['total_samples'], 7)
        self.assertEqual(result['guest_samples'], 6)
        self.assertEqual(result['first_sampled_frame'], 350)
        self.assertEqual(result['last_sampled_frame'], 351)
        self.assertEqual(result['exclusive']['APU/SPC/DSP']['samples'], 2)
        self.assertEqual(result['apu_outside_beam_driver_samples'], 1)
        self.assertEqual(result['outside_beam_driver_samples'], 1)
        self.assertEqual(result['exclusive']['65816/bridge']['samples'], 2)
        self.assertEqual(result['exclusive']['generated']['samples'], 1)
        self.assertAlmostEqual(sum(row['percent'] for row in result['exclusive'].values()), 100)
        self.assertEqual(result['exclusive']['beam/bus/other']['samples'], 1)
        self.assertEqual(dict(result['inclusive'])['interp_bridge_run_ex2'], 5)

    def test_no_guest_stack_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.csv'
            path.write_text('frame,stack_rvas\n350,external\n')
            with self.assertRaisesRegex(AssertionError, 'no unwound guest stacks'):
                profile.summarize_samples(path, [(1, 'host_main')])

    def test_first_divergence_includes_input_path(self):
        with tempfile.TemporaryDirectory() as directory:
            reference, candidate = Path(directory) / 'reference', Path(directory) / 'candidate'
            reference.mkdir()
            candidate.mkdir()
            (reference / 'state.csv').write_text('1,abcd\n2,ef01\n')
            (candidate / 'state.csv').write_text('1,abcd\n2,ffff\n')
            with self.assertRaisesRegex(AssertionError, 'row 2:.*shot.script'):
                profile.compare_state(reference, candidate)


if __name__ == '__main__':
    unittest.main()

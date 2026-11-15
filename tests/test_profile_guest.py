"""Check that profiling reports reject incomplete evidence and avoid double counts."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location('profile_guest', TOOLS / 'profile_guest.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class GuestProfileTest(unittest.TestCase):
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

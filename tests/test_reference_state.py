import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/reference_capture.py'


class ReferenceStateTest(unittest.TestCase):
    def test_roundtrip_frame_must_leave_a_replay_interval(self):
        for frame in ('0', '10', '11'):
            result = subprocess.run([sys.executable, str(SCRIPT), '--rom', 'unused',
                                     '--core', 'unused', '--frames', '10',
                                     '--roundtrip-frame', frame, '--output', 'unused'],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('--roundtrip-frame must be', result.stderr)

    @unittest.skipUnless(os.environ.get('KDC_TEST_REFERENCE'), 'requires local verified ROM and pinned core')
    def test_reference_reset_preserves_boot_modified_sram(self):
        rom = ROOT / "Kirby's Dream Course (USA).sfc"
        core = ROOT / 'local/reference/snes9x_libretro.dll'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            initial = root / 'initial.srm'
            initial.write_bytes(bytes((i * 73 + 19) & 255 for i in range(8192)))
            output = root / 'reference.json'
            subprocess.run([sys.executable, str(SCRIPT), '--rom', str(rom), '--core', str(core),
                            '--frames', '121', '--reset-frame', '120', '--initial-sram', str(initial),
                            '--sram-frame', '119', '--sram-frame', '120', '--output', str(output)],
                           check=True)
            before = (root / 'reference.frame000119.sram.bin').read_bytes()
            after = (root / 'reference.frame000120.sram.bin').read_bytes()
            self.assertNotEqual(before, initial.read_bytes())
            self.assertEqual(before, after)

    @unittest.skipUnless(os.environ.get('KDC_TEST_REFERENCE'), 'requires local verified ROM and pinned core')
    def test_reference_reset_and_state_replay_repeat(self):
        rom = ROOT / "Kirby's Dream Course (USA).sfc"
        core = ROOT / 'local/reference/snes9x_libretro.dll'
        with tempfile.TemporaryDirectory() as directory:
            outputs = []
            for run in ('a', 'b'):
                output = Path(directory) / (run + '.json')
                subprocess.run([sys.executable, str(SCRIPT), '--rom', str(rom), '--core', str(core),
                                '--frames', '240', '--reset-frame', '120', '--roundtrip-frame', '80',
                                '--audio-raw', '--output', str(output)], check=True)
                data = json.loads(output.read_text(encoding='utf-8'))
                raw = output.with_suffix('.audio.s16le').read_bytes()
                self.assertEqual(len(raw), sum(data['audio_raw']['sample_frames_per_video_frame']) * 4)
                self.assertEqual(data['roundtrip']['replayed_through_frame'], 240)
                outputs.append((data, raw))
            self.assertEqual(outputs[0], outputs[1])


if __name__ == '__main__':
    unittest.main()

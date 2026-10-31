import struct
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from compare_audio import compare, read_samples


class CompareAudioTest(unittest.TestCase):
    @staticmethod
    def pcm(samples):
        return b''.join(struct.pack('<hh', *sample) for sample in samples)

    def test_offset_and_first_stereo_difference(self):
        host = self.pcm([(0, 0), (1, -1), (2, 3)])
        reference = self.pcm([(9, 9), (0, 0), (1, -1), (2, 4)])
        self.assertEqual(compare(host, reference, 1, True), (0, 3, 2))
        self.assertEqual(compare(reference, host, -1, True), (1, 4, 3))
        self.assertEqual(compare(host, host, 0), (0, 3, None))
        with self.assertRaisesRegex(ValueError, 'unmatched'):
            compare(host, reference, 1)

    def test_silent_and_nonoverlapping_captures_rejected(self):
        with self.assertRaisesRegex(ValueError, 'silent'):
            compare(self.pcm([(0, 0)]), self.pcm([(1, 1)]), 0)
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            compare(self.pcm([(1, 1)]), self.pcm([(1, 1)]), 1)

    def test_pcm_requires_complete_stereo_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture'
            path.write_bytes(struct.pack('<hhhh', -32768, 32767, 0, -1))
            self.assertEqual(read_samples(path), path.read_bytes())
            for invalid in (b'', b'\x01'):
                path.write_bytes(invalid)
                with self.assertRaises(ValueError):
                    read_samples(path)

    def test_cli_exit_status_and_partial_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            host, reference = Path(directory) / 'host', Path(directory) / 'reference'
            script = Path(__file__).resolve().parents[1] / 'tools' / 'compare_audio.py'
            host.write_bytes(self.pcm([(1, 1), (2, 2)]))
            def run(*options):
                return subprocess.run([sys.executable, str(script), str(host), str(reference),
                                       '--offset', '0', *options], capture_output=True, text=True)
            reference.write_bytes(host.read_bytes())
            self.assertEqual(run().returncode, 0)
            reference.write_bytes(self.pcm([(1, 1), (3, 2)]))
            result = run()
            self.assertEqual(result.returncode, 1)
            self.assertIn('First difference', result.stdout)
            reference.write_bytes(self.pcm([(1, 1)]))
            result = run()
            self.assertEqual(result.returncode, 2)
            self.assertIn('unmatched', result.stderr)
            self.assertNotIn('usage:', result.stderr)
            self.assertEqual(run('--allow-partial').returncode, 0)
            reference.write_bytes(b'')
            self.assertEqual(run().returncode, 2)


if __name__ == '__main__':
    unittest.main()

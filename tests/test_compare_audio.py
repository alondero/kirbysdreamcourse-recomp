import struct
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from compare_audio import compare, read_samples


class CompareAudioTest(unittest.TestCase):
    def test_offset_and_first_stereo_difference(self):
        host = [(0, 0), (1, -1), (2, 3)]
        reference = [(9, 9), (0, 0), (1, -1), (2, 4)]
        self.assertEqual(compare(host, reference, 1), (0, 3, 2))
        self.assertEqual(compare(reference, host, -1), (1, 4, 3))

    def test_silent_and_nonoverlapping_captures_rejected(self):
        with self.assertRaisesRegex(ValueError, 'silent'):
            compare([(0, 0)], [(1, 1)], 0)
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            compare([(1, 1)], [(1, 1)], 1)

    def test_pcm_requires_complete_stereo_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture'
            path.write_bytes(struct.pack('<hhhh', -32768, 32767, 0, -1))
            self.assertEqual(read_samples(path), [(-32768, 32767), (0, -1)])
            path.write_bytes(b'\x01')
            with self.assertRaises(ValueError):
                read_samples(path)


if __name__ == '__main__':
    unittest.main()

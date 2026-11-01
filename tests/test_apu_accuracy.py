"""Opt-in integration checks using local pinned emulator source checkouts."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.environ.get('KDC_TEST_APU'), 'set KDC_TEST_APU=1; see docs/apu-accuracy.md')
class ApuAccuracyTest(unittest.TestCase):
    def test_reference_matrix_and_queue_boundaries(self):
        command = [sys.executable, str(ROOT / 'tools/check_apu.py')]
        if os.environ.get('KDC_APU_FRAMEWORK'):
            command += ['--framework', os.environ['KDC_APU_FRAMEWORK']]
        if os.environ.get('KDC_APU_REFERENCE'):
            command += ['--reference', os.environ['KDC_APU_REFERENCE']]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('native=1: queue boundary: passed', result.stdout)
        self.assertIn('native=1: upload queue boundary: passed', result.stdout)


if __name__ == '__main__':
    unittest.main()

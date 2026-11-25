import shutil
import subprocess
import sys
import unittest
from pathlib import Path


class HighRefreshProbeTest(unittest.TestCase):
    def test_probe_builds_and_reports_clean_presentations(self):
        root = Path(__file__).resolve().parents[1]
        compiler = shutil.which('gcc')
        framework = root / 'local/guarded-framework'
        if not compiler or not (framework / 'runner/src/snes/ppu.h').exists():
            self.skipTest('requires GCC and the pinned framework checkout')
        result = subprocess.run(
            [sys.executable, str(root / 'tools/probe_high_refresh.py'), '--cc', compiler],
            cwd=root, capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rows = [line.split(',') for line in result.stdout.splitlines() if line[:1].isdigit()]
        self.assertEqual([row[0] for row in rows], ['60', '120', '144', '240'])
        self.assertTrue(all(row[4] == '0' for row in rows), result.stdout)


if __name__ == '__main__':
    unittest.main()

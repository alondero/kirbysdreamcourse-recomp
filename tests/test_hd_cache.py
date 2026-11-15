import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest


class HdCacheTest(unittest.TestCase):
    def test_hot_images_and_pixels_survive_capacity_pressure(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        if not shutil.which('gcc') or not (framework / 'runner/src/snes/ppu.h').is_file():
            self.skipTest('Requires GCC and the patched pinned framework')
        result = subprocess.run([sys.executable, str(root / 'tools/benchmark_hd_cache.py'),
                                 '--framework', str(framework)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rows = json.loads(result.stdout)
        capacity = [row for row in rows if row['phase'] == 'capacity']
        self.assertEqual(len(capacity), 5)
        self.assertTrue(all(row['misses'] == 0 for row in capacity))

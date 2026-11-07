import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class HdScalerTest(unittest.TestCase):
    def test_palette_locked_xbr(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free C fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'hd_scaler_test.exe'
            subprocess.run([compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'src'), str(root / 'tests/hd_scaler_test.c'),
                            str(root / 'src/hd_scaler.c'), '-lm', '-o', str(executable)],
                           check=True, capture_output=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)

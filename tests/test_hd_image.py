import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class HdImageTest(unittest.TestCase):
    def test_exact_checker_squares_across_lines_and_partial_updates(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free C fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'hd_image_test.exe'
            result = subprocess.run([compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(root / 'src'), str(root / 'tests/hd_image_test.c'),
                                     str(root / 'src/hd_image.c'), str(root / 'src/hd_scaler.c'),
                                     '-lm', '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)


if __name__ == '__main__':
    unittest.main()

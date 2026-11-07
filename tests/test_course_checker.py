import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class CourseCheckerTest(unittest.TestCase):
    def test_analytic_geometry(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'checker.exe'
            result = subprocess.run([
                compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), str(root / 'tests/course_checker_test.c'),
                str(root / 'src/course_checker.c'), '-o', str(executable)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

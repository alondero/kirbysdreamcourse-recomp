import os
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

    def test_fit_matches_exhaustive_search(self):
        # KDC_TEST_TIMING=1 also requires a 4x speed-up; wall time is not a CI check.
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'checker_equivalence.exe'
            result = subprocess.run([
                compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), str(root / 'tests/course_checker_equivalence_test.c'),
                '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            timing = ['--timing'] if os.environ.get('KDC_TEST_TIMING') == '1' else []
            result = subprocess.run([str(executable), *timing], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

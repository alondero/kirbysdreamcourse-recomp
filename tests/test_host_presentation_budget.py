import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class PresentationBudgetTest(unittest.TestCase):
    def test_slow_presentation_preserves_simulation_rate(self):
        root = Path(__file__).resolve().parents[1]
        source = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework')) / 'runner/src'
        compiler = shutil.which('gcc')
        if not compiler or not (source / 'desktop/host_clock.c').is_file():
            self.skipTest('Requires GCC and the patched framework')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'budget.exe'
            result = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(source),
                str(root / 'tests/host_presentation_budget_test.c'),
                str(source / 'desktop/host_clock.c'), '-lm', '-o', str(executable)
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

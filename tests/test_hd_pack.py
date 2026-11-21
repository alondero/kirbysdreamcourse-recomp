import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class HdPackTest(unittest.TestCase):
    def test_pack_lookup_and_live_recolouring(self):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        api = source / 'snes/ppu.h'
        if compiler is None or not api.is_file():
            self.skipTest('Requires GCC and the patched pinned framework (SNESRECOMP_ROOT)')
        if 'PpuSetHdRgbaSource' not in api.read_text(encoding='utf-8', errors='replace'):
            self.fail('The framework lacks the HD RGBA patch; rerun tools/setup.ps1')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'hd_pack_test.exe'
            subprocess.run([compiler, '-std=c11', '-O1', '-Wall', '-Werror', '-I', str(root / 'src'),
                            '-I', str(source), str(root / 'tests/hd_pack_test.c'),
                            str(root / 'src/hd_pack.c'), '-o', str(executable)],
                           check=True, capture_output=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, cwd=root)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)


if __name__ == '__main__':
    unittest.main()

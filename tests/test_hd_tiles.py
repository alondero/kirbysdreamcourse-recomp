import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class HdTilesTest(unittest.TestCase):
    def test_shared_ppu_tile_replacement(self):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        api = source / 'snes/ppu.h'
        if compiler is None or not api.is_file():
            self.skipTest('Requires GCC and the patched pinned framework (SNESRECOMP_ROOT)')
        if 'PpuSetHdTiles' not in api.read_text(encoding='utf-8', errors='replace'):
            self.fail('The framework lacks the HD tile patch; rerun tools/setup.ps1')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'hd_tiles_test.exe'
            subprocess.run([compiler, '-std=c11', '-O1', '-ffunction-sections', '-fdata-sections',
                            '-Wl,--gc-sections', '-I', str(source),
                            str(root / 'tests/hd_tiles_test.c'),
                            str(source / 'snes/ws_shadow.c'), str(source / 'snes/ppu_legacy.c'),
                            '-lm', '-o', str(executable)], check=True, capture_output=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)

            policy_exe = Path(directory) / 'hd_textures_policy_test.exe'
            subprocess.run([compiler, '-std=c11', '-O1', '-ffunction-sections', '-fdata-sections',
                            '-Wl,--gc-sections', '-I', str(root / 'src'), '-I', str(source),
                            str(root / 'tests/hd_textures_policy_test.c'),
                            str(root / 'src/hd_textures.c'),
                            str(root / 'src/course_hud.c'),
                            str(root / 'src/course_view.c'),
                            str(source / 'snes/ws_shadow.c'), str(source / 'snes/ppu_legacy.c'),
                            '-lm', '-o', str(policy_exe)], check=True, capture_output=True)
            result = subprocess.run([str(policy_exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)

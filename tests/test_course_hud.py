import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class CourseHudTest(unittest.TestCase):
    def test_native_sampling_quiet_expanded_guards_and_guest_state(self):
        root = Path(__file__).resolve().parents[1]
        source = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework')) / 'runner/src'
        compiler = shutil.which('gcc')
        if not compiler or not (source / 'snes/ppu.h').exists():
            self.skipTest('Requires GCC and patched pinned framework')
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'hud.exe'
            cmd = [compiler, '-std=c11', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-I', str(root / 'src'), '-I', str(source),
                   str(root / 'tests/course_hud_test.c'), str(root / 'src/course_hud.c'),
                   str(root / 'src/course_view.c'), str(source / 'snes/ws_shadow.c'),
                   str(source / 'snes/ppu_legacy.c'), '-o', str(exe)]
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

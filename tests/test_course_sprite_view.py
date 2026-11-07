import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class SpriteViewTest(unittest.TestCase):
    def test_direction_animation_phase_and_attachment_layout(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC required for synthetic sprite view fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'sprite_view.exe'
            result = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), str(root / 'tests/course_sprite_view_test.c'),
                str(root / 'src/course_sprite_view.c'), '-lm', '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

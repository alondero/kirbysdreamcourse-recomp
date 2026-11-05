import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class OrbitRendererTest(unittest.TestCase):
    def test_original_sprites_orbit_zoom_occlusion_hud_and_read_only_rendering(self):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        if compiler is None or not (source / 'snes/ppu.h').is_file():
            self.skipTest('Requires GCC and the pinned framework headers')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_orbit_render_test.exe'
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(root / 'src'), '-I', str(source),
                                     str(root / 'tests/course_orbit_render_test.c'),
                                     str(root / 'src/course_orbit.c'),
                                     str(root / 'src/course_camera.c'),
                                     str(root / 'src/course_geometry.c'),
                                     '-lm', '-o', str(executable)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            result = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))


if __name__ == '__main__':
    unittest.main()

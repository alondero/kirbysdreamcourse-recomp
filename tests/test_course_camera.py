import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class CourseCameraTest(unittest.TestCase):
    def test_orbit_projection_deadzone_zoom_and_reset(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC required for ROM-free camera fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'camera.exe'
            result = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), str(root / 'src/course_camera.c'),
                str(root / 'tests/course_camera_test.c'), '-lm', '-o', str(executable)
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class CourseGeometryTest(unittest.TestCase):
    def test_collision_geometry_and_bounds(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free geometry fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_geometry_test.exe'
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'src'),
                            str(root / 'tests/course_geometry_test.c'),
                            str(root / 'src/course_geometry.c'),
                            '-lm', '-o', str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()

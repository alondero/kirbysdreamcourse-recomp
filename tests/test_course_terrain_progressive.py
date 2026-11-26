import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class CourseTerrainProgressiveTest(unittest.TestCase):
    def test_large_initial_enlargement_is_progressive_and_exact(self):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        if compiler is None or not (source / 'snes/ppu.h').is_file():
            self.skipTest('Requires GCC and the pinned framework headers')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_terrain_progressive_test.exe'
            result = subprocess.run([compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(root / 'src'), '-I', str(source),
                                     str(root / 'tests/course_terrain_progressive_test.c'),
                                     str(root / 'src/course_terrain.c'), str(root / 'src/hd_image.c'),
                                     str(root / 'src/hd_scaler.c'), str(root / 'src/host_parallel.c'),
                                     '-lm', '-lpthread', '-o', str(executable)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for threads in (1, 4):
                with self.subTest(threads=threads):
                    result = subprocess.run([str(executable)], capture_output=True, text=True,
                                            env=dict(os.environ, KDC_THREADS=str(threads)))
                    self.assertEqual(result.returncode, 0, result.stderr + result.stdout)


if __name__ == '__main__':
    unittest.main()

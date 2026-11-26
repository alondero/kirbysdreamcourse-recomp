import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class CourseTerrainCachePressureTest(unittest.TestCase):
    def test_parallel_block_cache_respects_byte_limit(self):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        if compiler is None or not (source / 'snes/ppu.h').is_file():
            self.skipTest('Requires GCC and the pinned framework headers')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_terrain_cache_pressure_test.exe'
            result = subprocess.run([compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                                     '-DKDC_TERRAIN_BLOCK_CACHE_LIMIT=262144',
                                     '-DKDC_TERRAIN_PROGRESSIVE_PIXELS=0x7fffffff',
                                     '-I', str(root / 'src'), '-I', str(source),
                                     str(root / 'tests/course_terrain_cache_pressure_test.c'),
                                     str(root / 'src/course_terrain.c'), str(root / 'src/hd_image.c'),
                                     str(root / 'src/hd_scaler.c'), str(root / 'src/host_parallel.c'),
                                     '-Wl,--wrap=malloc', '-Wl,--wrap=free', '-Wl,--wrap=KdcHdImageUpdate',
                                     '-lm', '-lpthread', '-o', str(executable)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            env = dict(os.environ, KDC_THREADS='16')
            result = subprocess.run([str(executable)], capture_output=True, text=True,
                                    env=env, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)


if __name__ == '__main__':
    unittest.main()

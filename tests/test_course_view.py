import shutil
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


class CourseViewTest(unittest.TestCase):
    def test_packed_course_rows_and_bounds(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free C fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_view_test.exe'
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'src'),
                            str(root / 'tests/course_view_test.c'), '-o', str(executable)],
                           check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)

    def test_big_tile_renderer_retains_native_pixel_phase(self):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        api = source / 'snes/ws_shadow.h'
        if compiler is None or not api.is_file() or 'WsShadowResolveTile8x8' not in api.read_text(encoding='utf-8', errors='replace'):
            self.skipTest('Requires GCC and patched pinned framework (SNESRECOMP_ROOT)')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_render_test.exe'
            subprocess.run([compiler, '-std=c11', '-ffunction-sections', '-fdata-sections',
                            '-Wl,--gc-sections', '-I', str(root / 'src'), '-I', str(source),
                            str(root / 'tests/course_render_test.c'),
                            str(root / 'src/course_view.c'), str(source / 'snes/ws_shadow.c'),
                            str(source / 'snes/ppu_legacy.c'),
                            '-o', str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)

    def test_shadow_fine_scroll_and_large_map_coordinates(self):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        api = source / 'snes/ws_shadow.h'
        if compiler is None or not api.is_file() or 'WsShadowResolveTile8x8' not in api.read_text(encoding='utf-8', errors='replace'):
            self.skipTest('Requires GCC and patched pinned framework (SNESRECOMP_ROOT)')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_shadow_test.exe'
            subprocess.run([compiler, '-std=c11', '-I', str(root / 'src'),
                            '-I', str(source), str(root / 'tests/course_shadow_test.c'),
                            str(root / 'src/course_view.c'), str(source / 'snes/ws_shadow.c'),
                            '-o', str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class CourseSpritesTest(unittest.TestCase):
    def test_recovery_and_raster_composition(self):
        self.run_fixture('course_sprites_test.c')

    def test_queue_only_clear_discards_unconsumed_requests(self):
        self.run_fixture('course_sprites_review_test.c', 'queue')

    def test_observers_honor_data_bank_without_guest_mutation(self):
        self.run_fixture('course_sprites_review_test.c', 'bank')

    def test_uncovered_native_margin_pixels_survive(self):
        self.run_fixture('course_sprites_review_test.c', 'margins')

    def run_fixture(self, fixture, *arguments):
        root = Path(__file__).resolve().parents[1]
        compiler = shutil.which('gcc')
        source = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework')) / 'runner/src'
        api = source / 'snes/ppu.h'
        if compiler is None or not api.is_file() or 'PpuSetExtraObjectProvider' not in api.read_text():
            self.skipTest('Requires GCC and patched pinned framework (SNESRECOMP_ROOT)')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_sprites_test.exe'
            subprocess.run([
                compiler, '-std=c11', '-ffunction-sections', '-fdata-sections',
                '-Wl,--gc-sections', '-I', str(root / 'src'), '-I', str(source),
                str(root / 'tests' / fixture), str(root / 'src/course_view.c'),
                str(source / 'snes/ws_shadow.c'), str(source / 'snes/ppu_legacy.c'),
                '-o', str(executable),
            ], check=True, capture_output=True)
            result = subprocess.run([str(executable), *arguments], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

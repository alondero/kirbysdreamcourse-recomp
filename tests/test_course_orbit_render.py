import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class OrbitRendererTest(unittest.TestCase):
    def test_original_sprites_orbit_zoom_occlusion_hud_and_read_only_rendering(self):
        self.run_fixture('course_orbit_render_test.c')

    def test_intermediate_camera_and_object_pixels_and_history_rejection(self):
        self.run_fixture('high_refresh_render_test.c')

    def test_sprite_art_cache_reuse_updates_and_pressure(self):
        self.run_fixture("course_orbit_art_cache_test.c", embedded=True)

    def run_fixture(self, fixture, embedded=False):
        compiler = shutil.which('gcc')
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        if compiler is None or not (source / 'snes/ppu.h').is_file():
            self.skipTest('Requires GCC and the pinned framework headers')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'course_orbit_render_test.exe'
            sources = [root / "src/course_camera.c", root / "src/course_geometry.c",
                       root / "src/course_sprite_view.c", root / "src/hd_tile_source.c",
                       root / "src/hd_scaler.c", root / "src/course_checker.c",
                       root / "src/course_terrain.c", root / "src/hd_image.c",
                       root / "src/host_parallel.c"]
            if not embedded:
                sources += [root / "src/course_orbit.c", root / "src/course_hud.c"]
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(root / 'src'), '-I', str(source),
                                     str(root / 'tests' / fixture),
                                     *map(str, sources),
                                     '-lm', '-o', str(executable)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            result = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))


if __name__ == '__main__':
    unittest.main()

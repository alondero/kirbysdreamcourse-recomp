"""Opt-in real-ROM HD textures check; all captures remain in ignored local/."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def load_ppm(path):
    data = path.read_bytes()
    magic, width, height, depth, pixels = data.split(maxsplit=4)
    assert magic == b'P6' and depth == b'255'
    return int(width), int(height), pixels


@unittest.skipUnless(os.environ.get('KDC_TEST_HD') == '1',
                     'Set KDC_TEST_HD=1 for real-ROM HD texture captures')
class HdTexturesRuntimeTest(unittest.TestCase):
    root = Path(__file__).resolve().parents[1]

    def capture(self, directory, name, frame, hd, dev_hole=None, high_refresh=False):
        rom = Path(os.environ.get('KDC_HD_ROM', self.root / "Kirby's Dream Course (USA).sfc"))
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b')
        executable = Path(os.environ.get('KDC_HD_EXECUTABLE',
                          self.root / 'local/guarded-build' /
                          ('KirbyDreamCourseRecomp.exe' if os.name == 'nt' else 'KirbyDreamCourseRecomp')))
        case = directory / name
        case.mkdir()
        if high_refresh:
            (case / 'kdc-presentation.cfg').write_text('1 120\n')
        host = case / executable.name
        shutil.copy2(executable, host)
        (case / 'config.ini').write_text(
            '[General]\nAutosave=0\nDisableFrameDelay=1\n'
            '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\nWidescreen=0\n'
            '[Sound]\nEnableAudio=0\n'
            '[Controller]\nSourceP1=0\nSourceP2=0\n'
            '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('SNESRECOMP_', 'KDC_', 'LNG_', 'SDL_'))}
        shot = case / 'shot.ppm'
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   SNESRECOMP_RUN_FRAMES=str(frame + 5),
                   SNESRECOMP_SCREENSHOT=str(shot),
                   SNESRECOMP_SCREENSHOT_FRAME=str(frame),
                   KDC_HD='1' if hd else '0')
        args = [str(host), '--no-launcher', '--rom', str(rom)]
        if dev_hole:
            args += ['--dev-hole', dev_hole]
        result = subprocess.run(args, env=env, cwd=case, capture_output=True, timeout=180)
        self.assertEqual(result.returncode, 0,
                         result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        return load_ppm(shot)

    def compare(self, native, hd, region=None):
        """Count HD pixels differing from the nearest-scaled native frame."""
        width, height, base = native
        hd_width, hd_height, pixels = hd
        self.assertEqual((hd_width, hd_height), (4 * width, 4 * height))
        x0, y0, x1, y1 = region or (0, 0, width, height)
        differing = 0
        for y in range(4 * y0, 4 * y1):
            for x in range(4 * x0, 4 * x1):
                at = (y * hd_width + x) * 3
                want = ((y // 4) * width + x // 4) * 3
                differing += pixels[at:at + 3] != base[want:want + 3]
        return differing

    def test_hd_detail_survives_course_composition(self):
        with tempfile.TemporaryDirectory(prefix='hd-persistence-', dir=self.root / 'local') as directory:
            for frame in (340, 380, 600):
                with self.subTest(frame=frame):
                    width, height, pixels = self.capture(
                        Path(directory), str(frame), frame, True, 'normal:1:1', high_refresh=True)
                    self.assertEqual((width, height), (1024, 896))
                    # A native composition enlarged 4x has no detail inside any
                    # 4x4 block. Check course art above the HUD after loading.
                    detail = sum(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] !=
                                 pixels[((y // 4 * 4) * width + x // 4 * 4) * 3:
                                        ((y // 4 * 4) * width + x // 4 * 4) * 3 + 3]
                                 for y in range(4 * 40, 4 * 160)
                                 for x in range(4 * 32, 4 * 224))
                    self.assertGreater(detail, 1000, 'HD course detail was replaced by a native composition')

    def test_course_art_scales_while_hud_and_menus_stay_native(self):
        with tempfile.TemporaryDirectory(prefix='hd-textures-', dir=self.root / 'local') as directory:
            directory = Path(directory)
            course_native = self.capture(directory, 'course-native', 380, False, 'normal:1:1')
            course_hd = self.capture(directory, 'course-hd', 380, True, 'normal:1:1')
            # Course terrain, scenery and sprites are redrawn.
            self.assertGreater(self.compare(course_native, course_hd), 10000)
            # The HUD panel's Kirby portrait box keeps its native art.
            self.assertEqual(self.compare(course_native, course_hd, (24, 182, 48, 206)), 0)
            # A title/menu scene keeps its native art.
            menu_native = self.capture(directory, 'menu-native', 300, False)
            menu_hd = self.capture(directory, 'menu-hd', 300, True)
            self.assertEqual(self.compare(menu_native, menu_hd), 0)


if __name__ == '__main__':
    unittest.main()

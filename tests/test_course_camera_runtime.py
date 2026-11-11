"""Opt-in real-ROM camera/shot replay; all captures remain in ignored local/."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('KDC_TEST_CAMERA') == '1',
                     'Set KDC_TEST_CAMERA=1 for real-ROM camera playback')
class CameraRuntimeTest(unittest.TestCase):
    def test_kracko_gordo_and_lightning_stay_anchored_during_rotation(self):
        root = Path(__file__).resolve().parents[1]
        rom = Path(os.environ.get('KDC_CAMERA_ROM', root / "Kirby's Dream Course (USA).sfc"))
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b')
        executable = Path(os.environ.get('KDC_CAMERA_EXECUTABLE',
                          root / 'local/guarded-build' /
                          ('KirbyDreamCourseRecomp.exe' if os.name == 'nt' else 'KirbyDreamCourseRecomp'))).resolve()
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('SNESRECOMP_', 'KDC_', 'LNG_', 'SDL_'))}
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', KDC_HD='1',
                   SNESRECOMP_RUN_FRAMES='820', SNESRECOMP_FRAMEDUMP_START='790',
                   SNESRECOMP_FRAMEDUMP_END='819', SNESRECOMP_FRAMEDUMP_PIXELS='1',
                   SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                   SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1')
        with tempfile.TemporaryDirectory(prefix='enemy-camera-', dir=root / 'local') as directory:
            temporary = Path(directory)

            def run(name, orbit, bounce):
                case = temporary / name
                case.mkdir()
                host = case / executable.name
                shutil.copy2(executable, host)
                (case / 'config.ini').write_text(
                    '[General]\nAutosave=0\nDisableFrameDelay=1\n'
                    '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\nWidescreen=0\n'
                    '[Sound]\nEnableAudio=1\nAudioFreq=32000\n'
                    '[Controller]\nSourceP1=0\nSourceP2=0\n'
                    '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
                (case / 'shot.script').write_text(
                    'wait 600\npress a 2\nwait 57\npress a 2\nwait 27\npress a 2\n')
                camera = case / 'camera.script'
                camera.write_text('500 32767 0 0\n740 0 -32768 0\n'
                                  '790 0 0 0\n800 0 0 1\n')
                run_env = dict(env, SNESRECOMP_LLE_BOUNCE=bounce,
                               KDC_ORBIT_SPRITE_LOG=str(case / 'sprites.log'))
                if orbit:
                    run_env['KDC_CAMERA_SCRIPT'] = str(camera)
                result = subprocess.run([
                    str(host), '--no-launcher', '--rom', str(rom),
                    '--dev-hole', 'multi-extra:3:1', '--script', str(case / 'shot.script'),
                    '--framedump', str(case / 'frames'),
                ], env=run_env, capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
                return case

            baseline = run('native-view', False, '1')
            orbit = run('orbit-view', True, '1')
            fallback = run('fallback-orbit', True, '0')
            for frame in range(790, 820):
                stem = f'frame_{frame:06}'
                with self.subTest(frame=frame):
                    expected = (baseline / 'frames' / (stem + '_wram.bin')).read_bytes()
                    self.assertEqual(expected, (orbit / 'frames' / (stem + '_wram.bin')).read_bytes())
                    self.assertEqual(expected, (fallback / 'frames' / (stem + '_wram.bin')).read_bytes())
                    image = (orbit / 'frames' / (stem + '.bmp')).read_bytes()
                    self.assertEqual(image, (fallback / 'frames' / (stem + '.bmp')).read_bytes())
                    if frame > 800:
                        self.assertEqual(image, (baseline / 'frames' / (stem + '.bmp')).read_bytes())
            for case in (orbit, fallback):
                sprites = [line.split() for line in (case / 'sprites.log').read_text().splitlines()]
                sprites = [s for s in sprites if 500 <= int(s[0]) < 800]
                self.assertTrue(sprites)
                self.assertFalse([s for s in sprites if s[4] == '1'], 'Unanchored course sprite')
                # These two Krackos and both Gordos share map 208. Bodies
                # and their terrain shadows must use the same world point.
                for x, y, z in ((11.5, 1.5, 2), (1.5, 11.5, 2),
                                (6.5, 4.5, 3), (8.5, 6.5, 3)):
                    group = [s for s in sprites if s[2] == '80e60b' and
                             tuple(map(float, s[7:10])) == (x, y, z)]
                    self.assertTrue(any(s[10] == '0' for s in group), 'Missing anchored body')
                    self.assertTrue(any(s[10] == '1' for s in group), 'Missing terrain shadow')
            def enemy_pieces(case):
                # Native/fallback observers can repeat unrelated guide
                # requests. Compare the special enemies independently of
                # those requests' positions in the presentation list.
                return {tuple([s[0], *s[2:]]) for line in (case / 'sprites.log').read_text().splitlines()
                        if (s := line.split())[2] == '80e60b' and 500 <= int(s[0]) < 800}

            self.assertEqual(enemy_pieces(orbit), enemy_pieces(fallback))

    def test_shot_state_fallback_and_return_to_original_view(self):
        root = Path(__file__).resolve().parents[1]
        rom = Path(os.environ.get('KDC_CAMERA_ROM', root / "Kirby's Dream Course (USA).sfc"))
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b')
        executable = Path(os.environ.get('KDC_CAMERA_EXECUTABLE',
                          root / 'local/guarded-build' /
                          ('KirbyDreamCourseRecomp.exe' if os.name == 'nt' else 'KirbyDreamCourseRecomp')))
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('SNESRECOMP_', 'KDC_', 'LNG_', 'SDL_'))}
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   SNESRECOMP_RUN_FRAMES='900', SNESRECOMP_FRAMEDUMP_START='790',
                   SNESRECOMP_FRAMEDUMP_END='899', SNESRECOMP_FRAMEDUMP_PIXELS='1',
                   SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                   SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1')
        with tempfile.TemporaryDirectory(prefix='camera-replay-', dir=root / 'local') as directory:
            temporary = Path(directory)

            def run(name, orbit, bounce):
                case = temporary / name
                case.mkdir()
                host = case / executable.name
                shutil.copy2(executable, host)
                (case / 'config.ini').write_text(
                    '[General]\nAutosave=0\nDisableFrameDelay=1\n'
                    '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\nWidescreen=0\n'
                    '[Sound]\nEnableAudio=1\nAudioFreq=32000\n'
                    '[Controller]\nSourceP1=0\nSourceP2=0\n'
                    '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
                # Original controls start and execute a shot during camera movement.
                (case / 'shot.script').write_text(
                    'wait 600\npress a 2\nwait 57\npress a 2\nwait 27\npress a 2\n')
                camera = case / 'camera.script'
                camera.write_text('500 32767 0 0\n560 0 32767 0\n'
                                  '610 0 0 0\n800 0 0 1\n')
                run_env = dict(env, SNESRECOMP_LLE_BOUNCE=bounce)
                if orbit:
                    run_env['KDC_CAMERA_SCRIPT'] = str(camera)
                result = subprocess.run([
                    str(host), '--no-launcher', '--rom', str(rom),
                    '--dev-hole', 'normal:1:1', '--script', str(case / 'shot.script'),
                    '--framedump', str(case / 'frames'),
                ], env=run_env, capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 0,
                                 result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
                return case / 'frames'

            baseline = run('native-view', False, '1')
            orbit = run('orbit-view', True, '1')
            fallback = run('fallback-orbit', True, '0')
            for frame in range(790, 900):
                stem = f'frame_{frame:06}'
                with self.subTest(frame=frame):
                    expected = (baseline / (stem + '_wram.bin')).read_bytes()
                    self.assertEqual(expected, (orbit / (stem + '_wram.bin')).read_bytes())
                    self.assertEqual(expected, (fallback / (stem + '_wram.bin')).read_bytes())
                    image = (orbit / (stem + '.bmp')).read_bytes()
                    self.assertEqual(image, (fallback / (stem + '.bmp')).read_bytes())
                    if frame > 800:
                        self.assertEqual(image, (baseline / (stem + '.bmp')).read_bytes())
            self.assertNotEqual((baseline / 'frame_000799.bmp').read_bytes(),
                                (orbit / 'frame_000799.bmp').read_bytes())


if __name__ == '__main__':
    unittest.main()

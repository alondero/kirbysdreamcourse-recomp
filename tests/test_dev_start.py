import os
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class DevStartTest(unittest.TestCase):
    def test_selection_and_lifecycle(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        compiler = shutil.which('gcc')
        if not compiler or not (framework / 'runner/src/cpu_state.h').is_file():
            self.skipTest('Requires GCC and pinned framework (SNESRECOMP_ROOT)')
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'dev_start_test.exe'
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'src'), '-I', str(framework / 'runner/src'),
                            str(root / 'tests/dev_start_test.c'), str(root / 'src/dev_start.c'),
                            '-o', str(exe)], check=True, capture_output=True)
            for target, map_id, course, players in (
                    ('normal:8:8', 63, 7, 0), ('extra:8:8', 127, 15, 0),
                    ('multi:4:8', 159, 19, 1), ('multi-extra:4:8', 223, 27, 1),
                    ('demo:1', 0, 0, 0), ('demo:2', 42, 5, 0),
                    ('demo:3', 44, 5, 0), ('demo:4', 77, 9, 0),
                    ('map:0', 0, 0, 0), ('map:184', 184, 0, 0),
                    ('map:255', 255, 16, 1)):
                with self.subTest(target=target):
                    result = subprocess.run([str(exe), target, str(map_id), str(course), str(players)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            invalid = ['', 'normal:0:1', 'normal:9:1', 'normal:1:0', 'extra:1:9',
                       'multi:5:1', 'multi-extra:5:1', 'map:-1', 'map:256',
                       'map:0x10', 'map:1junk', 'demo:0', 'demo:5',
                       'normal:1', 'normal:1:1:1', 'normal:1:', 'map:99999999999']
            for target in invalid:
                with self.subTest(invalid=target):
                    result = subprocess.run([str(exe), 'parse', '--dev-hole', target], capture_output=True)
                    self.assertEqual(result.returncode, 2)
            for args in (['--dev-hole'], ['--dev-two-player'],
                         ['--dev-hole', 'normal:1:1', '--dev-two-player'],
                         ['--dev-hole', 'map:1', '--dev-hole', 'map:2']):
                self.assertEqual(subprocess.run([str(exe), 'parse', *args], capture_output=True).returncode, 2)
            result = subprocess.run([str(exe), 'parse', '--rom', 'input.sfc', '--no-launcher'],
                                    capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout.splitlines(), ['--rom', 'input.sfc', '--no-launcher'])


@unittest.skipUnless(os.name == 'nt' and os.environ.get('KDC_TEST_DEV_START') == '1',
                     'set KDC_TEST_DEV_START=1 for local ROM startup checks')
class DevStartRuntimeTest(unittest.TestCase):
    def test_loaded_maps_and_repeatable_dispatch(self):
        root = Path(__file__).resolve().parents[1]
        rom = root / "Kirby's Dream Course (USA).sfc"
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b')
        executable = Path(os.environ.get('KDC_DEV_EXECUTABLE',
                                         root / 'local/guarded-build/KirbyDreamCourseRecomp.exe'))
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('SNESRECOMP_', 'KDC_', 'LNG_', 'SDL_'))}
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   SNESRECOMP_RUN_FRAMES='600', SNESRECOMP_FRAMEDUMP_START='599',
                   SNESRECOMP_FRAMEDUMP_END='599', SNESRECOMP_APU_BEAM_ELAPSED='1',
                   SNESRECOMP_APU_HANDOFF='1', SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1')
        with tempfile.TemporaryDirectory(prefix='dev-start-', dir=root / 'local') as temp:
            def run(target, expected_map, multiplayer=0, bounce='1'):
                case = Path(temp) / str(len(list(Path(temp).iterdir())))
                case.mkdir()
                host = case / 'game.exe'
                shutil.copy2(executable, host)
                (case / 'config.ini').write_text(
                    '[General]\nAutosave = 0\nDisableFrameDelay = 1\n'
                    '[Graphics]\nNewRenderer = 1\nNoSpriteLimits = 0\nWidescreen = 0\n'
                    '[Sound]\nEnableAudio = 1\nAudioFreq = 32000\nAudioChannels = 2\nAudioSamples = 512\n'
                    '[Controller]\nSourceP1 = 0\nSourceP2 = 0\n'
                    '[GamepadMap]\nEnableGamepad1 = false\nEnableGamepad2 = false\n')
                args = [str(host), '--no-launcher', '--rom', str(rom),
                        '--dev-hole', target, '--framedump', str(case / 'frames')]
                if target.startswith('map:') and multiplayer:
                    args.append('--dev-two-player')
                result = subprocess.run(args, env=dict(env, SNESRECOMP_LLE_BOUNCE=bounce),
                                        capture_output=True, text=True, errors='replace', timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f'Developer start: map {expected_map},', result.stderr)
                ram = (case / 'frames/frame_000599_wram.bin').read_bytes()
                def word(address):
                    return int.from_bytes(ram[address:address + 2], 'little')
                self.assertEqual(word(0xabb2), expected_map)
                self.assertEqual(word(0x17ff), multiplayer)
                self.assertGreater(word(0x8b92), 0)
                self.assertGreater(word(0x8b94), 0)
                self.assertEqual(word(0xed07), 0)  # Human control, including demo layouts.
                return ram

            first = run('normal:1:1', 0)
            self.assertEqual(first, run('normal:1:1', 0))
            self.assertEqual(first, run('normal:1:1', 0, bounce='0'))
            for target, map_id, players in (
                    ('normal:8:8', 63, 0), ('extra:8:8', 127, 0),
                    ('multi:4:8', 159, 1), ('multi-extra:4:8', 223, 1),
                    ('demo:2', 42, 0), ('demo:4', 77, 0), ('map:160', 160, 0),
                    ('map:184', 184, 0), ('map:255', 255, 0), ('map:160', 160, 1)):
                with self.subTest(target=target, multiplayer=players):
                    run(target, map_id, players)

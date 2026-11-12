import os
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class PracticeTest(unittest.TestCase):
    def test_menu_and_save_lifecycle(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        compiler = shutil.which('gcc')
        if not compiler or not (framework / 'runner/src/cpu_state.h').is_file():
            self.skipTest('Requires GCC and pinned framework (SNESRECOMP_ROOT)')
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'practice_test.exe'
            build = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), '-I', str(framework / 'runner/src'),
                str(root / 'tests/practice_test.c'), str(root / 'src/practice.c'),
                str(root / 'src/dev_start.c'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


@unittest.skipUnless(os.environ.get('KDC_TEST_PRACTICE') == '1',
                     'set KDC_TEST_PRACTICE=1 for real-ROM menu checks')
class PracticeRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.rom = Path(os.environ.get('KDC_PRACTICE_ROM', cls.root / "Kirby's Dream Course (USA).sfc"))
        if hashlib.sha256(cls.rom.read_bytes()).hexdigest() != (
                '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'):
            raise ValueError('Practice checks require the exact supported ROM')
        cls.executable = Path(os.environ.get('KDC_PRACTICE_EXECUTABLE',
            cls.root / 'local/guarded-build' /
            ('KirbyDreamCourseRecomp.exe' if os.name == 'nt' else 'KirbyDreamCourseRecomp')))

    @staticmethod
    def tap(button):
        return f'press {button} 2\nwait 12\n'

    def entry(self):
        return ('wait 500\n' + self.tap('start') + 'wait 120\ndump ordinary\n' +
                self.tap('down') * 3 + self.tap('a') + 'wait 30\n')

    def select(self, mode=0, last=False, ability=0):
        # Mode -> course -> hole -> ability -> play, using mapped P1 inputs.
        return (self.tap('right') * mode + self.tap('a') +
                (self.tap('left') if last else '') + self.tap('a') +
                (self.tap('left') if last else '') + self.tap('a') +
                self.tap('right') * ability + self.tap('a') + self.tap('a'))

    def test_picker_keeps_running(self):
        files, _ = self.run_case(self.entry() +
            'dump picker\nwait 90\ndump animated\n' + self.tap('b') + 'dump back\n', observe=True)
        for suffix in ('wram.bin', 'fb.bmp'):
            self.assertNotEqual(hashlib.sha256(files[f'picker.{suffix}']).hexdigest(),
                                hashlib.sha256(files[f'animated.{suffix}']).hexdigest())
        self.assertEqual(files['ordinary.sram.bin'], files['back.sram.bin'])
        clocks = [int(line.split()[1]) for line in files['apu.log'].decode().splitlines()]
        # The picker starts near tick 730. Its APU must continue advancing.
        self.assertGreater(clocks[800], clocks[740])

    def test_starting_abilities(self):
        for ability in range(11):
            with self.subTest(ability=ability):
                files, _ = self.run_case(self.entry() + self.select(ability=ability) +
                                        'wait 600\ndump loaded\n')
                ram = files['loaded.wram.bin']
                self.assertEqual(ram[0xd852:0xd856], bytes((ability, 0, ability, 0)))
                self.assertEqual(ram[0xd9c4:0xd9c6], bytes((ability, 0)))

    def test_completed_hole_returns_to_picker(self):
        for mode in (0, 3):
            for last in (False, True):
                with self.subTest(mode=mode, last=last):
                    # Discover this hole's allocated shot task, then replay
                    # identical input and mark that shot/hole finished. This
                    # exercises the original main-loop completion branch.
                    shot = (self.entry() + self.select(mode, last=last, ability=10) +
                            'wait 600\n' + self.tap('a') + 'dump shot\n')
                    before, _ = self.run_case(shot)
                    slot = int.from_bytes(before['shot.wram.bin'][0x6b48:0x6b4a], 'little')
                    self.assertLess(slot, 128)
                    self.assertEqual(slot % 2, 0)
                    script = shot + ('poke da4a 0100\npoke d956 01000100\n'
                                     f'poke {0x531c + slot:x} 0100\n'
                                     'wait 300\ndump completed\nwait 90\ndump picker\n')
                    script += self.tap('a') + 'wait 600\ndump replayed\n'
                    files, log = self.run_case(script)
                    self.assertEqual(before['shot.wram.bin'], files['shot.wram.bin'])
                    self.assertIn('Practice: hole finished, returned to picker', log)
                    self.assertNotEqual(files['completed.wram.bin'], files['picker.wram.bin'])
                    map_id = (223 if last else 192) if mode else (63 if last else 0)
                    self.check_map(files, 'replayed', map_id, mode >= 2)
                    self.assertEqual(files['replayed.wram.bin'][0xd852], 10)
                    self.assertEqual(files['ordinary.sram.bin'], files['saves/save.srm'])

    def run_case(self, script, bounce='1', executable=None, observe=False, frames=5000):
        with tempfile.TemporaryDirectory(prefix='practice-', dir=self.root / 'local') as directory:
            case = Path(directory)
            host = case / self.executable.name
            shutil.copy2(executable or self.executable, host)
            (case / 'saves').mkdir()
            (case / 'saves/save.srm').write_bytes(bytes(8192))
            (case / 'config.ini').write_text(
                '[General]\nAutosave=0\nDisableFrameDelay=1\n'
                '[Graphics]\nOutputMethod=SDL-Software\nNewRenderer=1\nNoSpriteLimits=0\n'
                '[Sound]\nEnableAudio=1\nAudioFreq=32000\n'
                '[Controller]\nSourceP1=0\nSourceP2=0\n'
                '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
            (case / 'input.script').write_text(script + '\nquit\n')
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
            env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       SNESRECOMP_RUN_FRAMES=str(frames), SNESRECOMP_APU_BEAM_ELAPSED='1',
                       SNESRECOMP_APU_HANDOFF='1', SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1',
                       SNESRECOMP_LLE_BOUNCE=bounce, SNESRECOMP_DUMP_DIR=str(case))
            if observe:
                env['KDC_APU_STATE_LOG'] = str(case / 'apu.log')
            result = subprocess.run([str(host), '--no-launcher', '--rom', str(self.rom),
                                     '--script', str(case / 'input.script')],
                                    env=env, capture_output=True, text=True, errors='replace', timeout=150)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('script quit', result.stderr)
            files = {str(p.relative_to(case)).replace('\\', '/'): p.read_bytes()
                     for p in case.rglob('*') if p.is_file() and p.suffix in ('.bin', '.srm', '.bmp', '.log')}
            return files, result.stderr

    def check_map(self, files, tag, map_id, players):
        ram = files[f'{tag}.wram.bin']
        word = lambda a: int.from_bytes(ram[a:a + 2], 'little')
        self.assertEqual(word(0xabb2), map_id)
        self.assertEqual(word(0x17ff), players)
        self.assertGreater(word(0x8b92), 0)
        self.assertGreater(word(0x8b94), 0)
        self.assertEqual(word(0xed07), 0)

    def test_starts_and_dispatch(self):
        for mode, map_id in enumerate((63, 127, 159, 223)):
            with self.subTest(mode=mode):
                script = self.entry() + self.select(mode, last=True) + 'wait 600\ndump loaded\n'
                native, _ = self.run_case(script)
                self.check_map(native, 'loaded', map_id, mode >= 2)
                self.assertEqual(native['ordinary.sram.bin'], native['saves/save.srm'])
                self.assertIn('saves/practice/save.srm', native)
                if mode in (0, 3):
                    fallback, _ = self.run_case(script, bounce='0')
                    self.assertEqual(native['loaded.wram.bin'], fallback['loaded.wram.bin'])
                if mode == 0:
                    repeated, _ = self.run_case(script)
                    self.assertEqual(native['loaded.wram.bin'], repeated['loaded.wram.bin'])

    def test_restart_change_exit(self):
        script = self.entry() + self.select() + 'wait 600\ndump first\n'
        script += self.tap('start') + 'dump paused\nwait 30\ndump still_paused\n'
        script += self.tap('b') + 'wait 30\n'
        script += self.tap('start') + self.tap('down') + self.tap('a')
        script += 'wait 600\ndump restarted\n'
        script += self.tap('start') + self.tap('down') * 2 + self.tap('a')
        script += self.select(3, last=True) + 'wait 600\ndump changed\n'
        script += self.tap('start') + self.tap('down') * 3 + self.tap('a')
        script += 'wait 600\ndump exited\n'
        files, log = self.run_case(script)
        self.check_map(files, 'first', 0, 0)
        self.check_map(files, 'restarted', 0, 0)
        self.check_map(files, 'changed', 223, 1)
        self.assertEqual(files['paused.wram.bin'], files['still_paused.wram.bin'])
        self.assertIn('Practice: returned to ordinary title', log)
        self.assertEqual(files['ordinary.sram.bin'], files['exited.sram.bin'])
        self.assertEqual(files['ordinary.sram.bin'], files['saves/save.srm'])

    def test_ordinary_boot_unchanged(self):
        baseline = os.environ.get('KDC_PRACTICE_BASELINE')
        if not baseline:
            self.skipTest('set KDC_PRACTICE_BASELINE to a pre-change executable')
        script = 'wait 240\ndump boot\n'
        before, _ = self.run_case(script, executable=baseline)
        after, _ = self.run_case(script)
        self.assertEqual(before['boot.wram.bin'], after['boot.wram.bin'])
        self.assertEqual(before['boot.sram.bin'], after['boot.sram.bin'])

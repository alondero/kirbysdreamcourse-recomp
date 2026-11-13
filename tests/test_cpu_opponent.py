import os
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class CpuOpponentTest(unittest.TestCase):
    def test_controller_ownership_search_and_shot_lifecycle(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        compiler = shutil.which('gcc')
        if not compiler or not (framework / 'runner/src/cpu_state.h').is_file():
            self.skipTest('Requires GCC and the pinned framework')
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'cpu_opponent_test.exe'
            build = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), '-I', str(framework / 'runner/src'),
                str(root / 'tests/cpu_opponent_test.c'), str(root / 'src/cpu_opponent.c'),
                '-lm', '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_player_two_name_box(self):
        root = Path(__file__).resolve().parents[1]
        compiler = shutil.which('gcc')
        if not compiler:
            self.skipTest('Requires GCC')
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'cpu_label_test.exe'
            build = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src'),
                str(root / 'tests/cpu_label_test.c'), str(root / 'src/cpu_label.c'),
                '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_preview_bus_lorom_mirror_boundaries(self):
        root = Path(__file__).resolve().parents[1]
        candidates = [
            os.environ.get('SNESRECOMP_ROOT', ''),
            str(root / 'local/guarded-framework'),
            str(root.parent.parent / 'local/guarded-framework'),
            str(root / 'build/_deps/snesrecomp-src'),
        ]
        framework = next((Path(p) for p in candidates if p and (Path(p) / 'runner/src/snes/interp816.c').is_file()), None)
        compiler = shutil.which('gcc')
        if not compiler or not framework:
            self.skipTest('Requires GCC and the pinned framework')
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'cpu_preview_test.exe'
            build = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), '-I', str(framework / 'runner/src'),
                str(root / 'tests/cpu_preview_test.c'), str(root / 'src/cpu_preview.c'),
                str(framework / 'runner/src/snes/interp816.c'),
                '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


@unittest.skipUnless(os.environ.get('KDC_TEST_CPU') == '1', 'set KDC_TEST_CPU=1 for real-ROM CPU checks')
class CpuRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.rom = Path(os.environ.get('KDC_PRACTICE_ROM', cls.root / "Kirby's Dream Course (USA).sfc"))
        if hashlib.sha256(cls.rom.read_bytes()).hexdigest() != (
                '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'):
            raise ValueError('CPU checks require the exact supported ROM')
        cls.executable = Path(os.environ['KDC_PRACTICE_EXECUTABLE'])

    def test_natural_cpu_turns_are_repeatable(self):
        # Share the practice harness's isolated saves, controller suppression,
        # pinned runtime settings and private temporary captures.
        from test_practice import PracticeRuntimeTest
        tap = PracticeRuntimeTest.tap
        # Title menu, 1P VS CPU, then the original two-player screens: PLAY
        # COURSE, course 1, the order roll, and EXIT on the handicap grid.
        entry = 'wait 500\n' + tap('start') + 'wait 120\n' + tap('down') * 4 + tap('a') + 'wait 60\n'
        setup = ('wait 90\n' + tap('a')) * 4 + 'wait 400\n' + (tap('down') + 'wait 20\n') * 3 + tap('a')
        shot = tap('a') + 'wait 30\n' + tap('a') + 'wait 18\n' + tap('a')
        for difficulty in range(1, 4):
            with self.subTest(difficulty=difficulty):
                script = (entry + (tap('down') + 'wait 12\n') * (difficulty - 1) + tap('a') + setup +
                          'wait 1700\ndump before\n' + shot + 'wait 1500\ndump after\n')
                first, log = PracticeRuntimeTest.run_case(self, script, frames=6500)
                repeated, _ = PracticeRuntimeTest.run_case(self, script, frames=6500)
                self.assertEqual(first['after.wram.bin'], repeated['after.wram.bin'])
                before, ram = first['before.wram.bin'], first['after.wram.bin']
                word = lambda data, address: int.from_bytes(data[address:address + 2], 'little')
                name = ('EASY', 'MEDIUM', 'HARD')[difficulty - 1]
                self.assertIn('CPU match: started (' + name + ')', log)
                self.assertIn('CPU ' + name + ': turn 1', log)
                level = 3 - difficulty  # Easy takes the DIFFICULT end of the grid.
                self.assertEqual([word(before, a) for a in (0xecfd, 0xed01, 0xed05)], [level] * 3)
                self.assertEqual(word(ram, 0x1803), 0, 'CPU must return control to P1')
                self.assertGreater(word(ram, 0x6a12), 0, 'CPU must naturally collect a star')
                self.assertNotEqual(before[0xd8ac:0xd8bc], ram[0xd8ac:0xd8bc])
                if difficulty == 3:
                    fallback, _ = PracticeRuntimeTest.run_case(self, script, bounce='0', frames=6500)
                    self.assertEqual(first['after.wram.bin'], fallback['after.wram.bin'])

    def test_preview_runner_matches_bridge_oracle_across_holes(self):
        import re
        from test_practice import PracticeRuntimeTest
        tap = PracticeRuntimeTest.tap
        entry = 'wait 500\n' + tap('start') + 'wait 120\n' + tap('down') * 4 + tap('a') + 'wait 60\n'
        hard_entry = entry + (tap('down') + 'wait 12\n') * 2 + tap('a')
        shot = tap('a') + 'wait 30\n' + tap('a') + 'wait 18\n' + tap('a')
        courses = [
            ('course1_hole1', ('wait 90\n' + tap('a')) * 4),
            ('course2_hole1', 'wait 90\n' + tap('a') + 'wait 90\n' + tap('right') + 'wait 12\n' + tap('a') + ('wait 90\n' + tap('a')) * 2),
        ]
        seen_maps = set()
        total_checked = 0
        for label, course_pick in courses:
            with self.subTest(hole=label):
                setup = course_pick + 'wait 400\n' + (tap('down') + 'wait 20\n') * 3 + tap('a')
                script = hard_entry + setup + 'wait 1700\ndump before\n' + shot + 'wait 1500\ndump after\n'
                fast, fast_log = PracticeRuntimeTest.run_case(
                    self, script, frames=6500, extra_env={'KDC_CPU_PREVIEW_VERIFY': '1'})
                oracle, oracle_log = PracticeRuntimeTest.run_case(
                    self, script, frames=6500, extra_env={'KDC_CPU_PREVIEW_BRIDGE': '1'})
                map_id = int.from_bytes(fast['before.wram.bin'][0xabb2:0xabb4], 'little')
                self.assertGreater(map_id, 0)
                self.assertNotIn(map_id, seen_maps, f'{label} reused map_id={map_id}')
                seen_maps.add(map_id)
                fast_turn = [l for l in fast_log.splitlines() if 'CPU HARD: turn 1 ' in l]
                oracle_turn = [l for l in oracle_log.splitlines() if 'CPU HARD: turn 1 ' in l]
                self.assertEqual(len(fast_turn), 1)
                self.assertEqual(fast_turn, oracle_turn)
                turn_m = re.search(r'\((\d+) previews over (\d+) frames\)', fast_turn[0])
                self.assertIsNotNone(turn_m, fast_turn[0])
                expected_previews = int(turn_m.group(1))
                verify_m = re.search(
                    r'CPU verify: (\d+)/(\d+) candidates matched oracle \(\$6AEE \+ \$B746\.\.\$CF45\), mismatches=(\d+)',
                    fast_log)
                self.assertIsNotNone(verify_m, fast_log)
                matched, checked, mismatches = map(int, verify_m.groups())
                self.assertEqual(mismatches, 0)
                self.assertGreaterEqual(checked, 77)
                self.assertEqual(matched, checked)
                self.assertEqual(checked, expected_previews)
                total_checked += checked
                self.assertEqual(fast['after.wram.bin'], oracle['after.wram.bin'])
                self.assertEqual(fast['after.sram.bin'], oracle['after.sram.bin'])
        self.assertGreaterEqual(total_checked, 128)

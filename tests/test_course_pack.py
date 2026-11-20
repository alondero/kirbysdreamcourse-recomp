import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class CoursePackTest(unittest.TestCase):
    def test_hal_decompress_ips_loader_and_hooks(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        compiler = shutil.which('gcc')
        if not compiler or not (framework / 'runner/src/cpu_state.h').is_file():
            self.skipTest('Requires GCC and pinned framework (SNESRECOMP_ROOT)')
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'course_pack_test.exe'
            build = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), '-I', str(framework / 'runner/src'),
                str(root / 'tests/course_pack_test.c'),
                str(root / 'src/course_pack.c'),
                str(root / 'src/course_pack_runtime.c'),
                '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            scan_dir = Path(directory) / 'scan'
            scan_dir.mkdir()
            (scan_dir / 'subdir.ips').mkdir()
            run_args = [str(exe), str(scan_dir)]
            if hasattr(os, 'mkfifo'):
                fifo = scan_dir / '00_pipe.ips'
                os.mkfifo(fifo)
                run_args.append(str(fifo))
            result = subprocess.run(run_args, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


@unittest.skipUnless(os.environ.get('KDC_TEST_COURSE_PACK') == '1',
                     'set KDC_TEST_COURSE_PACK=1 for real-ROM extra-course checks')
class CoursePackRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.rom = Path(os.environ.get('KDC_PRACTICE_ROM', cls.root / "Kirby's Dream Course (USA).sfc"))
        if hashlib.sha256(cls.rom.read_bytes()).hexdigest() != (
                '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'):
            raise ValueError('Extra-course checks require the exact supported ROM')
        cls.executable = Path(os.environ.get('KDC_PRACTICE_EXECUTABLE',
            cls.root / 'local/guarded-build' /
            ('KirbyDreamCourseRecomp.exe' if os.name == 'nt' else 'KirbyDreamCourseRecomp')))
        ips_env = os.environ.get('KDC_TEST_GDC_IPS', '')
        cls.gdc_ips = Path(ips_env) if ips_env else None

    @staticmethod
    def tap(button):
        return f'press {button} 2\nwait 12\n'

    def run_case(self, script, extra_args=None, enable_cfg=False, copy_gdc=False,
                 bounce='1', frames=6500, run_cwd=None, seed_sram=None):
        with tempfile.TemporaryDirectory(prefix='course-pack-', dir=self.root / 'local') as directory:
            case = Path(directory)
            host = case / self.executable.name
            shutil.copy2(self.executable, host)
            (case / 'saves').mkdir()
            (case / 'saves/save.srm').write_bytes(seed_sram if seed_sram is not None else bytes(8192))
            if enable_cfg:
                (case / 'kdc-courses.cfg').write_text('1\n', encoding='ascii')
            if copy_gdc and self.gdc_ips and self.gdc_ips.is_file():
                (case / 'courses').mkdir()
                shutil.copy2(self.gdc_ips, case / 'courses' / self.gdc_ips.name)
            (case / 'config.ini').write_text(
                '[General]\nAutosave=0\nDisableFrameDelay=1\n'
                '[Graphics]\nOutputMethod=SDL-Software\nNewRenderer=1\nNoSpriteLimits=0\n'
                '[Sound]\nEnableAudio=1\nAudioFreq=32000\nAudioChannels=2\nAudioSamples=512\n'
                '[Controller]\nSourceP1=0\nSourceP2=0\n'
                '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
            (case / 'input.script').write_text(script + '\nquit\n')
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
            env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       SNESRECOMP_RUN_FRAMES=str(frames), SNESRECOMP_APU_BEAM_ELAPSED='1',
                       SNESRECOMP_APU_HANDOFF='1', SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1',
                       SNESRECOMP_LLE_BOUNCE=bounce, SNESRECOMP_DUMP_DIR=str(case))
            args = [str(host), '--no-launcher', '--rom', str(self.rom),
                    '--script', str(case / 'input.script')]
            if extra_args:
                args.extend(extra_args)
            result = subprocess.run(args, cwd=run_cwd or case, env=env, capture_output=True,
                                    text=True, errors='replace', timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('script quit', result.stderr)
            files = {str(p.relative_to(case)).replace('\\', '/'): p.read_bytes()
                     for p in case.rglob('*') if p.is_file() and p.suffix in ('.bin', '.srm', '.sav')}
            return files, result.stderr

    def test_solo_course_in_1p_vs_cpu(self):
        # Title -> 1P VS CPU -> HARD -> MORE COURSES -> 1P NORMAL Course 1 -> Order Roll -> Handicap EXIT -> Hole 1
        tap = self.tap
        script = ('wait 500\n' + tap('start') + 'wait 120\n' +
                  tap('down') * 4 + tap('a') + 'wait 60\n' +        # 1P VS CPU
                  (tap('down') + 'wait 12\n') * 2 + tap('a') +      # HARD
                  'wait 90\n' + tap('down') + tap('a') + 'wait 30\n' +  # MORE COURSES
                  tap('a') + tap('a') + tap('a') +                  # PACK 1P NORMAL, COURSE 1, PLAY
                  'wait 300\n' + tap('a') + 'wait 90\n' + tap('a') + # Order Roll
                  'wait 350\n' + (tap('down') + 'wait 20\n') * 3 + tap('a') +  # Handicap EXIT
                  'wait 1200\ndump loaded\n')
        native, log = self.run_case(script, enable_cfg=True, bounce='1', frames=6000)
        fallback, _ = self.run_case(script, enable_cfg=True, bounce='0', frames=6000)
        self.assertIn('Custom 2P (CPU): 1P NORMAL / COURSE 1', log)
        self.assertEqual(native['loaded.wram.bin'], fallback['loaded.wram.bin'])
        ram = native['loaded.wram.bin']
        word = lambda a: int.from_bytes(ram[a:a + 2], 'little')
        self.assertEqual(word(0x17ff), 1)   # 2P mode
        self.assertEqual(word(0xabb2), 0)   # USA 1P Course 1 Hole 1 (map 0)!
        self.assertEqual(word(0x8b92), 12)  # 12x12 layout
        self.assertEqual(word(0x8b94), 12)
        self.assertIn('saves/cpu/save.srm', native)

    def test_gdc_ips_in_dev_start_practice_and_2p(self):
        if not self.gdc_ips or not self.gdc_ips.is_file():
            self.skipTest('Set KDC_TEST_GDC_IPS to a local GrumpsDreamCourse[1.01].ips')
        # 1. Direct --course-pack + --dev-hole pack-2p:gdc:1:1 and pack:gdc:19:8
        for sel, mp, exp_w, exp_h in (
                ('pack-2p:gdc:1:1', 1, 10, 10),
                ('pack:gdc:19:8', 0, 13, 31)):
            with self.subTest(selector=sel):
                files, log = self.run_case('wait 600\ndump loaded\n',
                                           extra_args=['--course-pack', str(self.gdc_ips),
                                                       '--dev-hole', sel],
                                           frames=1200)
                self.assertIn('Course pack loaded', log)
                ram = files['loaded.wram.bin']
                word = lambda a: int.from_bytes(ram[a:a + 2], 'little')
                self.assertEqual(word(0x17ff), mp)
                self.assertEqual(word(0x8b92), exp_w)
                self.assertEqual(word(0x8b94), exp_h)

        # 2. Drop-in courses/ folder + 2P submenu -> MORE COURSES -> GRUMP'S DREAM -> Danny's Woods!
        tap = self.tap
        script = ('wait 500\n' + tap('start') + 'wait 120\n' +
                  tap('down') + tap('a') + 'wait 90\n' +            # 2P GAME
                  tap('down') + tap('a') + 'wait 30\n' +            # MORE COURSES
                  tap('right') * 2 + tap('a') +                     # PACK: GRUMP'S DREAM
                  tap('a') + tap('a') +                             # COURSE 1 (DANNY'S WOODS), PLAY
                  'wait 300\n' + tap('a') + 'wait 90\n' + tap('a') + # Order Roll
                  'wait 350\n' + (tap('down') + 'wait 20\n') * 3 + tap('a') +  # Handicap EXIT
                  'wait 1200\ndump loaded\n')
        native, log = self.run_case(script, enable_cfg=True, copy_gdc=True, bounce='1', frames=6000)
        fallback, _ = self.run_case(script, enable_cfg=True, copy_gdc=True, bounce='0', frames=6000)
        self.assertIn("Custom 2P: GRUMP'S DREAM / DANNY'S WOODS", log)
        self.assertEqual(native['loaded.wram.bin'], fallback['loaded.wram.bin'])
        ram = native['loaded.wram.bin']
        word = lambda a: int.from_bytes(ram[a:a + 2], 'little')
        self.assertEqual(word(0x17ff), 1)
        self.assertEqual(word(0x8b92), 10)  # Danny's Woods Hole 1 is 10x10
        self.assertEqual(word(0x8b94), 10)
        self.assertIn('saves/custom/save.srm', native)

    def test_gdc_practice_mode_and_cpu_turn(self):
        if not self.gdc_ips or not self.gdc_ips.is_file():
            self.skipTest('Set KDC_TEST_GDC_IPS to a local GrumpsDreamCourse[1.01].ips')
        tap = self.tap
        # 1. Practice Mode -> GRUMP'S 2P (mode 5) -> Course 2 (Barry & Kevin) -> Hole 1 -> Ability UFO
        practice_script = (
            'wait 500\n' + tap('start') + 'wait 120\n' +
            tap('down') * 3 + tap('a') + 'wait 30\n' +              # PRACTICE
            tap('right') * 5 + tap('a') +                           # MODE: GRUMP'S 2P
            tap('right') + tap('a') +                               # COURSE 2 (BARRY & KEVIN)
            tap('a') +                                              # HOLE 1
            tap('left') + tap('a') +                                # ABILITY: UFO (10)
            tap('a') + 'wait 600\ndump loaded\n'                    # PLAY THIS HOLE
        )
        pfiles, plog = self.run_case(practice_script, enable_cfg=True, copy_gdc=True, frames=3000)
        self.assertIn('Practice: pack-2p:gdc:2:1', plog)
        pram = pfiles['loaded.wram.bin']
        word = lambda b, a: int.from_bytes(b[a:a + 2], 'little')
        self.assertEqual(word(pram, 0x17ff), 1)
        self.assertEqual(word(pram, 0x8b92), 13)  # Barry & Kevin Hole 1 is 13x13
        self.assertEqual(word(pram, 0x8b94), 13)
        self.assertEqual(pram[0xd852], 10)        # Starting ability UFO applied
        self.assertIn('saves/practice/save.srm', pfiles)

        # 2. 1P VS CPU (HARD) -> MORE COURSES -> GRUMP'S DREAM -> Danny's Woods -> CPU plays turn 1!
        shot = tap('a') + 'wait 30\n' + tap('a') + 'wait 18\n' + tap('a')
        cpu_script = (
            'wait 500\n' + tap('start') + 'wait 120\n' +
            tap('down') * 4 + tap('a') + 'wait 60\n' +              # 1P VS CPU
            (tap('down') + 'wait 12\n') * 2 + tap('a') +            # HARD
            'wait 90\n' + tap('down') + tap('a') + 'wait 30\n' +    # MORE COURSES
            tap('right') * 2 + tap('a') +                           # PACK: GRUMP'S DREAM
            tap('a') + tap('a') +                                   # DANNY'S WOODS, PLAY
            'wait 300\n' + tap('a') + 'wait 90\n' + tap('a') +      # Order Roll
            'wait 350\n' + (tap('down') + 'wait 20\n') * 3 + tap('a') +  # Handicap EXIT
            'wait 1200\ndump before\n' + shot + 'wait 1600\ndump after\n'
        )
        cfiles, clog = self.run_case(cpu_script, enable_cfg=True, copy_gdc=True, frames=6500)
        self.assertIn("Custom 2P (CPU): GRUMP'S DREAM / DANNY'S WOODS", clog)
        self.assertIn('CPU HARD: turn 1', clog)
        self.assertEqual(word(cfiles['before.wram.bin'], 0x8b92), 10)
        self.assertEqual(word(cfiles['after.wram.bin'], 0x1803), 0, 'CPU must finish turn and return control to P1')
        self.assertNotEqual(cfiles['before.wram.bin'][0xd8ac:0xd8bc],
                            cfiles['after.wram.bin'][0xd8ac:0xd8bc])

    def test_dev_hole_pack_blocks_savestates_and_discovers_exe_dir(self):
        if not self.gdc_ips or not self.gdc_ips.is_file():
            self.skipTest('Set KDC_TEST_GDC_IPS to a local GrumpsDreamCourse[1.01].ips')
        word = lambda b, a: int.from_bytes(b[a:a + 2], 'little')
        # 1. Custom --dev-hole pack-2p must block scripted save-state writes.
        files, _ = self.run_case('wait 600\nsavestate 5\nwait 10\ndump loaded\n',
                                 extra_args=['--course-pack', str(self.gdc_ips),
                                             '--dev-hole', 'pack-2p:gdc:1:1'],
                                 frames=1000)
        self.assertNotIn('saves/save5.sav', files)
        self.assertEqual(word(files['loaded.wram.bin'], 0x8b92), 10)

        # 2. Launching from a sibling directory must discover courses/ beside
        #    the executable AND keep explicit --course-pack relative to caller.
        with tempfile.TemporaryDirectory(prefix='course-sibling-', dir=self.root / 'local') as sib:
            sibling = Path(sib)
            sib_files, sib_log = self.run_case(
                'wait 600\ndump loaded\n',
                extra_args=['--dev-hole', 'pack:gdc:1:1'],
                copy_gdc=True, frames=1000, run_cwd=sibling)
            self.assertIn('Course pack loaded', sib_log)
            self.assertEqual(word(sib_files['loaded.wram.bin'], 0x8b92), 10)
            self.assertEqual(word(sib_files['loaded.wram.bin'], 0x8b94), 10)

            local_ips = sibling / 'caller_gdc.ips'
            shutil.copy2(self.gdc_ips, local_ips)
            rel_files, rel_log = self.run_case(
                'wait 600\ndump loaded\n',
                extra_args=['--course-pack', 'caller_gdc.ips',
                            '--dev-hole', 'pack:gdc:19:8'],
                copy_gdc=False, frames=1000, run_cwd=sibling)
            self.assertIn('Course pack loaded', rel_log)
            self.assertEqual(word(rel_files['loaded.wram.bin'], 0x8b92), 13)
            self.assertEqual(word(rel_files['loaded.wram.bin'], 0x8b94), 31)

    def test_custom_2p_full_eight_hole_progression_and_finale(self):
        if not self.gdc_ips or not self.gdc_ips.is_file():
            self.skipTest('Set KDC_TEST_GDC_IPS to a local GrumpsDreamCourse[1.01].ips')
        tap = self.tap
        # Deterministic 8-hole progression and 2P finale transition replay on
        # Grump's Dream Course Course 1 (Danny's Woods): exercises every hole
        # load (holes 1..8), the 2P scoreboard/progression loop ($80:F48C..F4F0),
        # the 2P course finale ($80:F53C), return to $83:D518 (reopening the
        # custom picker), and B exit back to the ordinary title menu.
        script = (
            'wait 500\n' + tap('start') + 'wait 120\n' +
            'dump ordinary\n' +
            tap('down') + tap('a') + 'wait 90\n' +
            tap('down') + tap('a') + 'wait 30\n' +
            tap('right') * 2 + tap('a') +
            tap('a') + tap('a') +
            'wait 300\n' + tap('a') + 'wait 90\n' + tap('a') +
            'wait 350\n' + (tap('down') + 'wait 20\n') * 3 + tap('a') +
            'forcepoke da4a 0100\nforcepoke d876 01000100\n'
            'forcepoke d956 01000100\nforcepoke ecbb 80008000\n'
        )
        for h in range(8):
            script += f'until16 6a5e == {h:04x} 4000\nwait 40\ndump hole_{h}\n'
            if h < 7:
                script += 'wait 950\n' + (tap('a') + 'wait 40\n') * 8
            else:
                # Clear the 2P finale into $83:D518 without advancing past the
                # reopened picker, then exit to the ordinary title menu with B.
                script += 'wait 1200\n' + (tap('a') + 'wait 60\n') * 12 + 'wait 300\n' + tap('b') + 'wait 180\ndump title\n'
        files, log = self.run_case(script, enable_cfg=True, copy_gdc=True, frames=25000)
        word = lambda b, a: int.from_bytes(b[a:a + 2], 'little')
        expected_dims = [
            (10, 10), (10, 10), (15, 8), (13, 13),
            (14, 14), (12, 14), (20, 20), (15, 15),
        ]
        for h, (exp_w, exp_h) in enumerate(expected_dims):
            ram = files[f'hole_{h}.wram.bin']
            self.assertEqual(word(ram, 0x6a5e), h)
            self.assertEqual(word(ram, 0x6a5a), 16)
            self.assertEqual(word(ram, 0x8b92), exp_w)
            self.assertEqual(word(ram, 0x8b94), exp_h)
        self.assertIn("Custom 2P: GRUMP'S DREAM / DANNY'S WOODS", log)
        self.assertIn('Custom 2P: returned to course picker', log)
        self.assertIn('Custom 2P: returned to ordinary title', log)
        self.assertEqual(files['ordinary.sram.bin'], files['title.sram.bin'])
        self.assertEqual(files['ordinary.sram.bin'], files['saves/save.srm'])
        self.assertIn('saves/custom/save.srm', files)

    def test_dev_hole_pack_isolates_sram_and_preserves_ps1_catalog(self):
        if not self.gdc_ips or not self.gdc_ips.is_file():
            self.skipTest('Set KDC_TEST_GDC_IPS to a local GrumpsDreamCourse[1.01].ips')
        tap = self.tap
        # 1. Direct --dev-hole pack-2p:gdc:1:8 driven through the 2P finale must
        #    write battery changes to saves/custom/save.srm and leave ordinary
        #    saves/save.srm byte-identical.
        seeded = bytes([0xa5]) * 8192
        finale_script = (
            'wait 80\n'
            'forcepoke da4a 0100\nforcepoke d876 01000100\n'
            'forcepoke d956 01000100\nforcepoke ecbb 80008000\n'
            'wait 1200\n' + (tap('a') + 'wait 60\n') * 12 + 'wait 120\n'
            'dump post_finale\n'
        )
        files, _ = self.run_case(
            finale_script,
            extra_args=['--course-pack', str(self.gdc_ips),
                        '--dev-hole', 'pack-2p:gdc:1:8'],
            frames=4000, seed_sram=seeded)
        self.assertEqual(files['saves/save.srm'], seeded)
        self.assertIn('saves/custom/save.srm', files)
        self.assertNotEqual(files['saves/custom/save.srm'], seeded)

        # 2. tools/play_experimental.ps1 -Executable <install> -DevHole pack-2p:gdc:1:1
        #    must preserve drop-in catalog discovery from <install>/courses while
        #    keeping <install>/saves untouched.
        if os.name == 'nt':
            with tempfile.TemporaryDirectory(prefix='ps1-install-', dir=self.root / 'local') as inst:
                install_dir = Path(inst)
                inst_exe = install_dir / self.executable.name
                shutil.copy2(self.executable, inst_exe)
                (install_dir / 'courses').mkdir()
                shutil.copy2(self.gdc_ips, install_dir / 'courses' / self.gdc_ips.name)
                (install_dir / 'saves').mkdir()
                (install_dir / 'saves/save.srm').write_bytes(seeded)
                env = {k: v for k, v in os.environ.items()
                       if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
                env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
                res = subprocess.run([
                    'powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                    '-File', str(self.root / 'tools/play_experimental.ps1'),
                    '-Rom', str(self.rom),
                    '-Executable', str(inst_exe),
                    '-DevHole', 'pack-2p:gdc:1:1',
                    '-Frames', '120',
                ], cwd=self.root, env=env, capture_output=True, text=True,
                   errors='replace', timeout=120)
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                combined = res.stdout + res.stderr
                self.assertIn('Course pack loaded', combined)
                self.assertIn("GRUMP'S DREAM", combined)
                self.assertEqual((install_dir / 'saves/save.srm').read_bytes(), seeded)





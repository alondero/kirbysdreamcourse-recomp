import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'nt' and os.environ.get('KDC_TEST_LAUNCHER') == '1',
                     'set KDC_TEST_LAUNCHER=1 for local Windows startup checks')
class LauncherStartupTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.executable = cls.root / 'local/guarded-build/KirbyDreamCourseRecomp.exe'
        cls.rom = Path(os.environ.get('KDC_LAUNCHER_ROM', cls.root / "Kirby's Dream Course (USA).sfc"))
        cls.data = cls.rom.read_bytes()
        if hashlib.sha256(cls.data).hexdigest() != '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b':
            raise ValueError('Startup checks require the exact supported local ROM')

    def run_host(self, image, route):
        # All ROM-derived fixtures and captures stay in the ignored local tree.
        with tempfile.TemporaryDirectory(prefix='launcher-startup-', dir=self.root / 'local') as directory:
            case = Path(directory)
            executable = case / 'game.exe'
            shutil.copy2(self.executable, executable)
            rom = case / 'input.sfc'
            rom.write_bytes(image)
            (case / 'config.ini').write_text(
                '[General]\nSkipLauncher = 1\nAutosave = 0\n'
                '[Graphics]\nOutputMethod = SDL-Software\n'
                '[Sound]\nEnableAudio = 1\nAudioFreq = 32000\n'
                '[Controller]\nSourceP1 = 0\nSourceP2 = 0\n')
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('SNESRECOMP_', 'KDC_', 'LNG_', 'SDL_'))}
            env.update(SNESRECOMP_RUN_FRAMES='1', SDL_AUDIODRIVER='dummy',
                       SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                       SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1', SNESRECOMP_LLE_BOUNCE='1',
                       KDC_FRAME_LOG=str(case / 'cpu.log'))
            args = [str(executable)]
            if route == 'cached_skip':
                (case / 'rom.cfg').write_text(str(rom))
            else:
                args += ['--rom', str(rom)]
            if route == 'no_launcher':
                args += ['--no-launcher']
            result = subprocess.run(args, cwd=case, env=env, capture_output=True,
                                    text=True, errors='replace', timeout=45)
            cpu = (case / 'cpu.log').read_text() if (case / 'cpu.log').exists() else ''
            return result, cpu

    def test_supported_rom_runs_with_skip_launcher(self):
        result, cpu = self.run_host(self.data, 'explicit_skip')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(cpu.splitlines()), 1, result.stdout + result.stderr)

    def test_headered_and_modified_roms_are_refused_before_guest_execution(self):
        images = {'headered': bytes(512) + self.data,
                  'modified': self.data[:-1] + bytes([self.data[-1] ^ 1])}
        for kind, image in images.items():
            for route in ('explicit_skip', 'cached_skip', 'no_launcher'):
                with self.subTest(image=kind, route=route):
                    result, cpu = self.run_host(image, route)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('headerless 1 MiB', result.stderr)
                    self.assertEqual(cpu, '', result.stdout + result.stderr)

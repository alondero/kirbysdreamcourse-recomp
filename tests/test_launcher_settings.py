import os
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LauncherSettingsTest(unittest.TestCase):
    def test_widescreen_staging_and_commit(self):
        root = Path(__file__).resolve().parents[1]
        compiler = shutil.which('gcc')
        ui = Path(os.environ.get('RECOMP_UI_ROOT', root / 'local/guarded-build/_deps/kdc_recomp_ui-src'))
        if compiler is None or not (ui / 'src/recomp_launcher.h').is_file():
            self.skipTest('Requires GCC and pinned recomp-ui checkout; run tools/setup.ps1')
        with tempfile.TemporaryDirectory() as directory:
            rom = Path(directory) / 'synthetic.sfc'
            data = bytes(1048576)
            rom.write_bytes(data)
            headered = Path(directory) / 'headered.sfc'
            headered.write_bytes(bytes(512) + data)
            modified = Path(directory) / 'modified.sfc'
            modified.write_bytes(data[:-1] + b'\x01')
            for name, flags in (('debug', []), ('release', ['-DNDEBUG'])):
                with self.subTest(configuration=name):
                    executable = Path(directory) / f'launcher_settings_{name}.exe'
                    result = subprocess.run(
                        [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', *flags,
                         '-I', str(root / 'src'), '-I', str(ui / 'src'),
                         str(root / 'tests/launcher_settings_test.c'),
                         str(root / 'src/launcher_settings.c'),
                         str(ui / 'src/common/sha256.c'), '-o', str(executable)],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    args = [str(executable), str(rom), hashlib.sha256(data).hexdigest(),
                            str(headered), str(modified)]
                    result = subprocess.run(args, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    # A wrong expected hash must fail even in a release build.
                    args[2] = '0' * 64
                    result = subprocess.run(args, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('CHECK failed', result.stderr)

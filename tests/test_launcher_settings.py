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
            executable = Path(directory) / 'launcher_settings_test.exe'
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root / 'src'), '-I', str(ui / 'src'),
                            str(root / 'tests/launcher_settings_test.c'),
                            str(root / 'src/launcher_settings.c'),
                            str(ui / 'src/common/sha256.c'), '-o', str(executable)],
                           check=True, capture_output=True)
            rom = Path(directory) / 'synthetic.sfc'
            data = bytes(1048576)
            rom.write_bytes(data)
            headered = Path(directory) / 'headered.sfc'
            headered.write_bytes(bytes(512) + data)
            modified = Path(directory) / 'modified.sfc'
            modified.write_bytes(data[:-1] + b'\x01')
            subprocess.run([str(executable), str(rom), hashlib.sha256(data).hexdigest(),
                            str(headered), str(modified)],
                           check=True, capture_output=True)

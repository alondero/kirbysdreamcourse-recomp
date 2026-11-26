import os
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


class LauncherSettingsTest(unittest.TestCase):
    def test_widescreen_staging_and_commit(self):
        root = Path(__file__).resolve().parents[1]
        compiler = shutil.which('gcc')
        ui = Path(os.environ.get('RECOMP_UI_ROOT', root / 'local/guarded-build/_deps/kdc_recomp_ui-src'))
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        if (compiler is None or not (ui / 'src/recomp_launcher.h').is_file() or
                not (framework / 'runner/src/snes/ppu.h').is_file()):
            self.skipTest('Requires GCC and the patched launcher and framework; run tools/setup.ps1')
        with tempfile.TemporaryDirectory() as directory:
            case = Path(directory)
            rom = Path(directory) / 'synthetic.sfc'
            data = bytes(1048576)
            rom.write_bytes(data)
            headered = Path(directory) / 'headered.sfc'
            headered.write_bytes(bytes(512) + data)
            modified = Path(directory) / 'modified.sfc'
            pack = (b'KDCHDUI1' + struct.pack('<IIII', 1, 1, 4, 0) +
                    struct.pack('<QHBBI', 0, 65535, 4, 4, 40) + bytes(32 * 32 * 5))
            (case / 'interface é.pack').write_bytes(pack)
            (case / 'unsupported.pack').write_bytes(pack[:8] + b'\x02' + pack[9:])
            (case / 'malformed.pack').write_bytes(pack[:-1])
            env = {k: v for k, v in os.environ.items() if k.upper() != 'KDC_HD_PACK'}
            for name, flags in (('debug', []), ('release', ['-DNDEBUG'])):
                with self.subTest(configuration=name):
                    rom.write_bytes(data)
                    (case / 'vanishing.pack').write_bytes(pack)
                    modified.write_bytes(data[:-1] + b'\x01')
                    for image in (rom, modified):
                        os.utime(image, (1_600_000_000, 1_600_000_000))
                    executable = Path(directory) / f'launcher_settings_{name}.exe'
                    result = subprocess.run(
                        [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', *flags,
                         '-I', str(root / 'src'), '-I', str(ui / 'src'),
                         '-I', str(framework / 'runner/src'),
                         str(root / 'tests/launcher_settings_test.c'),
                         str(root / 'src/launcher_settings.c'),
                         str(root / 'src/course_pack.c'),
                         str(root / 'src/hd_pack.c'),
                         str(ui / 'src/common/sha256.c'), '-Wl,--wrap=recompui_sha256_compute',
                         '-o', str(executable)],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    args = [str(executable), str(rom), hashlib.sha256(data).hexdigest(),
                            str(headered), str(modified)]
                    result = subprocess.run(args, capture_output=True, text=True, cwd=directory, env=env)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('Invalid HD interface pack.', result.stderr)
                    self.assertEqual((Path(directory) / 'kdc-widescreen.cfg').read_text().strip(), '21:9')
                    self.assertEqual((Path(directory) / 'kdc-hd.cfg').read_text().strip(), '0')
                    pack_config = case / 'kdc-hd-ui.cfg'
                    self.assertEqual(pack_config.read_text(encoding='utf-8'), '1\ninterface é.pack')
                    self.assertEqual((Path(directory) / 'kdc-transitions.cfg').read_text().strip(), '1')
                    self.assertEqual((Path(directory) / 'kdc-courses.cfg').read_text().strip(), '1')
                    rom.write_bytes(data)
                    reloaded = subprocess.run(args + ['reload', 'check'], capture_output=True,
                                              text=True, cwd=directory, env=env)
                    self.assertEqual(reloaded.returncode, 0, reloaded.stdout + reloaded.stderr)
                    cleared = subprocess.run(args + ['clear', 'check'], capture_output=True,
                                             text=True, cwd=directory, env=env)
                    self.assertEqual(cleared.returncode, 0, cleared.stdout + cleared.stderr)
                    self.assertEqual(pack_config.read_bytes(), b'1\n')
                    # Exercise the override on an install with no pack settings.
                    pack_config.unlink()
                    overridden = subprocess.run(args + ['override', 'check'], capture_output=True,
                                                text=True, cwd=directory,
                                                env={**env, 'KDC_HD_PACK': 'interface é.pack'})
                    self.assertEqual(overridden.returncode, 0, overridden.stdout + overridden.stderr)
                    pack_config.unlink()
                    (Path(directory) / 'kdc-courses.cfg').unlink()
                    (Path(directory) / 'kdc-transitions.cfg').unlink()
                    (Path(directory) / 'kdc-widescreen.cfg').unlink()
                    self.assertEqual((Path(directory) / 'kdc-hud.cfg').read_text().strip(), 'edges')
                    (Path(directory) / 'kdc-hud.cfg').unlink()
                    self.assertEqual((Path(directory) / 'kdc-presentation.cfg').read_text().strip(), '1 144')
                    (Path(directory) / 'kdc-presentation.cfg').unlink()
                    (Path(directory) / 'kdc-hd.cfg').unlink()
                    # A wrong expected hash must fail even in a release build.
                    args[2] = '0' * 64
                    result = subprocess.run(args, capture_output=True, text=True, cwd=directory)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('CHECK failed', result.stderr)
                    # Closing an in-session launcher ignores a refused commit;
                    # the provider must still report that its edit was rejected.
                    args[2] = hashlib.sha256(data).hexdigest()
                    args.append('remove-before-commit')
                    rom.write_bytes(data)
                    result = subprocess.run(args, capture_output=True, text=True, cwd=directory)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('Launcher feature settings were not applied:', result.stderr)
                    self.assertIn('headerless 1 MiB', result.stderr)
                    rom.write_bytes(data)

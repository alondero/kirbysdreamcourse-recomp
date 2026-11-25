"""Run the framework's real bridge suite with a diagnostic lookup counter."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class BridgeDiagnosticCacheTest(unittest.TestCase):
    def test_repeated_entries_read_diagnostic_once_absent_empty_or_enabled(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        source = framework / 'runner/src'
        compiler = shutil.which('gcc')
        if not compiler or not (framework / 'tests/interp816/bridge_test.c').exists():
            self.skipTest('Requires GCC and the patched pinned framework')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'bridge-cache-test.exe'
            command = [compiler, '-std=c11', '-O1', '-DSNESRECOMP_TIER2_TEST=1',
                       '-DSNESRECOMP_APU_HANDOFF_CACHE_PROBE=1',
                       f'-I{source}', f'-I{source / "snes"}',
                       str(framework / 'tests/interp816/bridge_test.c'),
                       str(source / 'snes/interp816.c'), str(source / 'sha256.c'),
                       str(source / 'snes/tier2_capture.c'), str(source / 'snes/interp_bridge.c'),
                       str(source / 'snes/cx4.c'), str(root / 'tests/bridge_diagnostic_cache_probe.c'),
                       '-Wl,--wrap=getenv', '-lm', '-o', str(executable)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
            for value in (None, '', '1'):
                with self.subTest(value=value):
                    run_env = dict(env)
                    if value is not None:
                        run_env['SNESRECOMP_WLOG_STATE'] = value
                    result = subprocess.run([str(executable)], env=run_env, cwd=directory,
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()

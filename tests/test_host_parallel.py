import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class HostParallelTest(unittest.TestCase):
    def test_every_piece_runs_once_across_back_to_back_jobs(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free C fixture')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'host_parallel_test.exe'
            result = subprocess.run([compiler, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(root / 'src'), str(root / 'tests/host_parallel_test.c'),
                                     str(root / 'src/host_parallel.c'), '-lpthread',
                                     '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            # Exercise both the pool and the forced inline path.
            for threads in ('4', '1'):
                env = dict(os.environ, KDC_THREADS=threads)
                result = subprocess.run([str(executable)], capture_output=True, text=True,
                                        env=env, timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)

    def test_every_fixture_building_the_pool_links_threads(self):
        # POSIX builds of host_parallel.c need the thread library. glibc 2.34
        # and newer include it in libc, so a missing flag only fails on older
        # or other C libraries: check the build commands directly.
        root = Path(__file__).resolve().parents[1]
        scripts = sorted((root / 'tests').glob('*.py')) + sorted((root / 'tools').glob('*.py'))
        missing = [script.relative_to(root).as_posix() for script in scripts
                   if script != Path(__file__).resolve()
                   and re.search(r'host_parallel\.c[\'"]', script.read_text(encoding='utf-8'))
                   and not re.search(r'[\'"]-lpthread[\'"]', script.read_text(encoding='utf-8'))]
        self.assertEqual(missing, [], 'compile host_parallel.c without linking threads')


if __name__ == '__main__':
    unittest.main()

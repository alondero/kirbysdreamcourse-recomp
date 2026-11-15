"""Measure cache pressure and the following frame without ROM data (requires GCC)."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--framework', type=Path, default=Path(
        os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework')))
    args = parser.parse_args()
    source = args.framework.resolve(strict=True) / 'runner/src'
    compiler = shutil.which('gcc')
    if not compiler:
        parser.error('GCC is required')
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / 'hd_cache_benchmark.exe'
        subprocess.run([compiler, '-std=c11', '-O2', '-ffunction-sections', '-fdata-sections',
                        '-Wl,--gc-sections', '-I', str(source), '-I', str(root / 'src'),
                        str(root / 'tests/hd_cache_benchmark.c'),
                        str(source / 'snes/ws_shadow.c'), str(source / 'snes/ppu_legacy.c'),
                        *[str(root / 'src' / name) for name in
                          ('hd_tile_source.c', 'hd_scaler.c', 'course_checker.c')],
                        '-lm', '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    print(json.dumps(rows, indent=2))


if __name__ == '__main__':
    main()

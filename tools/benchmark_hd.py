"""Measure HD raster cost on a deterministic course; keep captures in local/."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--rom', type=Path, default=Path("Kirby's Dream Course (USA).sfc"))
    parser.add_argument('--display', action='store_true', help='Use the real display instead of SDL dummy')
    parser.add_argument('--native-width', action='store_true', help='Disable widescreen')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--max-raster-ms', type=float, help='Fail if any HD raster mean exceeds this budget')
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
    rom = args.rom.resolve(strict=True)
    if hashlib.sha256(rom.read_bytes()).hexdigest() != '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b':
        parser.error('Unsupported ROM SHA-256')
    root = Path(__file__).resolve().parents[1]
    (root / 'local').mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='hd-benchmark-', dir=root / 'local'))
    print(output, flush=True)
    builds = [('candidate', args.executable.resolve(strict=True))]
    if args.baseline:
        builds.insert(0, ('baseline', args.baseline.resolve(strict=True)))
    results = []
    for repeat in range(args.repeat):
        captures = {}
        for name, executable in builds:
            for hd in (0, 1):
                case = output / f'{repeat}-{name}-hd{hd}'
                case.mkdir()
                host = case / executable.name
                shutil.copy2(executable, host)
                for dll in executable.parent.glob('*.dll'):
                    shutil.copy2(dll, case / dll.name)
                (case / 'config.ini').write_text(
                    '[General]\nAutosave=0\nDisableFrameDelay=1\n'
                    '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\n'
                    f'Widescreen={int(not args.native_width)}\nWindowSize=1536x896\nVSync=0\n'
                    '[Sound]\nEnableAudio=0\n[Controller]\nSourceP1=0\nSourceP2=0\n'
                    '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
                env = {k: v for k, v in os.environ.items()
                       if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
                env.update(SDL_AUDIODRIVER='dummy', SNESRECOMP_RUN_FRAMES='605',
                           KDC_HD=str(hd), SNESRECOMP_HOST_PROFILE='1',
                           SNESRECOMP_HOST_PROFILE_START_FRAME='350',
                           SNESRECOMP_SCREENSHOT=str(case / 'frame.ppm'),
                           # Capture before timing so PPM encoding/I/O cannot
                           # inflate composition time, especially with HD.
                           SNESRECOMP_SCREENSHOT_FRAME='340')
                if not args.display:
                    env['SDL_VIDEODRIVER'] = 'dummy'
                with (case / 'run.log').open('w') as log:
                    subprocess.run([str(host), '--no-launcher', '--rom', str(rom),
                                    '--dev-hole', 'normal:1:1'], cwd=case, env=env,
                                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
                log = (case / 'run.log').read_text()
                stages = {stage: float(mean) for stage, mean in re.findall(
                    r'video profile: stage=(\S+) count=\d+ total_ms=[\d.]+ mean_ms=([\d.]+)', log)}
                if 'raster-capture' not in stages:
                    raise AssertionError(f'{case}: missing host profile')
                captures[name, hd] = hashlib.sha256((case / 'frame.ppm').read_bytes()).hexdigest()
                result = dict(repeat=repeat, build=name, hd=hd, mean_ms=stages,
                              screenshot_sha256=captures[name, hd])
                results.append(result)
                (output / 'results.json').write_text(json.dumps(results, indent=2))
                print(f'{repeat} {name} HD={hd}: raster={stages["raster-capture"]:.3f} ms '
                      f'compose={stages["compose"]:.3f} ms present={stages["upload-present"]:.3f} ms', flush=True)
                if name == 'candidate' and hd and args.max_raster_ms is not None:
                    assert stages['raster-capture'] <= args.max_raster_ms, 'HD raster budget exceeded'
        if args.baseline:
            for hd in (0, 1):
                assert captures['baseline', hd] == captures['candidate', hd], f'HD={hd}: image changed'
    print('Completed; baseline images match.' if args.baseline else 'Completed.')


if __name__ == '__main__':
    main()

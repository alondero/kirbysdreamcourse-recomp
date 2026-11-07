"""Replay a private ROM and compare default output/state and high-refresh presents.

All recordings, images and state remain in a fresh ignored local/ directory.
This uses SDL's dummy backend: it checks composition and scheduling, not a monitor.
"""
import argparse
import csv
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--rates', nargs='+', type=int, default=[120, 144, 240])
    parser.add_argument('--pictures', action='store_true')
    parser.add_argument('--benchmark-only', action='store_true',
                        help='Profile frames 500-900 without state/image tracing; no fidelity claims')
    args = parser.parse_args()
    if not args.benchmark_only and not args.baseline:
        parser.error('--baseline is required for fidelity validation')
    if args.benchmark_only and args.pictures:
        parser.error('--benchmark-only excludes image capture')
    rom = args.rom.resolve(strict=True)
    if hashlib.sha256(rom.read_bytes()).hexdigest() != '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b':
        parser.error('Unsupported ROM SHA-256')
    if any(rate not in (90, 120, 144, 165, 240, 360) for rate in args.rates):
        parser.error('Choose supported high-refresh rates')
    root = Path(__file__).resolve().parents[1]
    (root / 'local').mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='high-refresh-validation-', dir=root / 'local'))
    print(output, flush=True)
    base_env = {k: v for k, v in os.environ.items()
                if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
    results = []

    def run(name, executable, rate):
        case = output / name
        case.mkdir()
        host = case / executable.name
        shutil.copy2(executable, host)
        (case / 'config.ini').write_text(
            '[General]\nAutosave=0\nDisableFrameDelay=0\n'
            '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\nWidescreen=0\n'
            '[Sound]\nEnableAudio=1\nAudioFreq=32000\n'
            '[Controller]\nSourceP1=0\nSourceP2=0\n'
            '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
        (case / 'kdc-presentation.cfg').write_text(f'{int(rate > 0)} {rate}\n')
        (case / 'shot.script').write_text('wait 600\npress a 2\nwait 57\npress a 2\nwait 27\npress a 2\n')
        (case / 'camera.script').write_text('500 32767 0 0\n560 0 0 0\n800 0 0 1\n')
        env = dict(base_env, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   SNESRECOMP_RUN_FRAMES='900', SNESRECOMP_LLE_BOUNCE='1',
                   SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                   SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1',
                   KDC_CAMERA_SCRIPT=str(case / 'camera.script'),
                   KDC_PRESENTATION_LOG=str(case / 'motion.csv'),
                   KDC_PRESENTATION_DRAW_LOG=str(case / 'draw.csv'),
                   SNESRECOMP_PRESENT_LOG=str(case / 'presents.csv'),
                   SNESRECOMP_FRAME_TIMING=str(case / 'timing.csv'),
                   SNESRECOMP_HOST_PROFILE='1', SNESRECOMP_STATE_TRACE=str(case / 'state.csv'),
                   SNESRECOMP_FRAMEDUMP_START='790', SNESRECOMP_FRAMEDUMP_END='899',
                   SNESRECOMP_FRAMEDUMP_PIXELS='1')
        if args.pictures:
            (case / 'pictures').mkdir()
            env.update(SNESRECOMP_SCREENSHOT_DIR=str(case / 'pictures'),
                       SNESRECOMP_SCREENSHOT_FROM='850', SNESRECOMP_SCREENSHOT_TO='870')
        if args.benchmark_only:
            for key in ('KDC_PRESENTATION_LOG', 'KDC_PRESENTATION_DRAW_LOG',
                        'SNESRECOMP_PRESENT_LOG', 'SNESRECOMP_STATE_TRACE',
                        'SNESRECOMP_FRAMEDUMP_START', 'SNESRECOMP_FRAMEDUMP_END',
                        'SNESRECOMP_FRAMEDUMP_PIXELS'):
                env.pop(key, None)
            env['SNESRECOMP_HOST_PROFILE_START_FRAME'] = '500'
        command = [str(host), '--no-launcher', '--rom', str(rom),
                   '--dev-hole', 'normal:1:1', '--script', str(case / 'shot.script')]
        if not args.benchmark_only:
            command += ['--framedump', str(case / 'frames')]
        with (case / 'run.log').open('w') as log:
            subprocess.run(command, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
        print(f'{name}: replay finished', flush=True)
        return case

    if args.benchmark_only:
        for rate in [0, *args.rates]:
            case = run(f'hz-{rate}', args.executable.resolve(strict=True), rate)
            match = re.search(r'video profile window: first=(\d+) last=(\d+) seconds=([0-9.]+) presentations=(\d+)',
                              (case / 'run.log').read_text())
            if not match:
                raise AssertionError(f'{case}: missing host profile')
            elapsed = float(match[3])
            results.append(dict(rate=rate, presentation_hz=int(match[4]) / elapsed,
                                simulation_hz=(int(match[2]) - int(match[1]) + 1) / elapsed))
        (output / 'benchmark.json').write_text(json.dumps(results, indent=2) + '\n')
        print(json.dumps(results, indent=2))
        return

    def equal(path, reference):
        if path.read_bytes() != reference.read_bytes():
            if path.name == 'state.csv':
                actual, expected = path.read_text().splitlines(), reference.read_text().splitlines()
                for i in range(max(len(actual), len(expected))):
                    a = actual[i] if i < len(actual) else '<missing>'
                    b = expected[i] if i < len(expected) else '<missing>'
                    if a != b:
                        raise AssertionError(f'First state divergence at row {i + 1}: {a} vs {b}; '
                                             f'preceding input: {path.parent / "shot.script"}')
            raise AssertionError(f'First comparison divergence: {path} versus {reference}')

    baseline = run('baseline', args.baseline.resolve(strict=True), 0)
    first_runs = {}
    cases = [('off', 0), ('off-repeat', 0)]
    cases += [(f'hz-{r}{suffix}', r) for r in args.rates for suffix in ('', '-repeat')]
    for name, rate in cases:
        case = run(name, args.executable.resolve(strict=True), rate)
        equal(case / 'state.csv', baseline / 'state.csv')
        equal(case / 'saves/save.srm', baseline / 'saves/save.srm')
        if rate in first_runs:
            equal(case / 'state.csv', first_runs[rate] / 'state.csv')
        else:
            first_runs[rate] = case
        for frame in range(790, 900):
            stem = f'frame_{frame:06}'
            equal(case / 'frames' / f'{stem}_wram.bin', baseline / 'frames' / f'{stem}_wram.bin')
            if not rate:
                equal(case / 'frames' / f'{stem}.bmp', baseline / 'frames' / f'{stem}.bmp')
        with (case / 'presents.csv').open() as stream:
            presents = list(csv.DictReader(stream))
        by_frame = {}
        for row in presents:
            by_frame.setdefault(int(row['frame']), set()).add(row['crc32'])
        moving_camera = sum(len(hashes) > 1 for frame, hashes in by_frame.items() if 500 <= frame < 560)
        moving_objects = sum(len(hashes) > 1 for frame, hashes in by_frame.items() if 835 <= frame < 900)
        interpolated_object_draws = 0
        if rate:
            with (case / 'draw.csv').open() as stream:
                interpolated_object_draws = sum(835 <= int(row['frame']) < 900 and
                                               int(row['interpolated_objects']) > 0
                                               for row in csv.DictReader(stream))
            if not interpolated_object_draws:
                raise AssertionError(f'{name}: no object geometry was interpolated during the shot')
        if rate and not moving_objects:
            raise AssertionError(f'{name}: no intermediate object-motion images')
        if rate and not args.pictures and not moving_camera:
            raise AssertionError(f'{name}: no intermediate camera images')
        results.append(dict(case=name, rate=rate, presents=len(presents),
                            camera_frames_with_intermediate_images=moving_camera,
                            object_frames_with_intermediate_images=moving_objects,
                            interpolated_object_draws=interpolated_object_draws,
                            state_equal=True, default_pixels_equal=True if not rate else None))
        (output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()

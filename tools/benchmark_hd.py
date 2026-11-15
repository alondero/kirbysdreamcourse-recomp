"""Measure HD raster cost on a deterministic course; keep captures in local/."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


# A still course view; a scripted shot (ball rolls, camera follows); the same
# shot while the orbit view turns, which also enables high refresh.
SHOT = 'wait 600\npress a 2\nwait 57\npress a 2\nwait 27\npress a 2\n'
SCENARIOS = {
    'still': dict(frames=605, screenshot=340, script=None, camera=None),
    'shot': dict(frames=1100, screenshot=800, script=SHOT, camera=None),
    'orbit': dict(frames=1100, screenshot=800, script=SHOT,
                  camera='500 32767 0 0\n700 0 32767 0\n900 -32767 0 0\n'),
}


def distribution(samples):
    """Nearest-rank percentiles; each sample is (frame, milliseconds)."""
    if not samples:
        raise ValueError('No timing samples')
    ordered = sorted(value for _, value in samples)
    frame, peak = max(samples, key=lambda sample: sample[1])
    return dict(count=len(samples), mean=sum(ordered) / len(ordered),
                **{f'p{p}': ordered[math.ceil(len(ordered) * p / 100) - 1]
                   for p in (50, 95, 99)}, max=peak, max_frame=frame,
                over_budget={str(budget): sum(value > budget for value in ordered)
                             for budget in (1000 / 60, 1000 / 30, 50, 100)})


def frame_timings(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) < 2:
        raise AssertionError(f'{path}: missing per-presentation timing samples')
    stages = [key for key in rows[0] if key.endswith('_ms')]
    samples = {key: [(int(row['frame']), float(row[key])) for row in rows]
               for key in stages}
    # The first presentation has no preceding timestamp in the trace. Drop
    # that interval rather than treating the profile's start as a presentation.
    samples['interval_ms'] = [(int(row['frame']), 1000 * (
        float(row['present_seconds']) - float(previous['present_seconds'])))
        for previous, row in zip(rows, rows[1:])]
    samples['work_ms'] = [(int(row['frame']), sum(float(row[key]) for key in stages
                                               if key != 'wait_ms')) for row in rows]
    counters = [key for key in rows[0] if key.startswith('hd_')]
    events = []
    for index, (previous, row) in enumerate(zip(rows, rows[1:]), start=1):
        delta = {key: int(row[key]) - int(previous[key]) for key in counters
                 if key != 'hd_cached_tiles'}
        if delta.get('hd_cache_clears', 0) or delta.get('hd_cache_trims', 0):
            events.append(dict(frame=int(row['frame']),
                               raster_ms=float(row['raster_ms']),
                               interval_ms=1000 * (float(row['present_seconds']) -
                                                  float(previous['present_seconds'])),
                               following_raster_ms=(float(rows[index + 1]['raster_ms'])
                                                    if index + 1 < len(rows) else None), **delta))
    return dict(distributions={key: distribution(values) for key, values in samples.items()},
                hd_counters={key: int(rows[-1][key]) - int(rows[0][key]) for key in counters
                             if key != 'hd_cached_tiles'}, cache_pressure_events=events)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--rom', type=Path, default=Path("Kirby's Dream Course (USA).sfc"))
    parser.add_argument('--display', action='store_true', help='Use the real display instead of SDL dummy')
    parser.add_argument('--native-width', action='store_true', help='Disable widescreen')
    parser.add_argument('--scenario', choices=SCENARIOS, default='still')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--max-raster-ms', type=float, help='Fail if any HD raster mean exceeds this budget')
    parser.add_argument('--max-compose-ms', type=float, help='Fail if HD composition mean exceeds this budget')
    parser.add_argument('--threads', type=int, choices=range(1, 17), help='Fix the rendering worker count')
    parser.add_argument('--hole', default='normal:1:1', help='Developer course start, e.g. normal:8:8')
    parser.add_argument('--frames', type=int, help='Override scenario length, e.g. 3600 for cache pressure')
    parser.add_argument('--profile-start', type=int, default=350, help='Use 1 to include course loading')
    parser.add_argument('--hd', type=int, choices=(0, 1), nargs='+', default=[0, 1])
    parser.add_argument('--no-captures', action='store_true', help='Timing only; omit the separate image replay')
    parser.add_argument('--max-p99-ms', type=float, help='Fail if HD presentation interval p99 exceeds this budget')
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
    scenario = SCENARIOS[args.scenario]
    frames = args.frames if args.frames is not None else scenario['frames']
    if not 1 <= args.profile_start < frames or frames > 36000:
        parser.error('Require 1 <= profile start < frames <= 36000 (host trace capacity)')
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
            for hd in args.hd:
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
                env.update(SDL_AUDIODRIVER='dummy', KDC_HD=str(hd),
                           SNESRECOMP_SCREENSHOT=str(case / 'frame.ppm'),
                           SNESRECOMP_SCREENSHOT_FRAME=str(scenario['screenshot']))
                if args.threads is not None:
                    env['KDC_THREADS'] = str(args.threads)
                command = [str(host), '--no-launcher', '--rom', str(rom), '--dev-hole', args.hole]
                if scenario['script']:
                    (case / 'shot.script').write_text(scenario['script'])
                    command += ['--script', str(case / 'shot.script')]
                if scenario['camera']:
                    (case / 'camera.script').write_text(scenario['camera'])
                    env['KDC_CAMERA_SCRIPT'] = str(case / 'camera.script')
                if not args.display:
                    env['SDL_VIDEODRIVER'] = 'dummy'
                # Encoding a 4x PPM takes hundreds of milliseconds. Capture
                # in a separate replay, including when profiling from boot.
                timed = dict(env, SNESRECOMP_RUN_FRAMES=str(frames),
                             SNESRECOMP_HOST_PROFILE='1',
                             SNESRECOMP_HOST_PROFILE_START_FRAME=str(args.profile_start),
                             SNESRECOMP_FRAME_TIMING=str(case / 'timing.csv'))
                del timed['SNESRECOMP_SCREENSHOT'], timed['SNESRECOMP_SCREENSHOT_FRAME']
                if not args.no_captures:
                    capture = dict(env, SNESRECOMP_RUN_FRAMES=str(scenario['screenshot'] + 5))
                    with (case / 'capture.log').open('w') as log:
                        subprocess.run(command, cwd=case, env=capture,
                                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
                with (case / 'run.log').open('w') as log:
                    subprocess.run(command, cwd=case, env=timed,
                                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
                log = (case / 'run.log').read_text()
                profile = re.findall(r'video profile: stage=(\S+) count=\d+ total_ms=[\d.]+ '
                                     r'mean_ms=([\d.]+) max_ms=([\d.]+) max_frame=(\d+)', log)
                stages = {stage: float(mean) for stage, mean, _, _ in profile}
                maxima = {stage: (float(peak), int(frame)) for stage, _, peak, frame in profile}
                fps = re.search(r'frame service: .* fps=([\d.]+)', log)
                if 'raster-capture' not in stages or not fps:
                    raise AssertionError(f'{case}: missing host profile')
                captures[name, hd] = (None if args.no_captures else
                    hashlib.sha256((case / 'frame.ppm').read_bytes()).hexdigest())
                timing = frame_timings(case / 'timing.csv')
                renderer = re.search(r'^Renderer: (.+)$', log, re.MULTILINE)
                result = dict(repeat=repeat, build=name, hd=hd, scenario=args.scenario,
                              hole=args.hole, threads=args.threads,
                              frames=frames, profile_start=args.profile_start, display=args.display,
                              renderer=renderer[1] if renderer else None,
                              fps=float(fps[1]), mean_ms=stages, max_ms=maxima,
                              **timing,
                              screenshot_sha256=captures[name, hd])
                results.append(result)
                (output / 'results.json').write_text(json.dumps(results, indent=2))
                print(f'{repeat} {name} HD={hd}: raster={stages["raster-capture"]:.3f} ms '
                      f'compose={stages["compose"]:.3f} ms (max {maxima["compose"][0]:.1f} ms '
                      f'at {maxima["compose"][1]}) present={stages["upload-present"]:.3f} ms '
                      f'fps={float(fps[1]):.1f}', flush=True)
                intervals = timing['distributions']['interval_ms']
                print(f'  interval p50/p95/p99/max: {intervals["p50"]:.3f}/'
                      f'{intervals["p95"]:.3f}/{intervals["p99"]:.3f}/{intervals["max"]:.3f} ms; '
                      f'cache clears/trims: {timing["hd_counters"].get("hd_cache_clears", "unavailable")}/'
                      f'{timing["hd_counters"].get("hd_cache_trims", "unavailable")}', flush=True)
                if name == 'candidate' and hd and args.max_raster_ms is not None:
                    assert stages['raster-capture'] <= args.max_raster_ms, 'HD raster budget exceeded'
                if name == 'candidate' and hd and args.max_compose_ms is not None:
                    assert stages['compose'] <= args.max_compose_ms, 'HD composition budget exceeded'
                if name == 'candidate' and hd and args.max_p99_ms is not None:
                    assert intervals['p99'] <= args.max_p99_ms, 'HD presentation p99 budget exceeded'
        if args.baseline and not args.no_captures:
            for hd in args.hd:
                assert captures['baseline', hd] == captures['candidate', hd], f'HD={hd}: image changed'
    print('Completed; baseline images match.' if args.baseline and not args.no_captures else 'Completed.')


if __name__ == '__main__':
    main()

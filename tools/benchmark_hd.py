"""Measure HD raster cost on a deterministic course; keep captures in local/."""
import argparse
import csv
import hashlib
from itertools import zip_longest
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
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
# Match direct-launch guarded releases, including for development executables.
# Do not inherit timing/bounce policy from the invoking shell.
RUNTIME_ENV = dict(SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                   SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1', SNESRECOMP_LLE_BOUNCE='1')


def distribution(samples):
    """Nearest-rank percentiles; each sample is (frame, milliseconds)."""
    if not samples:
        raise ValueError('No timing samples')
    ordered = sorted(value for _, value in samples)
    frame, peak = max(samples, key=lambda sample: sample[1])
    return dict(count=len(samples), mean=sum(ordered) / len(ordered),
                **{f'p{p}': ordered[math.ceil(len(ordered) * p / 100) - 1]
                   for p in (25, 50, 75, 95, 99)}, max=peak, max_frame=frame,
                over_budget={str(budget): sum(value > budget for value in ordered)
                             for budget in (1000 / 60, 1000 / 30, 50, 100)})


def run_order(builds, hd_modes, repeat):
    """Alternate AB/BA (or reverse the full matrix) without concurrent runs."""
    cases = [(name, executable, hd) for name, executable in builds for hd in hd_modes]
    return cases if repeat % 2 == 0 else cases[::-1]


def verify_trace(baseline, candidate, label, expected_lines):
    with baseline.open() as left, candidate.open() as right:
        count = 0
        for line, (a, b) in enumerate(zip_longest(left, right), 1):
            if a != b:
                raise AssertionError(f'{label} first difference at line {line}: '
                                     f'{a!r} != {b!r}')
            count = line
        if count != expected_lines:
            raise AssertionError(f'{label} expected {expected_lines} lines, got {count}')


def summarize_runs(results):
    """Run variation, distinct from the per-frame tails in each result."""
    groups = {}
    for result in results:
        if result['warmup']:
            continue
        groups.setdefault((result['build'], result['hd']), []).append(result)
    return [dict(build=name, hd=hd, runs=len(runs),
                 mean_ms={stage: dict(median=statistics.median(values),
                                     minimum=min(values), maximum=max(values),
                                     **distribution(list(enumerate(values))))
                          for stage in runs[0]['mean_ms']
                          for values in [[run['mean_ms'][stage] for run in runs]]})
            for (name, hd), runs in groups.items()]


def paired_comparison(results):
    """Compare non-warmup baseline/candidate run distributions without hiding overlap."""
    groups = {}
    for row in results:
        if row.get('warmup') or row.get('build') not in ('baseline', 'candidate'):
            continue
        groups.setdefault((row['hd'], row['build']), []).append(row)
    comparison = {}
    for hd in sorted({key[0] for key in groups}):
        baseline = groups.get((hd, 'baseline'), [])
        candidate = groups.get((hd, 'candidate'), [])
        if not baseline or not candidate:
            continue
        stages = sorted(set().union(*(row['mean_ms'] for row in baseline + candidate)))
        stage_rows = {}
        for stage in stages:
            left = [row['mean_ms'][stage] for row in baseline if stage in row['mean_ms']]
            right = [row['mean_ms'][stage] for row in candidate if stage in row['mean_ms']]
            if not left or not right:
                continue
            left_median = statistics.median(left)
            right_median = statistics.median(right)
            delta = right_median - left_median
            overlap = max(min(left), min(right)) <= min(max(left), max(right))
            assessment = ('inconclusive: baseline/candidate run ranges overlap' if overlap else
                          'consistent direction: baseline/candidate run ranges do not overlap')
            stage_rows[stage] = dict(baseline_median=left_median, candidate_median=right_median,
                                     baseline_range=[min(left), max(left)],
                                     candidate_range=[min(right), max(right)],
                                     delta_ms=delta,
                                     delta_percent=(delta / left_median * 100) if left_median else None,
                                     ranges_overlap=overlap, assessment=assessment)
        comparison[f'hd{hd}'] = dict(mean_ms=stage_rows)
    return comparison


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
    samples['renderer_ms'] = [(int(row['frame']), sum(float(row[key]) for key in
                              ('raster_ms', 'acquire_ms', 'compose_ms', 'present_ms')
                              if key in row)) for row in rows]
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


def cost_timings(path, first, last):
    """Diagnostic stages overlap; never add them to work_ms or each other."""
    with path.open(newline='') as stream:
        rows = [row for row in csv.DictReader(stream) if first <= int(row['frame']) <= last]
    if not rows:
        raise AssertionError(f'{path}: no diagnostic samples in {first}..{last}')
    return {key: distribution([(int(row['frame']), float(row[key])) for row in rows])
            for key in rows[0] if key.endswith('_ms')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--gpu-hd', action='store_true',
                        help='Enable OpenGL HD composition for the candidate only')
    parser.add_argument('--gpu-orbit', action='store_true',
                        help='Enable experimental OpenGL orbit terrain rasterization for the candidate only')
    parser.add_argument('--verify-presents', action='store_true',
                        help='Compare per-presentation pixel CRCs and guest state in separate paired replays')
    parser.add_argument('--rom', type=Path, default=Path("Kirby's Dream Course (USA).sfc"))
    parser.add_argument('--display', action='store_true', help='Use the real display instead of SDL dummy')
    parser.add_argument('--native-width', action='store_true', help='Disable widescreen')
    parser.add_argument('--scenario', choices=SCENARIOS, default='still')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--warmup', type=int, default=0,
                        help='Discard this many full replays of every case before measuring')
    parser.add_argument('--backend', choices=['auto', 'software', 'direct3d11', 'opengl'],
                        default='auto', help='Pin [Graphics] Renderer; unavailable drivers fail')
    parser.add_argument('--window-size', default='1536x896', help='Fixed output window size')
    parser.add_argument('--orbit-profile', action='store_true',
                        help='Diagnostic replay with KDC_ORBIT_PROFILE=1; keep separate from timings')
    parser.add_argument('--diagnostic-costs', action='store_true',
                        help='Use the isolated tools/diagnostics/hd-costs patches; buffered cost/GPU traces')
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
    if args.repeat < 1 or args.warmup < 0:
        parser.error('--repeat must be positive and --warmup nonnegative')
    if not re.fullmatch(r'[1-9][0-9]*x[1-9][0-9]*', args.window_size):
        parser.error('--window-size must be WIDTHxHEIGHT with positive dimensions')
    if args.backend in ('direct3d11', 'opengl') and not args.display:
        parser.error('Accelerated backends require --display')
    if (args.gpu_hd or args.gpu_orbit) and args.backend != 'opengl':
        parser.error('--gpu-hd and --gpu-orbit require --backend opengl')
    if args.gpu_orbit and args.scenario != 'orbit':
        parser.error('--gpu-orbit requires --scenario orbit')
    if args.verify_presents and (not args.baseline or args.no_captures):
        parser.error('--verify-presents requires --baseline and separate captures')
    scenario = SCENARIOS[args.scenario]
    frames = args.frames if args.frames is not None else scenario['frames']
    capture_frames = scenario['screenshot'] + 5
    if args.verify_presents:
        capture_frames = max(capture_frames, frames)
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
    metadata = dict(arguments={key: str(value) if isinstance(value, Path) else value
                               for key, value in vars(args).items()},
                    source_revision=subprocess.check_output(
                        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                    platform=platform.platform(), processor=platform.processor(),
                    rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                    builds={name: dict(executable=str(executable),
                                sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                                dll_sha256={dll.name: hashlib.sha256(dll.read_bytes()).hexdigest()
                                            for dll in executable.parent.glob('*.dll')})
                            for name, executable in builds},
                    order='forward/reverse on alternating repeats',
                    runtime_environment=RUNTIME_ENV,
                    harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    timing_kind='main-thread elapsed wall time',
                    gpu_timing=('separate OpenGL timestamp trace' if args.diagnostic_costs and
                                args.backend == 'opengl' else 'unmeasured'))
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    results = []
    for iteration in range(args.warmup + args.repeat):
        warmup = iteration < args.warmup
        repeat = iteration - args.warmup
        captures = {}
        for name, executable, hd in run_order(builds, args.hd, iteration):
            case = output / f'{repeat}-{name}-hd{hd}'
            case.mkdir()
            host = case / executable.name
            shutil.copy2(executable, host)
            for dll in executable.parent.glob('*.dll'):
                shutil.copy2(dll, case / dll.name)
            (case / 'config.ini').write_text(
                '[General]\nAutosave=0\nDisableFrameDelay=1\n'
                '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\n'
                f'Widescreen={int(not args.native_width)}\nWindowSize={args.window_size}\n'
                f'Renderer={args.backend}\nVSync=0\n'
                '[Sound]\nEnableAudio=0\n[Controller]\nSourceP1=0\nSourceP2=0\n'
                '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
            env.update(SDL_AUDIODRIVER='dummy', KDC_HD=str(hd),
                       SNESRECOMP_SCREENSHOT=str(case / 'frame.ppm'),
                       SNESRECOMP_SCREENSHOT_FRAME=str(scenario['screenshot']))
            env.update(RUNTIME_ENV)
            env['KDC_HD_GPU'] = str(int(args.gpu_hd and name == 'candidate'))
            if args.gpu_orbit and name == 'candidate':
                env['KDC_ORBIT_GPU'] = '1'
            if args.threads is not None:
                env['KDC_THREADS'] = str(args.threads)
            if args.orbit_profile:
                env['KDC_ORBIT_PROFILE'] = '1'
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
            if args.diagnostic_costs:
                timed['KDC_COST_TRACE'] = str(case / 'cost.csv')
                if args.backend == 'opengl':
                    timed['KDC_GPU_TRACE'] = str(case / 'gpu.csv')
            del timed['SNESRECOMP_SCREENSHOT'], timed['SNESRECOMP_SCREENSHOT_FRAME']
            if not args.no_captures and not warmup:
                capture = dict(env, SNESRECOMP_RUN_FRAMES=str(capture_frames))
                if args.verify_presents:
                    capture.update(SNESRECOMP_PRESENT_LOG=str(case / 'presents.csv'),
                                   SNESRECOMP_STATE_TRACE=str(case / 'state.csv'))
                with (case / 'capture.log').open('w') as log:
                    subprocess.run(command, cwd=case, env=capture,
                                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
            with (case / 'run.log').open('w') as log:
                subprocess.run(command, cwd=case, env=timed,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
            log = (case / 'run.log').read_text()
            if args.gpu_hd and name == 'candidate' and hd and 'GPU HD composition active' not in log:
                raise AssertionError(f'{case}: GPU HD compositor did not present a frame')
            if args.gpu_orbit and name == 'candidate' and hd and 'GPU orbit terrain active' not in log:
                raise AssertionError(f'{case}: GPU orbit terrain did not render a frame')
            if args.diagnostic_costs:
                assert (case / 'cost.csv').is_file(), 'Missing diagnostic costs; use the diagnostic build'
                if args.backend == 'opengl':
                    assert (case / 'gpu.csv').is_file(), 'Missing GPU timestamp capture'
            profile = re.findall(r'video profile: stage=(\S+) count=\d+ total_ms=[\d.]+ '
                                 r'mean_ms=([\d.]+) max_ms=([\d.]+) max_frame=(\d+)', log)
            stages = {stage: float(mean) for stage, mean, _, _ in profile}
            maxima = {stage: (float(peak), int(frame)) for stage, _, peak, frame in profile}
            fps = re.search(r'frame service: .* fps=([\d.]+)', log)
            if 'raster-capture' not in stages or not fps:
                raise AssertionError(f'{case}: missing host profile')
            if "is not available in this build" in log:
                raise AssertionError(f'{case}: requested renderer unavailable')
            window = re.search(r'video profile window: first=(\d+) last=(\d+) '
                               r'seconds=([\d.]+) presentations=(\d+)', log)
            if not window or (int(window[1]), int(window[2])) != (args.profile_start, frames):
                raise AssertionError(f'{case}: missing or incomplete profile window')
            captures[name, hd] = (None if args.no_captures or warmup else
                hashlib.sha256((case / 'frame.ppm').read_bytes()).hexdigest())
            timing = frame_timings(case / 'timing.csv')
            if timing['distributions']['work_ms']['count'] != int(window[4]):
                raise AssertionError(f'{case}: presentation trace is incomplete')
            stages['renderer-total'] = timing['distributions']['renderer_ms']['mean']
            renderer = re.search(r'^Renderer: (.+)$', log, re.MULTILINE)
            if not renderer:
                renderer = re.search(r'renderer initialized: (.+)', log)
            result = dict(repeat=repeat, warmup=warmup, build=name, hd=hd, scenario=args.scenario,
                          hole=args.hole, threads=args.threads,
                          frames=frames, profile_start=args.profile_start, display=args.display,
                          backend=args.backend, window_size=args.window_size,
                          orbit_profile=args.orbit_profile,
                          diagnostic_costs=args.diagnostic_costs,
                          updates=frames - args.profile_start + 1,
                          presentations=int(window[4]),
                          profile_seconds=float(window[3]),
                          simulation_fps=(frames - args.profile_start + 1) / float(window[3]),
                          presentation_fps=int(window[4]) / float(window[3]),
                          renderer_ms_per_presentation=timing['distributions']['renderer_ms']['mean'],
                          renderer_ms_per_update=(timing['distributions']['renderer_ms']['mean'] *
                                                 int(window[4]) / (frames - args.profile_start + 1)),
                          renderer=renderer[1] if renderer else None,
                          fps=float(fps[1]), mean_ms=stages, max_ms=maxima,
                          **timing,
                          screenshot_sha256=captures[name, hd])
            if args.diagnostic_costs:
                # Orbit enters at 501 in the pinned script. Keep that cold
                # present distinct from both the full window and warm orbit.
                windows = {'profile': (args.profile_start, frames)}
                if args.scenario == 'orbit' and args.profile_start <= 501 < frames:
                    windows.update(entry=(501, 501), warm=(502, frames))
                result['diagnostics'] = {
                    trace: {label: cost_timings(case / f'{trace}.csv', first, last)
                            for label, (first, last) in windows.items()}
                    for trace in ('cost', 'gpu') if (case / f'{trace}.csv').is_file()}
            results.append(result)
            (output / 'results.json').write_text(json.dumps(results, indent=2))
            (output / 'summary.json').write_text(json.dumps(summarize_runs(results), indent=2))
            print(f'{repeat} {name} HD={hd}: raster={stages["raster-capture"]:.3f} ms '
                  f'compose={stages["compose"]:.3f} ms (max {maxima["compose"][0]:.1f} ms '
                  f'at {maxima["compose"][1]}) present={stages["upload-present"]:.3f} ms '
                  f'fps={float(fps[1]):.1f}', flush=True)
            intervals = timing['distributions']['interval_ms']
            print(f'  interval p50/p95/p99/max: {intervals["p50"]:.3f}/'
                  f'{intervals["p95"]:.3f}/{intervals["p99"]:.3f}/{intervals["max"]:.3f} ms; '
                  f'cache clears/trims: {timing["hd_counters"].get("hd_cache_clears", "unavailable")}/'
                  f'{timing["hd_counters"].get("hd_cache_trims", "unavailable")}', flush=True)
            if not warmup and name == 'candidate' and hd and args.max_raster_ms is not None:
                assert stages['raster-capture'] <= args.max_raster_ms, 'HD raster budget exceeded'
            if not warmup and name == 'candidate' and hd and args.max_compose_ms is not None:
                assert stages['compose'] <= args.max_compose_ms, 'HD composition budget exceeded'
            if not warmup and name == 'candidate' and hd and args.max_p99_ms is not None:
                assert intervals['p99'] <= args.max_p99_ms, 'HD presentation p99 budget exceeded'
        if args.baseline and not args.no_captures and not warmup:
            for hd in args.hd:
                if args.verify_presents:
                    left = output / f'{repeat}-baseline-hd{hd}'
                    right = output / f'{repeat}-candidate-hd{hd}'
                    verify_trace(left / 'presents.csv', right / 'presents.csv',
                                 f'HD={hd} presentation', capture_frames + 1)
                    verify_trace(left / 'state.csv', right / 'state.csv',
                                 f'HD={hd} guest state', capture_frames)
                assert captures['baseline', hd] == captures['candidate', hd], f'HD={hd}: image changed'
    comparison = paired_comparison(results)
    (output / 'paired-comparison.json').write_text(json.dumps(comparison, indent=2) + '\n')
    for hd, row in comparison.items():
        for stage in ('raster-capture', 'compose', 'upload-present', 'renderer-total'):
            stats = row['mean_ms'].get(stage)
            if stats:
                print(f'paired {hd} {stage}: median delta {stats["delta_ms"]:+.3f} ms '
                      f'({stats["delta_percent"]:+.1f}%); {stats["assessment"]}', flush=True)
    print('Completed; baseline images match.' if args.baseline and not args.no_captures else 'Completed.')


if __name__ == '__main__':
    main()

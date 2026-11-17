"""Compare interpreter/bounce timing and optionally sample Windows x64 guest stacks.

ROM-derived traces, stacks, saves and logs stay in ignored local/. Timings and
sampling use separate fresh processes. Sampling reports main-thread occupancy,
not hardware-counter CPU percentages, and never supplies benchmark timings.
"""
import argparse
from bisect import bisect_right
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import time

from benchmark_hd import SCENARIOS
from setup import ROM_SHA256


def symbols(executable, nm, objdump):
    listing = subprocess.check_output([nm, '-n', str(executable)], text=True)
    entries = [(int(a, 16), kind, name) for a, kind, name in
               re.findall(r'^([0-9a-fA-F]+)\s+(\S)\s+(.+)$', listing, re.MULTILINE)]
    headers = subprocess.check_output([objdump, '-p', str(executable)], text=True)
    base = int(re.search(r'^ImageBase\s+([0-9a-fA-F]+)', headers, re.MULTILINE)[1], 16)
    # The host's upcoming frame number stays constant through RtlRunFrame,
    # including its APU tail after snes_frame_counter has already advanced.
    frame = next(address for address, _, name in entries if name == 'g_profile_frame')
    functions = [(address - base, name) for address, kind, name in entries if kind in 'Tt']
    return frame - base, functions


def summarize_samples(path, functions):
    addresses = [address for address, _ in functions]

    def resolve(value):
        if value.startswith('external'):
            return value
        index = bisect_right(addresses, int(value, 16)) - 1
        return functions[index][1] if index >= 0 else 'unknown'

    groups, leaves, inclusive = Counter(), Counter(), Counter()
    total = guest = truncated = apu_tail = outside_driver = 0
    first_frame = last_frame = None
    with path.open() as stream:
        for row in csv.DictReader(stream):
            stack = [resolve(value) for value in row['stack_rvas'].split(';')]
            frame = int(row['frame'])
            first_frame = frame if first_frame is None else min(first_frame, frame)
            last_frame = frame if last_frame is None else max(last_frame, frame)
            total += 1
            if len(stack) >= 96:
                truncated += 1
            if not any(name == 'RtlRunFrame' for name in stack):
                continue
            guest += 1
            in_driver = any(name.startswith('snes_beam_frame_driver_run_frame') for name in stack)
            if not in_driver:
                outside_driver += 1
            leaves[stack[0]] += 1
            inclusive.update(set(stack))
            # Innermost subsystem wins: an APU sync called from the interpreter
            # belongs to APU, not to both. Names retain compiler suffixes.
            group = 'beam/bus/other'
            for name in stack:
                if name.startswith(('apu_', 'spc_', 'dsp_', 'RtlApu', 'RtlRunSpc')):
                    group = 'APU/SPC/DSP'
                    break
                if name.startswith('dma_'):
                    group = 'DMA/HDMA'
                    break
                if name.startswith(('snes_advance_beam', 'snes_sync_master_clock', 'snes_refresh_charge')):
                    group = 'beam/bus/other'
                    break
                if name.startswith(('cpu_', 'interp_bridge_')):
                    group = '65816/bridge'
                    break
                if name.startswith(('bank_', 'I_RESET', 'I_NMI')):
                    group = 'generated'
                    break
            groups[group] += 1
            if group == 'APU/SPC/DSP' and not in_driver:
                apu_tail += 1
    if not guest:
        raise AssertionError(f'{path}: no unwound guest stacks')
    return dict(total_samples=total, guest_samples=guest, truncated_stacks=truncated,
                apu_outside_beam_driver_samples=apu_tail,
                outside_beam_driver_samples=outside_driver,
                first_sampled_frame=first_frame, last_sampled_frame=last_frame,
                exclusive={name: dict(samples=count, percent=100 * count / guest)
                           for name, count in groups.most_common()},
                leaf=leaves.most_common(), inclusive=inclusive.most_common())


def parse_timing(log, first, last):
    window = re.search(r'video profile window: first=(\d+) last=(\d+)', log)
    if not window or (int(window[1]), int(window[2])) != (first, last):
        raise AssertionError(f'Missing or unexpected profile window: {window}')
    profile = re.findall(r'video profile: stage=(\S+) count=(\d+) total_ms=([\d.]+) '
                         r'mean_ms=([\d.]+) max_ms=([\d.]+) max_frame=(\d+)', log)
    stages = {name: dict(count=int(count), total_ms=float(total), mean_ms=float(mean),
                        max_ms=float(peak), max_frame=int(frame))
              for name, count, total, mean, peak, frame in profile}
    if 'guest' not in stages or stages['guest']['count'] != last - first + 1:
        raise AssertionError('Missing or incomplete guest profile')
    return stages


def compare_state(reference, candidate):
    for filename in ('state.csv', 'frame.log', 'save.srm'):
        a, b = reference / filename, candidate / filename
        if a.read_bytes() == b.read_bytes():
            continue
        if filename in ('state.csv', 'frame.log'):
            left, right = a.read_text().splitlines(), b.read_text().splitlines()
            for i in range(max(len(left), len(right))):
                expected = left[i] if i < len(left) else '<missing>'
                actual = right[i] if i < len(right) else '<missing>'
                if expected != actual:
                    raise AssertionError(f'First state divergence at row {i + 1}: '
                                         f'{expected} vs {actual}; inputs: {candidate / "shot.script"}')
        raise AssertionError(f'{filename} differs: {reference} vs {candidate}')


def checkpoint_script(scenario):
    script = scenario['script'] or ''
    # The fixed benchmark scripts use only wait N / press BUTTON N. Saving
    # consumes an idle input frame; place it in the existing idle tail.
    duration = 0
    for line in script.splitlines():
        words = line.split()
        if not words or words[0] not in ('wait', 'press'):
            raise ValueError('Checkpoint requires a fixed wait/press benchmark script')
        duration += int(words[-1])
    tail = scenario['frames'] - 10 - duration
    if tail < 0:
        raise ValueError('No idle tail for validation checkpoint')
    return script + f'wait {tail}\nsavestate 0\n'


def compare_full_state(reference, candidate, first, last, checkpoint_reference=None):
    # Framedump labels the just-completed frame before the counter advances;
    # the host timer labels the upcoming frame. Keep that offset explicit.
    files = [(reference, f'frames/frame_{frame:06}_wram.bin')
             for frame in range(first - 1, last)]
    # Execution residue includes tier-specific host return frames and pacing
    # counters. Compare complete snapshots against the SAME bounce policy;
    # CPU/WRAM/SRAM still compare across all policies against the first case.
    files.append((checkpoint_reference or reference, 'checkpoint.sav'))
    for source, filename in files:
        expected, actual = (source / filename).read_bytes(), (candidate / filename).read_bytes()
        if expected != actual:
            offset = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b),
                          min(len(expected), len(actual)))
            raise AssertionError(f'First full-state divergence: {filename}, byte {offset}; '
                                 f'inputs: {candidate / "shot.script"}')


def source_samples(path, executable, addr2line, objdump, functions):
    """Resolve leaf instructions and external-leaf callers, retaining inline chains.

    Caller return addresses use PC-1; the suspended leaf PC is used as-is.
    These are source locations in an optimized binary, not exclusive line timers.
    """
    headers = subprocess.check_output([objdump, '-p', str(executable)], text=True)
    base = int(re.search(r'^ImageBase\s+([0-9a-fA-F]+)', headers, re.MULTILINE)[1], 16)
    counts = Counter()
    addresses = [pc for pc, _ in functions]
    with path.open() as stream:
        for row in csv.DictReader(stream):
            stack = row['stack_rvas'].split(';')
            names = [functions[bisect_right(addresses, int(pc, 16)) - 1][1]
                     for pc in stack if not pc.startswith('external') and int(pc, 16) >= addresses[0]]
            if 'RtlRunFrame' not in names:
                continue
            if stack[0].startswith('external'):
                caller = next((pc for pc in stack if not pc.startswith('external')), None)
                if caller:
                    counts[(stack[0], base + int(caller, 16) - 1)] += 1
            else:
                counts[('', base + int(stack[0], 16))] += 1
    addresses = sorted({pc for _, pc in counts})
    listing = subprocess.check_output(
        [addr2line, '-a', '-f', '-i', '-e', str(executable)],
        input=''.join(f'{pc:x}\n' for pc in addresses), text=True)
    locations = {}
    for block in re.split(r'(?m)^0x', listing)[1:]:
        lines = block.splitlines()
        locations[int(lines[0], 16)] = ' > '.join(
            f'{function} at {source}' for function, source in zip(lines[1::2], lines[2::2]))
    resolved = Counter()
    for (external, pc), count in counts.items():
        location = locations.get(pc, 'unresolved')
        resolved[f'{external} called from {location}' if external else location] += count
    return resolved.most_common()


def process_cpu_seconds(process):
    """Windows process CPU after wait(), while Popen still owns its handle."""
    if os.name != 'nt':
        return None
    import ctypes
    from ctypes import wintypes
    times = [wintypes.FILETIME() for _ in range(4)]
    get_times = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    get_times.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    get_times.restype = wintypes.BOOL
    if not get_times(int(process._handle), *(ctypes.byref(value) for value in times)):
        raise ctypes.WinError(ctypes.get_last_error())
    return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in times[2:]) / 1e7


def build_identity(host):
    digest = hashlib.sha256(host.read_bytes()).hexdigest()
    sidecar = host.with_suffix('.build.json')
    info = None
    if sidecar.exists():
        info = json.loads(sidecar.read_text())
        if not isinstance(info, dict) or info.get('sha256') != digest:
            raise ValueError(f'{sidecar}: build identity does not match executable')
    return dict(executable=str(host), sha256=digest, build_info=info,
                provenance='recorded' if info else 'unknown; harness revision is not build provenance')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--baseline', type=Path, help='Compare a previous build using the same scenarios/policies')
    parser.add_argument('--rom', type=Path, default=Path("Kirby's Dream Course (USA).sfc"))
    parser.add_argument('--scenario', choices=SCENARIOS, default='shot')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--warmup', type=int, default=2, help='Discarded fresh runs per build/policy')
    parser.add_argument('--bounce', type=int, choices=(0, 1), nargs='+', default=[0, 1])
    parser.add_argument('--modes', choices=('timing', 'sample', 'validate'), nargs='+',
                        default=['timing', 'sample', 'validate'], help='Separate measurement campaigns')
    parser.add_argument('--sampler', type=Path, help='Compiled windows_guest_sampler.c; separate sampled runs')
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--objdump', default='objdump')
    parser.add_argument('--addr2line', help='Optional matching tool for optimized source/inline attribution')
    parser.add_argument('--validate', action='store_true', help='Separate CPU/WRAM CRC + SRAM comparisons')
    parser.add_argument('--full-state', action='store_true',
                        help='Validation also compares full WRAM window and serialized state checkpoint')
    parser.add_argument('--max-guest-ms', type=float, help='Fail if any unsampled guest mean exceeds budget')
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
    if args.warmup < 0:
        parser.error('--warmup must be nonnegative')
    if len(set(args.bounce)) != len(args.bounce):
        parser.error('--bounce policies must be unique')
    if args.max_guest_ms is not None and 'timing' not in args.modes:
        parser.error('--max-guest-ms requires timing runs')
    if args.modes == ['sample'] and not args.sampler:
        parser.error('sample mode requires --sampler')
    if args.modes == ['validate'] and not args.validate:
        parser.error('validate mode requires --validate')
    if args.full_state and (not args.validate or 'validate' not in args.modes):
        parser.error('--full-state requires --validate and validate mode')
    rom = args.rom.resolve(strict=True)
    if hashlib.sha256(rom.read_bytes()).hexdigest() != ROM_SHA256:
        parser.error('Unsupported ROM SHA-256')
    executable = args.executable.resolve(strict=True)
    builds = [('candidate', executable)]
    if args.baseline:
        builds.insert(0, ('baseline', args.baseline.resolve(strict=True)))
    sampler = args.sampler.resolve(strict=True) if args.sampler else None
    if sampler and platform.system() != 'Windows':
        parser.error('--sampler requires Windows x64')
    root = Path(__file__).resolve().parents[1]
    (root / 'local').mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='guest-profile-', dir=root / 'local'))
    print(output, flush=True)
    scenario = SCENARIOS[args.scenario]
    first, last = 350, scenario['frames']
    clean_env = {key: value for key, value in os.environ.items()
                 if not key.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
    results = dict(metadata=dict(builds={name: build_identity(host) for name, host in builds},
                   rom_sha256=ROM_SHA256, platform=platform.platform(),
                   processor=platform.processor(), scenario=args.scenario,
                   first_frame=first, last_frame=last,
                   harness_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                   harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   sampler_sha256=hashlib.sha256(sampler.read_bytes()).hexdigest() if sampler else None,
                   warmups_per_build_policy=args.warmup, bounce_policies=args.bounce,
                   modes=args.modes, cpu_time_scope='whole process; not the guest frame window',
                   full_state_validation=args.full_state,
                   script_sha256=hashlib.sha256((scenario['script'] or '').encode()).hexdigest(),
                   camera_script_sha256=hashlib.sha256((scenario['camera'] or '').encode()).hexdigest(),
                   settings=dict(hd=0, threads=1, video='dummy', audio='dummy', audio_output=False,
                                 pacing=False, initial_sram='zero', dev_hole='normal:1:1',
                                 apu_beam_elapsed=1, apu_handoff=1, apu_absolute_preanchor=1)),
                   warmups=[], timings=[], samples=[], validation=[])
    if sampler:
        symbol_tables = {name: symbols(host, args.nm, args.objdump) for name, host in builds}

    def run(name, build, executable, bounce, mode):
        case = output / name
        case.mkdir()
        host = case / executable.name
        shutil.copy2(executable, host)
        for dll in executable.parent.glob('*.dll'):
            shutil.copy2(dll, case / dll.name)
        # Fresh config and SRAM per run; no inherited launcher/camera settings.
        (case / 'config.ini').write_text(
            '[General]\nAutosave=0\nDisableFrameDelay=1\n'
            '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\nWidescreen=1\nVSync=0\n'
            '[Sound]\nEnableAudio=0\n[Controller]\nSourceP1=0\nSourceP2=0\n'
            '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
        env = dict(clean_env, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', KDC_HD='0',
                   SNESRECOMP_LLE_BOUNCE=str(bounce), SNESRECOMP_RUN_FRAMES=str(last),
                   SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                   SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1', KDC_THREADS='1')
        command = [str(host), '--no-launcher', '--rom', str(rom), '--dev-hole', 'normal:1:1']
        if scenario['script']:
            (case / 'shot.script').write_text(scenario['script'])
            command += ['--script', str(case / 'shot.script')]
        if mode == 'validate' and args.full_state:
            (case / 'shot.script').write_text(checkpoint_script(scenario))
            if not scenario['script']:
                command += ['--script', str(case / 'shot.script')]
            command += ['--framedump', str(case / 'frames')]
            env.update(SNESRECOMP_FRAMEDUMP_START=str(first - 1),
                       SNESRECOMP_FRAMEDUMP_END=str(last - 1))
        if scenario['camera']:
            (case / 'camera.script').write_text(scenario['camera'])
            env['KDC_CAMERA_SCRIPT'] = str(case / 'camera.script')
        if mode in ('timing', 'warmup', 'sample'):
            env.update(SNESRECOMP_HOST_PROFILE='1', SNESRECOMP_HOST_PROFILE_START_FRAME=str(first))
        if mode == 'validate' or (mode == 'sample' and args.validate):
            env['SNESRECOMP_STATE_TRACE'] = str(case / 'state.csv')
            env['KDC_FRAME_LOG'] = str(case / 'frame.log')
        started = time.perf_counter()
        if os.name != 'nt':
            import resource
            usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
        with (case / 'run.log').open('w') as log:
            process = subprocess.Popen(command, cwd=case, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                if mode == 'sample':
                    frame_rva, _ = symbol_tables[build]
                    with (case / 'sampler.log').open('w') as sample_log:
                        subprocess.run([str(sampler), str(process.pid), f'{frame_rva:x}', str(first),
                                        str(last), str(case / 'samples.csv'), '120'],
                                       stdout=sample_log, stderr=subprocess.STDOUT, check=True, timeout=150)
                process.wait(timeout=180)
                if process.returncode:
                    raise subprocess.CalledProcessError(process.returncode, command)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
            cpu_seconds = process_cpu_seconds(process)
        if os.name != 'nt':
            usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
            cpu_seconds = (usage_after.ru_utime + usage_after.ru_stime -
                           usage_before.ru_utime - usage_before.ru_stime)
        (case / 'process.json').write_text(json.dumps(dict(
            elapsed_seconds=time.perf_counter() - started, cpu_seconds=cpu_seconds,
            cpu_scope='host process' if os.name == 'nt' else 'children (includes sampler if enabled)')) + '\n')
        if f'RUN_FRAMES reached after {last} frames' not in (case / 'run.log').read_text():
            raise AssertionError(f'{case}: replay did not reach its final frame')
        saves = list((case / 'saves').glob('*.srm'))
        if len(saves) != 1:
            raise AssertionError(f'{case}: expected one final SRAM file')
        shutil.copy2(saves[0], case / 'save.srm')
        if mode == 'validate' and args.full_state:
            snapshots = list((case / 'saves').glob('*.sav'))
            if len(snapshots) != 1:
                raise AssertionError(f'{case}: expected one full-state checkpoint')
            shutil.copy2(snapshots[0], case / 'checkpoint.sav')
        return case

    def save():
        (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')

    # Alternate AB/BA order to reduce thermal/order bias. Warmups are discarded.
    for repeat in range(args.warmup if 'timing' in args.modes else 0):
        for build, host in builds:
            for bounce in args.bounce:
                case = run(f'warmup-{repeat}-{build}-bounce{bounce}', build, host, bounce, 'warmup')
                results['warmups'].append(dict(case=case.name, discarded=True))
                save()
                print(f'{case.name}: discarded', flush=True)
    for repeat in range(args.repeat if 'timing' in args.modes else 0):
        for build, host in (builds if repeat % 2 == 0 else builds[::-1]):
            for bounce in (args.bounce if repeat % 2 == 0 else args.bounce[::-1]):
                case = run(f'timing-{repeat}-{build}-bounce{bounce}', build, host, bounce, 'timing')
                stages = parse_timing((case / 'run.log').read_text(), first, last)
                results['timings'].append(dict(repeat=repeat, build=build, bounce=bounce, stages=stages,
                                              process=json.loads((case / 'process.json').read_text())))
                save()
                print(f'{case.name}: guest={stages["guest"]["mean_ms"]:.3f} ms', flush=True)
    if sampler and 'sample' in args.modes:
        for repeat in range(args.repeat):
            for build, host in (builds if repeat % 2 == 0 else builds[::-1]):
                for bounce in (args.bounce if repeat % 2 == 0 else args.bounce[::-1]):
                    case = run(f'sample-{repeat}-{build}-bounce{bounce}', build, host, bounce, 'sample')
                    _, functions = symbol_tables[build]
                    summary = summarize_samples(case / 'samples.csv', functions)
                    summary['stages_with_sampler'] = parse_timing((case / 'run.log').read_text(), first, last)
                    if args.addr2line:
                        summary['source_leaf_and_external_callers'] = source_samples(
                            case / 'samples.csv', host, args.addr2line, args.objdump, functions)
                    summary['process'] = json.loads((case / 'process.json').read_text())
                    results['samples'].append(dict(repeat=repeat, build=build, bounce=bounce, **summary))
                    save()
                    print(f'{case.name}: {summary["guest_samples"]} guest samples; '
                          f'{summary["exclusive"]}', flush=True)
    if args.validate and 'validate' in args.modes:
        reference = None
        checkpoint_references = {}
        for repeat in range(2):
            for build, host in builds:
                for bounce in args.bounce:
                    case = run(f'validate-{repeat}-{build}-bounce{bounce}', build, host, bounce, 'validate')
                    if reference is None:
                        reference = case
                    compare_state(reference, case)
                    if args.full_state:
                        checkpoint_reference = checkpoint_references.setdefault(bounce, case)
                        compare_full_state(reference, case, first, last, checkpoint_reference)
                    results['validation'].append(dict(case=case.name, equal=True, full_state=args.full_state,
                                                     checkpoint_reference=checkpoint_references[bounce].name
                                                     if args.full_state else None))
                    save()
                    print(f'{case.name}: CPU/WRAM trace and SRAM equal', flush=True)
        if sampler:
            for case in sorted(output.glob('sample-*')):
                compare_state(reference, case)
                results['validation'].append(dict(case=case.name, equal=True))
                save()
                print(f'{case.name}: sampled CPU/WRAM trace and SRAM equal', flush=True)
    if args.max_guest_ms is not None:
        assert all(row['stages']['guest']['mean_ms'] <= args.max_guest_ms
                   for row in results['timings']), 'Guest budget exceeded'


if __name__ == '__main__':
    main()

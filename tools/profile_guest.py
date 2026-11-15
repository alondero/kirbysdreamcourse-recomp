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
    total = guest = truncated = 0
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
    if not guest:
        raise AssertionError(f'{path}: no unwound guest stacks')
    return dict(total_samples=total, guest_samples=guest, truncated_stacks=truncated,
                first_sampled_frame=first_frame, last_sampled_frame=last_frame,
                exclusive={name: dict(samples=count, percent=100 * count / guest)
                           for name, count in groups.most_common()},
                leaf=leaves.most_common(25), inclusive=inclusive.most_common(30))


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--baseline', type=Path, help='Compare a previous build using the same scenarios/policies')
    parser.add_argument('--rom', type=Path, default=Path("Kirby's Dream Course (USA).sfc"))
    parser.add_argument('--scenario', choices=SCENARIOS, default='shot')
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--sampler', type=Path, help='Compiled windows_guest_sampler.c; separate sampled runs')
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--objdump', default='objdump')
    parser.add_argument('--validate', action='store_true', help='Separate CPU/WRAM CRC + SRAM comparisons')
    parser.add_argument('--max-guest-ms', type=float, help='Fail if any unsampled guest mean exceeds budget')
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
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
    results = dict(metadata=dict(builds={name: dict(executable=str(host),
                   sha256=hashlib.sha256(host.read_bytes()).hexdigest()) for name, host in builds},
                   rom_sha256=ROM_SHA256, platform=platform.platform(),
                   processor=platform.processor(), scenario=args.scenario,
                   first_frame=first, last_frame=last,
                   git_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()),
                   timings=[], samples=[], validation=[])
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
        if scenario['camera']:
            (case / 'camera.script').write_text(scenario['camera'])
            env['KDC_CAMERA_SCRIPT'] = str(case / 'camera.script')
        if mode == 'timing':
            env.update(SNESRECOMP_HOST_PROFILE='1', SNESRECOMP_HOST_PROFILE_START_FRAME=str(first))
        if mode == 'validate' or (mode == 'sample' and args.validate):
            env['SNESRECOMP_STATE_TRACE'] = str(case / 'state.csv')
            env['KDC_FRAME_LOG'] = str(case / 'frame.log')
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
        if f'RUN_FRAMES reached after {last} frames' not in (case / 'run.log').read_text():
            raise AssertionError(f'{case}: replay did not reach its final frame')
        saves = list((case / 'saves').glob('*.srm'))
        if len(saves) != 1:
            raise AssertionError(f'{case}: expected one final SRAM file')
        shutil.copy2(saves[0], case / 'save.srm')
        return case

    def save():
        (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')

    # Alternate AB/BA order to reduce thermal/order bias.
    for repeat in range(args.repeat):
        for build, host in (builds if repeat % 2 == 0 else builds[::-1]):
            for bounce in ((0, 1) if repeat % 2 == 0 else (1, 0)):
                case = run(f'timing-{repeat}-{build}-bounce{bounce}', build, host, bounce, 'timing')
                stages = parse_timing((case / 'run.log').read_text(), first, last)
                results['timings'].append(dict(repeat=repeat, build=build, bounce=bounce, stages=stages))
                save()
                print(f'{case.name}: guest={stages["guest"]["mean_ms"]:.3f} ms', flush=True)
    if sampler:
        for repeat in range(args.repeat):
            for build, host in (builds if repeat % 2 == 0 else builds[::-1]):
                for bounce in ((0, 1) if repeat % 2 == 0 else (1, 0)):
                    case = run(f'sample-{repeat}-{build}-bounce{bounce}', build, host, bounce, 'sample')
                    _, functions = symbol_tables[build]
                    summary = summarize_samples(case / 'samples.csv', functions)
                    results['samples'].append(dict(repeat=repeat, build=build, bounce=bounce, **summary))
                    save()
                    print(f'{case.name}: {summary["guest_samples"]} guest samples; '
                          f'{summary["exclusive"]}', flush=True)
    if args.validate:
        reference = None
        for repeat in range(2):
            for build, host in builds:
                for bounce in (0, 1):
                    case = run(f'validate-{repeat}-{build}-bounce{bounce}', build, host, bounce, 'validate')
                    if reference is None:
                        reference = case
                    compare_state(reference, case)
                    results['validation'].append(dict(case=case.name, equal=True))
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

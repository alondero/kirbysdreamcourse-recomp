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


# A still course view; a scripted shot (ball rolls, camera follows); the same
# shot while the orbit view turns, which also enables high refresh.
SHOT = 'wait 600\npress a 2\nwait 57\npress a 2\nwait 27\npress a 2\n'
SCENARIOS = {
    'still': dict(frames=605, screenshot=340, script=None, camera=None),
    'shot': dict(frames=1100, screenshot=800, script=SHOT, camera=None),
    'orbit': dict(frames=1100, screenshot=800, script=SHOT,
                  camera='500 32767 0 0\n700 0 32767 0\n900 -32767 0 0\n'),
}


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
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
    scenario = SCENARIOS[args.scenario]
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
                env.update(SDL_AUDIODRIVER='dummy', KDC_HD=str(hd),
                           SNESRECOMP_SCREENSHOT=str(case / 'frame.ppm'),
                           SNESRECOMP_SCREENSHOT_FRAME=str(scenario['screenshot']))
                command = [str(host), '--no-launcher', '--rom', str(rom), '--dev-hole', 'normal:1:1']
                if scenario['script']:
                    (case / 'shot.script').write_text(scenario['script'])
                    command += ['--script', str(case / 'shot.script')]
                if scenario['camera']:
                    (case / 'camera.script').write_text(scenario['camera'])
                    env['KDC_CAMERA_SCRIPT'] = str(case / 'camera.script')
                if not args.display:
                    env['SDL_VIDEODRIVER'] = 'dummy'
                # Encoding a 4x PPM takes hundreds of milliseconds, so it must
                # not land inside the timed window: the still view captures
                # before timing starts, the moving scenarios in a separate
                # untimed run of the same deterministic script.
                timed = dict(env, SNESRECOMP_RUN_FRAMES=str(scenario['frames']),
                             SNESRECOMP_HOST_PROFILE='1', SNESRECOMP_HOST_PROFILE_START_FRAME='350')
                if scenario['screenshot'] >= 350:
                    del timed['SNESRECOMP_SCREENSHOT'], timed['SNESRECOMP_SCREENSHOT_FRAME']
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
                captures[name, hd] = hashlib.sha256((case / 'frame.ppm').read_bytes()).hexdigest()
                result = dict(repeat=repeat, build=name, hd=hd, scenario=args.scenario,
                              fps=float(fps[1]), mean_ms=stages, max_ms=maxima,
                              screenshot_sha256=captures[name, hd])
                results.append(result)
                (output / 'results.json').write_text(json.dumps(results, indent=2))
                print(f'{repeat} {name} HD={hd}: raster={stages["raster-capture"]:.3f} ms '
                      f'compose={stages["compose"]:.3f} ms (max {maxima["compose"][0]:.1f} ms '
                      f'at {maxima["compose"][1]}) present={stages["upload-present"]:.3f} ms '
                      f'fps={float(fps[1]):.1f}', flush=True)
                if name == 'candidate' and hd and args.max_raster_ms is not None:
                    assert stages['raster-capture'] <= args.max_raster_ms, 'HD raster budget exceeded'
        if args.baseline:
            for hd in (0, 1):
                assert captures['baseline', hd] == captures['candidate', hd], f'HD={hd}: image changed'
    print('Completed; baseline images match.' if args.baseline else 'Completed.')


if __name__ == '__main__':
    main()

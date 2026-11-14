"""Replay issue #67 without captures; keep private outputs under local/."""
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
    parser.add_argument('--rom', type=Path, default=Path("Kirby's Dream Course (USA).sfc"))
    parser.add_argument('--display', action='store_true')
    parser.add_argument('--backend', choices=['SDL-Software', 'SDL', 'OpenGL'], default='SDL-Software')
    parser.add_argument('--vsync', action='store_true')
    parser.add_argument('--no-audio', action='store_true', help='Control without audio producer backpressure')
    parser.add_argument('--rate', type=int, choices=[60, 90, 120, 144, 165, 240, 360], default=120)
    parser.add_argument('--repeat', type=int, default=2)
    parser.add_argument('--frames', type=int, default=1800)
    parser.add_argument('--aspect', choices=['16:9', '32:9'], default='32:9')
    parser.add_argument('--hd', type=int, choices=[0, 1], nargs='+', default=[1, 0])
    parser.add_argument('--min-fps', type=float, default=57)
    args = parser.parse_args()
    if args.repeat < 1 or args.frames <= 650:
        parser.error('Require positive repeats and more than 650 frames')
    rom = args.rom.resolve(strict=True)
    if hashlib.sha256(rom.read_bytes()).hexdigest() != '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b':
        parser.error('Unsupported ROM SHA-256')
    executable = args.executable.resolve(strict=True)
    local = Path(__file__).resolve().parents[1] / 'local'
    local.mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='orbit-pacing-', dir=local))
    print(output, flush=True)
    results = []
    for repeat in range(args.repeat):
        for hd in args.hd:
            case = output / f'{repeat}-hd{hd}'
            case.mkdir()
            host = case / executable.name
            shutil.copy2(executable, host)
            for dll in executable.parent.glob('*.dll'):
                shutil.copy2(dll, case / dll.name)
            (case / 'config.ini').write_text(
                '[General]\nAutosave=0\nDisableFrameDelay=0\n'
                '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\nWidescreen=1\n'
                f'WindowSize=1536x896\nOutputMethod={args.backend}\nVSync={int(args.vsync)}\n'
                f'[Sound]\nEnableAudio={int(not args.no_audio)}\nAudioFreq=32000\n'
                '[Controller]\nSourceP1=0\nSourceP2=0\n'
                '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
            for name, value in [('presentation', f'1 {args.rate}'), ('camera', '1'),
                                ('widescreen', args.aspect), ('hud', 'edges'), ('hd', str(hd))]:
                (case / f'kdc-{name}.cfg').write_text(value + '\n')
            (case / 'camera.script').write_text('500 32767 -32768 0\n650 0 0 0\n')
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
            env.update(SDL_AUDIODRIVER='dummy', SNESRECOMP_RUN_FRAMES=str(args.frames),
                       SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                       SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1', SNESRECOMP_LLE_BOUNCE='1',
                       KDC_CAMERA_SCRIPT=str(case / 'camera.script'), KDC_ORBIT_PROFILE='1',
                       SNESRECOMP_HOST_PROFILE='1', SNESRECOMP_HOST_PROFILE_START_FRAME='350')
            if not args.display:
                env['SDL_VIDEODRIVER'] = 'dummy'
            with (case / 'run.log').open('w') as log:
                subprocess.run([str(host), '--no-launcher', '--rom', str(rom),
                                '--dev-hole', 'multi-extra:3:1'], cwd=case, env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
            log = (case / 'run.log').read_text()
            match = re.search(r'video profile window: first=(\d+) last=(\d+) seconds=([\d.]+) presentations=(\d+)', log)
            if not match:
                raise AssertionError(f'Missing profile: {case}')
            seconds = float(match[3])
            renderer = re.search(r'^Renderer: (.+)$', log, re.MULTILINE)
            result = dict(repeat=repeat, hd=hd, aspect=args.aspect, backend=args.backend,
                          display=args.display, vsync=args.vsync, rate=args.rate,
                          audio=not args.no_audio,
                          renderer=renderer[1] if renderer else None,
                          simulation_fps=(int(match[2]) - int(match[1]) + 1) / seconds,
                          presentation_fps=int(match[4]) / seconds,
                          mean_ms=dict((stage, float(mean)) for stage, mean in re.findall(
                              r'video profile: stage=(\S+) count=\d+ total_ms=[\d.]+ mean_ms=([\d.]+)', log)))
            results.append(result)
            (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(json.dumps(result), flush=True)
    if any(r['simulation_fps'] < args.min_fps for r in results):
        raise SystemExit(f'Simulation fell below {args.min_fps} fps; see {output}')


if __name__ == '__main__':
    main()

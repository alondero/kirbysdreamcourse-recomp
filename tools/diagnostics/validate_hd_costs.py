"""Compare the isolated HD cost probes with a frozen build; private output in local/."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from benchmark_hd import RUNTIME_ENV, SCENARIOS
from profile_guest import compare_state
from setup import ROM_SHA256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--diagnostic', required=True, type=Path)
    parser.add_argument('--rom', required=True, type=Path)
    args = parser.parse_args()
    rom = args.rom.resolve(strict=True)
    if hashlib.sha256(rom.read_bytes()).hexdigest() != ROM_SHA256:
        parser.error('Unsupported ROM SHA-256')
    builds = [('baseline', args.baseline.resolve(strict=True)),
              ('diagnostic', args.diagnostic.resolve(strict=True)),
              ('repeat', args.diagnostic.resolve(strict=True))]
    local = Path(__file__).resolve().parents[2] / 'local'
    local.mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='hd-costs-validation-', dir=local))
    print(output, flush=True)
    clean_env = {key: value for key, value in os.environ.items()
                 if not key.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
    guest_references, image_references = {}, {}
    for scenario_name in ('shot', 'orbit'):
        scenario = SCENARIOS[scenario_name]
        for threads in (1, 12):
            for hd in (0, 1):
                reference = None
                for name, executable in builds:
                    case = output / f'{scenario_name}-{threads}-hd{hd}-{name}'
                    case.mkdir()
                    host = case / executable.name
                    shutil.copy2(executable, host)
                    for dll in executable.parent.glob('*.dll'):
                        shutil.copy2(dll, case / dll.name)
                    (case / 'config.ini').write_text(
                        '[General]\nAutosave=0\nDisableFrameDelay=1\n'
                        '[Graphics]\nNewRenderer=1\nNoSpriteLimits=0\nWidescreen=1\n'
                        'WindowSize=1536x896\nRenderer=software\nVSync=0\n'
                        '[Sound]\nEnableAudio=0\n[Controller]\nSourceP1=0\nSourceP2=0\n'
                        '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
                    (case / 'shot.script').write_text(scenario['script'])
                    env = dict(clean_env, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                               KDC_HD=str(hd), KDC_THREADS=str(threads),
                               SNESRECOMP_RUN_FRAMES='1100',
                               SNESRECOMP_STATE_TRACE=str(case / 'state.csv'),
                               KDC_FRAME_LOG=str(case / 'frame.log'),
                               SNESRECOMP_SCREENSHOT=str(case / 'frame.ppm'),
                               SNESRECOMP_SCREENSHOT_FRAME='800')
                    env.update(RUNTIME_ENV)
                    if scenario['camera']:
                        (case / 'camera.script').write_text(scenario['camera'])
                        env['KDC_CAMERA_SCRIPT'] = str(case / 'camera.script')
                    if name != 'baseline':
                        env['KDC_COST_TRACE'] = str(case / 'cost.csv')
                        env['KDC_ORBIT_PROFILE'] = '1'
                    with (case / 'run.log').open('w') as log:
                        subprocess.run([str(host), '--no-launcher', '--rom', str(rom),
                                        '--dev-hole', 'normal:1:1', '--script', str(case / 'shot.script')],
                                       cwd=case, env=env, stdout=log, stderr=subprocess.STDOUT,
                                       check=True, timeout=600)
                    if 'RUN_FRAMES reached after 1100 frames' not in (case / 'run.log').read_text():
                        raise AssertionError(f'{case}: incomplete replay')
                    saves = list((case / 'saves').glob('*.srm'))
                    if len(saves) != 1:
                        raise AssertionError(f'{case}: expected one final SRAM file')
                    shutil.copy2(saves[0], case / 'save.srm')
                    if reference is None:
                        reference = case
                        if scenario_name in guest_references:
                            compare_state(guest_references[scenario_name], case)
                        else:
                            guest_references[scenario_name] = case
                        image_key = (scenario_name, hd)
                        if image_key in image_references:
                            if (image_references[image_key] / 'frame.ppm').read_bytes() != (case / 'frame.ppm').read_bytes():
                                raise AssertionError(f'{case}: worker count changed frame-800 pixels')
                        else:
                            image_references[image_key] = case
                    else:
                        compare_state(reference, case)
                        if (reference / 'frame.ppm').read_bytes() != (case / 'frame.ppm').read_bytes():
                            raise AssertionError(f'{case}: first captured image divergence at frame 800')
                    print(f'{case.name}: image, CPU/WRAM trace and SRAM match', flush=True)


if __name__ == '__main__':
    main()

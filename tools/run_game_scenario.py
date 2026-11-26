"""Run a bounded Kirby gameplay scenario and keep the evidence under local/."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

from input_recording import host_script, validate_recording


ROOT = Path(__file__).resolve().parents[1]
WRAM_SIZE = 0x20000
PREFERENCES = {
    'kdc-camera.cfg', 'kdc-courses.cfg', 'kdc-hd.cfg', 'kdc-hd-ui.cfg',
    'kdc-hud.cfg', 'kdc-presentation.cfg', 'kdc-transitions.cfg', 'kdc-widescreen.cfg',
}


def fields(value, name, required, optional=()):
    if (not isinstance(value, dict) or set(required) - value.keys() or
            value.keys() - set(required) - set(optional)):
        raise ValueError(f'{name} must contain {", ".join(required)}'
                         + (f' (optional: {", ".join(optional)})' if optional else ''))


def integer(value, name, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f'{name} must be an integer in [{minimum}, {maximum}]')
    return value


def load_scenario(path):
    scenario = json.loads(path.read_text(encoding='utf-8-sig'))
    fixtures = ('recording', 'initial_sram', 'course_pack', 'camera_script')
    fields(scenario, 'scenario', ('frames',), (
        'dev_hole', 'dev_two_player', 'events', 'capture_from', 'screenshot',
        'checks', 'preferences', *fixtures,
    ))
    frames = integer(scenario['frames'], 'frames', 1, 2**31 - 1)
    dev_hole = scenario.get('dev_hole')
    if 'dev_hole' in scenario and (not isinstance(dev_hole, str) or not dev_hole.strip()):
        raise ValueError('dev_hole must be a nonempty selector')
    two_player = scenario.get('dev_two_player', False)
    if type(two_player) is not bool or (two_player and not (dev_hole or '').startswith('map:')):
        raise ValueError('dev_two_player must be a boolean, true requires a map: selector')
    for name in fixtures:
        if name in scenario:
            if not isinstance(scenario[name], str) or not scenario[name].strip():
                raise ValueError(f'{name} must be a fixture path')
            scenario[name] = (path.parent / scenario[name]).resolve(strict=True)
    if 'recording' in scenario and 'events' in scenario:
        raise ValueError('choose either events or recording')

    checks = scenario.setdefault('checks', [])
    if not isinstance(checks, list):
        raise ValueError('checks must be a list')
    for check in checks:
        fields(check, 'check', ('label', 'frame', 'offset'), ('bytes', 'equals', 'min', 'max'))
        if not isinstance(check['label'], str) or not check['label'].strip():
            raise ValueError('check label must be a nonempty string')
        integer(check['frame'], 'check frame', 0, frames - 1)
        width = integer(check.get('bytes', 2), 'check bytes', 1, 4)
        if width == 3:
            raise ValueError('check bytes must be 1, 2 or 4')
        offset = check['offset']
        if isinstance(offset, str):
            offset = int(offset, 0)
        check['offset'] = integer(offset, 'WRAM offset', 0, WRAM_SIZE - width)
        bounds = set(check) & {'equals', 'min', 'max'}
        if not bounds or ('equals' in bounds and len(bounds) != 1):
            raise ValueError('check requires equals or min/max')
        for bound in bounds:
            integer(check[bound], bound, 0, 2**(8 * width) - 1)
        if check.get('min', 0) > check.get('max', 2**(8 * width) - 1):
            raise ValueError('check min must be at most max')
    first_check = min((check['frame'] for check in checks), default=frames - 1)
    scenario['capture_from'] = integer(scenario.get('capture_from', first_check),
                                       'capture_from', 0, first_check)
    screenshot = scenario.setdefault('screenshot', frames - 1)
    if screenshot is not None:
        integer(screenshot, 'screenshot', 0, frames - 1)
    preferences = scenario.get('preferences', {})
    if (not isinstance(preferences, dict) or preferences.keys() - PREFERENCES or
            any(not isinstance(value, str) for value in preferences.values())):
        raise ValueError('preferences must map known kdc-*.cfg filenames to text')
    return scenario


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def stage_run(scenario, args, output, result):
    host = output / args.exe.name
    shutil.copy2(args.exe, host)
    for dll in args.exe.parent.glob('*.dll'):
        shutil.copy2(dll, output)
    assets = args.exe.parent / 'assets'
    if assets.is_dir():
        shutil.copytree(assets, output / 'assets')
    result['executable_sha256'] = digest(host)

    env = {name: value for name, value in os.environ.items()
           if not name.upper().startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
    managed = dict(SDL_AUDIODRIVER='dummy', SNESRECOMP_RUN_FRAMES=str(scenario['frames']),
                   SNESRECOMP_FRAMEDUMP_START=str(scenario['capture_from']),
                   SNESRECOMP_FRAMEDUMP_END=str(scenario['frames'] - 1),
                   SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                   SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1',
                   SNESRECOMP_LLE_BOUNCE='0' if args.fallback else '1')
    if not args.windowed:
        managed['SDL_VIDEODRIVER'] = 'dummy'
    if scenario['screenshot'] is not None:
        managed.update(SNESRECOMP_SCREENSHOT=str(output / 'screenshot.ppm'),
                       SNESRECOMP_SCREENSHOT_FRAME=str(scenario['screenshot'] + 1))

    command = [str(host), '--no-launcher', '--rom', str(args.rom),
               '--framedump', str(output / 'frames')]
    resolved = scenario.copy()
    dev_hole = scenario.get('dev_hole', '')
    if dev_hole:
        command += ['--dev-hole', dev_hole]
    if scenario.get('dev_two_player'):
        command.append('--dev-two-player')
    if 'course_pack' in scenario:
        pack = output / 'course.ips'
        shutil.copyfile(scenario['course_pack'], pack)
        result['course_pack_sha256'] = digest(pack)
        resolved['course_pack'] = pack.name
        command += ['--course-pack', str(pack)]
    if 'camera_script' in scenario:
        camera = output / 'camera.script'
        shutil.copyfile(scenario['camera_script'], camera)
        result['camera_script_sha256'] = digest(camera)
        resolved['camera_script'] = camera.name
        managed['KDC_CAMERA_SCRIPT'] = str(camera)

    saves = output / 'saves'
    if dev_hole.startswith(('pack:', 'pack-2p:')):
        saves /= 'custom'
    saves.mkdir(parents=True)
    sram = scenario['initial_sram'].read_bytes() if 'initial_sram' in scenario else bytes(8192)
    if len(sram) != 8192:
        raise ValueError('initial_sram must be exactly 8192 bytes')
    (output / 'initial.srm').write_bytes(sram)
    (saves / 'save.srm').write_bytes(sram)
    result['initial_sram_sha256'] = hashlib.sha256(sram).hexdigest()
    resolved['initial_sram'] = 'initial.srm'

    recording = {'frames': scenario['frames'], 'events': scenario.get('events', [])}
    if 'recording' in scenario:
        recording = json.loads(scenario['recording'].read_text(encoding='utf-8-sig'))
    validate_recording(recording)
    if recording['frames'] != scenario['frames']:
        raise ValueError('recording frame count differs from scenario')
    write_json(output / 'input.json', recording)
    result['input_sha256'] = digest(output / 'input.json')
    resolved.pop('events', None)
    resolved['recording'] = 'input.json'
    if recording['events']:
        script = output / 'input.script'
        script.write_text(host_script(recording), encoding='ascii', newline='\n')
        command += ['--script', str(script)]

    delay = 0 if args.paced or args.windowed else 1
    config = (f'[General]\nAutosave = 0\nDisableFrameDelay = {delay}\n'
              f'[Graphics]\nOutputMethod = {args.renderer}\nNewRenderer = 1\n'
              'NoSpriteLimits = 0\nWidescreen = 0\n'
              '[Sound]\nEnableAudio = 1\nAudioFreq = 32000\nAudioChannels = 2\nAudioSamples = 512\n'
              '[Controller]\nSourceP1 = 0\nSourceP2 = 0\n'
              '[GamepadMap]\nEnableGamepad1 = false\nEnableGamepad2 = false\n')
    (output / 'config.initial.ini').write_text(config, encoding='ascii')
    shutil.copyfile(output / 'config.initial.ini', output / 'config.ini')
    result['initial_config_sha256'] = digest(output / 'config.initial.ini')
    for name, value in scenario.get('preferences', {}).items():
        (output / name).write_text(value, encoding='utf-8', newline='\n')
    write_json(output / 'scenario-resolved.json', resolved)

    result.update(command=command, environment=managed)
    return command, env | managed, saves / 'save.srm', len(recording['events'])


def capture_png(ppm, target):
    magic, dimensions, depth, pixels = ppm.read_bytes().split(b'\n', 3)
    width, height = map(int, dimensions.split())
    stride = 3 * width
    if magic != b'P6' or depth != b'255' or min(width, height) <= 0 or len(pixels) != stride * height:
        raise ValueError('invalid or truncated composed screenshot')

    def chunk(kind, data):
        payload = kind + data
        return struct.pack('>I', len(data)) + payload + struct.pack('>I', zlib.crc32(payload))

    rows = b''.join(b'\0' + pixels[start:start + stride] for start in range(0, len(pixels), stride))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    target.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) +
                       chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    return [width, height]


def inspect_run(scenario, output, result, sram, event_count):
    failures = result['failures']
    log = (output / 'stderr.log').read_text(encoding='utf-8', errors='replace')
    frames = scenario['frames']
    if f'exit: RUN_FRAMES reached after {frames} frames\n' not in log:
        failures.append(f'host did not report completing its {frames}-frame budget')
    renderer = re.search(r'renderer initialized: (\S+)', log)
    if renderer:
        result['renderer'] = renderer[1]
    if event_count and f'script: loaded {event_count} entries from ' not in log:
        failures.append(f'host did not report loading {event_count} input events')
    if 'camera_script' in scenario and 'Camera script unavailable or invalid' in log:
        failures.append('host rejected the camera script')

    def wram_path(frame):
        return output / 'frames' / f'frame_{frame:06}_wram.bin'

    missing = next((frame for frame in range(scenario['capture_from'], frames)
                    if not (path := wram_path(frame)).is_file() or path.stat().st_size != WRAM_SIZE), None)
    if missing is None:
        result.update(final_wram_sha256=digest(wram_path(frames - 1)),
                      final_sram_sha256=digest(sram) if sram.is_file() else None)
    else:
        failures.append(f'first missing or incomplete WRAM dump at frame {missing}')

    result['checks'] = []
    for check in sorted(scenario['checks'], key=lambda item: item['frame']):
        path = wram_path(check['frame'])
        actual = None
        if path.is_file() and path.stat().st_size == WRAM_SIZE:
            offset = check['offset']
            with path.open('rb') as memory:
                memory.seek(offset)
                actual = int.from_bytes(memory.read(check.get('bytes', 2)), 'little')
        passed = (actual is not None and check.get('min', actual) <= actual <= check.get('max', actual)
                  and actual == check.get('equals', actual))
        result['checks'].append(dict(check, actual=actual, passed=passed))
        if not passed:
            expected = ', '.join(f'{key} {check[key]}' for key in ('equals', 'min', 'max')
                                 if key in check)
            failures.append(f"frame {check['frame']}: {check['label']} (expected {expected}, observed {actual})")

    if scenario['screenshot'] is not None:
        present = scenario['screenshot'] + 1
        if not re.search(rf'screenshot: wrote .* at frame {present}\s*$', log, re.MULTILINE):
            failures.append(f'no composed screenshot at frame {scenario["screenshot"]}')
        else:
            png = output / 'screenshot.png'
            dimensions = capture_png(output / 'screenshot.ppm', png)
            result['screenshot'] = dict(frame=scenario['screenshot'], path=png.name,
                                        dimensions=dimensions, sha256=digest(png))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scenario', type=Path)
    parser.add_argument('--exe', type=Path, default=ROOT / 'local/guarded-build' /
                        ('KirbyDreamCourseRecomp.exe' if os.name == 'nt' else 'KirbyDreamCourseRecomp'))
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--fallback', action='store_true', help='use interpreter fallback')
    parser.add_argument('--windowed', action='store_true', help='use a desktop window and real-time pacing')
    parser.add_argument('--paced', action='store_true', help='use real-time pacing with dummy video')
    parser.add_argument('--renderer', choices=('SDL', 'SDL-Software', 'OpenGL'), default='SDL')
    parser.add_argument('--timeout', type=float, default=120, help='wall-clock deadline in seconds')
    args = parser.parse_args()
    try:
        if not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError('timeout must be positive and finite')
        if args.renderer == 'OpenGL' and not args.windowed:
            raise ValueError('OpenGL requires --windowed')
        identity = dict(tuple(part.strip().strip('"') for part in line.split('=', 1))
                        for line in (ROOT / 'rom_identity.txt').read_text(encoding='utf-8').splitlines()
                        if line.strip() and not line.lstrip().startswith('#'))
        args.exe = args.exe.resolve(strict=True)
        args.rom = (args.rom or ROOT / identity['rom_file']).resolve(strict=True)
        rom_hash = digest(args.rom)
        if rom_hash != identity['expected_sha256']:
            raise ValueError('ROM SHA-256 mismatch; supply the supported USA image')
        scenario = load_scenario(args.scenario)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    runs = ROOT / 'local/scenarios'
    runs.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=runs))
    print(output, flush=True)
    result = dict(schema=1, rom_sha256=rom_hash, frames=scenario['frames'],
                  capture_from=scenario['capture_from'], timeout_seconds=args.timeout, failures=[])
    try:
        shutil.copyfile(args.scenario, output / 'scenario.json')
        command, env, sram, event_count = stage_run(scenario, args, output, result)
        with (output / 'stdout.log').open('wb') as stdout, (output / 'stderr.log').open('wb') as stderr:
            run = subprocess.run(command, cwd=output, env=env, stdout=stdout, stderr=stderr,
                                 timeout=args.timeout)
        result['exit_code'] = run.returncode
        if run.returncode:
            result['failures'].append(f'host exited with code {run.returncode}')
        inspect_run(scenario, output, result, sram, event_count)
    except subprocess.TimeoutExpired:
        result['failures'].append(f'host timed out after {args.timeout:g} seconds')
    except (OSError, ValueError) as error:
        result['failures'].append(str(error))
    except KeyboardInterrupt:
        result['failures'].append('run interrupted')
    result['passed'] = not result['failures']
    write_json(output / 'result.json', result)
    for failure in result['failures']:
        print(f'FAIL: {failure}')
    if result['passed']:
        print(f'PASS: {scenario["frames"]} frames, {len(result["checks"])} gameplay checks')
    return int(not result['passed'])


if __name__ == '__main__':
    raise SystemExit(main())

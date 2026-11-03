#!/usr/bin/env python3
"""Capture a local developer-start gallery for the supported USA ROM."""
import argparse
import concurrent.futures
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
ROM_HASH = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
NAMES = ('Whispy Woods', 'Mr. Shine & Mr. Bright', 'Kracko', 'Gordo')
DEMOS = {0: 1, 42: 2, 44: 3, 77: 4}
CONFIG = '''[General]
Autosave = 0
DisableFrameDelay = 1
[Graphics]
NewRenderer = 1
NoSpriteLimits = 0
Widescreen = 0
[Sound]
EnableAudio = 1
AudioFreq = 32000
AudioChannels = 2
AudioSamples = 512
[Controller]
SourceP1 = 0
SourceP2 = 0
[GamepadMap]
EnableGamepad1 = false
EnableGamepad2 = false
'''


def describe(i):
    hole = i % 8 + 1
    if i < 64:
        selector, label = f'normal:{i // 8 + 1}:{hole}', f'Course {i // 8 + 1}, hole {hole}'
    elif i < 128:
        course = (i - 64) // 8 + 1
        selector, label = f'extra:{course}:{hole}', f'Extra course {course}, hole {hole}'
    elif i < 160:
        course = (i - 128) // 8 + 1
        selector, label = f'multi:{course}:{hole}', f'{NAMES[course - 1]}, hole {hole}'
    elif i < 192:
        selector = f'map:{i}'
        label = (f'Demo Play course {(i - 160) // 8 + 1}, hole {hole}'
                 if i < 180 else 'Unclassified raw slot')
    elif i < 224:
        course = (i - 192) // 8 + 1
        selector, label = f'multi-extra:{course}:{hole}', f'Extra {NAMES[course - 1]}, hole {hole}'
    else:
        selector, label = f'map:{i}', 'Unclassified raw slot'
    if i in DEMOS:
        label += f'; attract demo {DEMOS[i]}'
    if i == 184:
        label += '; opening-sequence map data'
    return selector, label


def bmp_to_png(source, destination):
    """Convert the host's uncompressed 32-bit BMP without extra dependencies."""
    bmp = source.read_bytes()
    offset = struct.unpack_from('<I', bmp, 10)[0]
    header, width, height, planes, bits, compression = struct.unpack_from('<IiiHHI', bmp, 14)
    if bmp[:2] != b'BM' or header < 40 or width <= 0 or height == 0 or planes != 1 or bits != 32 or compression:
        raise ValueError('Unsupported framebuffer BMP format')
    rows = []
    for y in range(abs(height)):
        row = y if height < 0 else height - 1 - y
        pixels = bmp[offset + row * width * 4:offset + (row + 1) * width * 4]
        if len(pixels) != width * 4:
            raise ValueError('Truncated framebuffer BMP')
        rgb = bytearray(width * 3)
        for x in range(width):
            b, g, r = pixels[x * 4:x * 4 + 3]
            rgb[x * 3:x * 3 + 3] = bytes((r, g, b))
        rows.append(b'\x00' + rgb)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    destination.write_bytes(b'\x89PNG\r\n\x1a\n' +
                            chunk(b'IHDR', struct.pack('>IIBBBBB', width, abs(height), 8, 2, 0, 0, 0)) +
                            chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))


def capture(i, executable, rom, output):
    selector, _ = describe(i)
    with tempfile.TemporaryDirectory(prefix=f'hole-{i:03}-', dir=output) as temp:
        case = Path(temp)
        host = case / 'game.exe'
        shutil.copy2(executable, host)
        (case / 'config.ini').write_text(CONFIG, encoding='ascii')
        env = {k: v for k, v in os.environ.items() if not k.startswith(('SNESRECOMP_', 'KDC_', 'LNG_', 'SDL_'))}
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SNESRECOMP_FRAMEDUMP_PIXELS='1',
                   SNESRECOMP_RUN_FRAMES='600', SNESRECOMP_FRAMEDUMP_START='599', SNESRECOMP_FRAMEDUMP_END='599',
                   SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                   SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1', SNESRECOMP_LLE_BOUNCE='1')
        result = subprocess.run([str(host), '--no-launcher', '--rom', str(rom), '--dev-hole', selector,
                                 '--framedump', str(case / 'frames')], env=env, capture_output=True, timeout=150)
        stderr = result.stderr.decode(errors='replace')
        if result.returncode or f'Developer start: map {i},' not in stderr:
            raise RuntimeError(f'host exited {result.returncode} or did not report map {i}: {stderr[-1000:]}')
        frame = case / 'frames' / 'frame_000599'
        ram = frame.with_name(frame.name + '_wram.bin').read_bytes()
        if len(ram) < 0xabb4 or int.from_bytes(ram[0xabb2:0xabb4], 'little') != i:
            raise RuntimeError('final WRAM does not contain the requested map ID')
        bmp_to_png(frame.with_suffix('.bmp'), output / 'images' / f'{i:03}.png')


def parse_ids(value):
    ids = set()
    try:
        for part in value.split(','):
            ends = part.split('-')
            if len(ends) == 1:
                start = end = int(ends[0])
            elif len(ends) == 2:
                start, end = map(int, ends)
            else:
                raise ValueError
            if not 0 <= start <= end <= 255:
                raise ValueError
            ids.update(range(start, end + 1))
    except ValueError as exc:
        raise argparse.ArgumentTypeError('IDs must be 0-255, e.g. 0,42,160-179') from exc
    return sorted(ids)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=ROOT / "Kirby's Dream Course (USA).sfc")
    parser.add_argument('--executable', type=Path, default=ROOT / 'local/guarded-build/KirbyDreamCourseRecomp.exe')
    parser.add_argument('--output', type=Path, default=ROOT / 'local/hole-gallery')
    parser.add_argument('--ids', type=parse_ids, default=parse_ids('0-255'))
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    rom, executable, output = args.rom.resolve(), args.executable.resolve(), args.output.resolve()
    if not output.is_relative_to((ROOT / 'local').resolve()) or output == (ROOT / 'local').resolve():
        parser.error('output must be a new directory inside local/')
    if output.exists():
        parser.error('output already exists; choose a fresh directory')
    if not rom.is_file() or hashlib.sha256(rom.read_bytes()).hexdigest() != ROM_HASH:
        parser.error('ROM missing or SHA-256 does not match the supported USA ROM')
    if not executable.is_file():
        parser.error('executable not found; run tools/setup.ps1 or supply --executable')
    (output / 'images').mkdir(parents=True)
    failures = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(capture, i, executable, rom, output): i for i in args.ids}
        for future in concurrent.futures.as_completed(futures):
            i = futures[future]
            try:
                future.result()
                print(f'{i}: captured', flush=True)
            except Exception as exc:
                failures[i] = str(exc)
                print(f'{i}: FAILED: {exc}', flush=True)
    lines = ['# Local hole gallery', '', 'Initial viewport at frame 599 of a zero-input developer start.',
             'Raw slots use one-player context. Captures and game-derived output must remain local.', '',
             '| ID | Correspondence | Selector | Screenshot |', '| ---: | --- | --- | --- |']
    for i in args.ids:
        selector, label = describe(i)
        image = 'Capture failed' if i in failures else f'![Map {i}](images/{i:03}.png)'
        lines.append(f'| {i} | {label} | `{selector}` | {image} |')
    (output / 'index.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Gallery: {output / "index.md"}; {len(failures)} capture(s) failed')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())

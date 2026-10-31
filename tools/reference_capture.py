"""Run the pinned Snes9x libretro core and hash frame boundaries.

Output contains game-derived fingerprints and belongs in the ignored local directory.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
from input_recording import parse_buttons, read_recording

ROM_SHA256 = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
CORE_SHA256 = '72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def input_masks(frames, inputs, button_ids, port=0):
    """Compile validated one-based input intervals for one controller port."""
    if port not in (0, 1):
        raise ValueError('only controller ports 0 and 1 are supported')
    masks = [0] * (frames + 1)
    for item in inputs:
        bit = 0
        for button in parse_buttons(item['button']):
            button_port = int(button.startswith('p2:'))
            if button_port == port:
                name = button[3:] if button_port else button
                bit |= 1 << button_ids[name]
        for frame in range(item['frame'], item['frame'] + item['count']):
            masks[frame] |= bit
    return masks

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--core-sha256', default=CORE_SHA256,
                        help='explicit SHA-256 for a diagnostic core; defaults to the pinned binary')
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--reset-frame', type=int, action='append', default=[],
                        help='reset immediately before this 1-based frame; may be repeated')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--roundtrip-frame', type=int,
                        help='save after this frame and replay the remaining inputs to check state restoration')
    parser.add_argument('--wram-frame', type=int, action='append', default=[],
                        help='also save this frame WRAM beside output; may be repeated')
    parser.add_argument('--vram-frame', type=int, action='append', default=[],
                        help='also save this frame VRAM beside output; may be repeated')
    parser.add_argument('--sram-frame', type=int, action='append', default=[],
                        help='also save this frame SRAM beside output; may be repeated')
    parser.add_argument('--image-frame', type=int, action='append', default=[], help='also save this frame as a local PPM; may be repeated')
    parser.add_argument('--audio-raw', action='store_true', help='save local interleaved stereo s16le samples and per-frame counts')
    parser.add_argument('--initial-sram', type=Path, help='8192-byte save image; default all zero')
    parser.add_argument('--press', action='append', default=[], metavar='FRAME:COUNT:BUTTON',
                        help='hold buttons or a + separated chord for COUNT frames; prefix player-two buttons with p2:')
    parser.add_argument('--input-recording', type=Path,
                        help='validated frame-numbered JSON recording shared with the host')
    args = parser.parse_args()
    if args.roundtrip_frame is not None and not 1 <= args.roundtrip_frame < args.frames:
        parser.error('--roundtrip-frame must be at least 1 and less than --frames')
    if args.frames <= 0:
        parser.error('--frames must be positive')
    if any(not 1 <= frame <= args.frames for frame in args.reset_frame):
        parser.error('--reset-frame must be within the run')
    if len(set(args.reset_frame)) != len(args.reset_frame):
        parser.error('--reset-frame must not be repeated for the same frame')
    if any(frame < 1 or frame > args.frames for frame in args.image_frame):
        parser.error('--image-frame must be within the run')
    if any(frame < 1 or frame > args.frames for frame in args.vram_frame):
        parser.error('--vram-frame must be within the run')
    button_ids = {'b': 0, 'y': 1, 'select': 2, 'start': 3, 'up': 4, 'down': 5,
                  'left': 6, 'right': 7, 'a': 8, 'x': 9, 'l': 10, 'r': 11}
    inputs = []
    if args.input_recording:
        if args.press:
            parser.error('--input-recording cannot be combined with --press')
        try:
            recording = read_recording(args.input_recording)
            if recording['frames'] != args.frames:
                raise ValueError('recording frame count differs from requested run')
            inputs = recording['events']
        except (ValueError, OSError) as error:
            parser.error(str(error))
    for press in args.press:
        try:
            start, count, name = press.lower().split(':', 2)
            start, count = int(start), int(count)
            if start < 1 or count < 1 or start + count - 1 > args.frames:
                raise ValueError()
            parse_buttons(name)
        except ValueError:
            parser.error(f'invalid --press: {press}')
        inputs.append({'frame': start, 'count': count, 'button': name})
    masks = [input_masks(args.frames, inputs, button_ids, port) for port in (0, 1)]
    if digest(args.rom) != ROM_SHA256:
        raise SystemExit('ROM SHA-256 mismatch')
    if digest(args.core) != args.core_sha256.lower():
        raise SystemExit('Snes9x core SHA-256 mismatch')

    class GameInfo(c.Structure):
        _fields_ = [('path', c.c_char_p), ('data', c.c_void_p),
                    ('size', c.c_size_t), ('meta', c.c_char_p)]
    class SystemInfo(c.Structure):
        _fields_ = [('library_name', c.c_char_p), ('library_version', c.c_char_p),
                    ('valid_extensions', c.c_char_p), ('need_fullpath', c.c_bool),
                    ('block_extract', c.c_bool)]

    class Geometry(c.Structure):
        _fields_ = [('base_width', c.c_uint), ('base_height', c.c_uint),
                    ('max_width', c.c_uint), ('max_height', c.c_uint), ('aspect_ratio', c.c_float)]
    class Timing(c.Structure):
        _fields_ = [('fps', c.c_double), ('sample_rate', c.c_double)]
    class AvInfo(c.Structure):
        _fields_ = [('geometry', Geometry), ('timing', Timing)]

    env_t = c.CFUNCTYPE(c.c_bool, c.c_uint, c.c_void_p)
    video_t = c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t)
    sample_t = c.CFUNCTYPE(None, c.c_int16, c.c_int16)
    batch_t = c.CFUNCTYPE(c.c_size_t, c.POINTER(c.c_int16), c.c_size_t)
    poll_t = c.CFUNCTYPE(None)
    input_t = c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint)
    pixel_format = 0  # libretro defaults to 0RGB1555
    frame = None
    pixels = None
    audio = hashlib.sha256()
    audio_pcm = bytearray()
    audio_frame_counts = []
    current_frame = 0
    replaying = False
    @env_t
    def environment(command, data):
        nonlocal pixel_format
        if command == 10:  # SET_PIXEL_FORMAT
            pixel_format = c.cast(data, c.POINTER(c.c_uint))[0]
            return pixel_format in (0, 1, 2)
        if command == 3:  # GET_CAN_DUPE
            c.cast(data, c.POINTER(c.c_bool))[0] = True
            return True
        if command == 17:  # GET_VARIABLE_UPDATE
            c.cast(data, c.POINTER(c.c_bool))[0] = False
            return True
        return False
    @video_t
    def video(data, width, height, pitch):
        nonlocal frame, pixels
        if data:
            bpp = 4 if pixel_format == 1 else 2
            rows = [c.string_at(data + y * pitch, width * bpp) for y in range(height)]
            pixels = b''.join(rows)
            frame = {'width': width, 'height': height, 'pixel_format': pixel_format,
                     'sha256': hashlib.sha256(pixels).hexdigest()}
    @sample_t
    def sample(left, right):
        data = int(left).to_bytes(2, 'little', signed=True) + int(right).to_bytes(2, 'little', signed=True)
        audio.update(data)
        if args.audio_raw and not replaying:
            audio_pcm.extend(data)
    @batch_t
    def batch(data, count):
        samples = c.string_at(data, count * 4)
        audio.update(samples)
        if args.audio_raw and not replaying:
            audio_pcm.extend(samples)
        return count
    @poll_t
    def poll():
        pass
    @input_t
    def input_state(port, device, index, ident):
        if port not in (0, 1) or device != 1 or index != 0 or ident >= 12:
            return 0
        return int(bool(masks[port][current_frame] & (1 << ident)))

    core = c.CDLL(str(args.core.resolve()))
    for name, callback, typ in [('retro_set_environment', environment, env_t),
                                ('retro_set_video_refresh', video, video_t),
                                ('retro_set_audio_sample', sample, sample_t),
                                ('retro_set_audio_sample_batch', batch, batch_t),
                                ('retro_set_input_poll', poll, poll_t),
                                ('retro_set_input_state', input_state, input_t)]:
        fn = getattr(core, name)
        fn.argtypes = [typ]
        fn(callback)
    core.retro_get_system_info.argtypes = [c.POINTER(SystemInfo)]
    info = SystemInfo()
    core.retro_get_system_info(c.byref(info))
    core.retro_init()
    core.retro_load_game.argtypes = [c.POINTER(GameInfo)]
    core.retro_load_game.restype = c.c_bool
    rom = args.rom.read_bytes()
    rom_buffer = c.create_string_buffer(rom)
    game = GameInfo(str(args.rom.resolve()).encode(), c.cast(rom_buffer, c.c_void_p), len(rom), None)
    if not core.retro_load_game(c.byref(game)):
        raise SystemExit('Snes9x rejected the ROM')
    av_info = AvInfo()
    core.retro_get_system_av_info.argtypes = [c.POINTER(AvInfo)]
    core.retro_get_system_av_info(c.byref(av_info))
    core.retro_get_memory_data.argtypes = [c.c_uint]
    core.retro_get_memory_data.restype = c.c_void_p
    core.retro_get_memory_size.argtypes = [c.c_uint]
    core.retro_get_memory_size.restype = c.c_size_t
    def memory(kind):
        ptr, size = core.retro_get_memory_data(kind), core.retro_get_memory_size(kind)
        return c.string_at(ptr, size) if ptr and size else b''
    # Set a known SRAM image after loading, before the first frame.
    ptr, size = core.retro_get_memory_data(0), core.retro_get_memory_size(0)
    if not ptr or size != 8192:
        raise SystemExit(f'Unexpected SRAM size: {size}')
    initial_sram = args.initial_sram.read_bytes() if args.initial_sram else bytes(size)
    if len(initial_sram) != size:
        raise SystemExit(f'Initial SRAM must be {size} bytes')
    c.memmove(ptr, initial_sram, size)
    wram_ptr, wram_size = core.retro_get_memory_data(2), core.retro_get_memory_size(2)
    if not wram_ptr or wram_size != 131072:
        raise SystemExit(f'Unexpected WRAM size: {wram_size}')
    # The framework hard reset zeroes WRAM; Snes9x defaults to 0x55.
    c.memset(wram_ptr, 0, wram_size)
    core.retro_serialize_size.restype = c.c_size_t
    core.retro_serialize.argtypes = [c.c_void_p, c.c_size_t]
    core.retro_serialize.restype = c.c_bool
    core.retro_unserialize.argtypes = [c.c_void_p, c.c_size_t]
    core.retro_unserialize.restype = c.c_bool
    snapshot = None
    records = []
    for i in range(1, args.frames + 1):
        current_frame = i
        if i in args.reset_frame:
            core.retro_reset()
        audio = hashlib.sha256()
        before_audio = len(audio_pcm)
        core.retro_run()
        if args.audio_raw:
            audio_frame_counts.append((len(audio_pcm) - before_audio) // 4)
        if frame is None:
            raise SystemExit(f'No framebuffer at frame {i}')
        wram = memory(2)
        if len(wram) != 131072:
            raise SystemExit(f'Unexpected WRAM size at frame {i}: {len(wram)}')
        if i in args.wram_frame:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.with_name(f'{args.output.stem}.frame{i:06}.wram.bin').write_bytes(wram)
        if i in args.vram_frame:
            vram = memory(3)
            if len(vram) != 65536:
                raise SystemExit(f'Unexpected VRAM size at frame {i}: {len(vram)}')
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.with_name(f'{args.output.stem}.frame{i:06}.vram.bin').write_bytes(vram)
        sram = memory(0)
        if len(sram) != 8192:
            raise SystemExit(f'Unexpected SRAM size at frame {i}: {len(sram)}')
        if i in args.sram_frame:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.with_name(f'{args.output.stem}.frame{i:06}.sram.bin').write_bytes(sram)
        if i in args.image_frame:
            if pixel_format != 2:
                raise SystemExit('PPM export currently supports RGB565 only')
            rgb = bytearray()
            for offset in range(0, len(pixels), 2):
                color = int.from_bytes(pixels[offset:offset + 2], 'little')
                r, g, b = (color >> 11) & 31, (color >> 5) & 63, color & 31
                rgb.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
            args.output.parent.mkdir(parents=True, exist_ok=True)
            image_path = (args.output.with_suffix('.ppm') if len(args.image_frame) == 1
                          else args.output.with_name(f'{args.output.stem}.frame{i:06}.ppm'))
            image_path.write_bytes(
                f"P6\n{frame['width']} {frame['height']}\n255\n".encode() + rgb)
        records.append({'frame': i, 'video': frame, 'audio_sha256': audio.hexdigest(),
                        'wram_sha256': hashlib.sha256(wram).hexdigest(),
                        'sram_sha256': hashlib.sha256(sram).hexdigest()})
        if i == args.roundtrip_frame:
            size = core.retro_serialize_size()
            if not size:
                raise SystemExit('Reference core does not support serialization')
            snapshot = c.create_string_buffer(size)
            if not core.retro_serialize(snapshot, size):
                raise SystemExit('Reference state serialization failed')
    if snapshot is not None:
        replaying = True
        if not core.retro_unserialize(snapshot, len(snapshot)):
            raise SystemExit('Reference state restoration failed')
        # A NULL video callback repeats the preceding frame in libretro.
        frame = records[args.roundtrip_frame - 1]['video']
        for i in range(args.roundtrip_frame + 1, args.frames + 1):
            current_frame = i
            if i in args.reset_frame:
                core.retro_reset()
            audio = hashlib.sha256()
            core.retro_run()
            actual = {'frame': i, 'video': frame, 'audio_sha256': audio.hexdigest(),
                      'wram_sha256': hashlib.sha256(memory(2)).hexdigest(),
                      'sram_sha256': hashlib.sha256(memory(0)).hexdigest()}
            expected = records[i - 1]
            differences = [key for key in actual if actual[key] != expected[key]]
            if differences:
                raise SystemExit(f'Reference round-trip first divergence at frame {i}: ' + ', '.join(differences))
    result = {'core_sha256': args.core_sha256.lower(), 'rom_sha256': ROM_SHA256,
              'core_name': info.library_name.decode(), 'core_version': info.library_version.decode(),
              'initial_sram': hashlib.sha256(initial_sram).hexdigest() if args.initial_sram else 'zero',
              'initial_wram': 'zero',
              'input': inputs if inputs else 'none', 'frames': records}
    if snapshot is not None:
        result['roundtrip'] = {'saved_after_frame': args.roundtrip_frame,
                               'replayed_through_frame': args.frames,
                               'compared': ['video', 'audio_sha256', 'wram_sha256', 'sram_sha256']}
    if len(args.reset_frame) == 1:
        result['reset_before_frame'] = args.reset_frame[0]
    elif args.reset_frame:
        result['reset_before_frames'] = sorted(args.reset_frame)
    if args.input_recording:
        result['input_recording_sha256'] = digest(args.input_recording)
    if args.audio_raw:
        raw_path = args.output.with_suffix('.audio.s16le')
        raw_path.parent.mkdir(parents=True, exist_ok=True)
        raw_path.write_bytes(audio_pcm)
        result['audio_raw'] = {'format': 's16le stereo interleaved',
                               'sample_rate_hz': av_info.timing.sample_rate,
                               'sample_frames_per_video_frame': audio_frame_counts,
                               'sha256': hashlib.sha256(audio_pcm).hexdigest()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    core.retro_unload_game()
    core.retro_deinit()
    print(f"{result['core_name']} {result['core_version']}: captured {len(records)} frames to {args.output}")

if __name__ == '__main__':
    main()

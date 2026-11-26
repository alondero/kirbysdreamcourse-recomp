"""Controlled Snes9x 1.63 execution of the original sprite routines.

The test changes a fresh libretro CPU/WRAM snapshot and runs one frame.
The ROM bytes and core are verified independently of the synthetic state.
"""
import argparse
import ctypes as c
import hashlib
from pathlib import Path
import struct

from reference_capture import CORE_SHA256, ROM_SHA256, digest

REGISTERS = struct.Struct('>BB7H')  # PB, DB, P, A, D, S, X, Y, PC
START = 0x0200
WAIT = bytes.fromhex('80 fe')  # BRA to the same address
DIRECT = bytes.fromhex('22 1a 8a 80')  # JSL $80:8A1A
SELECT_NEXT = bytes.fromhex('a9 01 00 8f 00 01 7e a9 00 10')  # Set selector 1, reload A
CONSUME = bytes.fromhex('20 08 8c')  # JSR $8C08
APPEND = bytes.fromhex('22 02 8b 80')  # JSL $80:8B02
REWRITE_ATTRIBUTE = bytes.fromhex('a9 01 00 8f 01 12 7e')  # Set $7E:1201 to $0001
RTL_CLEAR = bytes.fromhex('22 41 88 80')  # JSL $80:8841
RTS_CLEAR = bytes.fromhex('20 e1 8a')  # JSR $8AE1


class GameInfo(c.Structure):
    _fields_ = [('path', c.c_char_p), ('data', c.c_void_p),
                ('size', c.c_size_t), ('meta', c.c_char_p)]


def snapshot_block(data, name):
    pos = data.index(b'\n') + 1
    while pos + 11 <= len(data):
        size = int(data[pos + 4:pos + 10])
        if data[pos:pos + 3] == name:
            return memoryview(data)[pos + 11:pos + 11 + size]
        pos += 11 + size
    raise ValueError(f'Missing snapshot block {name!r}')


def expect(case, field, actual, expected):
    if actual != expected:
        raise ValueError(f'{case}: {field} was {actual!r}, expected {expected!r}')


def run_cases(core, report):
    core.retro_serialize_size.restype = c.c_size_t
    for fn in (core.retro_serialize, core.retro_unserialize):
        fn.argtypes = [c.c_void_p, c.c_size_t]
        fn.restype = c.c_bool
    size = core.retro_serialize_size()
    buffer = c.create_string_buffer(size)

    def save():
        if not core.retro_serialize(buffer, size):
            raise RuntimeError('Could not serialize Snes9x state')
        return bytearray(buffer.raw)

    initial = save()
    mutate = APPEND + REWRITE_ATTRIBUTE + CONSUME
    for label, call, origin_x, limit, want_cursor, want_oam, want_count in (
        ('direct-wram-selector', DIRECT, 12, 0x800, 0x604, '11 1c de bc 00 00 00 00', 0),
        ('direct-selector-update', SELECT_NEXT + DIRECT, 12, 0x800, 0x604, '0d 16 aa aa 00 00 00 00', 0),
        ('direct-cross-bank-link', DIRECT, 12, 0x800, 0x604, '11 1c de bc 00 00 00 00', 0),
        ('direct-full', DIRECT, 12, 0x600, 0x600, '00 00 00 00 00 00 00 00', 0),
        ('direct-rejected-x', DIRECT, 256, 0x800, 0x600, '05 1c de bc 00 00 00 00', 0),
        ('queued-repeat', CONSUME * 2, 30, 0x800, 0x608, '23 85 34 22 23 85 34 22', 2),
        ('queued-rtl-clear', CONSUME + RTL_CLEAR + CONSUME, 30, 0x800, 0x604, '23 85 34 22 00 00 00 00', 0),
        ('queued-rts-clear', CONSUME + RTS_CLEAR + CONSUME, 30, 0x800, 0x604, '23 85 34 22 00 00 00 00', 0),
        ('queued-mutable', mutate, 30, 0x800, 0x604, '23 0f 01 00 00 00 00 00', 2),
    ):
        is_direct = call.endswith(DIRECT)
        initial_db = 0x7e if is_direct or call == mutate else 0x7f
        cross_bank = label == 'direct-cross-bank-link'
        origin_y = 20 if is_direct else 7
        results = []
        for _ in range(2):
            state = bytearray(initial)
            ram = snapshot_block(state, b'RAM')
            ram[:] = bytes(len(ram))
            sram = snapshot_block(state, b'SRA')
            sram[:] = bytes(len(sram))

            def put(addr, value):
                struct.pack_into('<H', ram, addr, value)

            for addr, value in [(0x2c, 0x600), (0x32, limit), (0x2e, 0x800),
                                (0x30, 0x8000), (0x2882, 0xffff)]:
                put(addr, value)
            if is_direct:
                ram[0x41] = 0x7f
                put(0x10100, 1)
                ram[0x11000:0x11007] = bytes.fromhex('7f 00 01 00 11 10 11')
                ram[0x11100:0x11105] = bytes.fromhex('08 de bc 05 81')
                ram[0x11110:0x11115] = bytes.fromhex('02 aa aa 01 80')
                if cross_bank:
                    # Link at $7F:FFFF takes its pointer from $80:0000.
                    # $7F:0000 holds a decoy for same-bank wrapping.
                    ram[0x1ffff] = 0x80
                    put(0x0000, 0x1100)
                    put(0x10000, 0x1110)
            elif call == mutate:
                ram[0x1200:0x1205] = bytes.fromhex('08 de bc 05 81')
            else:
                for addr, value in [(0x2884, 0x1200), (0x2904, origin_x),
                                    (0x2984, origin_y), (0x2a04, 2), (0x2500, 2),
                                    (0x12882, 0xffff), (0x1009e, 0x7f)]:
                    put(addr, value)
                ram[0x1200:0x1208] = bytes.fromhex('7e 00 25 7e 34 12 05 81')
            stub = call + WAIT
            ram[START:START + len(stub)] = stub
            a = 0xffff if cross_bank else 0x1000 if is_direct else 0x1200
            REGISTERS.pack_into(snapshot_block(state, b'REG'), 0,
                                0x80, initial_db, 0x0004, a, 0x1e00, 0x01ff, origin_x, origin_y, START)
            c.memmove(buffer, bytes(state), size)
            if not core.retro_unserialize(buffer, size):
                raise RuntimeError('Could not load synthetic Snes9x state')
            core.retro_run()
            end = save()
            written = snapshot_block(end, b'RAM')
            pb, db, p, a, d, s, x, y, pc = REGISTERS.unpack(snapshot_block(end, b'REG'))
            expect(label, 'return PB/DB/P/D/S/PC', (pb, db, p, d, s, pc), (
                0x80, 0x7e, 4 if is_direct else 5, 0x1e00, 0x01ff, START + len(call)))
            expect(label, 'DP cursor', struct.unpack_from('<H', written, 0x2c)[0], want_cursor)
            expect(label, 'OAM buffer start', written[0x600:0x608].hex(' '), want_oam)
            expect(label, 'DP origins', struct.unpack_from('<HH', written, 0x36), (origin_x, origin_y))
            expect(label, 'bucket byte count', struct.unpack_from('<H', written, 0x2a04)[0], want_count)
            results.append({key: bytes(snapshot_block(end, key)) for key in (b'RAM', b'SRA', b'REG', b'PPU')})
            if report:
                print(label, 'registers', [f'{v:04x}' for v in (pb, db, p, a, d, s, x, y, pc)],
                      'DP cursor', hex(want_cursor), 'OAM', want_oam, 'stack', written[0x1f0:0x200].hex())
        for key, first in results[0].items():
            second = results[1][key]
            if first != second:
                offset = next((i for i, (a, b) in enumerate(zip(first, second)) if a != b),
                              min(len(first), len(second)))
                raise ValueError(f'{label}: repeat differs in {key.decode()} at byte {offset:#x}')
            if report:
                print(key.decode(), hashlib.sha256(first).hexdigest())
        print(f'{label} passed twice')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--core-sha256', default=CORE_SHA256)
    parser.add_argument('--report', action='store_true')
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise SystemExit('ROM SHA-256 mismatch')
    if digest(args.core) != args.core_sha256.lower():
        raise SystemExit('Core SHA-256 mismatch')

    core = c.CDLL(str(args.core.resolve()))
    callbacks = []
    for name, typ, result in (
        ('environment', c.CFUNCTYPE(c.c_bool, c.c_uint, c.c_void_p), lambda command, _: command == 10),
        ('video_refresh', c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t), lambda *_: None),
        ('audio_sample', c.CFUNCTYPE(None, c.c_int16, c.c_int16), lambda *_: None),
        ('audio_sample_batch', c.CFUNCTYPE(c.c_size_t, c.c_void_p, c.c_size_t), lambda _, count: count),
        ('input_poll', c.CFUNCTYPE(None), lambda: None),
        ('input_state', c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint), lambda *_: 0),
    ):
        callback = typ(result)
        callbacks.append(callback)
        setter = getattr(core, f'retro_set_{name}')
        setter.argtypes = [typ]
        setter(callback)
    core.retro_init()
    core.retro_load_game.argtypes = [c.POINTER(GameInfo)]
    core.retro_load_game.restype = c.c_bool
    game_buffer = c.create_string_buffer(rom)
    game = GameInfo(str(args.rom.resolve()).encode(), c.cast(game_buffer, c.c_void_p), len(rom), None)
    if not core.retro_load_game(c.byref(game)):
        raise SystemExit('Snes9x rejected the ROM')
    try:
        run_cases(core, args.report)
    finally:
        core.retro_unload_game()
        core.retro_deinit()


if __name__ == '__main__':
    main()

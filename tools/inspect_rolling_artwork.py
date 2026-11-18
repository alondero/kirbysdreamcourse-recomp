"""Inspect recovered USA slow-roll metadata; never export artwork or ROM bytes.

This is an investigation tool, not a runtime view descriptor. Only the two
state-14 scripts recovered in docs/orbit-rolling-artwork.md are accepted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


ROM_SHA256 = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
BANK = 0x87
# Guest player offsets, not seat indices. Independently recovered tables.
TABLES = {0: (0x9ade, 0xd95e, 0x7c00), 2: (0xb990, 0xd960, 0x7e00)}


def offset(bank, address, length=1):
    if not 0x80 <= bank <= 0x9f or not 0x8000 <= address <= 0x10000 - length:
        raise ValueError('ROM field crosses the supported LoROM bank')
    return ((bank & 0x7f) << 15) | (address & 0x7fff)


def word(rom, address):
    return struct.unpack_from('<H', rom, offset(BANK, address, 2))[0]


def recover(rom, player):
    """Validate the bounded script shape, returning addresses and units only.

    The CLI verifies identity first. Keeping this decoder separate permits
    ROM-free malformed-record fixtures.
    """
    if len(rom) != 0x100000 or player not in TABLES:
        raise ValueError('expected 1 MiB ROM and guest player offset 0 or 2')
    table, mirror, destination = TABLES[player]
    entry = word(rom, table + 14 * 2)
    if rom[offset(BANK, entry)] != 0x24:
        raise ValueError('expected callback-clear command at slow-roll entry')
    phases = []
    for phase in range(2):
        pc = entry + 1 + phase * 9
        p = offset(BANK, pc, 9)
        if (rom[p] != 0x08 or word(rom, pc + 1) != mirror or
                word(rom, pc + 3) != 0 or rom[p + 5] != 0x12 or rom[p + 8] != 2):
            raise ValueError('unsupported slow-roll mirror/upload/delay commands')
        record = word(rom, pc + 6)
        q = offset(BANK, record, 9)
        length, source = struct.unpack_from('<HH', rom, q + 1)
        bank = rom[q + 5]
        dest = word(rom, record + 6)
        if rom[q] != 0 or length != 0x120 or dest != destination or rom[q + 8] != 0xff:
            raise ValueError('unsupported slow-roll DMA record')
        source_offset = offset(bank, source, length)
        phases.append(dict(phase=phase, command=pc + 5, record=record,
                           source_bank=bank, source_address=source,
                           source_offset=source_offset, destination_words=dest,
                           length_bytes=length, delay_ticks=2, mirror=0))
    end = entry + 19
    if rom[offset(BANK, end, 3)] != 0x02 or word(rom, end + 1) != entry + 1:
        raise ValueError('unsupported slow-roll loop boundary')
    return dict(player_offset=player, bank=BANK, state=14, table=table,
                entry=entry, mirror_word=mirror, phases=phases)


def resident_phase(rom, vram, descriptor):
    if len(vram) != 0x10000:
        raise ValueError('expected 64 KiB little-endian VRAM snapshot')
    matches = []
    for phase in descriptor['phases']:
        start = phase['source_offset']
        dest = phase['destination_words'] * 2
        length = phase['length_bytes']
        if vram[dest:dest + length] == rom[start:start + length]:
            matches.append(phase['phase'])
    # No guessing on upload lag, partial data or ambiguous synthetic content.
    return matches[0] if len(matches) == 1 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--vram', type=Path, help='optional local 64 KiB VRAM snapshot')
    args = parser.parse_args()
    try:
        rom = args.rom.read_bytes()
        if len(rom) != 0x100000 or hashlib.sha256(rom).hexdigest() != ROM_SHA256:
            raise ValueError('ROM identity mismatch; supported headerless USA image required')
        descriptors = [recover(rom, player) for player in TABLES]
        if args.vram:
            vram = args.vram.read_bytes()
            for descriptor in descriptors:
                descriptor['resident_phase'] = resident_phase(rom, vram, descriptor)
        print(json.dumps(dict(rom_sha256=ROM_SHA256, family='slow-roll',
                              directional_alternatives_established=False,
                              players=descriptors), indent=2))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

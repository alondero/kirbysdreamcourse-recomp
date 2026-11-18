"""Synthetic slow-roll scripts and pixels; no game content."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    'rolling', Path(__file__).resolve().parents[1] / 'tools/inspect_rolling_artwork.py')
rolling = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rolling)


class RollingMetadataTest(unittest.TestCase):
    def setUp(self):
        self.rom = bytearray(0x100000)
        for player, (table, mirror, dest) in rolling.TABLES.items():
            # Deliberately unrelated entry/record/source locations for P2.
            entry = 0x9000 if player == 0 else 0xc000
            self.put_word(table + 28, entry)
            self.rom[rolling.offset(0x87, entry)] = 0x24
            for phase in range(2):
                pc = entry + 1 + phase * 9
                record = (0xa000 if player == 0 else 0xd000) + phase * 9
                source = 0x8000 + player * 0x400 + phase * 0x120
                p = rolling.offset(0x87, pc)
                self.rom[p:p + 9] = struct.pack('<BHHBHB', 8, mirror, 0, 0x12, record, 2)
                p = rolling.offset(0x87, record)
                self.rom[p:p + 9] = struct.pack('<BHHBHB', 0, 0x120, source, 0x93, dest, 0xff)
                p = rolling.offset(0x93, source)
                self.rom[p:p + 0x120] = bytes([1 + player + phase]) * 0x120
            p = rolling.offset(0x87, entry + 19)
            self.rom[p:p + 3] = struct.pack('<BH', 2, entry + 1)

    def put_word(self, address, value):
        struct.pack_into('<H', self.rom, rolling.offset(0x87, address), value)

    def test_independent_player_tables_and_resident_phases(self):
        vram = bytearray(0x10000)
        for player in (0, 2):
            descriptor = rolling.recover(self.rom, player)
            self.assertEqual(descriptor['entry'], 0x9000 if player == 0 else 0xc000)
            for phase in descriptor['phases']:
                source, dest = phase['source_offset'], phase['destination_words'] * 2
                vram[dest:dest + 0x120] = self.rom[source:source + 0x120]
                self.assertEqual(rolling.resident_phase(self.rom, vram, descriptor), phase['phase'])
                vram[dest] ^= 0x80
                self.assertIsNone(rolling.resident_phase(self.rom, vram, descriptor))

    def test_reject_malformed_scripts_and_transfers(self):
        for address, value in ((0x9000, 0), (0x9001, 0x12), (0x9006, 0x10),
                               (0x9009, 3), (0x9013, 0), (0xa000, 1),
                               (0xa001, 0), (0xa005, 0x7e), (0xa008, 0)):
            with self.subTest(address=address):
                damaged = self.rom.copy()
                damaged[rolling.offset(0x87, address)] = value
                with self.assertRaises(ValueError):
                    rolling.recover(damaged, 0)
        for address, value in ((0x9afa, 0xfff0), (0x9002, 0xd960),
                               (0x9014, 0x9000), (0xa003, 0xff00), (0xa006, 0x7e00)):
            with self.subTest(address=address):
                damaged = self.rom.copy()
                struct.pack_into('<H', damaged, rolling.offset(0x87, address), value)
                with self.assertRaises(ValueError):
                    rolling.recover(damaged, 0)

    def test_reject_invalid_inputs_and_ambiguous_art(self):
        for player in (1, 4):
            with self.assertRaises(ValueError):
                rolling.recover(self.rom, player)
        with self.assertRaises(ValueError):
            rolling.recover(self.rom[:-1], 0)
        descriptor = rolling.recover(self.rom, 0)
        with self.assertRaises(ValueError):
            rolling.resident_phase(self.rom, b'', descriptor)
        source = descriptor['phases'][1]['source_offset']
        self.rom[source:source + 0x120] = bytes([1]) * 0x120
        vram = bytearray(0x10000)
        vram[0xf800:0xf920] = bytes([1]) * 0x120
        self.assertIsNone(rolling.resident_phase(self.rom, vram, descriptor))

        # The public CLI must reject even a structurally valid synthetic
        # image before treating its fields as evidence about the USA ROM.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.sfc'
            path.write_bytes(self.rom)
            result = subprocess.run([sys.executable, '-B', spec.origin,
                                     '--rom', str(path)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('ROM identity mismatch', result.stderr)
            self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    unittest.main()

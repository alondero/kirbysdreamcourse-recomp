import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


class SpriteTierdownHookTest(unittest.TestCase):
    def test_entry_hook_follows_the_generated_tierdown_guard(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        recompiler = framework / 'recompiler'
        if not (recompiler / 'v2/emit_function.py').is_file():
            self.skipTest('Requires patched pinned framework (SNESRECOMP_ROOT)')
        sys.path.insert(0, str(recompiler))
        try:
            from v2.emit_function import emit_function
        finally:
            sys.path.remove(str(recompiler))

        rom = bytearray(0x8000)
        code = bytes.fromhex('e6 40 a5 40 29 03 d0 f8 60')
        rom[:len(code)] = code
        with patch.dict(os.environ, {'SNESRECOMP_EMIT_SIMPLE_BUS_TIMING': '1'}):
            source = emit_function(bytes(rom), bank=0, start=0x8000, entry_m=1,
                                   entry_x=1, end=0x8009, func_name='bank_00_8000',
                                   entry_hooks={0x008000: 'HostSaw'})

        entry = source.index('  L_8000_M1X1:')
        hook = source.index('HostSaw(cpu, 0x008000u);', entry)
        tierdown = source.index('interp_tier_dispatch_tail_ex', entry)
        charge = source.index('cpu->cycles +=', entry)
        self.assertLess(tierdown, hook,
                        'a generated tier-down to the same PC must leave the report to the interpreter')
        self.assertLess(hook, charge)


if __name__ == '__main__':
    unittest.main()

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from run_game_scenario import inspect_run, load_scenario, WRAM_SIZE


class GameScenarioTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def scenario(self, **settings):
        path = self.root / 'scenario.json'
        path.write_text(json.dumps(dict(frames=2, screenshot=None, **settings)), encoding='utf-8')
        return load_scenario(path)

    def test_check_cannot_read_past_wram(self):
        check = dict(label='word', frame=0, offset='0x1ffff', equals=0)
        with self.assertRaisesRegex(ValueError, 'WRAM offset'):
            self.scenario(checks=[check])

    def test_check_rejects_a_clean_exit_without_intended_state(self):
        scenario = self.scenario(checks=[dict(label='shot', frame=1, offset='0x1234', min=7)])
        self.assertEqual(scenario['capture_from'], 1)
        (self.root / 'stderr.log').write_text('exit: RUN_FRAMES reached after 2 frames\n')
        frames = self.root / 'frames'
        frames.mkdir()
        memory = bytearray(WRAM_SIZE)
        memory[0x1234:0x1236] = (6).to_bytes(2, 'little')
        (frames / 'frame_000001_wram.bin').write_bytes(memory)
        result = {'failures': []}
        inspect_run(scenario, self.root, result, self.root / 'save.srm', 0)
        self.assertEqual(result['failures'], ['frame 1: shot (expected min 7, observed 6)'])
        self.assertEqual(result['checks'][0]['actual'], 6)

    def test_completed_frame_budget_still_requires_the_capture(self):
        scenario = self.scenario(capture_from=0)
        (self.root / 'stderr.log').write_text('exit: RUN_FRAMES reached after 2 frames\n')
        frames = self.root / 'frames'
        frames.mkdir()
        (frames / 'frame_000000_wram.bin').write_bytes(bytes(WRAM_SIZE - 1))
        (frames / 'frame_000001_wram.bin').write_bytes(bytes(WRAM_SIZE))
        result = {'failures': []}
        inspect_run(scenario, self.root, result, self.root / 'save.srm', 0)
        self.assertEqual(result['failures'], ['first missing or incomplete WRAM dump at frame 0'])


if __name__ == '__main__':
    unittest.main()

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from input_recording import host_script, read_recording


class RecordingTest(unittest.TestCase):
    def read(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'recording.json'
            path.write_text(json.dumps(data), encoding='utf-8')
            return read_recording(path)

    def test_host_waits_preserve_frame_numbers(self):
        recording = self.read({'frames': 25, 'events': [
            {'frame': 4, 'count': 3, 'button': 'start'},
            {'frame': 8, 'count': 2, 'button': 'a'},
            {'frame': 15, 'count': 1, 'button': 'left'},
        ]})
        self.assertEqual(host_script(recording),
                         'wait 3\npress start 3\nwait 0\npress a 2\nwait 4\npress left 1\n')

    def test_rejects_overlap_and_missing_idle_frame(self):
        for second_frame in (5, 6):
            with self.subTest(second_frame=second_frame), self.assertRaises(ValueError):
                self.read({'frames': 20, 'events': [
                    {'frame': 4, 'count': 2, 'button': 'a'},
                    {'frame': second_frame, 'count': 1, 'button': 'b'},
                ]})

    def test_rejects_out_of_bounds_and_invalid_button(self):
        for event in ({'frame': 20, 'count': 2, 'button': 'a'},
                      {'frame': 1, 'count': 1, 'button': 'turbo'},
                      {'frame': 1, 'count': 1, 'button': 'a+a'},
                      {'frame': 1, 'count': 1, 'button': 'a+'},
                      {'frame': True, 'count': 1, 'button': 'a'}):
            with self.subTest(event=event), self.assertRaises(ValueError):
                self.read({'frames': 20, 'events': [event]})

    def test_simultaneous_buttons_keep_one_hold(self):
        recording = self.read({'frames': 20, 'events': [
            {'frame': 4, 'count': 3, 'button': 'b+up'},
            {'frame': 10, 'count': 1, 'button': 'a'},
        ]})
        self.assertEqual(host_script(recording),
                         'wait 3\npress b+up 3\nwait 2\npress a 1\n')

    def test_two_player_chord_preserves_script_and_rejects_invalid_buttons(self):
        recording = self.read({'frames': 20, 'events': [
            {'frame': 4, 'count': 3, 'button': 'a+p2:a+p2:left'},
        ]})
        self.assertEqual(host_script(recording), 'wait 3\npress a+p2:a+p2:left 3\n')
        for button in ('p2:', 'p3:a', 'p2:p2:a', 'p2:a+p2:a'):
            with self.subTest(button=button), self.assertRaises(ValueError):
                self.read({'frames': 20, 'events': [
                    {'frame': 1, 'count': 1, 'button': button},
                ]})


if __name__ == '__main__':
    unittest.main()

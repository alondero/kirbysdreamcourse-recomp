"""Verify frame and button boundaries of the reference input lookup."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from reference_capture import input_masks


class ReferenceInputMasksTest(unittest.TestCase):
    def test_intervals_overlap_and_preserve_final_frame(self):
        buttons = {'a': 8, 'b': 0}
        events = [{'frame': 1, 'count': 2, 'button': 'a'},
                  {'frame': 2, 'count': 2, 'button': 'b'},
                  {'frame': 2, 'count': 1, 'button': 'a'},
                  {'frame': 4, 'count': 1, 'button': 'b'}]
        self.assertEqual(input_masks(4, events, buttons),
                         [0, 1 << 8, (1 << 8) | 1, 1, 1])

    def test_chord_sets_every_button_for_the_same_frames(self):
        buttons = {'b': 0, 'up': 4}
        self.assertEqual(input_masks(4, [{'frame': 2, 'count': 2, 'button': 'b+up'}], buttons),
                         [0, 0, 0x11, 0x11, 0])

    def test_simultaneous_players_have_independent_masks(self):
        buttons = {'a': 8, 'b': 0, 'left': 6}
        events = [{'frame': 2, 'count': 2, 'button': 'a+p2:b+p2:left'},
                  {'frame': 4, 'count': 1, 'button': 'p2:a'}]
        self.assertEqual(input_masks(4, events, buttons, 0), [0, 0, 0x100, 0x100, 0])
        self.assertEqual(input_masks(4, events, buttons, 1), [0, 0, 0x41, 0x41, 0x100])
        with self.assertRaises(ValueError):
            input_masks(4, events, buttons, 2)


if __name__ == '__main__':
    unittest.main()

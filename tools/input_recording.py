"""Validate frame-numbered inputs and translate them to the framework script.

Use '+' to hold multiple buttons for the same interval, for example 'b+up'.
Prefix a player-two button with 'p2:', including in chords such as 'a+p2:b'.
The framework inserts one idle frame after each hold. This format therefore
rejects overlapping and back-to-back events rather than silently shifting input.
"""
import argparse
import json
from pathlib import Path

BUTTONS = frozenset(('b', 'y', 'select', 'start', 'up', 'down', 'left', 'right',
                     'a', 'x', 'l', 'r'))


def parse_buttons(value):
    """Parse one button or a simultaneous chord held for the whole event."""
    if not isinstance(value, str):
        raise ValueError('button must be a string')
    parts = value.split('+')
    if (any((part[3:] if part.startswith('p2:') else part) not in BUTTONS
            for part in parts) or len(set(parts)) != len(parts)):
        raise ValueError('invalid button or chord')
    return parts


def read_recording(path):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    if not isinstance(data, dict) or set(data) != {'frames', 'events'}:
        raise ValueError('recording must contain exactly frames and events')
    frames, events = data['frames'], data['events']
    if type(frames) is not int or frames < 1 or not isinstance(events, list):
        raise ValueError('frames must be positive and events must be a list')
    previous_end = 0
    for item in events:
        if not isinstance(item, dict) or set(item) != {'frame', 'count', 'button'}:
            raise ValueError('each event must contain frame, count and button')
        start, count, button = item['frame'], item['count'], item['button']
        if (type(start) is not int or type(count) is not int or start < 1 or count < 1
                or start + count - 1 > frames):
            raise ValueError('invalid event or event outside recording')
        parse_buttons(button)
        if previous_end and start <= previous_end + 1:
            raise ValueError('events require at least one idle frame between holds')
        previous_end = start + count - 1
    return data


def host_script(recording):
    lines = []
    previous_end = 0
    for event in recording['events']:
        start = event['frame']
        wait = start - 1 if not previous_end else start - previous_end - 2
        lines.extend((f'wait {wait}', f"press {event['button']} {event['count']}"))
        previous_end = start + event['count'] - 1
    return '\n'.join(lines) + ('\n' if lines else '')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('recording', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, required=True)
    args = parser.parse_args()
    try:
        data = read_recording(args.recording)
        if data['frames'] != args.frames:
            raise ValueError('recording frame count differs from requested run')
    except (ValueError, OSError) as error:
        parser.error(str(error))
    args.output.write_text(host_script(data), encoding='ascii')


if __name__ == '__main__':
    main()

"""Check the stationary Gordo body and shadow in a gordo-reload scenario run."""

import argparse
from pathlib import Path


def check_anchor(run):
    host_log = (run / 'stderr.log').read_text(encoding='utf-8', errors='replace')
    saved = host_log.find('savestate save: saves/save0.sav')
    if saved < 0 or host_log.find('savestate load: saves/save0.sav', saved) < 0:
        raise ValueError('slot 0 was not saved and reloaded')
    ground_flags = {frame: set() for frame in range(1263, 1279)}
    with (run / 'orbit-sprites.log').open(encoding='ascii') as sprites:
        for number, line in enumerate(sprites, 1):
            row = line.split()
            if len(row) != 14:
                raise ValueError(f'malformed orbit sprite log row {number}')
            frame = int(row[0])
            if frame not in ground_flags or row[2] != '80e60b' or row[-2:] != ['448', '248']:
                continue
            if row[3:5] != ['1', '0'] or tuple(map(float, row[7:10])) != (8.5, 5.5, 5):
                raise ValueError(f'frame {frame}: Gordo lost its course position')
            ground_flags[frame].add(row[10])
    for frame, flags in ground_flags.items():
        if flags != {'0', '1'}:
            raise ValueError(f'frame {frame}: missing Gordo body or ground shadow')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path, help='directory printed by run_game_scenario.py')
    args = parser.parse_args()
    try:
        check_anchor(args.run)
    except (OSError, ValueError) as error:
        parser.exit(1, f'FAIL: {error}\n')
    print('PASS: Gordo body and shadow anchored from frame 1263 through 1278')


if __name__ == '__main__':
    main()

"""Compare two local stereo s16le captures using an explicit sample offset."""
import argparse
from pathlib import Path
import struct


def read_samples(path):
    data = path.read_bytes()
    if not data or len(data) % 4:
        raise ValueError(f'{path}: expected nonempty interleaved stereo s16le')
    return list(struct.iter_unpack('<hh', data))


def compare(host, reference, offset):
    """Compare host sample i with reference sample i + offset, without resampling."""
    start = max(0, -offset)
    end = min(len(host), len(reference) - offset)
    if start >= end:
        raise ValueError('offset leaves no overlapping samples')
    if not any(left or right for left, right in host[start:end]):
        raise ValueError('host overlap is silent')
    if not any(left or right for left, right in reference[start + offset:end + offset]):
        raise ValueError('reference overlap is silent')
    first = next((i for i in range(start, end)
                  if host[i] != reference[i + offset]), None)
    return start, end, first


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--offset', type=int, required=True,
                        help='compare host sample i with reference sample i + offset')
    args = parser.parse_args()
    try:
        host = read_samples(args.host)
        reference = read_samples(args.reference)
        start, end, first = compare(host, reference, args.offset)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'Compared host samples [{start}, {end}) with reference offset {args.offset}; '
          f'lengths: host {len(host)}, reference {len(reference)}')
    if first is None:
        print('No difference in overlapping samples')
    else:
        print(f'First difference: host {first} {host[first]}, '
              f'reference {first + args.offset} {reference[first + args.offset]}')


if __name__ == '__main__':
    main()

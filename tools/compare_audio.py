"""Compare two local stereo s16le captures using an explicit sample offset."""
import argparse
from pathlib import Path
import struct
import sys


def read_samples(path):
    data = path.read_bytes()
    if not data or len(data) % 4:
        raise ValueError(f'{path}: expected nonempty interleaved stereo s16le')
    return data


def compare(host, reference, offset, allow_partial=False):
    """Compare host sample i with reference sample i + offset, without resampling."""
    host_count, reference_count = len(host) // 4, len(reference) // 4
    start = max(0, -offset)
    end = min(host_count, reference_count - offset)
    if start >= end:
        raise ValueError('offset leaves no overlapping samples')
    if not allow_partial and (start != 0 or end != host_count or
                              start + offset != 0 or end + offset != reference_count):
        raise ValueError('captures have unmatched samples; use --allow-partial to compare the overlap')
    host_view, reference_view = memoryview(host), memoryview(reference)
    if not any(host_view[start * 4:end * 4]):
        raise ValueError('host overlap is silent')
    if not any(reference_view[(start + offset) * 4:(end + offset) * 4]):
        raise ValueError('reference overlap is silent')
    first = next((i for i in range(start, end)
                  if host_view[i * 4:i * 4 + 4] !=
                  reference_view[(i + offset) * 4:(i + offset) * 4 + 4]), None)
    return start, end, first


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--offset', type=int, required=True,
                        help='compare host sample i with reference sample i + offset')
    parser.add_argument('--allow-partial', action='store_true',
                        help='compare only the overlap when samples are unmatched')
    args = parser.parse_args()
    try:
        host = read_samples(args.host)
        reference = read_samples(args.reference)
        start, end, first = compare(host, reference, args.offset, args.allow_partial)
    except (OSError, ValueError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 2
    print(f'Compared host samples [{start}, {end}) with reference offset {args.offset}; '
          f'lengths: host {len(host) // 4}, reference {len(reference) // 4}')
    if first is None:
        print('No difference in overlapping samples')
    else:
        host_sample = struct.unpack_from('<hh', host, first * 4)
        reference_sample = struct.unpack_from('<hh', reference, (first + args.offset) * 4)
        print(f'First difference: host {first} {host_sample}, '
              f'reference {first + args.offset} {reference_sample}')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

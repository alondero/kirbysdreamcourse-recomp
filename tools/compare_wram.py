"""Compare two local host frame dumps and a reference_capture.py recording."""
import argparse
import hashlib
import json
from pathlib import Path
from input_recording import host_script, parse_buttons, read_recording


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--reference', required=True, type=Path)
    p.add_argument('--host-a', required=True, type=Path)
    p.add_argument('--host-b', required=True, type=Path)
    p.add_argument('--expect-press', action='append', default=[], metavar='FRAME:COUNT:BUTTON',
                   help='expected reference input; host scripts must be checked separately')
    p.add_argument('--expect-sram-sha256', default='zero',
                   help='expected initial SRAM hash, or zero for blank SRAM')
    p.add_argument('--allow-bounce-difference', action='store_true',
                   help='compare native and fallback runs; require explicit 1/0 bounce settings')
    p.add_argument('--start-frame', type=int, default=0, help='first zero-based host frame, inclusive')
    p.add_argument('--end-frame', type=int, help='last zero-based host frame, inclusive; default is the final reference frame')
    p.add_argument('--reference-offset', type=int, default=1, metavar='N',
                   help='compare host frame h with reference frame h+N (default: 1); an alternative offset is diagnostic, not proven alignment')
    p.add_argument('--recording', type=Path,
                   help='require both host manifests and reference to identify this recording')
    args = p.parse_args()
    if args.allow_bounce_difference and not args.recording:
        p.error('--allow-bounce-difference requires --recording')
    recording = json.loads(args.reference.read_text(encoding='utf-8'))
    if recording.get('reset_before_frame') is not None or recording.get('reset_before_frames'):
        raise SystemExit('Reset scenarios are not supported by this comparator')
    expected_input = []
    if args.recording:
        if args.expect_press:
            p.error('--recording cannot be combined with --expect-press')
        try:
            input_recording = read_recording(args.recording)
        except (ValueError, OSError) as error:
            p.error(str(error))
        expected_input = input_recording['events']
        recording_hash = hashlib.sha256(args.recording.read_bytes()).hexdigest()
        if recording.get('input_recording_sha256') != recording_hash:
            raise SystemExit('Reference recording hash mismatch')
        executable_hashes = []
        runtime_options = []
        config_hashes = []
        for directory in (args.host_a, args.host_b):
            manifest = json.loads((directory.parent / 'run.json').read_text(encoding='utf-8-sig'))
            if manifest.get('initial_state_sha256') is not None:
                raise SystemExit(f'Host initial save state is not supported by this comparator: {directory}')
            expected_sram = (hashlib.sha256(bytes(8192)).hexdigest()
                             if args.expect_sram_sha256 == 'zero' else args.expect_sram_sha256.lower())
            if (manifest.get('completed') is not True or manifest.get('frames') != input_recording['frames']
                    or manifest.get('rom_sha256') != recording.get('rom_sha256')
                    or manifest.get('initial_sram_sha256') != expected_sram
                    or manifest.get('input_recording_sha256') != recording_hash):
                raise SystemExit(f'Host manifest does not match scenario: {directory}')
            script_path = directory.parent / 'input.script'
            if (script_path.read_text(encoding='ascii') != host_script(input_recording)
                    or hashlib.sha256(script_path.read_bytes()).hexdigest() != manifest.get('input_script_sha256')):
                raise SystemExit(f'Host input script does not match recording: {directory}')
            executable = directory.parent / 'game.exe'
            if hashlib.sha256(executable.read_bytes()).hexdigest() != manifest.get('executable_sha256'):
                raise SystemExit(f'Host executable does not match manifest: {directory}')
            executable_hashes.append(manifest.get('executable_sha256'))
            runtime_options.append(manifest.get('runtime_options'))
            config_hash = manifest.get('initial_config_sha256')
            if config_hash is not None:
                config = directory.parent / 'config.initial.ini'
                if not config.is_file() or hashlib.sha256(config.read_bytes()).hexdigest() != config_hash:
                    raise SystemExit(f'Host initial config does not match manifest: {directory}')
            config_hashes.append(config_hash)
        if not executable_hashes[0] or executable_hashes[0] != executable_hashes[1]:
            raise SystemExit('Host runs use different executables')
        if args.allow_bounce_difference:
            if any(not isinstance(options, dict) for options in runtime_options):
                raise SystemExit('Host runtime options are missing')
            a_options, b_options = (options.copy() for options in runtime_options)
            bounce = {a_options.pop('SNESRECOMP_LLE_BOUNCE', None),
                      b_options.pop('SNESRECOMP_LLE_BOUNCE', None)}
            if bounce != {'0', '1'} or a_options != b_options:
                raise SystemExit('Host runs differ in more than the explicitly selected bounce mode')
        elif runtime_options[0] != runtime_options[1]:
            raise SystemExit('Host runs use different recorded runtime options')
        if config_hashes[0] != config_hashes[1]:
            raise SystemExit('Host runs use different initial configs')
    for press in args.expect_press:
        try:
            start, count, name = press.lower().split(':', 2)
            start, count = int(start), int(count)
            if start < 1 or count < 1:
                raise ValueError()
            parse_buttons(name)
        except ValueError:
            p.error(f'invalid --expect-press: {press}')
        expected_input.append({'frame': start, 'count': count, 'button': name})
    if (recording.get('rom_sha256') != '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
            or recording.get('core_sha256') != '72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0'
            or recording.get('initial_sram') != args.expect_sram_sha256.lower()
            or recording.get('initial_wram') != 'zero'
            or recording.get('input') != (expected_input if expected_input else 'none')):
        raise SystemExit('Reference identity or initial conditions do not match this scenario')
    reference = recording['frames']
    if args.recording and input_recording['frames'] != len(reference):
        raise SystemExit('Reference frame count does not match input recording')
    if not reference:
        raise SystemExit('Reference has no frames')
    if any(frame['frame'] != index + 1 for index, frame in enumerate(reference)):
        raise SystemExit('Reference frames are not consecutive from 1')
    # Host labels are zero based; reference labels are one based.  Restrict
    # both the available host timeline and the selected reference timeline.
    end_frame = min(len(reference) - 1, len(reference) - args.reference_offset) if args.end_frame is None else args.end_frame
    if (args.start_frame < 0 or end_frame < args.start_frame or end_frame >= len(reference)
            or args.start_frame + args.reference_offset < 1
            or end_frame + args.reference_offset > len(reference)):
        p.error('selected host frame range must be within the reference capture for the chosen offset')
    if args.reference_offset != 1:
        print(f'Diagnostic alignment: reference frame = host frame + {args.reference_offset}; '
              'this offset is not established as a global alignment')
    checked = end_frame - args.start_frame + 1
    mismatches = []
    for index in range(args.start_frame, end_frame + 1):
        frame = reference[index + args.reference_offset - 1]
        hashes = []
        for directory in (args.host_a, args.host_b):
            path = directory / f'frame_{index:06}_wram.bin'
            data = path.read_bytes()
            if len(data) != 131072:
                raise SystemExit(f'Invalid WRAM length: {path}')
            hashes.append(hashlib.sha256(data).hexdigest())
        if hashes[0] != hashes[1]:
            raise SystemExit(f'Host runs differ at zero-based frame {index}')
        if hashes[0] != frame['wram_sha256']:
            mismatches.append(index)
    if mismatches:
        first = mismatches[0]
        print(f'First WRAM mismatch: host frame {first}, reference frame {first + args.reference_offset}; '
              f'{len(mismatches)} of {checked} selected frames differ')
        raise SystemExit(1)
    print(f'Host captures and reference WRAM match for {checked} selected frames')


if __name__ == '__main__':
    main()

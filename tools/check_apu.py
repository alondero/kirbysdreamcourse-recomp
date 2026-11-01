"""Build ROM-free APU probes against pinned framework and reference sources."""
import argparse
import hashlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK_PIN = '147b55e252dcb3e7680a99c7c2f50e7183891844'
REFERENCE_PIN = 'fae2fea08f74180759ef540ee94259213f503480'
TIMER_CASES = 144
PORT_RACE_PHASES = 13
INDEXED_STORE_PHASES = 5
DSP_SCENARIOS = 360
DSP_SAMPLES_PER_SCENARIO = 256
SPC_RECORDS = TIMER_CASES + PORT_RACE_PHASES + INDEXED_STORE_PHASES


def run(*args, **kwargs):
    # Compare the canonical DSP regardless of desktop enhancement settings.
    environment = dict(os.environ, SNESRECOMP_AUDIO_SHADOW='0')
    result = subprocess.run([str(arg) for arg in args], capture_output=True,
                            check=True, timeout=120, env=environment, **kwargs)
    return result.stdout


def require_pin(repo, pin):
    actual = run('git', '-C', repo, 'rev-parse', 'HEAD', text=True).strip()
    if actual != pin:
        raise ValueError(f'{repo}: expected {pin}, found {actual}')


def build_host(framework, fixture, executable, native):
    source = framework / 'runner/src'
    run('gcc', '-std=c11', '-O2', '-UNDEBUG', '-DSNESRECOMP_TRACE=0',
        f'-DSNESRECOMP_APU_NATIVE={native}', '-ffunction-sections',
        '-fdata-sections', f'-I{source}', ROOT / 'tests' / fixture,
        *(source / 'snes' / part for part in
          ('apu.c', 'spc.c', 'dsp.c', 'dsp_shadow.c', 'audio_shadow.c')),
        source / 'audio_trace.c', '-Wl,--gc-sections', '-lm', '-o', executable)


def compare_lines(expected, actual, label):
    if len(expected) != len(actual):
        raise ValueError(f'{label}: expected {len(expected)} records, got {len(actual)}')
    for index, (reference, host) in enumerate(zip(expected, actual)):
        if reference != host:
            raise ValueError(f'{label}: first divergence at record {index}:\n'
                             f'  reference: {reference}\n  host:      {host}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--framework', type=Path, default=ROOT / 'local/guarded-framework')
    parser.add_argument('--reference', type=Path, default=ROOT / 'local/snes9x-source')
    args = parser.parse_args()
    try:
        require_pin(args.framework, FRAMEWORK_PIN)
        require_pin(args.reference, REFERENCE_PIN)
        print(f'Framework pin: {FRAMEWORK_PIN}; reference pin: {REFERENCE_PIN}')
        fingerprint = hashlib.sha256()
        source = args.framework / 'runner/src'
        for name in ('snes/apu.c', 'snes/apu.h', 'snes/apu_native.inc',
                     'snes/spc.c', 'snes/spc.h', 'snes/dsp.c', 'snes/dsp.h',
                     'snes/dsp_shadow.c', 'snes/audio_shadow.c', 'audio_trace.c'):
            fingerprint.update(name.encode('ascii') + b'\0')
            fingerprint.update((source / name).read_bytes())
        print(f'Framework APU source fingerprint (provenance only): {fingerprint.hexdigest()}')
        for compiler in ('gcc', 'g++'):
            if not shutil.which(compiler):
                raise ValueError(f'{compiler} required on PATH')
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            # Export the exact reference commit, ignoring local instrumentation.
            reference = tmp / 'reference'
            archive = run('git', '-C', args.reference, 'archive', '--format=zip', REFERENCE_PIN)
            with zipfile.ZipFile(io.BytesIO(archive)) as source:
                source.extractall(reference)
            ref_exe = tmp / 'reference.exe'
            run('g++', '-std=c++17', '-O2', '-UNDEBUG', '-D__LIBRETRO__',
                *(f'-I{reference / path}' for path in
                  ('', 'libretro', 'apu/bapu', 'libretro/libretro-common/include', 'apu/bapu/dsp')),
                ROOT / 'tests/apu_phase_reference.cpp', '-o', ref_exe)
            records = run(ref_exe, text=True).splitlines()
            compare_lines(records, run(ref_exe, text=True).splitlines(), 'reference repeat')
            pauses = [line for line in records if line.startswith('pause ')]
            compare_lines(['pause 5 516 0 3', 'pause 6 516 0 2', 'pause 7 516 0 1'],
                          pauses, 'reference comparison overshoot')
            expected = [line for line in records if not line.startswith('pause ')]
            if len(expected) != SPC_RECORDS + DSP_SCENARIOS * DSP_SAMPLES_PER_SCENARIO:
                raise ValueError('reference returned an incomplete matrix')
            dsp_records = expected[SPC_RECORDS:]
            for scenario in range(DSP_SCENARIOS):
                samples = dsp_records[scenario * DSP_SAMPLES_PER_SCENARIO:
                                      (scenario + 1) * DSP_SAMPLES_PER_SCENARIO]
                if not any(line.split()[-4:-2] != ['0', '0'] for line in samples):
                    raise ValueError(f'reference DSP scenario {scenario} is silent')
            for native in (0, 1):
                exe = tmp / f'phase-{native}.exe'
                build_host(args.framework, 'apu_phase_host.c', exe, native)
                for chunking in (0, 1):
                    actual = run(exe, chunking, text=True).splitlines()
                    compare_lines(expected, actual, f'native={native}, single-cycle={chunking}')
                print(f'native={native}: {TIMER_CASES} timer, {PORT_RACE_PHASES} port race, '
                      f'{INDEXED_STORE_PHASES} store phases and {DSP_SCENARIOS} DSP '
                      'scenarios match; whole spans and single cycles')
                exe = tmp / f'resume-{native}.exe'
                build_host(args.framework, 'apu_state_resume.c', exe, native)
                print(f'native={native}: {run(exe, text=True).strip()}')
                exe = tmp / f'queue-{native}.exe'
                build_host(args.framework, 'apu_queue_boundary.c', exe, native)
                print(f'native={native}: {run(exe, text=True).strip()}')
                print(f'native={native}: {run(exe, "--upload", text=True).strip()}')
    except (OSError, ValueError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print(f'error: {error}')
        if isinstance(error, subprocess.CalledProcessError):
            for output in (error.stdout, error.stderr):
                if output:
                    print(output.decode(errors='replace') if isinstance(output, bytes) else output)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

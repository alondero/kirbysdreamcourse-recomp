import os
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class TransitionsTest(unittest.TestCase):
    def test_boundaries_and_exclusions_do_not_change_guest_state(self):
        root = Path(__file__).resolve().parents[1]
        framework = Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework'))
        compiler = shutil.which('gcc')
        if not compiler or not (framework / 'runner/src/cpu_state.h').is_file():
            self.skipTest('Requires GCC and pinned framework')
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'transitions.exe'
            result = subprocess.run([
                compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), '-I', str(framework / 'runner/src'),
                str(root / 'tests/transitions_test.c'), str(root / 'src/transitions.c'),
                '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            env = dict(os.environ, SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                       SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1')
            result = subprocess.run([str(executable)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in ('SNESRECOMP_APU_BEAM_ELAPSED', 'SNESRECOMP_APU_HANDOFF',
                         'SNESRECOMP_APU_ABSOLUTE_PREANCHOR'):
                with self.subTest(disabled_clock=name):
                    result = subprocess.run([str(executable), 'unguarded'], env=dict(env, **{name: '0'}),
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


@unittest.skipUnless(os.environ.get('KDC_TEST_TRANSITIONS') == '1',
                     'requires a local first-hole completion recording and guarded build')
class TransitionsRuntimeTest(unittest.TestCase):
    def test_paced_solo_route(self):
        root = Path(__file__).resolve().parents[1]
        rom = Path(os.environ.get('KDC_TRANSITIONS_ROM', root / "Kirby's Dream Course (USA).sfc"))
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b')
        executable = Path(os.environ.get('KDC_TRANSITIONS_EXECUTABLE',
            root / 'local/guarded-build' / ('KirbyDreamCourseRecomp.exe' if os.name == 'nt'
                                          else 'KirbyDreamCourseRecomp')))
        # Private recording: ordinary boot -> finish first hole -> acknowledge
        # scorecard -> hole two. Keep input events unchanged between runs.
        import sys
        sys.path.insert(0, str(root / 'tools'))
        from input_recording import read_recording
        recording = read_recording(os.environ['KDC_TRANSITIONS_RECORDING'])
        frames = recording['frames']
        self.assertGreaterEqual(frames, 14000)
        actions = [(1, 'turbo on', 0), (11001, 'turbo off', 0),
                   (frames + 1, 'dump final', 0), (frames + 1, 'quit', 0)]
        actions += [(f, f'dump f{f-1}', 0) for f in (11001, 11501, 12001, 12501, 13001)]
        actions += [(e['frame'], f"press {e['button']} {e['count']}", e['count'] + 1)
                    for e in recording['events']]
        cursor, script = 1, []
        for frame, command, duration in sorted(actions, key=lambda a: a[0]):
            self.assertGreaterEqual(frame, cursor, 'checkpoint overlaps an input hold')
            script += [f'wait {frame - cursor}', command]
            cursor = frame + duration
        with tempfile.TemporaryDirectory(dir=root / 'local') as directory:
            results = {}
            for tag, enabled, bounce in (('off', 0, 1), ('on', 1, 1), ('off-repeat', 0, 1),
                                         ('fallback-off', 0, 0), ('fallback-on', 1, 0)):
                case = Path(directory) / tag
                case.mkdir(); (case / 'saves').mkdir()
                host = case / executable.name
                shutil.copy2(executable, host)
                (case / 'saves/save.srm').write_bytes(bytes(8192))
                (case / 'kdc-transitions.cfg').write_text(f'{enabled}\n')
                (case / 'input.script').write_text('\n'.join(script) + '\n')
                (case / 'config.ini').write_text(
                    '[General]\nAutosave=0\nDisableFrameDelay=0\n'
                    '[Graphics]\nOutputMethod=SDL-Software\nNewRenderer=1\nNoSpriteLimits=0\n'
                    '[Sound]\nEnableAudio=1\nAudioFreq=32000\n'
                    '[Controller]\nSourceP1=0\nSourceP2=0\n'
                    '[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\n')
                env = {k: v for k, v in os.environ.items()
                       if not k.startswith(('SNESRECOMP_', 'KDC_', 'SDL_', 'LNG_'))}
                env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                           SNESRECOMP_RUN_FRAMES=str(frames + 1),
                           SNESRECOMP_APU_BEAM_ELAPSED='1', SNESRECOMP_APU_HANDOFF='1',
                           SNESRECOMP_APU_ABSOLUTE_PREANCHOR='1', SNESRECOMP_LLE_BOUNCE=str(bounce),
                           SNESRECOMP_DUMP_DIR=str(case), SNESRECOMP_STATE_TRACE=str(case / 'state.csv'),
                           KDC_FRAME_LOG=str(case / 'cpu.log'), KDC_APU_STATE_LOG=str(case / 'apu.log'))
                result = subprocess.run([str(host), '--no-launcher', '--rom', str(rom),
                                         '--script', str(case / 'input.script')], env=env,
                                        capture_output=True, text=True, errors='replace', timeout=600)
                self.assertEqual(result.returncode, 0, result.stderr[-3000:])
                self.assertEqual('speed=3' in result.stderr, bool(enabled))
                if enabled:
                    self.assertIn('speed=1', result.stderr)  # exit before next-hole gameplay
                results[tag] = {p.name: p.read_bytes() for p in case.iterdir()
                                if p.suffix == '.bin' or p.name in ('cpu.log', 'apu.log', 'state.csv')}
                self.assertEqual(len((case / 'state.csv').read_text().splitlines()), frames)
                final = results[tag]['final.wram.bin']
                self.assertEqual(int.from_bytes(final[0x6a5e:0x6a60], 'little'), 1)
                self.assertEqual(int.from_bytes(final[0xabb2:0xabb4], 'little'), 1)
            for tag in ('on', 'off-repeat', 'fallback-off', 'fallback-on'):
                self.assertEqual(results['off'].keys(), results[tag].keys())
                for name in results['off']:
                    with self.subTest(variant=tag, observation=name):
                        self.assertEqual(results['off'][name], results[tag][name])

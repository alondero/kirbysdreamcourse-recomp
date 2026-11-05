"""Opt-in SDL event integration, using an existing Windows/Ninja game build.

KDC_TEST_SDL_CAMERA=1 enables this check; KDC_CAMERA_BUILD overrides
local/camera-build. No ROM is opened and no guest frame is executed. The
fixture compiles the actual host implementation so its static event pump,
seat assignment and guest input words are exercised without production hooks.
SDL's dummy driver and synthetic SDL focus events avoid opening a window.
"""
import os
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


class CameraSdlInputTest(unittest.TestCase):
    def test_real_sdl_events_selected_seats_and_guest_input_isolation(self):
        if os.environ.get('KDC_TEST_SDL_CAMERA') != '1':
            self.skipTest('Set KDC_TEST_SDL_CAMERA=1 for SDL virtual-pad integration')
        if os.name != 'nt':
            self.skipTest('This relink fixture currently supports Windows/Ninja builds')
        root = Path(__file__).resolve().parents[1]
        build = Path(os.environ.get('KDC_CAMERA_BUILD', root / 'local/camera-build')).resolve()
        if not (build / 'build.ninja').is_file():
            self.skipTest('Requires a complete Ninja build with static SDL3')
        ninja = (build / 'build.ninja').read_text(encoding='utf-8')
        cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')

        def cache_value(name):
            match = re.search(r'^' + name + r':[^=]+=(.+)$', cache, re.M)
            self.assertIsNotNone(match, name + ' missing in CMake cache')
            return match[1]

        host = re.search(r'^build [^\n]+/desktop/host_main\.c\.obj:.*\n'
                         r'(?:(?:  [^\n]*|)\n)*', ninja, re.M)
        self.assertIsNotNone(host, 'Built host compilation rule is missing')
        includes = re.search(r'^  INCLUDES = (.*)$', host[0], re.M)
        self.assertIsNotNone(includes)
        includes = shlex.split(includes[1])
        link = re.search(r'^build KirbyDreamCourseRecomp\.exe: .*$', ninja, re.M)
        self.assertIsNotNone(link)
        objects = link[0].split(' ', 3)[3].split(' |')[0].split()
        objects = [str(build / name) for name in objects
                   if not name.endswith(('/host_main.c.obj', '/src/main.c.obj'))]
        libraries = re.search(r'^  LINK_LIBRARIES = (.*)$', ninja[link.start():], re.M)
        self.assertIsNotNone(libraries)
        libraries = [str(build / name) if name.endswith('.a') else name
                     for name in shlex.split(libraries[1])]

        def run(command):
            result = subprocess.run(command, capture_output=True, timeout=120)
            self.assertEqual(result.returncode, 0,
                             result.stdout.decode(errors='replace') +
                             result.stderr.decode(errors='replace'))
            return result

        # All products are disposable host-test files, never game artifacts.
        with tempfile.TemporaryDirectory(prefix='kdc-sdl-camera-', dir=root / 'local') as directory:
            temporary = Path(directory)
            obj = temporary / 'input.o'
            exe = temporary / 'input.exe'
            run([cache_value('CMAKE_C_COMPILER'), '-std=gnu11', '-O1',
                 '-ffunction-sections', '-fdata-sections', '-DSNESRECOMP_SDL3=1',
                 '-DSDL_MAIN_HANDLED', '-DRECOMP_LAUNCHER', '-DSNESRECOMP_ENABLE_MODS=1',
                 *includes, '-c', str(root / 'tests/course_camera_sdl_test.c'), '-o', str(obj)])
            arguments = ['-static', '-Wl,--gc-sections', str(obj), *objects,
                         *libraries, '-o', str(exe)]
            response = temporary / 'link.rsp'
            response.write_text('\n'.join('"' + arg.replace('\\', '/') + '"'
                                          for arg in arguments), encoding='utf-8')
            run([cache_value('CMAKE_CXX_COMPILER'), '@' + str(response)])
            config = temporary / 'input.ini'
            config.write_text('[Controller]\nSourceP1 = 2\nSourceP2 = 2\n'
                              '[GamepadMap]\nEnableGamepad1 = true\nEnableGamepad2 = true\n',
                              encoding='ascii')
            result = run([str(exe), str(config)])
            self.assertIn(b'PASS: SDL virtual pads', result.stdout)


if __name__ == '__main__':
    unittest.main()

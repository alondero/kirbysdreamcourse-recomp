import shutil
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


class CameraInputTest(unittest.TestCase):
    def test_seated_input_keyboard_conflicts_and_gating(self):
        compiler = shutil.which('gcc')
        if compiler is None:
            self.skipTest('GCC is required for the ROM-free input fixture')
        root = Path(__file__).resolve().parents[1]
        headers = {
            'host_api.h': '''#pragma once
#include <stdbool.h>
bool snesrecomp_host_frame_is_live(void);
''',
            'host_main.h': '''#pragma once
#include <stdint.h>
typedef struct { int16_t right_x, right_y; int right_stick; } SnesDesktopAnalogInput;
int snesrecomp_desktop_input_active(void);
int snesrecomp_desktop_analog_input(int seat, SnesDesktopAnalogInput *out);
''',
            'config.h': '''#pragma once
#include "desktop/sdl_compat.h"
enum { kSnesHostPadSeats = 2 };
typedef struct { int gamepad_deadzone; int player_src[2]; } Config;
extern Config g_config;
int FindCmdForSdlKey(SDL_Keycode, SDL_Keymod);
''',
            'keybinds.h': '''#pragma once
#include "desktop/sdl_compat.h"
int keybinds_button_count(void);
SDL_Scancode keybinds_get_button(int, int);
''',
            'desktop/sdl_compat.h': '''#pragma once
#include <stdint.h>
typedef int SDL_Scancode;
typedef int SDL_Keycode;
typedef int SDL_Keymod;
enum { SDL_SCANCODE_KP_2=2, SDL_SCANCODE_KP_4=4, SDL_SCANCODE_KP_5=5,
       SDL_SCANCODE_KP_6=6, SDL_SCANCODE_KP_8=8 };
enum { SDLK_KP_2=12, SDLK_KP_4=14, SDLK_KP_5=15, SDLK_KP_6=16, SDLK_KP_8=18 };
enum { KMOD_CTRL=1, KMOD_ALT=2, KMOD_SHIFT=4, KMOD_GUI=8 };
const uint8_t *snesrecomp_sdl_get_keyboard_state(void);
SDL_Keymod SDL_GetModState(void);
''',
        }
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            for name, contents in headers.items():
                path = temporary / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding='ascii')
            executable = temporary / 'camera_input_test.exe'
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(temporary), '-I', str(root / 'src'),
                            str(root / 'tests/course_camera_host_test.c'),
                            str(root / 'src/course_camera_host.c'),
                            '-o', str(executable)], check=True, capture_output=True)
            environment = dict(os.environ)
            environment.pop('KDC_CAMERA_SCRIPT', None)
            subprocess.run([str(executable)], check=True, capture_output=True, env=environment)
            script = temporary / 'camera.txt'
            script.write_text('# frame horizontal vertical reset\n\n2 32767 -32768 1\n'
                              '5 -12000 6000 0 # hold axes\n', encoding='ascii')
            environment['KDC_CAMERA_SCRIPT'] = str(script)
            subprocess.run([str(executable), 'script'], check=True,
                           capture_output=True, env=environment)
            for text in ('0 32768 0 0\n', '0 0 -32769 0\n', '0 0 0 2\n',
                         '-1 0 0 0\n', '3 0 0 0\n2 0 0 0\n', '0 0 0\n',
                         '0 0 0 0 garbage\n', '99999999999999999999999 0 0 0\n',
                         '0 0 0 0 ' + ' ' * 256):
                script.write_text(text, encoding='ascii')
                result = subprocess.run([str(executable), 'invalid'], check=True,
                                        capture_output=True, env=environment)
                self.assertIn(b'physical camera input disabled', result.stderr)
            environment['KDC_CAMERA_SCRIPT'] = str(temporary / 'missing.txt')
            result = subprocess.run([str(executable), 'invalid'], check=True,
                                    capture_output=True, env=environment)
            self.assertIn(b'physical camera input disabled', result.stderr)


if __name__ == '__main__':
    unittest.main()

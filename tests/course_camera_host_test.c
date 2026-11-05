#include "course_camera_host.h"
#include "host_main.h"
#include "config.h"
#include "desktop/sdl_compat.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>

Config g_config = {.gamepad_deadzone = 8000, .player_src = {2, 0}};
static bool enabled = true;
static bool live = true;
int snes_frame_counter;
static int active = 1, connected = 1, modifiers;
static int calls, resets, horizontal, vertical, reset;
static int bound_scan = -1, system_code = -1;
static uint8_t keys[32];
static SnesDesktopAnalogInput pad;

bool KdcOrbitEnabled(void) { return enabled; }
bool snesrecomp_host_frame_is_live(void) { return live; }
void KdcOrbitReset(void) { ++resets; }
void KdcOrbitInput(int x, int y, bool r)
{
    ++calls; horizontal = x; vertical = y; reset = r;
}
int snesrecomp_desktop_input_active(void) { return active; }
int snesrecomp_desktop_analog_input(int seat, SnesDesktopAnalogInput *out)
{
    assert(seat == 0);
    *out = pad;
    return connected;
}
const uint8_t *snesrecomp_sdl_get_keyboard_state(void) { return keys; }
SDL_Keymod SDL_GetModState(void) { return modifiers; }
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod)
{
    (void)mod;
    return code == system_code;
}
int keybinds_button_count(void) { return 12; }
SDL_Scancode keybinds_get_button(int player, int button)
{
    return player == 1 && button == 0 ? bound_scan : -1;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        active = 0; /* Explicit playback works without keyboard focus. */
        pad = (SnesDesktopAnalogInput){32767, -32768, 1};
        KdcOrbitBeforeFrame();
        assert(!horizontal && !vertical && !reset);
        if (!strcmp(argv[1], "invalid")) {
            active = 1;
            snes_frame_counter = 2;
            KdcOrbitBeforeFrame();
            assert(!horizontal && !vertical && !reset);
            return 0;
        }
        snes_frame_counter = 2;
        KdcOrbitBeforeFrame();
        assert(horizontal == 32767 && vertical == -32768 && reset);
        int old_calls = calls;
        KdcOrbitBeforeFrame();
        assert(calls == old_calls);
        snes_frame_counter = 3;
        KdcOrbitBeforeFrame();
        assert(horizontal == 32767 && vertical == -32768 && !reset);
        snes_frame_counter = 5;
        KdcOrbitBeforeFrame();
        assert(horizontal == -12000 && vertical == 6000 && !reset);
        snes_frame_counter = 1;
        KdcOrbitBeforeFrame();
        assert(!horizontal && !vertical && !reset);
        snes_frame_counter = 2;
        KdcOrbitBeforeFrame();
        assert(horizontal == 32767 && vertical == -32768 && reset);
        live = false;
        old_calls = calls;
        snes_frame_counter = 5;
        KdcOrbitBeforeFrame();
        assert(calls == old_calls);
        return 0;
    }
    pad = (SnesDesktopAnalogInput){32767, -32768, 1};
    KdcOrbitBeforeFrame();
    assert(horizontal == 32767 && vertical == -32768 && reset);
    pad = (SnesDesktopAnalogInput){8000, -7999, 0};
    KdcOrbitBeforeFrame();
    assert(horizontal == 8000 && vertical == -7999 && !reset);

    connected = 0;
    keys[SDL_SCANCODE_KP_6] = keys[SDL_SCANCODE_KP_8] = 1;
    keys[SDL_SCANCODE_KP_5] = 1;
    KdcOrbitBeforeFrame();
    assert(horizontal == 32767 && vertical == -32767 && reset);
    keys[SDL_SCANCODE_KP_4] = 1;
    KdcOrbitBeforeFrame();
    assert(!horizontal);
    keys[SDL_SCANCODE_KP_4] = keys[SDL_SCANCODE_KP_8] = 0;
    keys[SDL_SCANCODE_KP_5] = 0;
    g_config.player_src[0] = 1;
    bound_scan = SDL_SCANCODE_KP_6;
    KdcOrbitBeforeFrame();
    assert(!horizontal); /* Rebound SNES controls win. */
    bound_scan = -1;
    system_code = SDLK_KP_6;
    KdcOrbitBeforeFrame();
    assert(!horizontal); /* Rebound system hotkeys win too. */
    system_code = -1;
    modifiers = KMOD_CTRL;
    KdcOrbitBeforeFrame();
    assert(!horizontal);
    modifiers = 0;
    int old_calls = calls;
    active = 0;
    KdcOrbitBeforeFrame();
    assert(calls == old_calls); /* No movement behind modal/focus/pause gate. */
    active = 1;
    enabled = false;
    KdcOrbitBeforeFrame();
    assert(calls == old_calls && resets == 1);
    return 0;
}

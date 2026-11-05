#include "course_camera_host.h"
#include "course_orbit.h"
#include "launcher_settings.h"
#include "host_main.h"
#include "host_api.h"
#include "config.h"
#include "keybinds.h"
#include "desktop/sdl_compat.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CameraEvent {
    int frame, horizontal, vertical;
    bool reset;
} CameraEvent;

static CameraEvent *events;
static size_t event_count, next_event;
static bool script_checked, script_requested, script_valid, script_started;
static int script_last_frame, script_horizontal, script_vertical;
extern int snes_frame_counter;

static bool ParseEvent(char *line, CameraEvent *event)
{
    long values[4];
    for (unsigned i = 0; i < 4; ++i) {
        while (isspace((unsigned char)*line)) ++line;
        if (!*line) return false;
        char *end;
        errno = 0;
        values[i] = strtol(line, &end, 10);
        if (errno || end == line || (*end && !isspace((unsigned char)*end)))
            return false;
        line = end;
    }
    while (isspace((unsigned char)*line)) ++line;
    if (*line && *line != '#') return false;
    if (values[0] < 0 || values[0] > INT_MAX ||
        values[1] < -32768 || values[1] > 32767 ||
        values[2] < -32768 || values[2] > 32767 ||
        values[3] < 0 || values[3] > 1) return false;
    *event = (CameraEvent){(int)values[0], (int)values[1], (int)values[2], values[3] != 0};
    return true;
}

static void LoadScript(void)
{
    script_checked = true;
    const char *path = getenv("KDC_CAMERA_SCRIPT");
    if (!path || !*path) return;
    script_requested = true;
    FILE *file = fopen(path, "r");
    bool valid = file != NULL;
    unsigned line_number = 0;
    size_t capacity = 0;
    char line[256];
    while (valid && fgets(line, sizeof(line), file)) {
        ++line_number;
        if (strlen(line) == sizeof(line) - 1 && !strchr(line, '\n')) {
            valid = false;
            break;
        }
        char *text = line;
        while (isspace((unsigned char)*text)) ++text;
        if (!*text || *text == '#') continue;
        CameraEvent event;
        if (!ParseEvent(text, &event) || event_count == 65536 ||
            (event_count && event.frame < events[event_count - 1].frame)) {
            valid = false;
            break;
        }
        if (event_count == capacity) {
            capacity = capacity ? capacity * 2 : 256;
            CameraEvent *grown = realloc(events, capacity * sizeof(*events));
            if (!grown) { valid = false; break; }
            events = grown;
        }
        events[event_count++] = event;
    }
    if (file) {
        if (ferror(file)) valid = false;
        fclose(file);
    }
    script_valid = valid;
    if (!valid) {
        free(events);
        events = NULL;
        event_count = 0;
        fprintf(stderr, "Camera script unavailable or invalid at line %u: %s; physical camera input disabled.\n",
                line_number, path);
    }
}

static bool ScriptInput(void)
{
    if (!script_checked) LoadScript();
    if (!script_requested) return false;
    if (!script_valid) { KdcOrbitInput(0, 0, false); return true; }
    int frame = snes_frame_counter;
    if (script_started && frame == script_last_frame) return true;
    if (!script_started || frame < script_last_frame) {
        next_event = 0;
        script_horizontal = script_vertical = 0;
    }
    script_started = true;
    script_last_frame = frame;
    bool reset = false;
    while (next_event < event_count && events[next_event].frame <= frame) {
        const CameraEvent *event = &events[next_event++];
        script_horizontal = event->horizontal;
        script_vertical = event->vertical;
        reset |= event->reset && event->frame == frame;
    }
    KdcOrbitInput(script_horizontal, script_vertical, reset);
    return true;
}

static bool CameraKey(const uint8_t *keys, SDL_Scancode scan, SDL_Keycode code)
{
    if (!keys[scan] || FindCmdForSdlKey(code, SDL_GetModState())) return false;
    /* Custom SNES bindings take precedence over presentation shortcuts. */
    for (int player = 0; player < kSnesHostPadSeats; ++player) {
        if (g_config.player_src[player] != 1) continue;
        for (int button = 0; button < keybinds_button_count(); ++button)
            if (keybinds_get_button(player + 1, button) == scan) return false;
    }
    return true;
}

void KdcOrbitBeforeFrame(void)
{
    if (!snesrecomp_host_frame_is_live()) return;
    if (!KdcOrbitEnabled()) {
        KdcOrbitReset();
        return;
    }
    /* Explicit test playback also works with SDL's headless dummy driver.
     * It replaces physical input, and never advances on speculative ticks. */
    if (ScriptInput() || !snesrecomp_desktop_input_active()) return;
    int horizontal = 0, vertical = 0;
    bool reset = false;
    SnesDesktopAnalogInput pad;
    /* The host owns device opening, seat selection and disconnect handling.
     * P1's selected controller controls the shared presentation camera. */
    if (snesrecomp_desktop_analog_input(0, &pad)) {
        /* Camera math applies its radial deadzone once, independently of
         * the SNES directional controls' configurable deadzone. */
        horizontal = pad.right_x;
        vertical = pad.right_y;
        reset = pad.right_stick != 0;
    }
    const uint8_t *keys = snesrecomp_sdl_get_keyboard_state();
    if (keys && !(SDL_GetModState() & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_GUI))) {
        int right = CameraKey(keys, SDL_SCANCODE_KP_6, SDLK_KP_6);
        int left = CameraKey(keys, SDL_SCANCODE_KP_4, SDLK_KP_4);
        int down = CameraKey(keys, SDL_SCANCODE_KP_2, SDLK_KP_2);
        int up = CameraKey(keys, SDL_SCANCODE_KP_8, SDLK_KP_8);
        if (right || left) horizontal = (right - left) * 32767;
        if (down || up) vertical = (down - up) * 32767;
        reset |= CameraKey(keys, SDL_SCANCODE_KP_5, SDLK_KP_5);
    }
    KdcOrbitInput(horizontal, vertical, reset);
}

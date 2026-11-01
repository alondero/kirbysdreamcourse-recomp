/*
 * Kirby Dream Course — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.h): the pre-boot launcher, ROM
 * resolution and digest checks, config.ini and keybinds.ini, the window and
 * the SDL / OpenGL presenters, audio, gamepads, the in-game save-state
 * browser and rewind filmstrip, the OSD, the pacing clock, crash handlers and
 * the post-mortem report, mod packages, Generate & rebuild, and the netplay
 * barrier when the project is built with it. A fix there reaches this
 * project on a submodule pull; nothing in this file needs to change for it.
 *
 * This file used to BE the host -- a ~700-line copy made at scaffold time --
 * so every project scaffolded before a fix kept the bug. Two ports of the
 * same game, a week apart, had different hosts.
 *
 * Nothing here ever ships a ROM. The player supplies one; the host makes
 * that easy (launcher, then positional argument, then a copy beside the
 * executable, then the rom.cfg cache, then a file picker) and checks what it
 * is handed against the digests in rom_identity.txt.
 *
 * Grow this file only with what is specific to THIS title: a custom
 * presenter (prepare_frame / draw_frame), an SPC player, a pacing rule
 * (keep_pacing_debt), a Mods provider. See SnesDesktopHostGame for the
 * complete list of hooks and what each is for.
 */

#include "host_main.h"
#include "game_rtl.h"
#include "launcher_settings.h"
#include "snes/ppu.h"
#include "snesrecomp_rom_identity.h"  /* generated from rom_identity.txt */

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#include "config.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

/* Widen the raster, keeping the original pixel shape and camera centre. */
static void PrepareWideFrame(int drawable_w, int drawable_h, int *width, int *height)
{
    *height = 224;
    *width = 256;
    if (!g_config.widescreen) return;
    int extra = PpuWsExtraOverride();
    if (extra >= 0) {
        *width += 2 * extra;
        return;
    }
    double aspect = drawable_h > 0 ? (double)drawable_w / drawable_h : 4.0 / 3.0;
    *width = SnesDisplayAspect_ComputeAdaptiveFrame(
        256, 224, kPpuBufWidth, aspect,
        SnesDisplayAspect_Clamp(g_config.display_aspect)).width;
}

static const struct RecompLauncherCModProvider *LauncherSettings(void)
{
    return KdcLauncherSettings(&g_config.widescreen, SNESRECOMP_ROM_EXPECTED_SHA256);
}

static const char *ValidateRomPath(const char *path)
{
    return KdcValidateRomPath(path, SNESRECOMP_ROM_EXPECTED_SHA256);
}

static const SnesDesktopHostGame kGameHost = {
    .display_name        = "Kirby's Dream Course",
    .window_title        = "Kirby's Dream Course",
    .region              = SNESRECOMP_ROM_REGION,
    .rom_file            = SNESRECOMP_ROM_FILE,
    .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
    .expected_crc32_hex  = SNESRECOMP_ROM_EXPECTED_CRC32,
    .rom_help           = "Use the unmodified headerless 1 MiB USA image; copier headers are not accepted.",
    .game_id             = SNESRECOMP_ROM_GAME_ID,
    .build_version       = SNES_GAME_VERSION,
    .game_info           = &kGameInfo,
    .num_players         = 2,
    .widescreen_supported = 1,
    .native_widescreen    = 1,
    .in_game_launcher   = 1,
    .launcher_with_rom  = 1,
    .validate_rom_path  = ValidateRomPath,
    .display_aspect_supported = 1,
    .mods_provider      = LauncherSettings,
    .configure_launcher = KdcConfigureLauncher,
    .prepare_frame       = PrepareWideFrame,
    /* Match SNES sprite evaluation by default; users may override this in
     * their existing config.ini. Other values mirror the framework template. */
    .default_config_ini =
        "[General]\nAutosave = 0\nDisableFrameDelay = 0\n"
        "[Graphics]\nWindowSize = Auto\nFullscreen = 0\nWindowScale = 3\n"
        "NewRenderer = 1\nIgnoreAspectRatio = 0\nNoSpriteLimits = 0\nWidescreen = 0\n"
        "[Sound]\nEnableAudio = 1\nVolume = 100\nAudioFreq = 32000\n"
        "AudioChannels = 2\nAudioSamples = 512\n"
        "[KeyMap]\nFullscreen = Alt+Return\nReset = Ctrl+r\nPause = Shift+p\n"
        "PauseDimmed = p\nTurbo = Tab\nWindowBigger = Ctrl+Up\n"
        "WindowSmaller = Ctrl+Down\nVolumeUp = Keypad +\nVolumeDown = Keypad -\n"
        "DisplayPerf = f\nToggleRenderer = r\nSaveStateMenu = F11\nRewind = F12\n"
        "Load = F1,F2,F3,F4,F5,F6,F7,F8,F9,F10\n"
        "Save = Shift+F1,Shift+F2,Shift+F3,Shift+F4,Shift+F5,Shift+F6,Shift+F7,Shift+F8,Shift+F9,Shift+F10\n"
        "[Controller]\nSourceP1 = 1\nSourceP2 = 0\n"
        "[GamepadMap]\nEnableGamepad1 = true\nEnableGamepad2 = true\n"
        "Controls = DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb\n"
        "ControlsP2 = DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb\n",
    /* Battery-backed SRAM shows the launcher's SAVES panel. Leave NULL for a
     * title without one. The path is exe-relative. */
    .sram_path           = "saves/save.srm",
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
int main(int argc, char **argv)
{
    return snesrecomp_desktop_main(&kGameHost, argc, argv);
}

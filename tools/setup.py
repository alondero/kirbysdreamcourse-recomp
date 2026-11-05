"""Build the guarded native game on Linux or Windows; keep derived code local."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK_REV = "147b55e252dcb3e7680a99c7c2f50e7183891844"
ROM_SHA256 = "0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b"
# Ordinary builds share this guarded profile in application order. Experimental
# combinations continue to use prepare_native_experiment.ps1.
PATCHES = (
    "apu-handoff", "simple-bus-timing", "refresh-clock-experiment",
    "launcher-with-rom", "widescreen-tile-provider", "scenery-provider",
    "extra-object-provider", "apu-rate-experiment", "preanchor-experiment",
    "beam-apu-clock-experiment", "beam-apu-fraction-experiment", "hdma-prime-experiment",
    "spc-timer-phase-experiment", "refresh-park-experiment", "refresh-position-experiment",
    "soft-reset-vram-experiment", "savestate-script-probe", "beam-state-experiment",
    "state-preflight-experiment", "refresh-zero-epoch-experiment", "zero-dma-experiment",
    "absolute-preanchor-experiment", "raster-state-experiment", "field-timing-experiment",
    "rdnmi-beam-experiment", "instruction-boundaries-experiment", "incremental-brr-experiment",
    "shared-noise-experiment", "nmi-entry-experiment", "nmi-trigger-experiment",
    "refresh-short-experiment", "live-wram-experiment", "hdma-count-experiment",
    "beam-hdma-experiment", "hdma-interrupt-experiment", "late-nmi-experiment",
    "irq-entry-experiment", "emu-stack-experiment", "cpu-io-openbus-experiment",
    "nested-nmi-experiment", "wai-wake-experiment", "ppu-latch-phase-experiment",
    "hvbjoy-phase-experiment", "reset-phase-experiment", "wai-cycle-experiment",
    "ppu2-openbus-experiment", "apu-guest-read-experiment", "raster-live-restore-experiment",
    "manual-joypad-strobe-experiment", "autojoy-phase-experiment", "spc-indexed-write-state-experiment",
)
STATE_OPTIONS = (
    "STATE", "RASTER_STATE", "FIELD_STATE", "DSP_STATE", "BEAM_HDMA_STATE",
    "LATE_NMI_STATE", "PPU2_BUS_STATE", "AUTOJOY_STATE", "SPC_D4_STATE",
)


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def git_output(directory, *args):
    return subprocess.check_output(
        ["git", "-C", str(directory), *args], text=True).strip()


def build(args):
    rom = Path(args.rom).resolve(strict=True)
    with rom.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != ROM_SHA256:
            raise ValueError("ROM SHA-256 mismatch. Use the supported headerless USA ROM.")
    if not re.fullmatch(r"[A-Za-z0-9._+-]+", args.version):
        raise ValueError("Unsupported build version")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        raise ValueError("Unsupported build name")
    for command in ("git", "gcc", "g++", "cargo", "ninja", args.cmake):
        if not shutil.which(command):
            raise ValueError(f"Missing build tool: {command}")
    local = ROOT / "local"
    local.mkdir(exist_ok=True)
    pristine = local / "snesrecomp"
    framework = local / f"{args.name}-framework"
    project = local / args.name
    output = local / f"{args.name}-build"
    for directory in (framework, project, output):
        if directory.exists():
            raise ValueError(f"{directory} already exists; choose a fresh --name")
    if not pristine.exists():
        run("git", "clone", "-c", "core.autocrlf=false", "--no-checkout",
            "https://github.com/RetroPortingToolKit/snesrecomp.git", pristine)
        run("git", "-C", pristine, "checkout", "--detach", FRAMEWORK_REV)
    if git_output(pristine, "rev-parse", "HEAD") != FRAMEWORK_REV:
        raise ValueError("local/snesrecomp is not at the pinned revision")
    if git_output(pristine, "status", "--porcelain"):
        raise ValueError("local/snesrecomp has local changes; use a clean pinned checkout")
    run("git", "clone", "-c", "core.autocrlf=false", "--no-hardlinks", "--no-checkout", pristine, framework)
    run("git", "-C", framework, "checkout", "--detach", FRAMEWORK_REV)
    patches = list(PATCHES)
    if args.apu_boundary_sync:
        patches.append("apu-boundary-sync")
    for name in patches:
        patch = ROOT / "patches" / f"snesrecomp-{name}.patch"
        run("git", "-C", framework, "apply", "--check", patch)
        run("git", "-C", framework, "apply", patch)
    config = project / "recomp"
    config.mkdir(parents=True)
    shutil.copy2(ROOT / "recomp/bank00.cfg", config)
    symbols = (ROOT / "recomp/symbols.toml").read_text(encoding="utf-8-sig")
    pattern = r'(\[\[func\]\]\s*\nname\s*=\s*"I_RESET".*?\nemit\s*=\s*)false'
    symbols, count = re.subn(pattern, r"\g<1>true", symbols, flags=re.DOTALL)
    if count != 1:
        raise ValueError("Expected one disabled I_RESET symbol")
    (config / "symbols.toml").write_text(symbols, encoding="utf-8")
    env = dict(os.environ, SNESRECOMP_EMIT_SIMPLE_BUS_TIMING="1", SNESRECOMP_EMIT_AOT_DENY_GATE="1")
    run(sys.executable, framework / "snesrecomp_cli.py", "generate", "--rom", rom,
        "--project-root", project, "--cfg-dir", "recomp", "--out-dir", "generated",
        "--funcs-h", "recomp/funcs.h", "--expected-crc32", "df8153d9",
        "--expected-sha256", ROM_SHA256, env=env)
    options = [
        "-DCMAKE_BUILD_TYPE=Release", f"-DKDC_BUILD_VERSION={args.version}",
        f"-DKDC_RELEASE_DEFAULTS={'ON' if args.release_defaults else 'OFF'}",
        f"-DSNESRECOMP_ROOT={framework}", f"-DKDC_GENERATED_DIR={project / 'generated'}",
        f"-DKDC_FUNCTIONS_DIR={config}",
        *[f"-DKDC_EXPERIMENTAL_{name}=ON" for name in STATE_OPTIONS],
    ]
    if args.apu_boundary_sync:
        options.append("-DKDC_EXPERIMENTAL_APU_BOUNDARY_STATE=ON")
    if args.sdl_source:
        options.append(f"-DFETCHCONTENT_SOURCE_DIR_SDL3={Path(args.sdl_source).resolve(strict=True)}")
    if sys.platform == "win32":
        windres = Path(shutil.which("gcc")).parent / "windres.exe"
        if windres.is_file():
            options.append(f"-DCMAKE_RC_COMPILER={windres.as_posix()}")
    else:
        # SDL backends use host display/audio libraries; never bundle glibc/GL.
        options += ["-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON",
                    # Optional launcher chat emoji must not add machine-dependent
                    # FreeType/HarfBuzz dependencies to this game's package.
                    "-DCMAKE_DISABLE_FIND_PACKAGE_Freetype=ON"]
    run(args.cmake, "-S", ROOT, "-B", output, "-G", "Ninja", *options)
    run(args.cmake, "--build", output, "--parallel", str(args.jobs))
    executable = "KirbyDreamCourseRecomp" + (".exe" if sys.platform == "win32" else "")
    print(f"Built {output / executable}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", default="Kirby's Dream Course (USA).sfc")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--sdl-source")
    parser.add_argument("--version", default="dev")
    parser.add_argument("--name", default="guarded")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--release-defaults", action="store_true")
    parser.add_argument("--apu-boundary-sync", action="store_true")
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()

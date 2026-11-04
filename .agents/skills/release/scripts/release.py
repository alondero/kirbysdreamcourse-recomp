"""Local Windows/source packaging and read-only GitHub release verification."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import quote
import zipfile


REPO = "alondero/kirbysdreamcourse-recomp"
REPO_URL = f"https://github.com/{REPO}"
PRIVATE_DIRS = {"local", "generated", "recordings", "saves", "roms", "rom-assets", ".git", ".worktrees"}
PRIVATE_SUFFIXES = {".sfc", ".smc", ".fig", ".bin", ".sav", ".srm", ".state",
                    ".exe", ".dll", ".pdb", ".wav", ".pcm", ".log"}
NOTICES = {"LICENSE", "LICENSE.snesrecomp", "LICENSE.SDL3.txt", "THIRD_PARTY_ATTRIBUTION.md"}
WINDOWS_DLLS = set("advapi32 bcrypt cfgmgr32 comctl32 comdlg32 d2d1 d3d11 d3d12 dbghelp "
                   "dinput8 dwmapi dwrite dxgi gdi32 hid imm32 iphlpapi kernel32 ksuser "
                   "msimg32 msvcrt ntdll ole32 oleaut32 opengl32 powrprof propsys psapi "
                   "rpcrt4 secur32 setupapi shell32 shlwapi user32 userenv usp10 uxtheme "
                   "version winhttp winmm winspool wintrust ws2_32 wtsapi32 xinput1_4".split())


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, encoding="utf-8").stdout.strip()


def api(endpoint):
    return json.loads(run("gh", "api", endpoint))


def commit(ref):
    return run("git", "rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}")


def version(value):
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", value):
        raise argparse.ArgumentTypeError("Expected vMAJOR.MINOR.PATCH with optional prerelease suffix")
    return value


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inspect_zip(path, source=False):
    with zipfile.ZipFile(path) as archive:
        if not archive.infolist():
            raise ValueError("Empty ZIP")
        bad = archive.testzip()
        if bad:
            raise ValueError(f"ZIP integrity failure: {bad}")
        names = []
        for entry in archive.infolist():
            raw_name = entry.orig_filename  # ZipInfo normalizes backslashes on Windows.
            name = PurePosixPath(raw_name)
            if (name.is_absolute() or ".." in name.parts or "\\" in raw_name
                    or ":" in raw_name or "\0" in raw_name):
                raise ValueError(f"Unsafe ZIP path: {raw_name}")
            names.append(name)
        if source:
            roots = {name.parts[0] for name in names}
            if len(roots) != 1:
                raise ValueError("Source ZIP must have one top-level directory")
            relative = [PurePosixPath(*name.parts[1:]) for name in names if len(name.parts) > 1]
            for name in relative:
                parts = {part.lower() for part in name.parts}
                if (parts & PRIVATE_DIRS or name.suffix.lower() in PRIVATE_SUFFIXES
                        or re.search(r"\.state\d+$", name.name, re.IGNORECASE)
                        or name.as_posix().lower().startswith(("assets/extracted/", "src/gen/"))
                        or name.as_posix().lower() == "recomp/funcs.h"):
                    raise ValueError(f"Private/generated content in source ZIP: {name}")
            missing = NOTICES - {name.as_posix() for name in relative}
            if missing:
                raise ValueError(f"Missing source notices: {', '.join(sorted(missing))}")
        return [name.as_posix() for name in names]


def changes(args):
    target = commit(args.target)
    if args.base:
        base = commit(args.base)
        run("git", "merge-base", "--is-ancestor", base, target)
        revision = f"{base}..{target}"
    else:
        revision = target
    commits = run("git", "rev-list", "--reverse", revision).splitlines()
    print(f"Target: {target}\nBase: {args.base or '(first release; includes root commit)'}")
    print(run("git", "log", "--reverse", "--oneline", revision))
    shipped = set(commits)
    prs = {}
    for sha in commits:
        pages = json.loads(run("gh", "api", "--paginate", "--slurp",
                               f"repos/{REPO}/commits/{sha}/pulls?per_page=100"))
        for page in pages:
            for pr in page:
                if pr.get("merged_at") and pr.get("merge_commit_sha") in shipped:
                    prs[pr["number"]] = pr
    print("\nMerged PRs in this commit range:")
    for number, pr in sorted(prs.items()):
        print(f"- #{number}: {pr['title']} ({pr['html_url']})")
    if not prs:
        print("(none; review the commits directly)")


def source_package(args):
    target = commit(args.target)
    root = Path(run("git", "rev-parse", "--show-toplevel"))
    destination = root / "local" / "releases" / args.version
    destination.mkdir(parents=True, exist_ok=True)
    path = destination / f"kirbysdreamcourse-recomp-source-{args.version}.zip"
    if path.exists():
        raise ValueError(f"Refusing to overwrite {path}")
    # Validate before placing an archive at the final path, even for an unsafe commit.
    with tempfile.TemporaryDirectory(prefix="kdc-source-") as temporary:
        candidate = Path(temporary) / path.name
        run("git", "archive", "--format=zip", f"--prefix=kirbysdreamcourse-recomp-{args.version}/",
            f"--output={candidate}", target)
        inventory = inspect_zip(candidate, source=True)
        path.write_bytes(candidate.read_bytes())
    print("\n".join(inventory))
    print(f"\nCommit: {target}\nArchive: {path}\nSHA-256: {sha256(path)}")


def windows_files(stage, target, tag):
    """Check binary staging; generated source and ROM inputs remain private."""
    # Resolve the root too: Windows TEMP may use an 8.3 alias such as RUNNER~1.
    stage = stage.resolve(strict=True)
    required = NOTICES | {"KirbyDreamCourseRecomp.exe", "README.txt", "build-info.json"}
    files = []
    names = set()
    for path in sorted(stage.rglob("*")):
        if path.is_symlink() or path.resolve() != stage / path.relative_to(stage):
            raise ValueError(f"Links are not permitted in Windows staging: {path}")
        if path.is_dir():
            continue
        name = path.relative_to(stage)
        lower_parts = {part.lower() for part in name.parts}
        if (lower_parts & PRIVATE_DIRS
                or path.suffix.lower() in PRIVATE_SUFFIXES - {".exe", ".dll"}
                or re.search(r"\.state\d+$", path.name, re.IGNORECASE)
                or name.as_posix().lower().startswith("assets/extracted/")):
            raise ValueError(f"Private/generated content in Windows staging: {name}")
        root_file = len(name.parts) == 1 and (name.name in required or path.suffix.lower() == ".dll")
        resource = len(name.parts) > 1 and name.parts[0] in {"assets", "licenses"}
        if not (root_file or resource) or (resource and path.suffix.lower() in {".exe", ".dll"}):
            raise ValueError(f"Unreviewed Windows staging path: {name}")
        names.add(name.as_posix())
        files.append(path)
    missing = required - names
    if missing or not any(name.startswith("assets/") for name in names) \
            or not any(name.startswith("licenses/") for name in names):
        raise ValueError(f"Missing Windows package files/resources/notices: {sorted(missing)}")
    with (stage / "KirbyDreamCourseRecomp.exe").open("rb") as executable:
        if executable.read(2) != b"MZ":
            raise ValueError("Expected a Windows x64 PE executable")
        executable.seek(0x3C)
        offset = int.from_bytes(executable.read(4), "little")
        executable.seek(offset)
        header = executable.read(26)
        characteristics = int.from_bytes(header[22:24], "little")
        if (len(header) != 26 or header[:6] != b"PE\0\0\x64\x86"
                or header[24:26] != b"\x0b\x02" or not characteristics & 2 or characteristics & 0x2000):
            raise ValueError("Expected a Windows x64 PE executable")
    info = json.loads((stage / "build-info.json").read_text(encoding="utf-8-sig"))
    if info.get("source_commit") != target or info.get("version") != tag:
        raise ValueError("Windows build metadata does not match the target commit/version")
    for field in ("build_command", "toolchain", "framework_revision", "ui_revision"):
        if not isinstance(info.get(field), str) or not info[field].strip():
            raise ValueError(f"Missing Windows build metadata: {field}")
    return files


def windows_package(args):
    target = commit(args.target)
    stage = Path(args.stage).resolve(strict=True)
    if not stage.is_dir():
        raise ValueError("Windows stage must be a directory")
    files = windows_files(stage, target, args.version)
    root = Path(run("git", "rev-parse", "--show-toplevel"))
    destination = root / "local" / "releases" / args.version
    destination.mkdir(parents=True, exist_ok=True)
    path = destination / "kirbysdreamcourse-recomp-windows-x64.zip"
    if path.exists():
        raise ValueError(f"Refusing to overwrite {path}")
    with tempfile.TemporaryDirectory(prefix="kdc-windows-") as temporary:
        candidate = Path(temporary) / path.name
        with zipfile.ZipFile(candidate, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for file in files:
                archive.write(file, file.relative_to(stage).as_posix())
        inventory = inspect_zip(candidate)
        with path.open("xb") as output:
            output.write(candidate.read_bytes())
    print("\n".join(inventory))
    print(f"\nCommit: {target}\nArchive: {path}\nSHA-256: {sha256(path)}")
    print("Test the extracted ZIP before publishing; packaging alone is not a gameplay check.")


def stage_windows(args):
    target = commit(args.target)
    root = Path(run("git", "rev-parse", "--show-toplevel"))
    build = Path(args.build).resolve(strict=True)
    stage = Path(args.stage).resolve()
    if stage.exists():
        raise ValueError(f"Refusing to overwrite staging directory {stage}")
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    build_version = re.search(r"^KDC_BUILD_VERSION:STRING=(.+)$", cache, re.MULTILINE)
    if not build_version or build_version[1].strip() != args.version \
            or not re.search(r"^KDC_RELEASE_DEFAULTS:BOOL=(?:1|ON|TRUE)\s*$", cache, re.MULTILINE):
        raise ValueError("Build cache must match the release version and enable release defaults")
    stage.mkdir(parents=True)
    executable = build / "KirbyDreamCourseRecomp.exe"
    shutil.copy2(executable, stage / executable.name)
    for name in ("assets", "licenses"):
        shutil.copytree(build / name, stage / name)
    for name in NOTICES:
        shutil.copy2(root / name, stage / name)
    for name in ("gcc", "mingw-w64"):
        shutil.copytree(Path(args.toolchain_notices) / name, stage / "licenses/toolchain" / name)
    # Inspect import closure; never rely on the compiler's PATH at runtime.
    dependencies = {}
    pending = [executable]
    visited = set()
    while pending:
        binary = pending.pop()
        if binary.name.lower() in visited:
            continue
        visited.add(binary.name.lower())
        imports = re.findall(r"DLL Name:\s*(\S+)", run(args.objdump, "-p", str(binary)), re.IGNORECASE)
        if not imports:
            raise ValueError(f"No PE import information from objdump: {binary.name}")
        dependencies[binary.name] = imports
        for name in imports:
            if name.lower().removesuffix(".dll") in WINDOWS_DLLS or name.lower().startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            candidates = [build / name, Path(args.objdump).parent / name]
            source = next((path for path in candidates if path.is_file()), None)
            if source is None:
                raise ValueError(f"Required runtime DLL not found: {name}")
            destination = stage / name
            if not destination.exists():
                shutil.copy2(source, destination)
            pending.append(destination)
    framework = root / "local/guarded-framework"
    ui = re.search(r"^RECOMP_UI_ROOT:PATH=(.+)$", cache, re.MULTILINE)
    if not ui:
        raise ValueError("Cannot find the pinned UI source in CMakeCache.txt")
    info = {
        "source_commit": target, "version": args.version,
        "build_command": f"./tools/build_release.ps1 -Version {args.version} -Rom <private-input>",
        "toolchain": run(str(Path(args.objdump).parent / "gcc.exe"), "--version").splitlines()[0],
        "framework_revision": run("git", "-C", str(framework), "rev-parse", "HEAD"),
        "ui_revision": run("git", "-C", ui[1].strip(), "rev-parse", "HEAD"),
        "cmake_version": run(re.search(r"^CMAKE_COMMAND:INTERNAL=(.+)$", cache, re.MULTILINE)[1].strip(), "--version").splitlines()[0],
        "dependencies": dependencies,
        "release_defaults": True,
    }
    (stage / "build-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(root / "tools/release-readme.txt", stage / "README.txt")
    windows_files(stage, target, args.version)
    print(f"Staged Windows EXE and inspected DLL imports: {stage}")


def smoke_windows(args):
    root = Path(run("git", "rev-parse", "--show-toplevel"))
    identity = (root / "rom_identity.txt").read_text(encoding="utf-8-sig")
    expected = re.search(r"^expected_sha256\s*=\s*([0-9a-f]{64})\s*$", identity, re.MULTILINE)[1]
    rom = Path(args.rom).resolve(strict=True)
    if sha256(rom) != expected:
        raise ValueError("ROM SHA-256 mismatch before release smoke test")
    archive = Path(args.archive).resolve(strict=True)
    inspect_zip(archive)
    defaults = {"SNESRECOMP_APU_BEAM_ELAPSED": "1", "SNESRECOMP_APU_HANDOFF": "1",
                "SNESRECOMP_APU_ABSOLUTE_PREANCHOR": "1", "SNESRECOMP_LLE_BOUNCE": "1"}
    base = {key: value for key, value in os.environ.items()
            if not key.startswith(("SNESRECOMP_", "KDC_", "SDL_"))}
    # Eliminate developer-tool DLL search paths; Windows system libraries stay available.
    system = Path(os.environ["SystemRoot"])
    base.update(PATH=os.pathsep.join((str(system / "System32"), str(system))),
                SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SNESRECOMP_RUN_FRAMES=str(args.frames))
    with tempfile.TemporaryDirectory(prefix="release-smoke-", dir=root / "local") as temporary:
        workspace = Path(temporary)
        for mode in ("native", "fallback"):
            logs = []
            for explicit in (False, True):
                case = workspace / f"{mode}-{explicit}"
                with zipfile.ZipFile(archive) as zipped:
                    zipped.extractall(case)
                (case / "config.ini").write_text(
                    "[General]\nSkipLauncher = 1\nAutosave = 0\n"
                    "[Graphics]\nOutputMethod = SDL-Software\n"
                    "[Sound]\nEnableAudio = 1\nAudioFreq = 32000\n"
                    "[Controller]\nSourceP1 = 0\nSourceP2 = 0\n", encoding="utf-8")
                log = case / "cpu.log"
                env = {**base, "KDC_FRAME_LOG": str(log)}
                if explicit:
                    env.update(defaults)
                if mode == "fallback":
                    env["SNESRECOMP_LLE_BOUNCE"] = "0"
                command = (str(case / "KirbyDreamCourseRecomp.exe"), "--rom", str(rom))
                result = subprocess.run(command, cwd=case, env=env, capture_output=True, timeout=90)
                if result.returncode != 0:
                    raise ValueError(f"{mode} clean-PATH launch failed ({result.returncode}): {result.stderr.decode(errors='replace')}")
                lines = log.read_text().splitlines()
                if len(lines) != args.frames:
                    raise ValueError(f"{mode} expected {args.frames} frames, got {len(lines)}")
                logs.append(lines)
            for frame, (direct, explicit) in enumerate(zip(*logs), 1):
                if direct != explicit:
                    raise ValueError(f"First direct/default-environment divergence: {mode} frame {frame}")
            print(f"{mode}: {args.frames} frames; direct defaults match explicit developer settings; clean PATH")
        invalid = workspace / "wrong.sfc"
        invalid.write_bytes(bytes(1048576))  # Correct length, deliberately wrong SHA-256.
        case = workspace / "native-False"
        invalid_log = workspace / "invalid.log"
        env = {**base, "KDC_FRAME_LOG": str(invalid_log)}
        result = subprocess.run((str(case / "KirbyDreamCourseRecomp.exe"), "--no-launcher", "--rom", str(invalid)),
                                cwd=case, env=env, capture_output=True, timeout=30)
        if (result.returncode == 0 or invalid_log.exists()
                or "headerless 1 MiB" not in result.stderr.decode(errors="replace")):
            raise ValueError("Wrong-ROM input was not clearly rejected before guest execution")
        print("Wrong-ROM input rejected before guest execution")
    print("Untested here: interactive launcher, physical controllers, audio quality and full-game/reference replay.")


def verify_metadata(release, tag, title, prerelease, archive):
    expected_url = f"{REPO_URL}/releases/tag/{tag}"
    if release.get("tag_name") != tag or release.get("html_url") != expected_url:
        raise ValueError("Release is not bound to the expected tag URL")
    if release.get("draft") is not False or release.get("prerelease") is not prerelease:
        raise ValueError("Unexpected draft/prerelease state")
    if release.get("name") != title:
        raise ValueError("Unexpected release title")
    assets = release.get("assets", [])
    if len(assets) != 1 or assets[0].get("name") != archive.name:
        raise ValueError("Expected exactly the tested ZIP asset; unexpected or missing assets")
    asset = assets[0]
    if asset.get("state") != "uploaded" or asset.get("size") != archive.stat().st_size:
        raise ValueError("Asset upload state or size differs from the tested archive")


def verify(args):
    archive = Path(args.archive).resolve(strict=True)
    inspect_zip(archive)
    target = commit(args.target)
    refs = run("git", "ls-remote", "--tags", f"{REPO_URL}.git",
               f"refs/tags/{args.version}", f"refs/tags/{args.version}^{{}}")
    refs = dict(line.split()[::-1] for line in refs.splitlines())
    remote_sha = refs.get(f"refs/tags/{args.version}^{{}}", refs.get(f"refs/tags/{args.version}"))
    if remote_sha != target:
        raise ValueError(f"Remote tag commit {remote_sha} differs from expected {target}")
    release = api(f"repos/{REPO}/releases/tags/{quote(args.version, safe='')}")
    verify_metadata(release, args.version, args.title, args.prerelease == "true", archive)
    with tempfile.TemporaryDirectory(prefix="kdc-release-") as temporary:
        run("gh", "release", "download", args.version, "--repo", REPO, "--pattern", archive.name,
            "--dir", temporary)
        downloaded = Path(temporary) / archive.name
        inspect_zip(downloaded)
        digest = sha256(archive)
        if sha256(downloaded) != digest:
            raise ValueError("Downloaded asset SHA-256 differs from the tested archive")
    print(f"Verified: {release['html_url']}\nCommit: {target}\nAsset: {archive.name}\nSHA-256: {digest}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    change = commands.add_parser("changes", help="List shipped commits and merged PRs")
    change.add_argument("--target", required=True)
    change.add_argument("--base")
    change.set_defaults(action=changes)
    package = commands.add_parser("source-package", help="Archive tracked source locally, without ROM inputs")
    package.add_argument("--target", required=True)
    package.add_argument("--version", required=True, type=version)
    package.set_defaults(action=source_package)
    windows = commands.add_parser("windows-package", help="ZIP a reviewed, prebuilt Windows x64 EXE stage")
    windows.add_argument("--target", required=True)
    windows.add_argument("--version", required=True, type=version)
    windows.add_argument("--stage", required=True)
    windows.set_defaults(action=windows_package)
    stage = commands.add_parser("stage-windows", help="Stage a guarded build and inspect DLL import closure")
    stage.add_argument("--target", required=True)
    stage.add_argument("--version", required=True, type=version)
    stage.add_argument("--build", required=True)
    stage.add_argument("--stage", required=True)
    stage.add_argument("--objdump", required=True)
    stage.add_argument("--toolchain-notices", required=True)
    stage.set_defaults(action=stage_windows)
    smoke = commands.add_parser("smoke-windows", help="Run the packaged EXE with private ROM input and clean PATH")
    smoke.add_argument("--archive", required=True)
    smoke.add_argument("--rom", required=True)
    smoke.add_argument("--frames", type=int, default=400, choices=range(1, 10001), metavar="1..10000")
    smoke.set_defaults(action=smoke_windows)
    check = commands.add_parser("verify", help="Verify a published ZIP against the tested local archive")
    check.add_argument("--target", required=True)
    check.add_argument("--version", required=True, type=version)
    check.add_argument("--archive", required=True)
    check.add_argument("--title", required=True)
    check.add_argument("--prerelease", required=True, choices=("true", "false"))
    check.set_defaults(action=verify)
    args = parser.parse_args()
    try:
        args.action(args)
    except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

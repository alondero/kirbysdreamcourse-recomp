"""Local Windows/source packaging and read-only GitHub release verification."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
from urllib.parse import quote
import zipfile


REPO = "alondero/kirbysdreamcourse-recomp"
REPO_URL = f"https://github.com/{REPO}"
PRIVATE_DIRS = {"local", "generated", "recordings", "saves", "roms", ".git", ".worktrees"}
PRIVATE_SUFFIXES = {".sfc", ".smc", ".fig", ".bin", ".sav", ".srm", ".state",
                    ".exe", ".dll", ".pdb", ".wav", ".pcm", ".log"}
NOTICES = {"LICENSE", "LICENSE.snesrecomp", "LICENSE.SDL3.txt", "THIRD_PARTY_ATTRIBUTION.md"}


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
    """Check a reviewed stage, without claiming to prove linked-code provenance."""
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
    print("Packaging does not certify linked-code provenance or runtime dependencies; review and test before publishing.")


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
    except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

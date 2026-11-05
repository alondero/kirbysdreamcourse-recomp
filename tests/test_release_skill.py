"""ROM-free checks for binary/source packaging and release verification."""

from contextlib import chdir, redirect_stdout
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile


HELPER = Path(__file__).resolve().parents[1] / ".agents/skills/release/scripts/release.py"
SPEC = importlib.util.spec_from_file_location("release_skill", HELPER)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseSkillTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.archive = self.root / "source.zip"
        self.write_zip()
        self.tag = "v0.1.2-experimental"
        self.target = "a" * 40
        self.metadata = {
            "tag_name": self.tag,
            "html_url": f"{release.REPO_URL}/releases/tag/{self.tag}",
            "draft": False,
            "prerelease": True,
            "name": "Experimental source",
            "assets": [{"name": self.archive.name, "state": "uploaded",
                        "size": self.archive.stat().st_size}],
        }

    def write_zip(self, extra=None):
        with zipfile.ZipFile(self.archive, "w") as archive:
            for name in sorted(release.NOTICES | {"README.md"}):
                archive.writestr(f"project/{name}", "Synthetic fixture\n")
            if extra:
                entry = zipfile.ZipInfo()
                entry.filename = extra  # Keep unsafe separators verbatim on Windows too.
                archive.writestr(entry, "Synthetic fixture\n")

    def test_source_archive_requires_notices(self):
        release.inspect_zip(self.archive, source=True)
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr("project/README.md", "source")
        with self.assertRaisesRegex(ValueError, "Missing source notices"):
            release.inspect_zip(self.archive, source=True)

    def test_source_archive_rejects_private_and_generated_files(self):
        for name in ("game.sfc", "GAME.SMC", "local/output.c", "generated/block.c",
                     "src/gen/game.c", "assets/extracted/tile.png", "recomp/funcs.h",
                     "slot.state12", "game.exe", "recordings/input.txt", "save.srm"):
            with self.subTest(name=name):
                self.write_zip(f"project/{name}")
                with self.assertRaisesRegex(ValueError, "Private/generated"):
                    release.inspect_zip(self.archive, source=True)

    def test_zip_rejects_unsafe_paths_and_corrupt_data(self):
        for name in ("../escape", "/absolute", "project/../escape", "C:/escape", "project\\escape"):
            with self.subTest(name=name):
                self.write_zip(name)
                with self.assertRaisesRegex(ValueError, "Unsafe ZIP path"):
                    release.inspect_zip(self.archive)
        self.archive.write_bytes(b"not a ZIP")
        with self.assertRaises(zipfile.BadZipFile):
            release.inspect_zip(self.archive)

    def test_metadata_rejects_wrong_state_binding_title_or_assets(self):
        release.verify_metadata(self.metadata, self.tag, "Experimental source", True, self.archive)
        for field, value in (("draft", True), ("prerelease", False), ("tag_name", "v0.0.0"),
                             ("html_url", f"{release.REPO_URL}/releases/untagged-1"),
                             ("name", self.tag), ("assets", []),
                             ("assets", self.metadata["assets"] * 2)):
            with self.subTest(field=field, value=value):
                metadata = {**self.metadata, field: value}
                with self.assertRaises(ValueError):
                    release.verify_metadata(metadata, self.tag, "Experimental source", True, self.archive)
        for field, value in (("state", "new"), ("size", 0), ("name", "other.zip")):
            metadata = copy.deepcopy(self.metadata)
            metadata["assets"][0][field] = value
            with self.subTest(asset_field=field), self.assertRaises(ValueError):
                release.verify_metadata(metadata, self.tag, "Experimental source", True, self.archive)

    def verify_fixture(self, annotated=False, wrong_commit=False, wrong_bytes=False):
        def fake_run(*args):
            if args[:2] == ("git", "rev-parse"):
                return self.target
            if args[:2] == ("git", "ls-remote"):
                sha = "b" * 40 if wrong_commit else self.target
                if annotated:
                    return f"{'c' * 40}\trefs/tags/{self.tag}\n{sha}\trefs/tags/{self.tag}^{{}}"
                return f"{sha}\trefs/tags/{self.tag}"
            if args[:3] == ("gh", "release", "download"):
                downloaded = Path(args[-1]) / self.archive.name
                shutil.copyfile(self.archive, downloaded)
                if wrong_bytes:
                    with zipfile.ZipFile(downloaded, "a") as archive:
                        archive.writestr("unexpected.txt", "different artifact")
                return ""
            self.fail(f"Unexpected command: {args}")

        args = SimpleNamespace(archive=str(self.archive), target=self.target, version=self.tag,
                               title="Experimental source", prerelease="true")
        with patch.object(release, "run", side_effect=fake_run), \
                patch.object(release, "api", return_value=self.metadata), redirect_stdout(io.StringIO()):
            release.verify(args)

    def test_verify_accepts_lightweight_and_annotated_tags(self):
        self.verify_fixture()
        self.verify_fixture(annotated=True)

    def test_verify_rejects_wrong_tag_commit(self):
        with self.assertRaisesRegex(ValueError, "differs from expected"):
            self.verify_fixture(annotated=True, wrong_commit=True)

    def test_verify_rejects_different_download(self):
        with self.assertRaisesRegex(ValueError, "SHA-256 differs"):
            self.verify_fixture(wrong_bytes=True)

    def test_source_package_uses_commit_not_untracked_files_and_refuses_overwrite(self):
        def git(*args):
            return subprocess.run(("git", "-C", str(self.root), *args), check=True,
                                  capture_output=True, encoding="utf-8").stdout.strip()

        git("init", "--quiet")
        for name in release.NOTICES | {"README.md"}:
            (self.root / name).write_text("Synthetic tracked source\n", encoding="utf-8")
        git("add", ".")
        git("-c", "user.name=Release Test", "-c", "user.email=test@example.invalid",
            "commit", "--quiet", "-m", "Synthetic source")
        target = git("rev-parse", "HEAD")
        (self.root / "private.sfc").write_text("Synthetic untracked input", encoding="utf-8")
        # Run the actual CLI from the fixture repo, exercising git archive and root detection.
        command = (sys.executable, str(HELPER), "source-package", "--target", target, "--version", self.tag)
        result = subprocess.run(command, cwd=self.root, capture_output=True, encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr)
        archive = next((self.root / "local/releases" / self.tag).glob("*.zip"))
        inventory = release.inspect_zip(archive, source=True)
        self.assertFalse(any("private.sfc" in name for name in inventory))
        digest = release.sha256(archive)
        repeated = subprocess.run(command, cwd=self.root, capture_output=True, encoding="utf-8")
        self.assertNotEqual(repeated.returncode, 0)
        self.assertIn("Refusing to overwrite", repeated.stderr)
        self.assertEqual(release.sha256(archive), digest)

    def windows_stage(self):
        stage = self.root / "windows-stage"
        stage.mkdir()
        for name in release.NOTICES | {"README.txt", "assets/launcher.txt", "licenses/ui.txt"}:
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Synthetic fixture\n", encoding="utf-8")
        # Header fixture only, not a runnable executable or a gameplay test.
        header = bytearray(154)
        header[:2] = b"MZ"
        header[0x3C:0x40] = (128).to_bytes(4, "little")
        header[128:134] = b"PE\0\0\x64\x86"
        header[150:152] = (2).to_bytes(2, "little")
        header[152:154] = b"\x0b\x02"
        (stage / "KirbyDreamCourseRecomp.exe").write_bytes(header)
        self.build_info = {"source_commit": self.target, "version": self.tag,
                           "build_command": "synthetic test fixture", "toolchain": "test",
                           "framework_revision": "f" * 40, "ui_revision": "e" * 40}
        (stage / "build-info.json").write_text(json.dumps(self.build_info), encoding="utf-8")
        return stage

    def test_windows_package_contains_exe_resources_notices_and_provenance(self):
        stage = self.windows_stage()
        args = SimpleNamespace(target=self.target, version=self.tag, stage=str(stage))
        def fake_run(*command):
            return str(self.root) if "--show-toplevel" in command else self.target
        with patch.object(release, "run", side_effect=fake_run), redirect_stdout(io.StringIO()):
            release.windows_package(args)
            archive = self.root / "local/releases" / self.tag / "kirbysdreamcourse-recomp-windows-x64.zip"
            names = release.inspect_zip(archive)
            self.assertIn("KirbyDreamCourseRecomp.exe", names)
            self.assertIn("build-info.json", names)
            self.assertIn("assets/launcher.txt", names)
            self.assertIn("licenses/ui.txt", names)
            self.assertTrue(release.NOTICES.issubset(names))
            with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
                release.windows_package(args)

    def test_windows_stage_accepts_a_relative_directory(self):
        self.windows_stage()
        with chdir(self.root):
            files = release.windows_files(Path("windows-stage"), self.target, self.tag)
        self.assertTrue(any(file.name == "KirbyDreamCourseRecomp.exe" for file in files))

    @unittest.skipUnless(os.name == "nt", "Windows short-path aliases only")
    def test_windows_stage_accepts_an_83_alias(self):
        import ctypes
        stage = self.windows_stage()
        buffer = ctypes.create_unicode_buffer(32768)
        count = ctypes.windll.kernel32.GetShortPathNameW(str(stage), buffer, len(buffer))
        if not count or count >= len(buffer):
            self.skipTest("No Windows short-path alias is available")
        short = Path(buffer.value)
        if short == stage.resolve():
            self.skipTest("8.3 aliases are disabled for the test directory")
        files = release.windows_files(short, self.target, self.tag)
        self.assertTrue(any(file.name == "KirbyDreamCourseRecomp.exe" for file in files))

    def test_windows_stage_still_rejects_a_link_inside_the_stage(self):
        stage = self.windows_stage()
        outside = self.root / "outside"
        outside.mkdir()
        try:
            (stage / "assets/external").symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest("Creating a directory symlink is not available")
        with self.assertRaisesRegex(ValueError, "Links are not permitted"):
            release.windows_files(stage, self.target, self.tag)

    def test_windows_stage_rejects_game_data_and_session_settings(self):
        stage = self.windows_stage()
        for name in ("game.sfc", "save.srm", "slot.state1", "config.ini", "other.exe",
                     "generated/compiled.c", "assets/extracted/tile.png"):
            with self.subTest(name=name):
                path = stage / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("Synthetic fixture", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Private/generated|Unreviewed"):
                    release.windows_files(stage, self.target, self.tag)
                path.unlink()

    def test_windows_stage_rejects_missing_exe_and_wrong_architecture(self):
        stage = self.windows_stage()
        exe = stage / "KirbyDreamCourseRecomp.exe"
        content = exe.read_bytes()
        for replacement in (b"not a Windows program", content[:132] + b"\x4c\x01" + content[134:]):
            exe.write_bytes(replacement)
            with self.assertRaisesRegex(ValueError, "x64 PE"):
                release.windows_files(stage, self.target, self.tag)
        exe.unlink()
        with self.assertRaisesRegex(ValueError, "Missing Windows package"):
            release.windows_files(stage, self.target, self.tag)

    def test_windows_stage_rejects_stale_metadata_and_missing_resources(self):
        stage = self.windows_stage()
        info_path = stage / "build-info.json"
        for field, value in (("source_commit", "b" * 40), ("version", "v0.0.0"), ("build_command", "")):
            with self.subTest(field=field):
                info_path.write_text(json.dumps({**self.build_info, field: value}), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "metadata"):
                    release.windows_files(stage, self.target, self.tag)
        info_path.write_text(json.dumps(self.build_info), encoding="utf-8")
        (stage / "assets/launcher.txt").unlink()
        with self.assertRaisesRegex(ValueError, "Missing Windows package"):
            release.windows_files(stage, self.target, self.tag)

    def stage_fixture(self, missing_dll=False):
        build = self.windows_stage()
        (build / "CMakeCache.txt").write_text(
            f"KDC_BUILD_VERSION:STRING={self.tag}\nKDC_RELEASE_DEFAULTS:BOOL=ON\n"
            f"RECOMP_UI_ROOT:PATH={self.root / 'ui'}\nCMAKE_COMMAND:INTERNAL=cmake\n")
        for name in release.NOTICES:
            shutil.copy2(build / name, self.root / name)
        (self.root / "tools").mkdir()
        (self.root / "tools/release-readme.txt").write_text("Synthetic player instructions")
        for name in ("gcc", "mingw-w64"):
            directory = self.root / "toolchain-notices" / name
            directory.mkdir(parents=True)
            (directory / "COPYING").write_text("Synthetic notice")
        if not missing_dll:
            (build / "SDL3.dll").write_bytes(b"synthetic DLL")
            (build / "support.dll").write_bytes(b"synthetic transitive dependency")
        args = SimpleNamespace(target=self.target, version=self.tag, build=str(build),
                               stage=str(self.root / "stage"), objdump=str(self.root / "bin/objdump.exe"),
                               toolchain_notices=str(self.root / "toolchain-notices"))
        def fake_run(*command):
            if "--show-toplevel" in command:
                return str(self.root)
            if "-p" in command:
                name = Path(command[-1]).name
                dependencies = {"KirbyDreamCourseRecomp.exe": ["KERNEL32.dll", "SDL3.dll"],
                                "SDL3.dll": ["support.dll"], "support.dll": ["KERNEL32.dll"]}
                return "\n".join(f"DLL Name: {item}" for item in dependencies[name])
            if "--version" in command:
                return "Synthetic tool 1.0"
            return self.target
        with patch.object(release, "run", side_effect=fake_run), redirect_stdout(io.StringIO()):
            release.stage_windows(args)
        return self.root / "stage"

    def test_staging_collects_transitive_dlls_and_toolchain_notices(self):
        stage = self.stage_fixture()
        self.assertTrue((stage / "SDL3.dll").is_file())
        self.assertTrue((stage / "support.dll").is_file())
        self.assertTrue((stage / "licenses/toolchain/gcc/COPYING").is_file())
        info = json.loads((stage / "build-info.json").read_text())
        self.assertEqual(info["dependencies"]["SDL3.dll"], ["support.dll"])
        self.assertEqual(info["source_commit"], self.target)

    def test_staging_fails_when_a_required_runtime_dll_is_missing(self):
        with self.assertRaisesRegex(ValueError, "Required runtime DLL not found: SDL3.dll"):
            self.stage_fixture(missing_dll=True)


class WorkflowPublishTests(unittest.TestCase):
    """The publish workflow's PowerShell must splat release flags as an array."""

    WORKFLOW = Path(__file__).resolve().parents[1] / ".github/workflows/build-release.yml"
    SKILL = Path(__file__).resolve().parents[1] / ".agents/skills/release/SKILL.md"

    def release_script(self):
        """Return the release job's PowerShell lines from the publish workflow."""
        lines = self.WORKFLOW.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            if "gh release create" not in line:
                continue
            start = index
            while start >= 0 and not lines[start].strip().startswith("run:"):
                start -= 1
            block = lines[start + 1: index + 1]
            indent = min(len(item) - len(item.lstrip()) for item in block if item.strip())
            return [item[indent:] for item in block]
        self.fail("The publish workflow no longer creates a release")

    def powershell(self):
        for name in ("pwsh", "powershell"):
            found = shutil.which(name)
            if found:
                return found
        self.skipTest("No PowerShell executable is available")

    def test_release_flags_splat_as_an_array(self):
        # A single-element array assigned from an if-expression is unrolled into
        # a string by the pipeline; splatting that string passes one argument per
        # character and fails the publish step. Run the workflow's own flag
        # selection and check the arguments gh would receive.
        script = self.release_script()
        create = next(index for index, line in enumerate(script) if "gh release create" in line)
        prefix = "\n".join(script[:create])
        probe = prefix + "\nfunction Show-Args { Write-Output ($args -join '|') }\nShow-Args @flags\n"
        shell = self.powershell()
        for prerelease, expected in (("true", "--prerelease|--latest=false"), ("false", "--latest")):
            with self.subTest(prerelease=prerelease), tempfile.TemporaryDirectory() as temporary:
                (Path(temporary) / "dist").mkdir()
                environment = {**os.environ, "PRERELEASE": prerelease,
                               "RELEASE_TAG": "v0.1.0", "SOURCE_SHA": "a" * 40,
                               "LINUX_VERSION": "v0.1.0", "LINUX_SOURCE_SHA": "a" * 40}
                result = subprocess.run((shell, "-NoProfile", "-Command", probe), cwd=temporary,
                                        env=environment, capture_output=True, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)

    def test_publish_title_matches_the_documented_title(self):
        script = "\n".join(self.release_script())
        self.assertIn("Kirby's Dream Course Recompiled $env:RELEASE_TAG (Windows & Linux x64)", script)
        skill = self.SKILL.read_text(encoding="utf-8")
        self.assertIn("Kirby's Dream Course Recompiled <version> (Windows & Linux x64)", skill)


class LinuxReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.stage = Path(self.temporary.name)
        self.target = "a" * 40
        self.tag = "v0.1.2"
        for name in release.NOTICES | {"start.sh", "README.txt"}:
            (self.stage / name).write_text("Synthetic fixture\n", encoding="utf-8")
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        header[16:20] = b"\x03\0\x3e\0"
        (self.stage / "KirbyDreamCourseRecomp").write_bytes(header)
        for name in ("assets/font.ttf", "licenses/notice.txt"):
            path = self.stage / name
            path.parent.mkdir(exist_ok=True)
            path.write_text("Synthetic fixture\n", encoding="utf-8")
        self.info = {"source_commit": self.target, "version": self.tag,
                     "build_command": "python3 tools/build_release.py", "toolchain": "gcc",
                     "framework_revision": "b" * 40, "ui_revision": "c" * 40, "required_glibc": "2.39"}
        self.metadata = self.stage / "build-info.json"
        self.metadata.write_text(json.dumps(self.info), encoding="utf-8")

    def test_linux_stage_requires_elf_notices_and_matching_provenance(self):
        release.linux_files(self.stage, self.target, self.tag)
        for field in ("source_commit", "version", "required_glibc", "framework_revision"):
            with self.subTest(field=field):
                self.metadata.write_text(json.dumps({**self.info, field: ""}), encoding="utf-8")
                with self.assertRaises(ValueError):
                    release.linux_files(self.stage, self.target, self.tag)
        self.metadata.write_text(json.dumps(self.info), encoding="utf-8")
        binary = self.stage / "KirbyDreamCourseRecomp"
        original = binary.read_bytes()
        for offset, replacement in ((0, b"MZ"), (4, b"\x01"), (18, b"\xb7\0")):
            with self.subTest(offset=offset):
                damaged = bytearray(original)
                damaged[offset:offset + len(replacement)] = replacement
                binary.write_bytes(damaged)
                with self.assertRaisesRegex(ValueError, "x86-64 ELF"):
                    release.linux_files(self.stage, self.target, self.tag)
        binary.write_bytes(original)
        (self.stage / "assets/font.ttf").unlink()
        with self.assertRaisesRegex(ValueError, "Missing Linux"):
            release.linux_files(self.stage, self.target, self.tag)

    def test_linux_stage_rejects_private_content_source_and_session_settings(self):
        for name in ("game.sfc", "config.ini", "save.state2", "rom.cfg", "assets/extracted/tile.dat",
                     "assets/game.c", "assets/library.so", "licenses/game.srm"):
            with self.subTest(name=name):
                path = self.stage / name
                path.parent.mkdir(exist_ok=True, parents=True)
                path.write_text("Private synthetic fixture", encoding="utf-8")
                try:
                    with self.assertRaisesRegex(ValueError, "Unreviewed/private"):
                        release.linux_files(self.stage, self.target, self.tag)
                finally:
                    path.unlink()

    def test_two_platform_release_requires_both_exact_assets(self):
        windows = self.stage / "windows.zip"
        linux = self.stage / "linux.zip"
        windows.write_bytes(b"windows")
        linux.write_bytes(b"linux")
        metadata = {"tag_name": self.tag, "html_url": f"{release.REPO_URL}/releases/tag/{self.tag}",
                    "draft": False, "prerelease": True, "name": "Both platforms",
                    "assets": [{"name": p.name, "size": p.stat().st_size, "state": "uploaded"}
                               for p in (windows, linux)]}
        release.verify_metadata(metadata, self.tag, "Both platforms", True, windows, [linux])
        for assets in (metadata["assets"][:1], metadata["assets"] * 2):
            with self.subTest(assets=assets), self.assertRaises(ValueError):
                release.verify_metadata({**metadata, "assets": assets}, self.tag,
                                        "Both platforms", True, windows, [linux])


if __name__ == "__main__":
    unittest.main()

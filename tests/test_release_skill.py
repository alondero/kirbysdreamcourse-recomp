"""ROM-free checks for source packaging and published-release verification."""

from contextlib import redirect_stdout
import copy
import importlib.util
import io
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


if __name__ == "__main__":
    unittest.main()

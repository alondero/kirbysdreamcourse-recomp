"""The public Android payload is limited to the game library, UI assets and notices."""
from pathlib import Path
import tempfile
import unittest
import zipfile

from tools.build_android import ASSETS, ICON_RESOURCES, verify_apk


class AndroidPackageTests(unittest.TestCase):
    def write_apk(self, path, extra=(), machine=183):
        header = b"\x7fELF\x02\x01" + bytes(10) + b"\x03\x00" + machine.to_bytes(2, "little")
        with zipfile.ZipFile(path, "w") as apk:
            for name in ("AndroidManifest.xml", "resources.arsc", "classes.dex",
                         "META-INF/version-control-info.textproto", *ICON_RESOURCES, *extra):
                apk.writestr(name, b"fixture")
            for name in ASSETS:
                apk.writestr("assets/" + name, b"fixture")
            apk.writestr("lib/arm64-v8a/libmain.so", header)

    def test_release_metadata_and_reviewed_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "test.apk"
            self.write_apk(path)
            verify_apk(path)
            for private in ("assets/course.sfc", "assets/assets/img/bank00.c", "assets/saves/save.srm"):
                with self.subTest(private=private):
                    self.write_apk(path, [private])
                    with self.assertRaisesRegex(ValueError, "Unexpected APK contents"):
                        verify_apk(path)
            self.write_apk(path, machine=62)
            with self.assertRaisesRegex(ValueError, "ARM64"):
                verify_apk(path)

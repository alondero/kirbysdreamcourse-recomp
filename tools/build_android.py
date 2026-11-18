"""Generate the guarded arm64 game, assemble, verify and stage its APK."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NDK = "28.2.13676358"
TOOLS = "35.0.0"
APK = "kirbysdreamcourse-recomp-android-arm64.apk"
RELEASE_CERT_SHA256 = "17778ffb0a101469d202c21639516ed3987d122904bfa90807b41b81cbad4a89"
ASSETS = {
    "assets/fonts/LatoLatin-Regular.ttf", "assets/fonts/LatoLatin-Bold.ttf",
    "assets/fonts/NotoSansSymbols2-Regular.ttf", "assets/fonts/OpenMoji-black-glyf.ttf",
    "assets/img/brand_mark.tga", "assets/img/flags.png", "assets/img/pad.tga",
    *(f"assets/img/verdict_{name}.tga" for name in ("ok", "warn", "bad", "none")),
    "licenses/LICENSE", "licenses/LICENSE.snesrecomp", "licenses/LICENSE.SDL3.txt",
    "licenses/NDK.txt", "licenses/NDK-toolchain.txt", "licenses/THIRD_PARTY_ATTRIBUTION.md",
    *(f"licenses/recomp-ui/{name}" for name in ("LICENSE.txt", "ImGui.txt", "fonts.md",
      "images.md", "Lato-OFL.txt", "Noto-OFL.txt", "OpenMoji-CC-BY-SA-4.0.txt")),
}


def verify_apk(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        required = {"assets/" + name for name in ASSETS}
        required.update(("AndroidManifest.xml", "resources.arsc", "classes.dex", "lib/arm64-v8a/libmain.so"))
        unexpected = {n for n in names if n not in required and not re.fullmatch(
            r"classes[0-9]+\.dex|META-INF/com/android/build/gradle/app-metadata\.properties|"
            r"META-INF/version-control-info\.textproto|META-INF/(?:MANIFEST\.MF|[A-Za-z0-9_-]+\.(?:SF|RSA|EC|DSA))", n)}
        if unexpected or required.difference(names) or len(names) != len(set(names)) or archive.testzip():
            raise ValueError(f"Unexpected APK contents: extra={unexpected}, missing={required.difference(names)}")
        with archive.open("lib/arm64-v8a/libmain.so") as library:
            header = library.read(20)
        # ELF64, little-endian, ET_DYN, EM_AARCH64
        if header[:6] != b"\x7fELF\x02\x01" or header[16:20] != b"\x03\x00\xb7\x00":
            raise ValueError("Android game library must be an ARM64 ELF shared object")


def verify_signature(signer, apk, release):
    output = subprocess.check_output([signer, "verify", "--verbose", "--print-certs", apk], text=True)
    if release:
        fingerprints = re.findall(r"^Signer #\d+ certificate SHA-256 digest: ([a-fA-F0-9]{64})$", output, re.MULTILINE)
        if [value.lower() for value in fingerprints] != [RELEASE_CERT_SHA256]:
            raise ValueError("APK signer differs from the Kirby Android release certificate")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True)
    parser.add_argument("--version", default="v0.0.0-dev")
    parser.add_argument("--release", action="store_true", help="Sign with the configured release key")
    parser.add_argument("--sdk", default=os.getenv("ANDROID_HOME") or os.getenv("ANDROID_SDK_ROOT"))
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", args.version):
        parser.error("Expected vMAJOR.MINOR.PATCH[-suffix]")
    if not args.sdk:
        parser.error("Set ANDROID_HOME or pass --sdk")
    sdk = Path(args.sdk).resolve(strict=True)
    if args.release:
        for key in ("ANDROID_KEYSTORE_PATH", "ANDROID_KEYSTORE_PASSWORD", "ANDROID_KEY_ALIAS", "ANDROID_KEY_PASSWORD"):
            if not os.getenv(key):
                parser.error(f"Release signing requires {key}")
    mode = "release" if args.release else "debug"
    subprocess.run([sys.executable, ROOT / "tools/setup.py", "--rom", Path(args.rom).resolve(),
                    "--version", args.version, "--name", "android", "--release-defaults",
                    "--android-ndk", sdk / "ndk" / NDK, "--cmake", args.cmake], check=True)
    wrapper = ROOT / "android" / ("gradlew.bat" if os.name == "nt" else "gradlew")
    launcher = [wrapper] if os.name == "nt" else ["sh", wrapper]
    subprocess.run([*launcher, "-p", ROOT / "android", "assemble" + mode.title()],
                   env=dict(os.environ, ANDROID_HOME=str(sdk), KDC_VERSION=args.version), check=True)
    apk = ROOT / f"android/app/build/outputs/apk/{mode}/app-{mode}.apk"
    verify_apk(apk)
    signer = sdk / "build-tools" / TOOLS / ("apksigner.bat" if os.name == "nt" else "apksigner")
    verify_signature(signer, apk, args.release)
    out = ROOT / "local/releases" / args.version / APK
    out.parent.mkdir(exist_ok=True, parents=True)
    shutil.copyfile(apk, out)
    with out.open("rb") as stream:
        print(f"{out}\nSHA-256 {hashlib.file_digest(stream, 'sha256').hexdigest()}")


if __name__ == "__main__":
    main()

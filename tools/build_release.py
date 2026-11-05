"""Generate, build and package the guarded Linux x86-64 release."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--rom", default="Kirby's Dream Course (USA).sfc")
    args = parser.parse_args()
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", args.version):
        parser.error("Expected vMAJOR.MINOR.PATCH with optional prerelease suffix")
    os.chdir(root)
    target = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], text=True).strip():
        parser.exit(1, "Release builds require a clean source checkout.\n")
    subprocess.run([sys.executable, "tools/setup.py", "--rom", args.rom,
                    "--version", args.version, "--release-defaults"], check=True)
    subprocess.run([sys.executable, ".agents/skills/release/scripts/release.py", "package-linux",
                    "--target", target, "--version", args.version,
                    "--build", "local/guarded-build", "--stage", "local/release-stage-linux"], check=True)


if __name__ == "__main__":
    main()

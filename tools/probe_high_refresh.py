"""Build/run the ROM-free orbit presentation probe against a pinned framework."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--framework', type=Path,
                        default=Path(os.environ.get('SNESRECOMP_ROOT', root / 'local/guarded-framework')))
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    pin = subprocess.check_output(['git', '-C', str(args.framework), 'rev-parse', 'HEAD'],
                                  text=True).strip()
    if pin != '147b55e252dcb3e7680a99c7c2f50e7183891844':
        parser.error('framework must be at the project pin (local setup patches are allowed)')
    source = args.framework / 'runner/src'
    with tempfile.TemporaryDirectory(prefix='kdc-presentation-') as directory:
        executable = Path(directory) / 'probe.exe'
        subprocess.run([args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(root / 'src'), '-I', str(source),
                        str(root / 'tools/high_refresh_probe.c'),
                        str(root / 'src/course_orbit.c'),
                        str(root / 'src/course_sprite_view.c'),
                        str(root / 'src/course_camera.c'),
                        str(root / 'src/course_geometry.c'),
                        str(root / 'src/course_checker.c'),
                        str(root / 'src/course_hud.c'),
                        str(source / 'desktop/host_clock.c'), '-lm', '-o', str(executable)],
                       check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()

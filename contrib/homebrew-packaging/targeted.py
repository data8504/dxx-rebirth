"""Comparison-only SDL repair; intentionally not a second SCons integration."""
import os
from pathlib import Path
import shutil
import subprocess


def package(executable, libs):
    def output(*args):
        return subprocess.check_output(args, text=True).strip()
    version = output('pkg-config', '--modversion', 'sdl2')
    seeds = []
    if int(version.split('.')[2]) >= 50:
        source = os.environ.get('DXX_SDL3_LIBRARY')
        if source is None:
            source = str(Path(output('brew', '--prefix', 'sdl3')) /
                         'lib/libSDL3.dylib')
        source = Path(source).resolve(strict=True)
        libs.mkdir(parents=True, exist_ok=True)
        seed = libs / source.name
        shutil.copy2(source, seed)
        seed.chmod(seed.stat().st_mode | 0o200)
        seeds = [seed]
        alias = libs / 'libSDL3.dylib'
        if alias != seed:
            alias.symlink_to(seed.name)
    command = ['dylibbundler', '-of', '-cd', '-b', '-x', str(executable),
               '-d', str(libs)]
    for seed in seeds:
        command.extend(['-x', str(seed)])
    log = subprocess.check_output(command, text=True)
    print(log)
    return log, ({source.name: str(source)} if seeds else {})

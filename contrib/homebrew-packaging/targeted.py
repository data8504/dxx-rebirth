"""Evaluation adapter for the same primitives used by the production SCons action."""
import importlib.util
from pathlib import Path
import shutil
import subprocess

spec = importlib.util.spec_from_file_location('sdl2_compat',
    Path(__file__).resolve().parents[2] / 'common/arch/cocoa/sdl2_compat.py')
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)


def package(executable, libs):
    runtime, metadata, expected = repair.discover([shutil.which('pkg-config')])
    assert runtime is not None, 'native comparison requires selected sdl2-compat'
    repair.verify_selected(executable, expected)
    log = subprocess.check_output(['dylibbundler', '-od', '-b', '-x', str(executable),
                                   '-d', str(libs)], text=True)
    seed = libs / 'libSDL3.dylib'
    shutil.copy2(runtime, seed)
    log += subprocess.check_output(['dylibbundler', '-of', '-b', '-x', str(seed),
                                    '-d', str(libs)], text=True)
    print('Selected SDL2 metadata:', metadata[0], metadata[0].read_text())
    print('Selected SDL3 runtime:', runtime)
    print(log)
    return log, {seed.name: str(runtime)}

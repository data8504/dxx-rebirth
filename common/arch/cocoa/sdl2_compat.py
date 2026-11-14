"""Find the runtime library required by the selected sdl2-compat pkg-config file."""
from pathlib import Path
import subprocess


def discover(command, override=None):
    def variable(module, name):
        value = subprocess.check_output([*command, '--variable=' + name, module],
                                        text=True).strip()
        if not value:
            raise ValueError(f'pkg-config {module} has no {name}')
        return value
    if override:
        # An explicit path also supports builds without pkg-config metadata.
        return Path(override).resolve(strict=True), [], None
    if not command:
        return None, [], None
    try:
        pc = Path(variable('sdl2', 'pcfiledir')) / 'sdl2.pc'
        fields = dict(line.split(':', 1) for line in pc.read_text().splitlines()
                      if ':' in line and not line.lstrip().startswith('#'))
    except (OSError, ValueError, subprocess.CalledProcessError):
        return None, [], None  # Preserve manual/fallback SDL2 configurations.
    if fields.get('Name', '').strip() != 'sdl2_compat':
        return None, [pc], None
    sdl2 = (Path(variable('sdl2', 'libdir')) / 'libSDL2.dylib').resolve(strict=True)
    try:
        sdl3 = (Path(variable('sdl3', 'libdir')) / 'libSDL3.dylib').resolve(strict=True)
        pc3 = Path(variable('sdl3', 'pcfiledir')) / 'sdl3.pc'
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise ValueError('selected SDL2 is sdl2-compat; install SDL3 pkg-config '
                         'metadata or set macos_sdl3_library=/path/to/libSDL3.dylib') from error
    return sdl3, [pc, pc3], sdl2


def verify_selected(executable, expected):
    if expected is None:
        return
    expected = Path(expected)
    lines = subprocess.check_output(['otool', '-L', str(executable)], text=True)
    paths = [line.strip().split(' (compatibility version', 1)[0]
             for line in lines.splitlines()[1:]]
    if not any(p.startswith('/') and Path(p).resolve() == expected for p in paths):
        raise ValueError(f'SDL2 selected by pkg-config ({expected}) does not match '
                         'the linked executable; use consistent SDL2 metadata/link flags')

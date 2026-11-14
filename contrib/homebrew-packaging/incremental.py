"""Exercise the actual opt-in SCons path; no installed keg mutations."""
import json
from pathlib import Path
import subprocess
import time

args = ['scons', '-j4', 'opengl=1', 'sdl2=1', 'macos_add_frameworks=0',
        'macos_bundle_libs=1', 'macos_homebrew_runtime_deps=1',
        'build/D1X-Rebirth.app', 'build/D2X-Rebirth.app']
stamps = [Path(f'build/D{n}X-Rebirth.app/Contents/libs/libSDL3.dylib') for n in (1, 2)]


def build():
    start = time.monotonic()
    subprocess.run(args, check=True)
    return time.monotonic() - start


result = {'initial_seconds': build()}
before = {str(p): p.stat().st_mtime_ns for p in stamps}
result['noop_seconds'] = build()
after = {str(p): p.stat().st_mtime_ns for p in stamps}
result['noop_repackages'] = before != after
# This bounded prototype deliberately repackages instead of caching discovery.
assert before != after, 'AlwaysBuild should re-evaluate installed dependencies'
missing = Path('build/D1X-Rebirth.app/Contents/libs/libSDL3.dylib')
assert missing.exists()
missing.unlink()
result['repair_seconds'] = build()
result['missing_alias_restored'] = missing.exists()
assert missing.exists()
Path('packaging-evidence/incremental.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))

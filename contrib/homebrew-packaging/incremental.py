"""Verify real SCons targeted output, incremental behavior, and isolated startup."""
import json
import os
from pathlib import Path
import subprocess
import time

args = ['scons', '-j4', 'opengl=1', 'sdl2=1', 'macos_add_frameworks=0',
        'macos_bundle_libs=1', 'build/D1X-Rebirth.app', 'build/D2X-Rebirth.app']
libs = [Path(f'build/D{n}X-Rebirth.app/Contents/libs/libSDL3.dylib') for n in (1, 2)]


def build():
    start = time.monotonic()
    result = subprocess.run(args, check=True, capture_output=True, text=True)
    print(result.stdout, result.stderr)
    return dict(seconds=time.monotonic()-start,
                repackaged=result.stdout.count('dylibbundler -od'))


result = {'initial': build()}
assert all(p.exists() for p in libs), 'automatic metadata detection must add SDL3'
result['noop'] = build()
assert result['noop']['repackaged'] == 0, result['noop']
libs[0].unlink()
result['missing_output'] = build()
assert libs[0].exists()
assert result['missing_output']['repackaged'] == 1, result['missing_output']
Path('build/D1X-Rebirth.app/Contents/MacOS/d1x-rebirth').unlink()
result['missing_executable'] = build()
assert result['missing_executable']['repackaged'] == 1
result['recovered_noop'] = build()
assert result['recovered_noop']['repackaged'] == 0
# A changed source library must trigger repackaging through ordinary SCons deps.
# Use a private source copy via the explicit override, never mutate a keg.
source = Path('packaging-evidence/override-SDL3.dylib').resolve()
import shutil
source.write_bytes(libs[0].read_bytes())
args.insert(-2, 'macos_sdl3_library=' + str(source))
result['override'] = build()
result['override_noop'] = build()
assert result['override_noop']['repackaged'] == 0
with source.open('ab') as stream:
    stream.write(b'\0' * 16)
result['changed_override'] = build()
assert result['changed_override']['repackaged'] == 2
args.remove('macos_sdl3_library=' + str(source))
result['restore_automatic'] = build()
# Verify the actual SCons-generated applications, not only the comparison adapter.
apps = [Path(f'build/D{n}X-Rebirth.app').resolve() for n in (1, 2)]
for app in apps:
    for library in (app/'Contents/libs').glob('*.dylib'):
        subprocess.run(['codesign', '--force', '--sign', '-', str(library)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
prefix = Path(subprocess.check_output(['brew','--prefix'], text=True).strip())
hidden = prefix.with_name(prefix.name + '-dxx-hidden')
assert not hidden.exists()
subprocess.run(['/usr/bin/sudo','/bin/mv',str(prefix),str(hidden)],check=True)
try:
    assert not prefix.exists()
    for n, app in enumerate(apps, 1):
        env = {k:v for k,v in os.environ.items() if not k.startswith(('DYLD_', 'SDL_'))}
        env['DYLD_PRINT_LIBRARIES'] = '1'
        completed = subprocess.run([str(app/f'Contents/MacOS/d{n}x-rebirth'),'-help'],
                                  env=env,cwd='/tmp',capture_output=True,text=True,timeout=30)
        log = completed.stdout + completed.stderr
        Path(f'packaging-evidence/d{n}x-production-startup.log').write_text(log)
        assert completed.returncode == 0
        loaded = [s for s in log.splitlines() if s.startswith('dyld[')]
        assert any('libSDL3.dylib' in s and str(app) in s for s in loaded)
        assert not any(str(prefix) in s or str(hidden) in s for s in loaded)
        result[f'd{n}x_startup'] = dict(exit_code=completed.returncode,bundled_sdl3=True,
            signature='ad-hoc verified',scope='-help only; no gameplay/audio/rendering/input')
finally:
    subprocess.run(['/usr/bin/sudo','/bin/mv',str(hidden),str(prefix)],check=True)
Path('packaging-evidence/incremental.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))

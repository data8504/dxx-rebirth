"""Run only on an expendable macOS development runner, after building both apps.

Never uploads apps or changes release artifacts. Temporarily hides the entire
Homebrew prefix during startup checks, restoring it in a finally block.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import re
import targeted

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'packaging-evidence'
OUT.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location(
    'homebrew_bundle', ROOT / 'contrib/homebrew-packaging/generic_reference.py')
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


def command(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def output(*args):
    return bundle.output(*args)


def sign(app):
    for path in (app / 'Contents/libs').glob('*.dylib'):
        if not path.is_symlink():
            command(['codesign', '--force', '--sign', '-', path])
    exe = next((app / 'Contents/MacOS').iterdir())
    command(['codesign', '--force', '--sign', '-', exe])
    command(['codesign', '--force', '--sign', '-', app])
    command(['codesign', '--verify', '--deep', '--strict', '--verbose=2', app])


def measure(app):
    files = [p for p in app.rglob('*') if p.is_file()]
    unique = [p for p in files if not p.is_symlink()]
    # Consistent compression measurement, without symlink dereference.
    archive = OUT / (app.parent.name + '-' + app.name + '.zip')
    command(['/usr/bin/ditto', '-c', '-k', '--keepParent', app, archive])
    result = dict(files=len(files), unique_files=len(unique),
                  uncompressed_bytes=sum(p.stat().st_size for p in unique),
                  compressed_bytes=archive.stat().st_size,
                  libraries=[p.name for p in (app / 'Contents/libs').iterdir()
                             if p.is_file() and not p.is_symlink()])
    result['architectures'] = {p.name: output('lipo', '-archs', p)
                              for p in (app / 'Contents/libs').glob('*.dylib')
                              if not p.is_symlink()}
    assert all('arm64' in a.split() for a in result['architectures'].values())
    archive.unlink()  # Measurement only; don't publish experimental game apps.
    return result


info = json.loads(output('brew', 'info', '--json=v2', '--installed', '--formula'))
(OUT / 'installed.json').write_text(json.dumps(info, indent=2))
(OUT / 'brew-version.txt').write_text(output('brew', '--version'))
(OUT / 'machine.txt').write_text(output('sw_vers') + '\n' + output('uname', '-a'))
prefix = Path(output('brew', '--prefix'))
cellar = Path(output('brew', '--cellar')).resolve()

def owner(path):
    try:
        return tuple(Path(path).resolve().relative_to(cellar).parts[:2])
    except ValueError:
        return None

def provenance(log):
    return {name: str(Path(prefix) / name)
            for name, prefix in re.findall(r'^ \* (.+) from (.+)$', log, re.M)}

results = {}
for game in ('d1x', 'd2x'):
    name = game.upper().replace('X', 'X') + '-Rebirth.app'
    raw = ROOT / f'build/{game}-rebirth/{game}-rebirth'
    template = ROOT / 'build' / name
    apps = {}
    for mode in ('original', 'generic', 'targeted'):
        app = OUT / mode / name
        app.parent.mkdir(exist_ok=True)
        shutil.copytree(template, app, symlinks=True)
        exe = app / 'Contents/MacOS' / raw.name
        # Every comparison starts from the same pristine executable/resources,
        # even when the initial build already produced the repaired app.
        shutil.rmtree(app / 'Contents/libs', ignore_errors=True)
        shutil.copy2(raw, exe)
        started = time.monotonic()
        discovery = None
        explicit_sources = {}
        if mode == 'original':
            log = output('dylibbundler', '-od', '-b', '-x', exe,
                         '-d', app / 'Contents/libs')
            print(log)
        elif mode == 'generic':
            try:
                discovery = bundle.bundle(exe, app / 'Contents/libs')
            except (ValueError, OSError, subprocess.CalledProcessError) as error:
                apps[mode] = (None, dict(error=str(error),
                    packaging_seconds_until_failure=time.monotonic()-started))
                (OUT / f'{game}-generic-failure.txt').write_text(str(error))
                print('Generic prototype rejected:', error)
                continue
        else:
            log, explicit_sources = targeted.package(exe, app / 'Contents/libs')
        if discovery is None:
            discovery = dict(sources=provenance(log), substitutions=[])
            discovery['sources'].update(explicit_sources)
            discovery['packages'] = sorted({owner(p) for p in discovery['sources'].values()
                                             if owner(p)})
        (OUT / f'{game}-{mode}-discovery.json').write_text(json.dumps(discovery, indent=2))
        elapsed = time.monotonic() - started
        if mode == 'generic':
            # Evaluation inventory only: package edges do not identify which
            # of these other runtime-looking payloads are deployable or needed.
            omitted = {}
            selected = {Path(p).resolve() for p in discovery['sources'].values()}
            for package_name, version in discovery['packages']:
                keg = cellar / package_name / version
                omitted[package_name] = [str(p.relative_to(keg)) for p in keg.rglob('*')
                    if p.is_file() and p.resolve() not in selected
                    and p.relative_to(keg).parts[0] in ('bin', 'lib', 'libexec', 'share')]
            (OUT / f'{game}-omitted-package-payload.json').write_text(json.dumps(omitted, indent=2))
        sign(app)
        result = measure(app)
        result['packaging_seconds'] = elapsed
        result['signature'] = 'ad-hoc verification passed'
        result['packages'] = discovery['packages']
        result['substitutions'] = discovery['substitutions']
        apps[mode] = (app, result)
    # Store original Mach-O load commands separately from metadata evidence.
    compat = Path(output('brew', '--prefix', 'sdl2-compat')) / 'lib/libSDL2.dylib'
    (OUT / 'sdl2-compat-load-commands.txt').write_text(output('otool', '-l', compat))
    results[game] = {mode: result for mode, (_, result) in apps.items()}
    hidden = prefix.with_name(prefix.name + '-dxx-hidden')
    if hidden.exists():
        raise RuntimeError(f'refuse to overwrite {hidden}')
    command(['/usr/bin/sudo', '/bin/mv', prefix, hidden])
    try:
        assert not prefix.exists(), 'Homebrew prefix must be inaccessible'
        for mode, (app, result) in apps.items():
            if app is None:
                continue
            exe = app / 'Contents/MacOS' / raw.name
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(('DYLD_', 'SDL_'))}
            env['DYLD_PRINT_LIBRARIES'] = '1'
            try:
                completed = subprocess.run([str(exe), '-help'], env=env,
                                           capture_output=True, text=True, timeout=30,
                                           cwd='/tmp')
                log = completed.stdout + completed.stderr
                exit_code = completed.returncode
            except subprocess.TimeoutExpired as error:
                # sdl2-compat can show a modal error dialog before aborting.
                # subprocess.run kills and waits on timeout; retain its output.
                log = (error.stdout or b'') + (error.stderr or b'')
                if isinstance(log, bytes):
                    log = log.decode(errors='replace')
                exit_code = 'timeout'
            (OUT / f'{game}-{mode}-startup.log').write_text(log)
            loaded = [line for line in log.splitlines()
                      if line.startswith('dyld[')]
            result['startup'] = dict(exit_code=exit_code,
                bundled_sdl3=any('libSDL3' in s and str(app) in s for s in loaded),
                homebrew_loaded=any(str(prefix) in s or str(hidden) in s
                                    for s in loaded),
                loaded_images=loaded,
                scope='-help startup only; no gameplay/audio/render/input check')
    finally:
        command(['/usr/bin/sudo', '/bin/mv', hidden, prefix])
    (OUT / 'results.json').write_text(json.dumps(results, indent=2))
    for mode in ('generic', 'targeted'):
        if 'error' in results[game][mode]:
            continue
        state = results[game][mode]['startup']
        assert state['exit_code'] == 0 and state['bundled_sdl3'], state
        assert not state['homebrew_loaded'], state
    assert results[game]['original']['startup']['exit_code'] != 0
    assert not any('libSDL3' in p for p in results[game]['original']['libraries'])

print(json.dumps(results, indent=2))

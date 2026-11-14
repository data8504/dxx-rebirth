"""Experimental runtime discovery: absolute Mach-O paths and installed Homebrew kegs.

Only top-level lib/*.dylib files and aliases are selected. No framework, plugin,
helper, data, or @rpath discovery. Existing dylibbundler handles relocation.
"""
import json
from pathlib import Path
import shutil
import subprocess


def output(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def discover(executable):
    cellar = Path(output('brew', '--cellar')).resolve()
    info = json.loads(output('brew', 'info', '--json=v2', '--installed', '--formula'))
    installed = {(f['name'], i['version']): i for f in info['formulae'] for i in f['installed']}
    pending, seen, packages, names, substitutions = [Path(executable)], set(), set(), {}, []
    def enqueue(path):
        real = path.resolve(strict=True)
        for name in (path.name, real.name):
            if name in names and names[name] != real:
                raise ValueError(f'flat dylib filename collision: {name}')
            names[name] = real
        pending.append(real)
    while pending:
        path = pending.pop().resolve(strict=True)
        if path in seen:
            continue
        seen.add(path)
        for line in output('otool', '-L', path).splitlines()[1:]:
            dependency = line.strip().split(' (compatibility version', 1)[0]
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if not dependency.startswith('/'):
                # A dylib's own LC_ID_DYLIB is also printed by otool -L.
                if dependency.startswith('@') and Path(dependency).name == path.name:
                    continue
                raise ValueError(f'absolute-path discovery required: {path}: {dependency}')
            enqueue(Path(dependency))
        try:
            relative = path.relative_to(cellar)
        except ValueError:
            continue  # Non-Homebrew libraries retain linked-dependency processing.
        key = tuple(relative.parts[:2])
        todo = [key]
        while todo:
            key = todo.pop()
            if key in packages:
                continue
            if key not in installed:
                raise ValueError(f'no installed receipt metadata for keg {key}')
            packages.add(key)
            deps = installed[key].get('runtime_dependencies')
            if not isinstance(deps, list):
                raise ValueError(f'missing installed runtime dependencies for {key}')
            for dep in deps:
                wanted = (dep['full_name'].split('/')[-1], dep['pkg_version'])
                selected = wanted
                if wanted not in installed:
                    active = cellar.parent / 'opt' / wanted[0]
                    selected = tuple(active.resolve(strict=True).relative_to(cellar).parts)
                    if selected not in installed or selected[0] != wanted[0]:
                        raise ValueError(f'no matching active keg for {wanted}')
                    substitutions.append([list(key), list(wanted), list(selected)])
                todo.append(selected)
            for library in sorted(cellar.joinpath(*key, 'lib').glob('*.dylib')):
                enqueue(library)
    return names, sorted(packages), substitutions


def bundle(executable, destination):
    names, packages, substitutions = discover(executable)
    destination = Path(destination)
    if destination.exists():
        shutil.rmtree(destination)  # Same overwrite behavior as existing -od.
    destination.mkdir(parents=True)
    sources = sorted(set(names.values()))
    for source in sources:
        shutil.copy2(source, destination / source.name)
    for name, source in names.items():
        if name != source.name:
            (destination / name).symlink_to(source.name)
    command = ['dylibbundler', '-of', '-cd', '-b', '-x', str(executable), '-d', str(destination)]
    for source in sources:
        command.extend(['-x', str(destination / source.name)])
    subprocess.run(command, check=True)
    return dict(packages=packages, substitutions=substitutions,
                sources={name: str(path) for name, path in names.items()})


def register(env, app, executable, appfile):
    def action(target, source, env):
        shutil.copy2(str(source[0]), str(source[1]))
        bundle(str(source[1]), str(target[0]))
        return 0
    nodes = env.Command(app.Dir('Contents/libs'), [executable, *appfile, __file__], action)
    env.AlwaysBuild(nodes)  # Re-discover installed state; no cache or custom scanner.
    env.Depends(app, nodes)

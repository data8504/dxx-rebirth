"""Disposable-runner validation only; keep apps out of uploaded evidence."""
import json, os, pathlib, subprocess
root=pathlib.Path.cwd()
apps=[root/f'D{n}X-Rebirth.app' for n in (1,2)]
def run(*args):
    return subprocess.check_output([str(a) for a in args],text=True).strip()
for app in apps:
    assert app.is_dir(),app
    run('codesign','--force','--sign','-',app)
    run('codesign','--verify','--deep','--strict',app)
prefix=pathlib.Path(run('brew','--prefix')); hidden=prefix.with_name(prefix.name+'-sdl3-hidden')
assert not hidden.exists()
results=[]
subprocess.run(['/usr/bin/sudo','/bin/mv',str(prefix),str(hidden)],check=True)
try:
    assert not prefix.exists()
    for app in apps:
        exe=app/'Contents/MacOS'/app.stem.lower()
        env={k:v for k,v in os.environ.items() if not k.startswith(('DYLD_','SDL_'))}
        env['DYLD_PRINT_LIBRARIES']='1'
        r=subprocess.run([str(exe),'-help'],env=env,cwd='/tmp',capture_output=True,text=True,timeout=30)
        log=r.stdout+r.stderr;(root/f'macos-{app.stem}-startup.log').write_text(log)
        images=[s for s in log.splitlines() if s.startswith('dyld[')]
        local_sdl3=any(str(app) in s and ('/libSDL3.0.dylib' in s or '/libSDL3.dylib' in s) for s in images)
        external=any(str(prefix) in s or str(hidden) in s for s in images)
        results.append(dict(app=app.name,exit_code=r.returncode,bundled_sdl3=local_sdl3,homebrew_images=external,signature='ad-hoc verified'))
        assert r.returncode==0 and local_sdl3 and not external,results[-1]
finally:
    subprocess.run(['/usr/bin/sudo','/bin/mv',str(hidden),str(prefix)],check=True)
    (root/'macos-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results))

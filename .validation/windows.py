"""Exercise packaged Windows executables with only app/system DLL paths."""
import json,os,pathlib,subprocess
root=pathlib.Path.cwd();results=[]
system=pathlib.Path(os.environ.get('SystemRoot',r'C:\Windows'))
for n in (1,2):
    app=root/f'D{n}X-Rebirth';exe=app/f'd{n}x-rebirth.exe'
    assert exe.is_file(),exe
    env={k:v for k,v in os.environ.items() if not k.startswith(('SDL_','DYLD_'))}
    env['PATH']=';'.join(map(str,(app,system/'System32',system)))
    r=subprocess.run([str(exe),'-help'],cwd=app,env=env,capture_output=True,text=True,errors='replace',timeout=30)
    (root/f'windows-D{n}X-startup.log').write_text(r.stdout+r.stderr)
    results.append(dict(app=app.name,exit_code=r.returncode,dlls=sorted(p.name for p in app.glob('*.dll')),path=env['PATH']))
    assert r.returncode==0,results[-1]
(root/'windows-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results))

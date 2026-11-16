#!/usr/bin/env python3
"""Private full PE32 DSOUND builds using retained native248 recipes. No installs."""
import hashlib, json, os, shlex, subprocess, sys
from pathlib import Path
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASE = Path(os.environ['JKFLOW_BUILD_ROOT'])
SRC = BASE/'native247-wine-source'
BUILD = BASE/'native248-build-wine/build'
OUT = BASE/'native286-dsound259'
GEN = ROOT/'build/agent-tls/jobs/agent-259-host-05/generated/dlls/dsound'
ENV = dict(os.environ, PATH=str(BASE/'private-bin')+':'+str(BASE/'deps/rootfs/usr/bin')+':'+os.environ['PATH'])
REPORT = dict(scope='Private C/mingw PE builds only; no runtime or console changes', commands=[], inputs={}, outputs={})
previous=HERE/'native286-dsound259-build-progress.json'
if previous.exists():
    REPORT=json.loads(previous.read_text())
def digest(p):
    d=Path(p).read_bytes(); return dict(bytes=len(d),sha256=hashlib.sha256(d).hexdigest())
def save():
    (HERE/'native286-dsound259-build-progress.json').write_text(json.dumps(REPORT,indent=2)+'\n')
def pin(p):
    p=Path(p); REPORT['inputs'][str(p)]=digest(p)
def run(a,cwd,label,check=True):
    p=subprocess.run(list(map(str,a)),cwd=cwd,env=ENV,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    REPORT['commands'].append(dict(label=label,argv=list(map(str,a)),cwd=str(cwd),exit=p.returncode,output=p.stdout));save()
    print(label, p.returncode, p.stdout[-1500:],flush=True)
    if check:p.check_returncode()
    return p
OUT.mkdir(exist_ok=True)
recipes=run(['make','-s','-n','-o','Makefile','-o','config.status','dlls/dsound/i386-windows/dsound.dll'],BUILD,'read original recipes').stdout
(HERE/'native286-dsound259-recipes.txt').write_text(recipes)
commands=[shlex.split(line) for line in recipes.replace('\\\n','').splitlines() if line.strip()]
original_compile=[a for a in commands if '-c' in a and a[0]=='i686-w64-mingw32-gcc']
link=next(a for a in commands if 'winegcc' in a[0])
for a in link:
    if a.endswith('.a'):
        p=BUILD/a
        if not p.exists():p=BASE/'wine-build'/a
        assert p.is_file(),p
        # Import archives borrowed only with byte-identical module specs.
        if a.startswith('dlls/'):
            name=a.split('/')[1]
            s=SRC/'dlls'/name/(name+'.spec');t=BASE/'wine-source/dlls'/name/(name+'.spec')
            assert s.read_bytes()==t.read_bytes(),(s,t)
            pin(s);pin(t)
        pin(p)
for kind in sys.argv[1:] or ['control','generated']:
    dest=OUT/kind; dest.mkdir(exist_ok=False)
    ds=dest/'source';ds.mkdir()
    for p in (SRC/'dlls/dsound').iterdir():
        if p.is_file():
            q=ds/p.name
            changed=(GEN/p.name) if kind=='generated' else (HERE/('native286-dsound259-'+p.name))
            if kind!='control' and changed.is_file():q.write_bytes(changed.read_bytes());pin(changed)
            else:q.symlink_to(p);pin(p)
    if kind!='control':
        for name in ('pw259_dsound_obs.c','pw259_dsound_obs.h'):
            p=(GEN/name) if kind=='generated' else HERE/('native286-dsound259-'+name)
            (ds/name).write_bytes(p.read_bytes());pin(p)
    mapping={}
    for orig in original_compile:
        a=orig[:];old=a[a.index('-o')+1];new=dest/Path(old).name;mapping[old]=str(new)
        a[a.index('-o')+1]=str(new)
        a=[str(ds/Path(x).name) if x.startswith(str(SRC/'dlls/dsound')+'/') and x.endswith('.c') else x for x in a]
        a[1:1]=['-I'+str(ds)]
        run(a,BUILD,kind+' '+new.name)
    if kind!='control':
        a=original_compile[0][:];a[a.index('-o')+1]=str(dest/'pw259_dsound_obs.o')
        a=[str(ds/'pw259_dsound_obs.c') if x==str(SRC/'dlls/dsound/buffer.c') else x for x in a]
        a[1:1]=['-I'+str(ds)]
        run(a,BUILD,kind+' actual C wrapper')
    for orig in commands:
        if not ('widl' in orig[0] or '/wrc' in orig[0]):continue
        a=orig[:];old=a[a.index('-o')+1];new=dest/Path(old).name;mapping[old]=str(new);a[a.index('-o')+1]=str(new)
        run(a,BUILD,kind+' '+new.name)
    a=link[:];a[a.index('-o')+1]=str(dest/'dsound.dll');a[a.index('--wine-objdir')+1]=str(BUILD)
    a=[mapping.get(x,str((BUILD/x) if (BUILD/x).is_file() else (BASE/'wine-build'/x))) if (x.endswith('.o') or x.endswith('.res') or x.endswith('.a')) else x for x in a]
    if kind!='control':a.insert(a.index('-Wl,--disable-stdcall-fixup'),str(dest/'pw259_dsound_obs.o'))
    if kind not in ('control','generated'):
        p=BASE/'wine-build/dlls/kernelbase/i386-windows/libkernelbase.a'
        for q in (SRC/'dlls/kernelbase/kernelbase.spec',BASE/'wine-source/dlls/kernelbase/kernelbase.spec',p):pin(q)
        assert (SRC/'dlls/kernelbase/kernelbase.spec').read_bytes()==(BASE/'wine-source/dlls/kernelbase/kernelbase.spec').read_bytes()
        a.insert(a.index('-Wl,--disable-stdcall-fixup'),str(p))
    run(a,BUILD,kind+' FULL PE32 link')
    artifact=HERE/('native286-dsound259-'+kind+'.dll');artifact.write_bytes((dest/'dsound.dll').read_bytes())
    run(['llvm-strip-18','--strip-debug',artifact],dest,kind+' strip debug')
    REPORT['outputs'][str(artifact)]=digest(artifact)
    save()
(HERE/'native286-dsound259-build.json').write_text(json.dumps(REPORT,indent=2)+'\n')

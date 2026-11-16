#!/usr/bin/env python3
"""R3: restore arithmetic, fix archive order, rebuild all 10 C source units."""
import hashlib,json,os,subprocess
from pathlib import Path
H=Path(__file__).resolve().parent;B=Path(os.environ['JKFLOW_BUILD_ROOT']);OLD=B/'native286-dsound259/candidate-r2';OUT=B/'native286-dsound259/candidate-r3'
R=json.loads((H/'native286-dsound259-build.json').read_text());ENV=dict(os.environ,PATH=str(B/'private-bin')+':'+str(B/'deps/rootfs/usr/bin')+':'+os.environ['PATH'])
report=dict(commands=[],inputs={},outputs={},reason='Observer object must precede static archives; original arithmetic needs no source/runtime workaround')
def pin(p):
 p=Path(p);d=p.read_bytes();return dict(path=str(p),bytes=len(d),sha256=hashlib.sha256(d).hexdigest())
def run(a,cwd,label):
 p=subprocess.run(list(map(str,a)),cwd=cwd,env=ENV,text=True,capture_output=True)
 report['commands'].append(dict(label=label,argv=list(map(str,a)),cwd=str(cwd),exit=p.returncode,output=p.stdout+p.stderr))
 (H/'native286-dsound259-r3-build-progress.json').write_text(json.dumps(report,indent=2)+'\n');print(label,p.returncode,p.stdout+p.stderr,flush=True);p.check_returncode()
s=(H/'native286-dsound259-pw259_dsound_obs.h').read_text();a=s.index('/* Wine\'s retained i386 compiler-rt');b=s.index('#define PW259_DS_FLAGS',a);s=s[:a]+s[b:]
for x in ('c->abs_mix','c->ext_end','start','a'):s=s.replace('pw259_ds_mod('+x+', c->buflen)',x+' % c->buflen')
assert 'pw259_ds_mod' not in s and '__udivmoddi4' not in s
header=H/'native286-dsound259-r3-pw259_dsound_obs.h';header.write_text(s)
OUT.mkdir(exist_ok=False);(OUT/'source').mkdir()
for p in (OLD/'source').iterdir():(OUT/'source'/p.name).symlink_to(header if p.name=='pw259_dsound_obs.h' else p)
rows=[r for r in R['commands'] if r['label'].startswith('candidate-r2 ') and (r['label'].endswith('.o') or r['label']=='candidate-r2 actual C wrapper')]
assert len(rows)==11
for row in rows:
 args=[x.replace(str(OLD),str(OUT)) for x in row['argv']];run(args,Path(row['cwd']),row['label'].replace('candidate-r2','R3'))
for p in OLD.glob('*.res'):(OUT/p.name).symlink_to(p)
row=next(r for r in R['commands'] if r['label']=='candidate-r2 FULL PE32 link');args=[x.replace(str(OLD),str(OUT)) for x in row['argv']]
args.remove(str(OUT/'pw259_dsound_obs.o'));first_archive=next(i for i,x in enumerate(args) if x.endswith('.a'));args.insert(first_archive,str(OUT/'pw259_dsound_obs.o'))
for x in args:
 if x.startswith('/') and Path(x).is_file():report['inputs'][x]=pin(x)
for p in (OUT/'source').iterdir():
 if p.is_file():report['inputs'][str(p)]=pin(p)
run(args,Path(row['cwd']),'R3 FULL PE32 link')
dll=H/'native286-dsound259-candidate-r3.dll';dll.write_bytes((OUT/'dsound.dll').read_bytes());run(['llvm-strip-18','--strip-debug',dll],OUT,'R3 strip debug')
report['outputs'][str(dll)]=pin(dll)
(H/'native286-dsound259-r3-build.json').write_text(json.dumps(report,indent=2)+'\n');print(pin(dll))

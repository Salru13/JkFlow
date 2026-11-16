from pathlib import Path
import os,subprocess,json,shlex,hashlib
R=Path(__file__).resolve().parent;B=Path(os.environ['JKFLOW_BUILD_ROOT']);D=B/'native286-cs-sampler';W=B/'native248-build-wine';O=B/'orbis-win-9749fcd';SDK=O/'sdk/orbis-sdk-v1/sdk'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
recipes=json.loads((R/'native286-cs-sampler-recipes.json').read_text());rows=[]
pins=json.loads((R/'native286-cs-sampler-source-pins.json').read_text())
assert all(sha(p['original'])==p['original_sha256'] for p in pins.values())
retained={n:sha(W/'build/dlls/ntdll/unix'/(n+'.o')) for n in ['signal_x86_64','thread']}
env=dict(os.environ,PW_PS4_ROOT=str(O),MESA_BUILD=str(B/'native248-platform'),OO_PS4_TOOLCHAIN=str(SDK),PW_PRX_LINK='1',PW_SHARED_LIBC='1')
env['PATH']=':'.join(str(p) for p in [W/'tools',B/'private-bin',B/'deps/rootfs/usr/bin'])+':'+env['PATH']
def run(cmd,name):
 p=subprocess.run(cmd,cwd=W/'build',env=env,text=True,capture_output=True);(D/(name+'.log')).write_text(p.stdout+p.stderr)
 rows.append(dict(name=name,command=cmd,exit=p.returncode,tail=(p.stdout+p.stderr)[-2500:]));(R/'native286-cs-sampler-build.json').write_text(json.dumps(dict(commands=rows),indent=2)+'\n')
 if p.returncode:raise RuntimeError(name+'\n'+p.stderr[-2500:])
for row in recipes['compile']:
 name=row['name'];cmd=shlex.split(row['stdout'].replace('\\\n',' '));original=cmd[cmd.index('-o')+2]
 for variant in ['control','candidate']:
  c=cmd.copy();out=D/(variant+'-'+name+'.o');c[c.index('-o')+1]=str(out)
  if variant=='candidate':c[c.index(original)]=str(D/(name+'.c'));c+=['-I'+str(B/'native247-wine-source/dlls/ntdll/unix')]
  run(c,variant+'-'+name)
  rows[-1]['object_sha256']=sha(out);rows[-1]['retained_object_sha256']=sha(W/'build/dlls/ntdll/unix'/(name+'.o'))
  # Byte differences from DWARF/source paths do not establish code mismatch.
  for obj in [out,W/'build/dlls/ntdll/unix'/(name+'.o')]:
   raw=subprocess.check_output(['llvm-objdump-18','-d','--no-show-raw-insn',str(obj)],text=True)
   (D/(variant+'-'+name+('-retained' if obj!=out else '')+'.dis')).write_text(raw)
assert all(sha(p['original'])==p['original_sha256'] for p in pins.values())
assert all(sha(W/'build/dlls/ntdll/unix'/(n+'.o'))==h for n,h in retained.items())
result=dict(commands=rows,original_inputs_unchanged=True,original_object_hashes=retained)
(R/'native286-cs-sampler-build.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps([dict(name=x['name'],exit=x['exit'],sha256=x.get('object_sha256'),retained=x.get('retained_object_sha256')) for x in rows],indent=2))

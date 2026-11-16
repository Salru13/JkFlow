from pathlib import Path
import subprocess,json,os,hashlib,shlex,re,struct
R=Path(__file__).resolve().parent;B=Path(os.environ['JKFLOW_BUILD_ROOT']);D=B/'native288-standard-handles';W=B/'native248-build-wine';O=B/'orbis-win-9749fcd';SDK=O/'sdk/orbis-sdk-v1/sdk'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
pins=json.loads((R/'native288-standard-handles-inputs.json').read_text());assert all(sha(pins[x]['path'])==pins[x]['sha256'] for x in ['original','candidate'])
prior=json.loads((R/'native286-cs-sampler-link.json').read_text());link=next(x['command'] for x in prior['commands'] if x['name']=='candidate-link');convert=next(x['command'] for x in prior['commands'] if x['name']=='convert')
original_inputs={}
for x in link:
 if x.endswith(('.o','.a')):
  p=Path(x);p=p if p.is_absolute() else W/'build'/p;assert p.is_file();original_inputs[str(p)]=sha(p)
assert sha(B/'native286-cs-sampler/candidate.elf')==prior['candidate_elf_sha256']=='2d96f4909fc01d2a2df2ed424e8db4688ea44507379965fcd685f59cb42740ec'
(D/'empty-stubs').mkdir(exist_ok=True)
env=dict(os.environ,PW_PS4_ROOT=str(O),MESA_BUILD=str(B/'native248-platform'),OO_PS4_TOOLCHAIN=str(SDK),PW_PRX_LINK='1',PW_SHARED_LIBC='1',PW_PRX_STUBS='-L'+str(D/'empty-stubs')+' -Wl,--as-needed')
env['PATH']=':'.join(str(p) for p in [W/'tools',B/'private-bin',B/'deps/rootfs/usr/bin'])+':'+env['PATH'];rows=[]
def run(cmd,name):
 p=subprocess.run(cmd,cwd=W/'build',env=env,text=True,capture_output=True);(D/(name+'.log')).write_text(p.stdout+p.stderr);rows.append(dict(name=name,command=cmd,exit=p.returncode,tail=(p.stdout+p.stderr)[-2000:]));(R/'native288-standard-handles-build.json').write_text(json.dumps(dict(status='IN_PROGRESS',commands=rows),indent=2)+'\n');assert not p.returncode,(name,p.stderr[-3000:]);print(name,'PASS',flush=True)
recipe=json.loads((R/'native288-standard-handles-recipe.json').read_text());cmd=shlex.split(recipe['stdout'].replace('\\\n',' '));old=pins['original']['path'];assert cmd.count(old)==1
for mode in ['control','candidate']:
 c=cmd.copy();c[c.index('-o')+1]=str(D/(mode+'-env.o'))
 if mode=='candidate':c[c.index(old)]=pins['candidate']['path'];c+=['-I'+str(Path(old).parent)]
 run(c,mode+'-compile')
assert sha(D/'control-env.o')==sha(W/'build/dlls/ntdll/unix/env.o'),'rebuilt control object mismatch'
for mode in ['control','candidate']:
 c=link.copy();c[c.index('-o')+1]=str(D/(mode+'.elf'));idx=c.index('dlls/ntdll/unix/env.o');c[idx]=str(D/(mode+'-env.o'));run(c,mode+'-link')
 if mode=='control':assert sha(D/'control.elf')==prior['candidate_elf_sha256'],'control link mismatch'
c=convert.copy()
for flag,name in [('-in','candidate.elf'),('-out','candidate.oelf'),('-lib','ntdll.prx')]:c[c.index(flag)+1]=str(D/name)
run(c,'convert')
def output(*args):return subprocess.check_output(args,text=True)
def symbols(p):
 u=set();d=set()
 for l in output('llvm-readelf-18','--dyn-syms','--wide',str(p)).splitlines():
  f=l.split()
  if len(f)<8 or not f[0].rstrip(':').isdigit():continue
  if f[6]=='UND':
   if f[4]=='GLOBAL':u.add(f[7].split('@')[0])
  else:d.add(f[7].split('@')[0])
 return u,d
u0,d0=symbols(D/'control.elf');u1,d1=symbols(D/'candidate.elf');assert d0==d1;assert u0==u1,('imports',sorted(u1-u0),sorted(u0-u1))
providers=[];provided=set()
for n in re.findall(r'Shared library: \[(.*?)\]',output('llvm-readelf-18','-d',str(D/'candidate.elf'))):
 p=next(p for p in [W/'prx/stubs'/n,SDK/'lib'/n] if p.exists());provided|=symbols(p)[1];providers.append(dict(name=n,path=str(p),sha256=sha(p)))
assert not u1-provided,sorted(u1-provided)
def segs(p):
 d=p.read_bytes();off=struct.unpack_from('<Q',d,32)[0];sz,num=struct.unpack_from('<HH',d,54);return [struct.unpack_from('<IIQQQQQQ',d,off+i*sz) for i in range(num)]
a=segs(D/'candidate.elf');b=segs(D/'candidate.oelf');coverage=[]
for typ,fl,off,va,phys,fs,mem,align in a:
 if typ!=1 or not fl&2:continue
 relro=any(t==0x6474e552 and v<=va and va+mem<=v+m for t,f,o,v,p,s,m,z in a)
 found=[(v,m) for t,f,o,v,p,s,m,z in b if v==va and (t==1 and f&2 or relro and t==0x61000010)];assert len(found)==1
 end=(sum(found[0])+16383)&~16383;assert end>=va+mem;coverage.append(dict(va=hex(va),required=hex(va+mem),mapped=hex(end)))
dis=output('llvm-objdump-18','-d','--no-show-raw-insn',str(D/'candidate.elf'))
for fn in ['pw_cs_handler','pw_cs_sampler_abort']:
 body=re.search(r'^[0-9a-f]+ <'+fn+r'>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',dis,re.M|re.S).group(0);assert not re.search(r'\bcallq?\b|%fs:|%gs:',body);(R/('native288-standard-handles-'+fn+'.dis')).write_text(body)
init=re.search(r'^[0-9a-f]+ <init_startup_info>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',dis,re.M|re.S).group(0);assert '<wine_server_fd_to_handle>' not in init
(R/'native288-standard-handles-init.dis').write_text(init)
raw=(D/'candidate.elf').read_bytes();assert b'PW_STD_HANDLES' in raw and b'PW_CS_PC_SAMPLE' in raw
assert all(sha(p)==h for p,h in original_inputs.items()) and sha(old)==pins['original']['sha256']
allocated=sum(p.stat().st_blocks*512 for p in D.rglob('*') if p.is_file() and not p.is_symlink());assert allocated<100*1024*1024
result=dict(status='PASS_PRIVATE_OFFLINE_ONLY',commands=rows,original_inputs=original_inputs,original_inputs_unchanged=True,control_env_object_byte_exact=True,control_elf_byte_exact=True,control_sha256=sha(D/'control.elf'),candidate_elf_sha256=sha(D/'candidate.elf'),candidate_prx_sha256=sha(D/'ntdll.prx'),candidate_env_object_sha256=sha(D/'candidate-env.o'),exports_unchanged=True,imports_unchanged=True,providers=providers,writable_coverage=coverage,sampler_handler_and_abort_no_calls_or_TLS=True,initial_standard_fd_conversion_calls=0,allocated_bytes=allocated,hardware_tested=False,FPS_gain_claimed=False)
(R/'native288-standard-handles-build.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ['commands','original_inputs','providers']},indent=2))

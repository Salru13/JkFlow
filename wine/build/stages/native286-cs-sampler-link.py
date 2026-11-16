from pathlib import Path
import subprocess,json,os,hashlib,re,struct
R=Path(__file__).resolve().parent;B=Path(os.environ['JKFLOW_BUILD_ROOT']);D=B/'native286-cs-sampler';W=B/'native248-build-wine';O=B/'orbis-win-9749fcd';SDK=O/'sdk/orbis-sdk-v1/sdk'
v=json.loads((B/'native266-ntdll-link-r1/verification.json').read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();rows=[]
original_inputs={}
for arg in v['candidate_command']:
 if arg.endswith(('.o','.a')):
  p=Path(arg);p=p if p.is_absolute() else W/'build'/p
  if p.is_file():original_inputs[str(p)]=sha(p)
(D/'empty-stubs').mkdir(exist_ok=True)
env=dict(os.environ,PW_PS4_ROOT=str(O),MESA_BUILD=str(B/'native248-platform'),OO_PS4_TOOLCHAIN=str(SDK),PW_PRX_LINK='1',PW_SHARED_LIBC='1',PW_PRX_STUBS='-L'+str(D/'empty-stubs')+' -Wl,--as-needed')
env['PATH']=':'.join(str(p) for p in [W/'tools',B/'private-bin',B/'deps/rootfs/usr/bin'])+':'+env['PATH']
def run(cmd,name):
 p=subprocess.run(cmd,cwd=W/'build',env=env,text=True,capture_output=True);(D/(name+'.log')).write_text(p.stdout+p.stderr);rows.append(dict(name=name,command=cmd,exit=p.returncode))
 assert not p.returncode,(name,p.stderr[-3000:])
for mode in ['control','candidate']:
 c=v['candidate_command'].copy();c[c.index('-o')+1]=str(D/(mode+'.elf'))
 if mode=='candidate':
  for n in ['signal_x86_64','thread']:
   old='dlls/ntdll/unix/'+n+'.o';assert c.count(old)==1;c[c.index(old)]=str(D/('candidate-'+n+'.o'))
 run(c,mode+'-link')
 if mode=='control':assert sha(D/'control.elf')==v['candidate_elf_sha256']
c=v['conversion_command'].copy()
for flag,name in [('-in','candidate.elf'),('-out','candidate.oelf'),('-lib','ntdll.prx')]:c[c.index(flag)+1]=str(D/name)
run(c,'convert')
def output(*args):return subprocess.check_output(args,text=True)
def symbols(p):
 und=set();defs=set()
 for l in output('llvm-readelf-18','--dyn-syms','--wide',str(p)).splitlines():
  f=l.split()
  if len(f)<8 or not f[0].rstrip(':').isdigit():continue
  if f[6]=='UND':
   if f[4]=='GLOBAL':und.add(f[7].split('@')[0])
  else:defs.add(f[7].split('@')[0])
 return und,defs
u0,d0=symbols(D/'control.elf');u1,d1=symbols(D/'candidate.elf');assert d0==d1,('exports',sorted(d1-d0),sorted(d0-d1))
providers=[];provided=set()
for n in re.findall(r'Shared library: \[(.*?)\]',output('llvm-readelf-18','-d',str(D/'candidate.elf'))):
 p=next(p for p in [W/'prx/stubs'/n,SDK/'lib'/n] if p.exists());provided|=symbols(p)[1];providers.append(dict(name=n,path=str(p),sha256=sha(p)))
assert not u1-provided,sorted(u1-provided)
def segs(p):
 d=p.read_bytes();off=struct.unpack_from('<Q',d,32)[0];sz,num=struct.unpack_from('<HH',d,54)
 return [struct.unpack_from('<IIQQQQQQ',d,off+i*sz) for i in range(num)]
a=segs(D/'candidate.elf');b=segs(D/'candidate.oelf');coverage=[]
for typ,fl,off,va,phys,fs,mem,align in a:
 if typ!=1 or not fl&2:continue
 relro=any(t==0x6474e552 and v<=va and va+mem<=v+m for t,f,o,v,p,s,m,z in a)
 found=[(v,m) for t,f,o,v,p,s,m,z in b if v==va and (t==1 and f&2 or relro and t==0x61000010)];assert len(found)==1
 end=(sum(found[0])+16383)&~16383;assert end>=va+mem;coverage.append(dict(va=hex(va),required=hex(va+mem),mapped=hex(end)))
dis=output('llvm-objdump-18','-d','--no-show-raw-insn',str(D/'candidate.elf'))
handler=re.search(r'^[0-9a-f]+ <pw_cs_handler>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',dis,re.M|re.S).group(0)
assert not re.search(r'\bcallq?\b|%fs:|%gs:',handler),handler
abort_hook=re.search(r'^[0-9a-f]+ <pw_cs_sampler_abort>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',dis,re.M|re.S).group(0)
assert not re.search(r'\bcallq?\b|%fs:|%gs:',abort_hook),abort_hook
(R/'native286-cs-sampler-handler.dis').write_text(handler)
(R/'native286-cs-sampler-abort.dis').write_text(abort_hook)
assert all(sha(p)==h for p,h in original_inputs.items())
result=dict(status='PASS_PRIVATE_NOT_HARDWARE_QUALIFIED',commands=rows,control_sha256=sha(D/'control.elf'),candidate_elf_sha256=sha(D/'candidate.elf'),candidate_prx_sha256=sha(D/'ntdll.prx'),exports_unchanged=True,added_imports=sorted(u1-u0),providers=providers,writable_coverage=coverage,handler_no_calls_or_TLS=True,abort_hook_no_calls_or_TLS=True,handler_disassembly=handler,original_inputs_unchanged=True,original_inputs=original_inputs,hardware_tested=False)
(R/'native286-cs-sampler-link.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ['commands','providers','handler_disassembly','original_inputs']},indent=2))

"""Private fd-only build and reproducible Wine11 wineserver link."""
import hashlib,json,os,re,shlex,subprocess,struct
from pathlib import Path
here=Path(__file__).resolve().parent
base=Path(os.environ['JKFLOW_BUILD_ROOT']); work=base/'native248-build-wine'
orbis=base/'orbis-win-9749fcd'; sdk=orbis/'sdk/orbis-sdk-v1/sdk'
out=base/'native266-wineserver-r1'; out.mkdir(exist_ok=True)
source=base/'native247-wine-source/server/fd.c'; sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(source)=='61d28a8d946627bd44196381ca14b389b39016b1d482741c69868c76ecdb79f4'
patch=here.parent/'agent/orbis-tls/task253_orbis_kqueue_selection.patch'
(out/'server').mkdir(exist_ok=True); (out/'server/fd.c').write_bytes(source.read_bytes())
env=os.environ.copy();env.update(PW_PS4_ROOT=str(orbis),MESA_BUILD=str(base/'native248-platform'),OO_PS4_TOOLCHAIN=str(sdk),PW_PRX_LINK='1',PW_SHARED_LIBC='1')
env['PATH']=':'.join(str(p) for p in (work/'tools',base/'private-bin',base/'deps/rootfs/usr/bin'))+':'+env['PATH']
records=[]
def run(cmd,name,cwd=None):
 p=subprocess.run([str(x) for x in cmd],cwd=cwd or work/'build',env=env,text=True,capture_output=True)
 (out/(name+'.log')).write_text(p.stdout+p.stderr);records.append(dict(name=name,command=[str(x) for x in cmd],returncode=p.returncode))
 assert p.returncode==0,p.stderr[-3000:];return p.stdout
run(['patch','-p1','--batch','-i',patch],'patch',out)
query=run(['make','-s','-n','-o','Makefile','-o','config.status','-W',source,'server/fd.o'],'make-n').replace('\\\n',' ')
line=next(l for l in query.splitlines() if ' -c ' in l and str(source) in l)
compile=shlex.split(line);compile[compile.index(str(source))]=str(out/'server/fd.c');compile[compile.index('-o')+1]=str(out/'fd.o');compile[1:1]=['-iquote',str(source.parent)]
run(compile,'compile')
undefined=run(['llvm-nm-18','-u',out/'fd.o'],'fd-imports')
assert ' kqueue' in undefined and ' kevent' in undefined and 'epoll_' not in undefined
text=(work/'make.log').read_text(errors='replace').replace('\\\n',' ')
line=[l for l in text.splitlines() if '-o server/wineserver ' in l][-1]
objects=[a for a in shlex.split(line) if a.endswith('.o') and not a.startswith('/')]
assert objects.count('server/fd.o')==1
prx=work/'prx';cc=orbis/'build/ps4-wine-sdk/bin/prospero-clang'
wrapsource=(base/'native248-build-bridge/wine/ps5/pw_wine_cwd_libc.c').read_text()
wraps=['-Wl,--wrap='+n for n in sorted(set(re.findall(r'__wrap_([a-z_]*)',wrapsource)))]
shims=[prx/'obj/pw_wineserver_stack_ps4.o',prx/'heap/pw_wineserver_heap_ps4.o',prx/'heap/pw_wine_heap.o',prx/'heap/pw_wine_heap_libc.o']
shims += [prx/('obj/'+n+'.o') for n in ('pw_wine_compat','pw_wine_compat_libc','pw_wine_threads','pw_wine_threads_libc','pw_wine_cwd','pw_wine_cwd_libc')]
(out/'empty-stubs').mkdir(exist_ok=True);env['PW_PRX_STUBS']='-L'+str(out/'empty-stubs')+' -Wl,--as-needed'
recipe=[cc,'-shared','-Wl,-Bsymbolic','-Wl,--eh-frame-hdr','-Wl,-soname,wineserver.prx','-Wl,--warn-unresolved-symbols']+wraps+['-Wl,--wrap=pthread_create','-o',out/'control.elf']+objects+shims+[prx/'obj/wineserver_desc.o']
inputs={str(work/'build'/p):sha(work/'build'/p) for p in objects};inputs.update({str(p):sha(p) for p in shims+[prx/'obj/wineserver_desc.o']})
run(recipe,'control-link')
assert sha(out/'control.elf')=='b10a7af26c1708919bb350ecdf7b2b02306fdd8c9d0707f5c58a0dd2c12c9869',sha(out/'control.elf')
candidate=recipe.copy();candidate[candidate.index(out/'control.elf')]=out/'candidate.elf';candidate[candidate.index('server/fd.o')]=out/'fd.o'
run(candidate,'candidate-link')
def writable(path):
 d=path.read_bytes();off=struct.unpack_from('<Q',d,32)[0];size,count=struct.unpack_from('<HH',d,54)
 return [(v,m) for i in range(count) for t,f,_,v,_,_,m,_ in [struct.unpack_from('<IIQQQQQQ',d,off+i*size)] if t==1 and f&2]
conversion=[orbis/'scripts/create-fself-repro.sh',sdk/'bin/linux/create-fself','-in',out/'candidate.elf','-out',out/'candidate.oelf','-lib',out/'wineserver.prx','-libname','wineserver.prx','-ptype','fake','-library-path',str(prx/'stubs')+':'+str(sdk/'lib')]
run(conversion,'convert')
va,mem=max(writable(out/'candidate.elf'),key=lambda x:sum(x));end=max(v+m for v,m in writable(out/'candidate.oelf') if v==va);mapped=(end+0x3fff)&~0x3fff
assert mapped>=va+mem,(hex(mapped),hex(va+mem))
def symbols(path):
 u,d=set(),set()
 for l in subprocess.check_output(['llvm-readelf-18','--dyn-syms','--wide',str(path)],text=True).splitlines():
  f=l.split()
  if len(f)<8 or not f[0].rstrip(':').isdigit():continue
  if f[6]=='UND':
   if f[4]=='GLOBAL':u.add(f[7].split('@')[0])
  else:d.add(f[7].split('@')[0])
 return u,d
old_u,old_d=symbols(out/'control.elf');new_u,new_d=symbols(out/'candidate.elf')
assert not new_d-old_d and old_d-new_d=={'epoll_create','epoll_create1','epoll_ctl','epoll_pwait','epoll_wait'},sorted(old_d^new_d)
assert {'kqueue','kevent'} <= new_u and not ({'kqueue','kevent'} & new_d)
disassembly=run(['llvm-objdump-18','-d',out/'candidate.elf'],'candidate-disassembly')
assert '<kqueue@plt>' in disassembly and '<kevent@plt>' in disassembly and 'epoll_' not in disassembly
provided=set();providers=[]
dynamic=subprocess.check_output(['llvm-readelf-18','-d',str(out/'candidate.elf')],text=True)
for name in re.findall(r'Shared library: \[(.*?)\]',dynamic):
 path=next(p for p in (prx/'stubs'/name,sdk/'lib'/name) if p.is_file());provided|=symbols(path)[1];providers.append(dict(name=name,path=str(path),sha256=sha(path)))
assert not new_u-provided,sorted(new_u-provided)
assert {'kqueue','kevent'} <= symbols(sdk/'lib/libkernel.so')[1]
assert all(sha(Path(p))==h for p,h in inputs.items()) and sha(source)=='61d28a8d946627bd44196381ca14b389b39016b1d482741c69868c76ecdb79f4'
receipt=dict(status='PASS',source_sha256=sha(source),patch_sha256=sha(patch),candidate_source_sha256=sha(out/'server/fd.c'),object_sha256=sha(out/'fd.o'),control_sha256=sha(out/'control.elf'),candidate_elf_sha256=sha(out/'candidate.elf'),candidate_prx_sha256=sha(out/'wineserver.prx'),added_imports=sorted(new_u-old_u),removed_imports=sorted(old_u-new_u),descriptor_exports_unchanged=True,removed_local_epoll_definitions=sorted(old_d-new_d),providers=providers,writable_required=hex(va+mem),writable_converted=hex(end),writable_mapped=hex(mapped),inputs=inputs,commands=records,hardware_tested=False)
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');(here/'native266-wineserver-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:v for k,v in receipt.items() if k not in ('providers','commands','inputs')},indent=2))



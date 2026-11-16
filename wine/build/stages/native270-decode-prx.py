"""Link only private WineVulkan PRX control/candidate from retained actual inputs."""
from pathlib import Path
import hashlib,json,os,re,shlex,shutil,struct,subprocess,sys
repo=Path(sys.argv[1]);base=Path(os.environ['JKFLOW_BUILD_ROOT']);out=base/'native270-decode'
reports=repo/'research/native-wine';work=base/'native248-build-wine';build=work/'build';prx=work/'prx'
orbis=base/'orbis-win-9749fcd';sdk=orbis/'sdk/orbis-sdk-v1/sdk';cc=orbis/'build/ps4-wine-sdk/bin/prospero-clang'
env=os.environ.copy();env['PATH']=str(base/'private-bin')+':'+str(base/'deps/rootfs/usr/bin')+':'+env['PATH']
env.update(MESA_BUILD=str(base/'native248-platform'),OO_PS4_TOOLCHAIN=str(sdk),PW_PRX_LINK='1',PW_SHARED_LIBC='1',LLVM_OBJCOPY='llvm-objcopy-18')
stubs=prx/'stubs';providers=[stubs/(x+'.so') for x in ['ntdll','win32u','wineserver']]
env['PW_PRX_STUBS']='-L'+str(stubs)+' -Wl,--as-needed '+' '.join(map(str,providers))
commands=[];inputs={};outputs={}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pin(p):inputs[str(p)]={'sha256':sha(p),'bytes':Path(p).stat().st_size}
def run(args,cwd):
 p=subprocess.run(list(map(str,args)),cwd=cwd,env=env,text=True,capture_output=True)
 commands.append(dict(command=list(map(str,args)),cwd=str(cwd),exit=p.returncode,stdout=p.stdout,stderr=p.stderr))
 (reports/'native270-decode-prx-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 if p.returncode:print(p.stdout+p.stderr);raise SystemExit(1)
 assert not re.search(r'undefined symbol|unresolved symbol|missing library for symbol',p.stdout+p.stderr,re.I),p.stdout+p.stderr
 return p.stdout
def loads(p):
 data=p.read_bytes();offset=struct.unpack_from('<Q',data,0x20)[0];size,count=struct.unpack_from('<HH',data,0x36)
 return [struct.unpack_from('<IIQQQQQQ',data,offset+i*size) for i in range(count)]
def bss_gate(elf,oelf):
 old=[(x[3],x[6]) for x in loads(elf) if x[0]==1 and x[1]&2]
 new=[(x[3],x[6]) for x in loads(oelf) if x[0]==1 and x[1]&2]
 va,ms=max(old,key=lambda s:sum(s));end=max((ov+om for ov,om in new if ov==va),default=0)
 return {'elf_rw_end':va+ms,'converted_rw_end':end,'mapped_rw_end':(end+0x3fff)&~0x3fff,'pass':((end+0x3fff)&~0x3fff)>=va+ms}
text=(work/'make.log').read_text(errors='replace').replace('\\\n',' ')
lines=[x for x in text.splitlines() if '-o dlls/winevulkan/winevulkan.so ' in x];assert lines
objects=[build/x for x in shlex.split(lines[-1]) if x.endswith('.o') and not x.startswith('/')];assert len(objects)==9
objects += [prx/'heap/pw_wine_heap_libc.o',prx/'obj/pw_wine_dl_libc.o',prx/'obj/winevulkan_desc.o']
cwd_source=base/'native248-build-bridge/wine/ps5/pw_wine_cwd_libc.c'
wraps=['-Wl,--wrap='+s for s in sorted(set(re.findall('__wrap_([a-z_]+)',cwd_source.read_text())))]
for p in objects+providers+[cc,cwd_source,orbis/'scripts/prx-ps4.sh',orbis/'scripts/create-fself-repro.sh',sdk/'bin/linux/create-fself',orbis/'build/ps4-wine-sdk/module_param.o',orbis/'build/ps4-wine-sdk/liborbis-compat-16bit-wchar.a',orbis/'src/freebsd-compat/prx_init_array.ld',orbis/'src/freebsd-compat/prx_module_param.ld',orbis/'prospero-win/wine/ps5/prx_eh_frame.ld']+[sdk/'lib'/x for x in ['libc.a','libkernel.so','libc++.a','libSceNet.so','libSceLibcInternal.so']]:pin(p)
for name in ['pw_vk_batch_unix.o','pw_vk_generated_unix.o']:pin(out/name)
artifacts=reports/'native270-decode-artifacts';artifacts.mkdir(exist_ok=True)
for kind in ['control','candidate']:
 dest=out/('prx-'+kind);dest.mkdir(exist_ok=True);elf=dest/'winevulkan.shared.elf';oelf=dest/'winevulkan.oelf';module=dest/'winevulkan.prx'
 linked=[out/p.name if kind=='candidate' and p.name in ['pw_vk_batch_unix.o','pw_vk_generated_unix.o'] else p for p in objects]
 run([cc,'-shared','-Wl,-Bsymbolic','-Wl,--eh-frame-hdr','-Wl,-soname,winevulkan.prx','-Wl,--warn-unresolved-symbols',*wraps,'-o',elf,*linked],dest)
 run(['bash',orbis/'scripts/create-fself-repro.sh',sdk/'bin/linux/create-fself','-in',elf,'-out',oelf,'-lib',module,'-libname','winevulkan.prx','-ptype','fake','-library-path',str(stubs)+':'+str(sdk/'lib')],dest)
 gate=bss_gate(elf,oelf);assert gate['pass'],gate
 outputs[kind]={'files':{str(p):{'sha256':sha(p),'bytes':p.stat().st_size} for p in [elf,oelf,module]},'bss':gate}
 if kind=='candidate':
  for p in [elf,oelf,module]:shutil.copyfile(p,artifacts/p.name)
 print('PASS linked/converted/BSS',kind,flush=True)
outputs['control_original_parity']={'elf':sha(out/'prx-control/winevulkan.shared.elf')==sha(prx/'winevulkan.shared.elf'),'prx':sha(out/'prx-control/winevulkan.prx')==sha(prx/'sce_module/winevulkan.prx')}
for p,data in inputs.items():assert sha(p)==data['sha256'],'read-only input changed: '+p
(reports/'native270-decode-prx.json').write_text(json.dumps({'inputs':inputs,'outputs':outputs,'environment':{k:env[k] for k in ['MESA_BUILD','OO_PS4_TOOLCHAIN','PW_PRX_LINK','PW_SHARED_LIBC','PW_PRX_STUBS','LLVM_OBJCOPY']}},indent=2)+'\n')
print('Control original parity',outputs['control_original_parity'])

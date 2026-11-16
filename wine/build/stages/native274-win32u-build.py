"""Relink the actual accepted win32u, changing only the reviewed Vulkan object."""
from pathlib import Path
import argparse,hashlib,json,os,re,shlex,struct,subprocess
ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--output',type=Path);args=ap.parse_args()
R=Path(__file__).resolve().parent;B=Path(os.environ['JKFLOW_BUILD_ROOT'])
W=B/'native248-build-wine';OLD=W/'prx';PROF=B/'native250-performance/prx'
BASE=B/'native266-package/prx';ORB=B/'orbis-win-9749fcd';SDK=ORB/'sdk/orbis-sdk-v1/sdk'
O=args.output or B/'native274-win32u-link-r2';assert O.parent.resolve()==B.resolve() and O.name.startswith('native274-win32u-link-');assert not O.exists();O.mkdir()
src=B/'native247-wine-source/dlls/win32u/vulkan.c'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
expected='17ff89382b02a1447ae7d53a8293d6d06696f70c4318b12a8fd7c02724403ace'
source_record=json.loads((R/'native274-source.json').read_text())
assert sha(src)==source_record['original_sha256']
assert args.source.resolve()!=src.resolve() and sha(args.source)==source_record['candidate_sha256']
assert sha(BASE/'win32u.shared.elf')==sha(PROF/'win32u.shared.elf')==expected
env=dict(os.environ,PW_SHARED_LIBC='1',PW_PRX_LINK='1',PW_PS4_ROOT=str(ORB),MESA_BUILD=str(B/'native248-platform'),OO_PS4_TOOLCHAIN=str(SDK))
env['PATH']=':'.join(str(p) for p in (W/'tools',B/'private-bin',B/'deps/rootfs/usr/bin'))+':'+env['PATH']
env['LLVM_OBJCOPY']=str(ORB/'build/ps4-wine-sdk/bin/llvm-objcopy')
stubs=OLD/'stubs';env['PW_PRX_STUBS']=' '.join(['-L'+str(stubs),'-Wl,--as-needed']+[str(p) for p in sorted(stubs.glob('*.so'))])
commands=[]
def run(argv,name,cwd=W/'build'):
    cmd=[str(x) for x in argv];p=subprocess.run(cmd,cwd=cwd,env=env,text=True,capture_output=True)
    (O/(name+'.log')).write_text(p.stdout+p.stderr)
    commands.append(dict(name=name,argv=cmd,exit=p.returncode));assert p.returncode==0,(name,p.stderr[-4000:])
    return p.stdout
text=(W/'make.log').read_text(errors='replace').replace('\\\n',' ')
line=[l for l in text.splitlines() if '-o dlls/win32u/win32u.so ' in l][-1]
objects=[v for v in shlex.split(line) if v.endswith('.o') and not v.startswith('/')]
assert objects.count('dlls/win32u/vulkan.o')==1
extra=[OLD/'heap/pw_wine_heap_libc.o',OLD/'obj/pw_wine_dl_libc.o']
extra += [PROF/'obj'/(s+'.o') for s in ('pw_vk_radv_profile','pw_vk_radv_profile_wrap','native250-performance-loader','native250-performance-log','win32u_desc')]
inputs={str(W/'build'/p):sha(W/'build'/p) for p in objects};inputs.update({str(p):sha(p) for p in extra});inputs[str(src)]=sha(src)
recipe=[ORB/'build/ps4-wine-sdk/bin/prospero-clang','-shared','-Wl,-Bsymbolic','-Wl,--eh-frame-hdr','-Wl,-soname,win32u.prx','-Wl,--warn-unresolved-symbols','-Wl,--wrap=dlopen','-Wl,--wrap=dlsym','-o',O/'control.elf',*objects,*extra]
run(recipe,'control-link');assert sha(O/'control.elf')==expected,'Actual protected control link did not reproduce'
query=run(['make','-s','-n','-o','Makefile','-o','config.status','-W',src,'dlls/win32u/vulkan.o'],'compile-recipe').replace('\\\n',' ')
cmd=shlex.split(next(l for l in query.splitlines() if ' -c ' in l and str(src) in l))
assert not any(x in cmd for x in ('-MD','-MMD','-MF','-MT')), 'Review dependency-output redirection before execution'
cmd[cmd.index(str(src))]=str(args.source);cmd[cmd.index('-o')+1]=str(O/'vulkan.o');cmd[1:1]=['-iquote',str(src.parent)]
run(cmd,'compile')
candidate=recipe.copy();candidate[candidate.index(O/'control.elf')]=O/'candidate.elf';candidate[candidate.index('dlls/win32u/vulkan.o')]=O/'vulkan.o'
run(candidate,'candidate-link')
run([ORB/'scripts/create-fself-repro.sh',SDK/'bin/linux/create-fself','-in',O/'candidate.elf','-out',O/'candidate.oelf','-lib',O/'win32u.prx','-libname','win32u.prx','-ptype','fake','-library-path',str(stubs)+':'+str(SDK/'lib')],'convert')
def symbols(path):
    und=set();defined=set()
    for l in subprocess.check_output(['llvm-readelf-18','--dyn-syms','--wide',str(path)],text=True).splitlines():
        f=l.split()
        if len(f)<8 or not f[0].rstrip(':').isdigit():continue
        if f[6]=='UND':
            if f[4]=='GLOBAL':und.add(f[7])
        else:defined.add(f[7])
    return und,defined
assert symbols(O/'control.elf')==symbols(O/'candidate.elf'),'Runtime imports/exports changed'
def segments(p):
    d=p.read_bytes();offset=struct.unpack_from('<Q',d,32)[0];size,count=struct.unpack_from('<HH',d,54)
    return [struct.unpack_from('<IIQQQQQQ',d,offset+i*size) for i in range(count)]
original=segments(O/'candidate.elf');converted=segments(O/'candidate.oelf');coverage=[]
for typ,flags,off,va,phys,filesz,mem,align in original:
    if typ!=1 or not flags&2:continue
    is_relro=any(t==0x6474e552 and start<=va and va+mem<=start+length for t,f,o,start,p,fs,length,a in original)
    matching=[(t,f,v,m) for t,f,o,v,p,fs,m,a in converted if v==va and
              ((t==1 and f&2) or (is_relro and t==0x61000010 and not f&2))]
    assert len(matching)==1,('Missing unique corresponding writable/converted-RELRO segment',hex(va),matching)
    t,f,v,m=matching[0];mapped=((v+m+0x3fff)&~0x3fff)
    assert mapped>=va+mem
    coverage.append(dict(original_va=hex(va),required_end=hex(va+mem),converted_type=hex(t),
                         converted_end=hex(v+m),mapped_end=hex(mapped),original_gnu_relro=is_relro))
assert all(sha(Path(p))==h for p,h in inputs.items()),'Protected link inputs changed'
for name in ('compile','candidate-link','control-link'):
    assert not re.search(r'(?im)(warning:|undefined symbol|unresolved)',(O/(name+'.log')).read_text()),name
record=dict(status='LOCAL_BUILD_PASS',candidate_source=str(args.source),candidate_source_sha256=sha(args.source),
    control_elf_sha256=sha(O/'control.elf'),candidate_elf=str(O/'candidate.elf'),candidate_elf_sha256=sha(O/'candidate.elf'),
    candidate_prx=str(O/'win32u.prx'),candidate_prx_sha256=sha(O/'win32u.prx'),candidate_object_sha256=sha(O/'vulkan.o'),
    imports_exports_unchanged=True,writable_coverage_passed=True,writable_coverage=coverage,inputs=inputs,commands=commands,
    PS4_tested=False,recreation_loop_observed=False,FPS_gain_proven=False)
(R/'native274-win32u-build.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:v for k,v in record.items() if k not in ('inputs','commands')},indent=2))

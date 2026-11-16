"""Relink a private combined module, proving the original control first."""
import argparse, hashlib, json, os, re, struct, subprocess
from pathlib import Path
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('assembled',type=Path)
parser.add_argument('out',type=Path)
args = parser.parse_args()
base = Path(os.environ['JKFLOW_BUILD_ROOT'])
working = base/'native248-build-wine'
orbis = base/'orbis-win-9749fcd'
sdk = orbis/'sdk/orbis-sdk-v1/sdk'
reference = base/'native263-event-link'
args.out.mkdir(parents=True,exist_ok=False)
(args.out/'empty-stubs').mkdir()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
original = json.loads((reference/'inputs-before.json').read_text())
assert all(sha(Path(p)) == expected for p,expected in original.items())
objects = args.assembled/'objects'
compiled = json.loads((objects/'compile-receipt.json').read_text())
assert all(row['returncode'] == 0 for row in compiled['objects'])
for name,expected in compiled['source_inputs'].items():
    assert sha(args.assembled/'dlls/ntdll/unix'/name) == expected
for row in compiled['objects']:
    assert sha(objects/(row['name']+'.o')) == row['sha256']
recipe = json.loads((reference/'link-receipt.json').read_text())['control_command']
env = os.environ.copy()
env.update(PW_PS4_ROOT=str(orbis),MESA_BUILD=str(base/'native248-platform'),OO_PS4_TOOLCHAIN=str(sdk),PW_PRX_LINK='1',PW_SHARED_LIBC='1',PW_PRX_STUBS='-L'+str(args.out/'empty-stubs')+' -Wl,--as-needed')
env['PATH'] = ':'.join(str(p) for p in (working/'tools',base/'private-bin',base/'deps/rootfs/usr/bin'))+':'+env['PATH']
def execute(cmd,log):
    result = subprocess.run(cmd,cwd=working/'build',env=env,text=True,capture_output=True)
    log.write_text(result.stdout+result.stderr)
    if result.returncode:
        raise RuntimeError(str(log)+'\n'+result.stderr[-3000:])
def command(name,combined=False,pad=None):
    cmd = recipe.copy()
    cmd[cmd.index('-o')+1] = str(args.out/(name+'.elf'))
    if combined:
        assert cmd.count('dlls/ntdll/unix/sync.o') == 1
        cmd[cmd.index('dlls/ntdll/unix/sync.o')] = str(objects/'runtime_qualified_backend.o')
        cmd += [str(objects/(row['name']+'.o')) for row in compiled['objects'] if row['name'] not in ('baseline_backend','runtime_qualified_backend')]
    if pad:
        cmd.append(str(pad))
    return cmd
control = command('control')
execute(control,args.out/'control.log')
assert sha(args.out/'control.elf') == 'b25d6d4df48f3b2c63574fac78ad8c8bbc08404220be1e3b579fe6fa046a29e2'
candidate = command('candidate',True)
execute(candidate,args.out/'candidate.log')
def writable(path):
    data = path.read_bytes(); offset=struct.unpack_from('<Q',data,32)[0]; size,count=struct.unpack_from('<HH',data,54)
    return [(va,mem) for i in range(count) for kind,flags,_,va,_,_,mem,_ in [struct.unpack_from('<IIQQQQQQ',data,offset+i*size)] if kind == 1 and flags & 2]
def convert():
    cmd = [str(orbis/'scripts/create-fself-repro.sh'),str(sdk/'bin/linux/create-fself'),'-in',str(args.out/'candidate.elf'),'-out',str(args.out/'candidate.oelf'),'-lib',str(args.out/'ntdll.prx'),'-libname','ntdll.prx','-ptype','fake','-library-path',str(working/'prx/stubs')+':'+str(sdk/'lib')]
    execute(cmd,args.out/'conversion.log')
    va,size = max(writable(args.out/'candidate.elf'),key=lambda row:sum(row))
    end = max(v+m for v,m in writable(args.out/'candidate.oelf') if v == va)
    return va+size,end,(end+0x3fff)&~0x3fff,cmd
required,end,mapped,conversion = convert()
padding = 0
if mapped < required:
    padding = (0x4100-(end&0x3fff))&0x3fff or 0x100
    source = args.out/'bss-pad.c'; source.write_text('static __attribute__((used,aligned(16))) char n264_pad['+str(padding)+'];\n')
    obj = args.out/'bss-pad.o'
    execute([recipe[0],'-std=gnu11','-O2','-fPIC','-fno-common','-c',str(source),'-o',str(obj)],args.out/'pad.log')
    candidate = command('candidate',True,obj)
    execute(candidate,args.out/'candidate.log')
    required,end,mapped,conversion = convert()
assert mapped >= required
def run(*cmd):return subprocess.check_output(cmd,text=True)
def symbols(path):
    undefined,defined=set(),set()
    for line in run('llvm-readelf-18','--dyn-syms','--wide',str(path)).splitlines():
        fields=line.split()
        if len(fields)<8 or not fields[0].rstrip(':').isdigit():continue
        name=fields[7].split('@')[0]
        if fields[6]=='UND':
            if fields[4]=='GLOBAL':undefined.add(name)
        else:defined.add(name)
    return undefined,defined
old_u,old_d=symbols(args.out/'control.elf'); new_u,new_d=symbols(args.out/'candidate.elf')
assert old_d == new_d,('exports changed',sorted(new_d-old_d),sorted(old_d-new_d))
provided=set();providers=[]
for name in re.findall(r'Shared library: \[(.*?)\]',run('llvm-readelf-18','-d',str(args.out/'candidate.elf'))):
    path=next((p for p in (working/'prx/stubs'/name,sdk/'lib'/name) if p.is_file()),None)
    assert path,name
    provided |= symbols(path)[1]
    providers.append(dict(name=name,path=str(path),sha256=sha(path)))
assert not new_u-provided,sorted(new_u-provided)
assert all(sha(Path(p)) == expected for p,expected in original.items()),'original input changed'
result=dict(status='PASS',control_sha256=sha(args.out/'control.elf'),candidate_elf_sha256=sha(args.out/'candidate.elf'),candidate_prx_sha256=sha(args.out/'ntdll.prx'),added_imports=sorted(new_u-old_u),exports_unchanged=True,providers=providers,writable_required=hex(required),writable_converted=hex(end),writable_mapped=hex(mapped),padding=padding,baseline_inputs_unchanged=True,control_command=control,candidate_command=candidate,conversion_command=conversion,hardware_qualified=False,scope='local link/container; opt-in runtime must qualify native API before selecting new backend')
(args.out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('control_command','candidate_command','conversion_command','providers')},indent=2))

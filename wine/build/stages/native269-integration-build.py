"""Merge two pinned patches and link private PE DLL/control pairs without make mutations."""
from pathlib import Path
import difflib,hashlib,json,os,shlex,shutil,subprocess,sys
repo=Path(sys.argv[1]); base=Path(os.environ['JKFLOW_BUILD_ROOT'])
source=base/'native247-wine-source';build=base/'native248-build-wine/build'
out=base/'native269-integration';assert not out.exists();out.mkdir()
reports=repo/'research/native-wine'; artifacts=reports/'native269-integration-artifacts';assert not artifacts.exists();artifacts.mkdir()
env=os.environ.copy();env['PATH']=str(base/'private-bin')+':'+str(base/'deps/rootfs/usr/bin')+':'+env['PATH']
(out/'tmp').mkdir();env['TMPDIR']=str(out/'tmp')
commands=[];inputs={};outputs={}
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pin(p):
 p=Path(p);inputs[str(p)]={'sha256':digest(p),'bytes':p.stat().st_size}
def run(args,cwd=None):
 p=subprocess.run(args,cwd=cwd,env=env,capture_output=True,text=True)
 commands.append({'command':args,'cwd':str(cwd),'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
 (reports/'native269-integration-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 if p.returncode:print(p.stdout+p.stderr);raise SystemExit(p.returncode)
 return p.stdout
original=source/'dlls/winevulkan/pw_vk_batch_pe.c';pin(original)
assert digest(original)=='ad1b25cbbdfcf79a3de850d383d40da0a1ea9c70474fc1ad1761b7d4b86d0058'
d=out/'source/dlls/winevulkan';d.mkdir(parents=True);shutil.copyfile(original,d/original.name)
patches=[('native269-registry.patch','51d5f5e4a1c3444f0c82c19e150081b5430080ddf8ba3eb24a875bc8ad79cd08'),('native269-query-profile.patch','f47ec88de4a1ca233a2e1301a8615b0a27d3ff719c5f833c6b066e051e3ed44c')]
for name,sha in patches:
 patch=reports/name;assert digest(patch)==sha;pin(patch)
 run(['patch','--batch','--fuzz=0','-p1','-i',str(patch)],out/'source')
candidate=d/original.name
shutil.copyfile(candidate,reports/'native269-integration-pw_vk_batch_pe.c')
(reports/'native269-integration.patch').write_text(''.join(difflib.unified_diff(original.read_text().splitlines(True),candidate.read_text().splitlines(True),fromfile='a/dlls/winevulkan/pw_vk_batch_pe.c',tofile='b/dlls/winevulkan/pw_vk_batch_pe.c')))
for arch in ('i386-windows','x86_64-windows'):
 target='dlls/winevulkan/'+arch+'/pw_vk_batch_pe.o'
 recipe=run(['make','-s','-n','-o','Makefile','-o','config.status','-W',str(original),target],build)
 rows=[shlex.split(x) for x in recipe.replace('\\\n',' ').splitlines() if ' -o '+target+' ' in x];assert len(rows)==1
 obj=out/(arch+'-pw_vk_batch_pe.o');args=rows[0];args[args.index(str(original))]=str(candidate);args[args.index('-o')+1]=str(obj)
 run(args,build)
 dlltarget='dlls/winevulkan/'+arch+'/winevulkan.dll'
 recipe=run(['make','-s','-n','-o','Makefile','-o','config.status','-W',str(original),dlltarget],build)
 rows=[shlex.split(x) for x in recipe.replace('\\\n',' ').splitlines() if ' -o '+dlltarget+' ' in x and 'winegcc' in x];assert len(rows)==1
 for kind in ('control','candidate'):
  destination=out/kind/arch;destination.mkdir(parents=True);dll=destination/'winevulkan.dll'
  args=rows[0][:];args[args.index('-o')+1]=str(dll);args[args.index('--wine-objdir')+1]=str(build)
  for i,arg in enumerate(args):
   if arg.startswith(('dlls/','libs/')):
    dependency=build/arg;pin(dependency)
    args[i]=str(obj) if kind=='candidate' and arg==target else str(dependency)
   elif arg.startswith('/') and Path(arg).is_file():pin(arg)
  # Run linker in a private cwd, with private temp directory and absolute read-only inputs.
  run(args,destination)
  stripped=destination/'stripped/winevulkan.dll';stripped.parent.mkdir();shutil.copyfile(dll,stripped)
  run(['llvm-strip-18','--strip-debug',str(stripped)],destination)
  outputs[kind+'/'+arch]={'unstripped':str(dll),'sha256':digest(dll),'stripped':str(stripped),'stripped_sha256':digest(stripped)}
  if kind=='candidate':
   dest=artifacts/arch/'winevulkan.dll';dest.parent.mkdir();shutil.copyfile(stripped,dest)
 print('linked',arch,'control and candidate',flush=True)

# Real PE32 adapter fixtures. Reuse only the exact previously built generated-codec object.
generated=reports/'native269-query-candidate/generated.o';pin(generated)
for p in (source/'dlls/winevulkan').glob('pw_vk_*.h'):pin(p)
fixtureout=out/'fixtures';fixtureout.mkdir()
common=[str(source/'dlls/winevulkan'/(n+'.c')) for n in ('pw_vk_command_stream','pw_vk_spsc','pw_vk_wire','pw_vk_template_cache','pw_vk_retire','pw_vk_codec')]
for name,p in [('query',reports/'native269-query-fixture.c'),('progress',reports/'shared-prospero-108194e/tests/lab/vk_batch_progress.c'),('query-progress',reports/'native269-query-checks/concurrent/query-progress.c')]:
 pin(p);fixture=fixtureout/(name+'.c');shutil.copyfile(p,fixture)
 args=['i686-w64-mingw32-gcc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-D__WINESRC__','-I'+str(d),'-I'+str(source/'include'),'-I'+str(source/'dlls/winevulkan'),str(fixture),*common,str(generated),'-o',str(fixtureout/(name+'.exe'))]
 run(args,fixtureout);shutil.copyfile(fixtureout/(name+'.exe'),artifacts/(name+'.exe'))
for path,data in inputs.items():assert digest(path)==data['sha256'],'read-only input changed: '+path
receipt={'base_source_sha256':digest(original),'merged_source_sha256':digest(candidate),'inputs':inputs,'outputs':outputs,'scope':'Private link/control and PE32 fixture builds; no package/console/Unix PRX changes'}
(reports/'native269-integration-build.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS all private links/builds; read-only input hashes unchanged')

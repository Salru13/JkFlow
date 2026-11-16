"""Compile private assembled sync.c under both backend configurations."""
import argparse, hashlib, json, shlex, subprocess
import os  # JKFLOW_BUILD_ROOT
from pathlib import Path
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('assembled',type=Path)
args = parser.parse_args()
base = Path(os.environ['JKFLOW_BUILD_ROOT'])
source = base/'native247-wine-source/dlls/ntdll/unix/sync.c'
build = base/'native248-build-wine/build'
directory = args.assembled/'dlls/ntdll/unix'
out = args.assembled/'objects'
out.mkdir(exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {p.name:sha(p) for p in directory.iterdir() if p.is_file()}
query = subprocess.check_output(['make','-s','-n','-o','Makefile','-o','config.status','-W',str(source),'dlls/ntdll/unix/sync.o'],cwd=build,text=True).replace('\\\n',' ')
lines = [line for line in query.splitlines() if ' -c ' in line and str(source) in line]
assert len(lines) == 1
command = shlex.split(lines[0])
command[command.index(str(source))] = str(directory/'sync.c')
command[1:1] = ['-iquote',str(source.parent)]
receipts = []
for name, enabled in [('baseline_backend',0),('runtime_qualified_backend',1)]:
    cmd = command.copy()
    obj = out/(name+'.o')
    cmd[cmd.index('-o')+1] = str(obj)
    cmd.append('-DPW_ORBIS_ALERT_UMTX_QUALIFIED='+str(enabled))
    result = subprocess.run(cmd,cwd=build,text=True,capture_output=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    receipts.append(dict(name=name,returncode=result.returncode,command=cmd,sha256=sha(obj) if obj.exists() else None))
for name in ('native263-qualification','native263-qualification-sdk','native263-native','native263-native-platform','native264-qual-report'):
    path = directory/(name+'.c')
    if not path.exists():
        continue
    cmd = [arg for arg in command if arg != '-Wdeclaration-after-statement']
    cmd[cmd.index(str(directory/'sync.c'))] = str(path)
    obj = out/(name+'.o')
    cmd[cmd.index('-o')+1] = str(obj)
    cmd += ['-Werror']
    result = subprocess.run(cmd,cwd=build,text=True,capture_output=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    receipts.append(dict(name=name,returncode=result.returncode,command=cmd,sha256=sha(obj) if obj.exists() else None))
assert before == {p.name:sha(p) for p in directory.iterdir() if p.is_file()}
receipt = dict(source_inputs=before,objects=receipts,scope='SDK object builds only; runtime opt-in plus verified native qualification still required')
(out/'compile-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({r['name']:r['returncode'] for r in receipts}))
raise SystemExit(any(r['returncode'] for r in receipts))

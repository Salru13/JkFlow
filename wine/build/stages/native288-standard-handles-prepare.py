from pathlib import Path
import json,hashlib,subprocess,shutil
import os  # JKFLOW_BUILD_ROOT
R=Path(__file__).resolve().parent;B=Path(os.environ['JKFLOW_BUILD_ROOT']);D=B/'native288-standard-handles';S=B/'native247-wine-source/dlls/ntdll/unix/env.c'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(S)=='d191fe5f0f1dd6cca073ea93330449e82b15a7f245b54a531e233271ceebb827'
red=json.loads((R/'native288-standard-handles-tests-baseline.json').read_text());assert red['rows'][1]['expected_failure'] and red['rows'][1]['exit']!=0
compare=R/'native284-upstream-review-input.json';rows=json.loads(compare.read_text());file=next(f for x in rows for f in x['files'] if f['filename'].endswith('1075-ntdll-ps4-no-standard-handles.patch'))
lines=file['patch'].splitlines();assert lines[0].startswith('@@ -0,0');patch='\n'.join(x[1:] for x in lines[1:] if x.startswith('+'))+'\n'
assert 'PW_STD_HANDLES' in patch and 'return;' in patch
patchfile=R/'native288-standard-handles-upstream1075.patch';patchfile.write_text(patch)
dst=D/'source/dlls/ntdll/unix/env.c';dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(S,dst)
p=subprocess.run(['patch','--fuzz=0','-p1','--batch'],cwd=D/'source',input=patch,text=True,capture_output=True);assert not p.returncode,p.stdout+p.stderr
shutil.copyfile(dst,R/'native288-standard-handles-env.c')
result=dict(original=dict(path=str(S),sha256=sha(S)),candidate=dict(path=str(dst),sha256=sha(dst)),upstream_compare_sha256=sha(compare),patch_sha256=sha(patchfile),upstream_url=file['raw_url'],apply_result=p.stdout+p.stderr,scope='Exact1075 only; no1074/server or other source changes')
(R/'native288-standard-handles-inputs.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

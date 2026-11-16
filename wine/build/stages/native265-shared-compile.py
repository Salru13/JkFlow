"""Private single-object compile from configured baseline Wine11 flags."""
import pathlib,shlex,subprocess,json,hashlib,sys
import os  # JKFLOW_BUILD_ROOT
out=pathlib.Path(sys.argv[1]);b=pathlib.Path(os.environ['JKFLOW_BUILD_ROOT']);src=b/'native247-wine-source/dlls/ntdll/unix/server.c';build=b/'native248-build-wine/build'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before=sha(src)
r=subprocess.run(['make','-n','-W',str(src),'dlls/ntdll/unix/server.o'],cwd=build,text=True,capture_output=True,check=True)
cmd=shlex.split(r.stdout.replace('\\\n',' '));cmd[cmd.index(str(src))]=str(out/'server.c');cmd[cmd.index('-o')+1]=str(out/'server.o');cmd+=['-iquote',str(src.parent)]
r=subprocess.run(cmd,cwd=build,text=True,capture_output=True);(out/'compile.log').write_text(r.stdout+r.stderr)
assert sha(src)==before
receipt={'command':cmd,'returncode':r.returncode,'input_sha256':before,'input_unchanged':True,'candidate_sha256':sha(out/'server.c'),'object_sha256':sha(out/'server.o') if (out/'server.o').exists() else None}
(out/'compile-receipt.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt,indent=2));sys.exit(r.returncode)

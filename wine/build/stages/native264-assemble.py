"""Assemble reviewed source changes in a new directory; baseline is read-only.

Do not compile from live shared component files. The emitted manifest pins the
private copies, including the authoritative qualification ABI header.
"""
import argparse, hashlib, json, subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
here = Path(__file__).resolve().parent
base = here/'native251-current-sync-source/dlls/ntdll/unix/sync.c'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(base) == '34dfa5599ee672e9d8b5f69aba5700b5a4a5a3f147c6347ae2bd376c68c5757d'
inputs = [base] + [here/name for name in ('native263-event.patch','native263-alert-candidate.patch','native263-qualification.h','native263-qualification.c','native263-qualification-sdk.c','native263-native.c','native263-native-platform.c','native264-qual-report.c')]
before = {str(p):sha(p) for p in inputs}
args.out.mkdir(parents=True,exist_ok=False)
directory = args.out/'dlls/ntdll/unix'
directory.mkdir(parents=True)
(directory/'sync.c').write_bytes(base.read_bytes())
for patch in inputs[1:3]:
    sections = patch.read_text().split('\n--- ')
    filtered = []
    for i, section in enumerate(sections):
        text = section if i == 0 else '--- '+section
        first = text.splitlines()[0]
        if first not in ('--- a/dlls/ntdll/unix/sync.c','--- a/dlls/ntdll/unix/pw_orbis_alert_umtx.h','--- a/dlls/ntdll/unix/native263-qualification.h','--- /dev/null'):
            raise ValueError('Unexpected patch target '+first)
        # The current shared ABI is authoritative; never apply the older
        # frozen header embedded by the alert patch.
        if '+++ b/dlls/ntdll/unix/native263-qualification.h' in text:
            continue
        filtered.append(text)
    local = args.out/patch.name
    local.write_text('\n'.join(filtered).rstrip('\n')+'\n',newline='\n')
    result = subprocess.run(['patch','--batch','--fuzz=0','-p1','-i',str(local.resolve())],cwd=args.out,text=True,capture_output=True)
    (args.out/(patch.name+'.apply.log')).write_text(result.stdout+result.stderr)
    if result.returncode:
        raise SystemExit(result.stdout+result.stderr)
for source in inputs[3:]:
    (directory/source.name).write_bytes(source.read_bytes())
adapter = directory/'pw_orbis_alert_umtx.h'
text = adapter.read_text()
anchor = 'return abi && abi->schema == 1'
assert text.count(anchor) == 1
text = text.replace(anchor,'return abi && abi->word_bits == 32 && abi->schema == 1')
adapter.write_text(text,newline='\n')
bridge = directory/'native263-native.c'
text = bridge.read_text()
anchor = 'int n263_native_opt_in(void);'
assert text.count(anchor) == 1
text = text.replace(anchor,'void n264_report_qualification(const struct n263_state *, int, int, int);\n'+anchor)
anchor = '''int good = !n263_native_prepare(&binding, &ops) &&
                   atomic_load_explicit(&phase, memory_order_acquire) == RUNNING &&
                   !n263_qualify(&retained, &binding, &ops) &&'''
assert text.count(anchor) == 1
text = text.replace(anchor,'''int prepared = !n263_native_prepare(&binding, &ops);
        int probed = prepared && atomic_load_explicit(&phase, memory_order_acquire) == RUNNING;
        int good = probed && !n263_qualify(&retained, &binding, &ops) &&''')
anchor = '        return good ? &retained.abi : 0;'
assert text.count(anchor) == 1
text = text.replace(anchor,'        n264_report_qualification(&retained, prepared, probed, good);\n'+anchor)
bridge.write_text(text,newline='\n')
assert before == {str(p):sha(p) for p in inputs}, 'Component changed while assembling'
manifest = dict(baseline_sha256=before[str(base)],inputs=before,outputs={p.relative_to(args.out).as_posix():sha(p) for p in directory.iterdir() if p.is_file()},authoritative_abi='native263-qualification.h from current qualification component, word_bits32 required',enabled_on_console=False,scope='isolated source assembly; not a build, package or FPS proof')
(args.out/'assembly.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))

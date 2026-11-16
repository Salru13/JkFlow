"""Compose the qualified native264 module with actual fast-path reason counters."""
from pathlib import Path
import hashlib,json,subprocess,sys
import os  # JKFLOW_BUILD_ROOT
here=Path(__file__).resolve().parent
base=Path(os.environ['JKFLOW_BUILD_ROOT'])
stage=base/'native265-shared-r1'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt=json.loads((stage/'compile-receipt.json').read_text())
assert receipt['returncode']==0 and receipt['input_unchanged']
assert sha(stage/'server.c')==receipt['candidate_sha256']=='aa33b601ff8fd2e1e43ea5dde5c3c3f7790080e7a4d4c364dc34b902b0e89043'
assert sha(stage/'server.o')==receipt['object_sha256']=='3eccf792b591c33806462f7e249897d355b8c34b88ce16c36f0a1c37f2d07a80'
assert sha(base/'native247-wine-source/dlls/ntdll/unix/server.c')==receipt['input_sha256']
text=(here/'native264-link.py').read_text()
needle="        assert cmd.count('dlls/ntdll/unix/sync.o') == 1"
assert text.count(needle)==1
text=text.replace(needle,"        assert cmd.count('dlls/ntdll/unix/server.o') == 1\n        cmd[cmd.index('dlls/ntdll/unix/server.o')] = '$JKFLOW_BUILD_ROOT/native265-shared-r1/server.o'\n"+needle)
text=text.replace("scope='local link/container; opt-in runtime must qualify native API before selecting new backend'", "scope='local combined264+265 link; shared selection and reason diagnostics default off; no physical gain claim'")
driver=base/'native266-ntdll-driver.py'
assert not driver.exists()
driver.write_text(text)
subprocess.run([sys.executable,str(driver),str(base/'native264-assembled-r5'),str(base/'native266-ntdll-link-r1')],check=True)

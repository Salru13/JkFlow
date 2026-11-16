"""Build frozen r4 and link privately; never writes the known working build."""
import subprocess, sys
import os  # JKFLOW_BUILD_ROOT
from pathlib import Path
here = Path(__file__).resolve().parent
base = Path(os.environ['JKFLOW_BUILD_ROOT'])
assembled = base/'native264-assembled-r5'
subprocess.run([sys.executable,str(here/'native264-compile.py'),str(assembled)],check=True)
subprocess.run([sys.executable,str(here/'native264-link.py'),str(assembled),str(base/'native264-runtime-link-r2')],check=True)

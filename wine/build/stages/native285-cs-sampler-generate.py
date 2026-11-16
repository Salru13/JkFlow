from pathlib import Path
import difflib,hashlib,json,shutil
import os  # JKFLOW_BUILD_ROOT
R=Path(__file__).resolve().parent;B=Path(os.environ['JKFLOW_BUILD_ROOT']);D=B/'native285-cs-sampler';D.mkdir(exist_ok=True)
src=B/'native247-wine-source/dlls/ntdll/unix';pins={}
for name in ['signal_x86_64.c','thread.c']:
 original=(src/name).read_text();s=original
 if name=='signal_x86_64.c':
  marker='/**********************************************************************\n *\t\tsignal_init_process'
  assert s.count(marker)==1
  s=s.replace(marker,'#include "native285-cs-sampler-impl.h"\n\n'+marker)
  marker='    __atomic_store_n( &pw_native_caps, pw_native_ready_caps(), __ATOMIC_RELEASE );'
  assert s.count(marker)==1;s=s.replace(marker,marker+'\n    pw_cs_init();')
 else:
  marker='static void set_native_thread_name( HANDLE handle, const UNICODE_STRING *name )\n{'
  assert s.count(marker)==1;s=s.replace(marker,marker+'\n#ifdef __PROSPERO__\n    pw_cs_sampler_name(handle, name);\n#endif')
  marker='    if (InterlockedDecrement( &nb_threads ) <= 0) abort_process( status );\n    pthread_exit_wrapper( status );';assert s.count(marker)==1
  s=s.replace(marker,'    if (InterlockedDecrement( &nb_threads ) <= 0) abort_process( status );\n#ifdef __PROSPERO__\n    pw_cs_sampler_abort();\n#endif\n    pthread_exit_wrapper( status );')
  marker='    pthread_exit_wrapper( status );';parts=s.split(marker);assert len(parts)==4
  s=parts[0]+marker+''.join(part+'#ifdef __PROSPERO__\n    pw_cs_sampler_exit();\n#endif\n'+marker for part in parts[1:3])+parts[3]
  marker='            pthread_join( data->pthread_id, NULL );';assert s.count(marker)==1
  s=s.replace(marker,'#ifdef __PROSPERO__\n            pw_cs_sampler_before_reap(data);\n#endif\n'+marker)
  marker='#include "unix_private.h"';assert s.count(marker)==1
  s=s.replace(marker,marker+'\n#ifdef __PROSPERO__\nextern void pw_cs_sampler_name(HANDLE, const UNICODE_STRING *);\nextern void pw_cs_sampler_exit(void);\nextern void pw_cs_sampler_abort(void);\nextern void pw_cs_sampler_before_reap(struct thread_data *);\n#endif')
 (D/name).write_text(s);(R/('native285-cs-sampler-'+name)).write_text(s)
 (R/('native285-cs-sampler-'+name+'.patch')).write_text(''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='a/dlls/ntdll/unix/'+name,tofile='b/dlls/ntdll/unix/'+name)))
 pins[name]=dict(original=str(src/name),original_sha256=hashlib.sha256((src/name).read_bytes()).hexdigest(),candidate_sha256=hashlib.sha256((D/name).read_bytes()).hexdigest())
for n in ['native285-cs-sampler-core.h','native285-cs-sampler-impl.h']:shutil.copyfile(R/n,D/n)
(R/'native285-cs-sampler-source-pins.json').write_text(json.dumps(pins,indent=2)+'\n');print(json.dumps(pins,indent=2))

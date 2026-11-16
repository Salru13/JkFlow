#!/usr/bin/env python3
"""Build winevulkan.prx with an asynchronous Vulkan replay worker.

The deployed 32-bit winevulkan.dll already negotiates batch version 3
(__wine_pw_vk_batch_async_v2). This adds the missing Unix half:

  * vulkan.c answers that capability when PW_VK_ASYNC_REPLAY=1. Unset means
    the old synchronous backend, so one package can A/B both.
  * pw_vk_batch_unix handles version 3 by handing batches to one worker thread.

Ordering rule: every record runs in its original order. A caller may overtake
pending records only for vkAllocateDescriptorSets. All other synchronous calls,
and flushes, wait for the worker first. A batch holding anything but direct
vkCmd* forwarders or descriptor-template updates waits for the worker and then
runs on the calling thread as before, so Wine-TLS users never reach the worker.

Run in WSL. Controls: an unpatched relink must reproduce the admitted ELF and
PRX byte for byte, and unpatched vulkan.c must compile to the admitted code.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
NATIVE = HERE.parent
BASE = Path(os.environ['JKFLOW_BUILD_ROOT'])
ORBIS = BASE / 'orbis-win-9749fcd'
SDK = ORBIS / 'sdk/orbis-sdk-v1/sdk'
WINE = BASE / 'native247-wine-source'
OUT = BASE / 'native288-async260'
ELF_SHA = 'db36ff502aa58b3a9839419e3c0dafc76a7baf6dbc4e10b0c31057ff8d44198d'
PRX_SHA = '42dac77c38a182f8e7548e72f73bfde5daff169508bc598f90e8b9ab86cd61d2'
BASE_SOURCE = NATIVE / 'native280-decoder-config-candidate.c'

ASYNC_CODE = r'''
/* Asynchronous replay (batch version 3). See asyncbuild260.py for the rules. */
#include "pw_vk_async_core.h"
#include "pw_vk_async_eligible.h"
#include <stdio.h>
#include <time.h>
#define PW_VK_ASYNC_RING (8u*1024u*1024u)

static NTSTATUS replay_bytes(void *bytes,size_t size)
{
 struct replay_context ctx;size_t completed;NTSTATUS status=STATUS_SUCCESS;
 ctx.version=PW_VK_BATCH_VERSION;ctx.arena=ctx.local;ctx.capacity=sizeof(ctx.local);ctx.failure=STATUS_SUCCESS;
 if(pw_vk_stream_replay(bytes,size,preflight,&ctx,&completed)!=PW_VK_STREAM_OK)status=ctx.failure?ctx.failure:STATUS_INVALID_PARAMETER;
 else if(pw_vk_stream_replay(bytes,size,replay,&ctx,&completed)!=PW_VK_STREAM_OK)status=STATUS_UNSUCCESSFUL;
 if(ctx.arena&&ctx.arena!=ctx.local)free(ctx.arena);
 return status;
}
static int async_run(void *context,const void *bytes,size_t size)
{
 (void)context;return replay_bytes((void *)bytes,size)!=STATUS_SUCCESS;
}
struct async_scan { unsigned blocked,code; };
static int async_scan_record(void *context,const struct pw_vk_stream_record *r)
{
 struct async_scan *s=context;unsigned code;
 if(r->opcode!=PW_VK_BATCH_GENERATED_OPCODE)return 0;
 if(r->payload_bytes<4){s->blocked=1;return 1;}
 memcpy(&code,r->payload,4);
 if(pw_vk_async_code_eligible(code))return 0;
 s->blocked=1;s->code=code;return 1;
}

static struct pw_async async_queue;
static pthread_once_t async_once=PTHREAD_ONCE_INIT;
static int async_ready;
static void async_start(void){async_ready=!pw_async_start(&async_queue,PW_VK_ASYNC_RING,async_run,NULL);}

static unsigned async_mode; /* 0 unknown, 1 off, 2 on */
int pw_vk_async_enabled(void)
{
 unsigned mode=__atomic_load_n(&async_mode,__ATOMIC_ACQUIRE);
 if(!mode){
  const char *value=getenv("PW_VK_ASYNC_REPLAY");
  mode=value&&value[0]=='0'&&!value[1]?1:2; /* on by default; PW_VK_ASYNC_REPLAY=0 turns it off */
  __atomic_store_n(&async_mode,mode,__ATOMIC_RELEASE);
  __wine_dbg_output(mode==2?"PW_VK_ASYNC_MODE version=1 enabled=1\n":"PW_VK_ASYNC_MODE version=1 enabled=0\n");
 }
 return mode==2;
}

/* All counters are touched only by callers, which the PE gate serializes. */
static struct { uint64_t batches,queued,inline_batches,no_wait,waits,wait_ns,bytes; uint64_t blocked[unix_count+2],waited[unix_count+2],waited_ns[unix_count+2]; } async_stats;
static int async_stats_on=-1;
static uint64_t async_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static int async_wait(unsigned code)
{
 uint64_t start,spent;int failed;
 if(!async_stats_on)return pw_async_wait_all(&async_queue);
 start=async_now();failed=pw_async_wait_all(&async_queue);spent=async_now()-start;
 async_stats.waits++;async_stats.wait_ns+=spent;
 if(code<=unix_count+1){async_stats.waited[code]++;async_stats.waited_ns[code]+=spent;}
 return failed;
}
static void async_report(void)
{
 char line[320];unsigned i,top=0;uint64_t best=0;
 for(i=0;i<=unix_count;i++)if(async_stats.blocked[i]>best){best=async_stats.blocked[i];top=i;}
 snprintf(line,sizeof(line),"PW_VK_ASYNC batches=%llu queued=%llu inline=%llu no_wait=%llu waits=%llu wait_ms=%llu kb=%llu top_blocked_code=%u top_blocked=%llu\n",
  (unsigned long long)async_stats.batches,(unsigned long long)async_stats.queued,(unsigned long long)async_stats.inline_batches,(unsigned long long)async_stats.no_wait,
  (unsigned long long)async_stats.waits,(unsigned long long)(async_stats.wait_ns/1000000),(unsigned long long)(async_stats.bytes/1024),top,(unsigned long long)best);
 __wine_dbg_output(line);
 {
  /* The four calls that cost the most waiting since the last report. */
  char top_line[300];size_t used=snprintf(top_line,sizeof(top_line),"PW_VK_ASYNC_WAITS");unsigned n;
  for(n=0;n<4;n++){
   unsigned best_code=0;uint64_t best_ns=0;
   for(i=0;i<=unix_count+1;i++)if(async_stats.waited_ns[i]>best_ns){best_ns=async_stats.waited_ns[i];best_code=i;}
   if(!best_ns)break;
   used+=snprintf(top_line+used,sizeof(top_line)-used," code=%u n=%llu ms=%llu",best_code,(unsigned long long)async_stats.waited[best_code],(unsigned long long)(best_ns/1000000));
   async_stats.waited_ns[best_code]=0;async_stats.waited[best_code]=0;
  }
  snprintf(top_line+used,sizeof(top_line)-used,"\n");
  __wine_dbg_output(top_line);
 }
}
static __attribute__((noinline)) NTSTATUS async_batch(void *args)
{
 struct pw_vk_batch_params *p=args;void *bytes=UlongToPtr(p->batch);
 if(p->bytes>PW_VK_BATCH_SCRATCH||p->code>unix_count+1||p->code==unix_pw_vk_batch)return STATUS_INVALID_PARAMETER;
 pthread_once(&async_once,async_start);
 if(async_stats_on<0){const char *v=getenv("PW_VK_ASYNC_STATS");async_stats_on=v&&v[0]=='1'&&!v[1];}
 p->status=STATUS_SUCCESS;
 if(p->bytes){
  struct async_scan scan={0,0};size_t completed;
  int ok=pw_vk_stream_replay(bytes,p->bytes,async_scan_record,&scan,&completed)==PW_VK_STREAM_OK&&!scan.blocked;
  if(async_stats_on){async_stats.batches++;async_stats.bytes+=p->bytes;}
  if(async_ready&&ok&&pw_async_fits(&async_queue,p->bytes)){
   pw_async_submit(&async_queue,bytes,p->bytes);
   if(async_stats_on)async_stats.queued++;
  }else{
   NTSTATUS status;
   if(async_ready&&async_wait(p->code))return STATUS_UNSUCCESSFUL;
   if((status=replay_bytes(bytes,p->bytes)))return status;
   if(async_stats_on){async_stats.inline_batches++;if(scan.blocked&&scan.code<=unix_count)async_stats.blocked[scan.code]++;}
  }
  if(async_stats_on&&!(async_stats.batches&0x3fff))async_report();
 }
 /* Both private sentinels are flush-only and must never reach the dispatcher. */
 if(p->code>=unix_count){
  if(async_ready&&async_wait(p->code))return STATUS_UNSUCCESSFUL;
  return STATUS_SUCCESS;
 }
 /* These only create new objects from ones that already exist, so they may
  * overtake queued records. Pending destruction cannot free a handle they would
  * receive, because the old object still exists until the worker runs it. */
 if(p->code==unix_vkAllocateDescriptorSets||p->code==unix_vkCreateFramebuffer){if(async_stats_on)async_stats.no_wait++;}
 else if(async_ready&&async_wait(p->code))return STATUS_UNSUCCESSFUL;
 p->status=pw_vk_batch_dispatch(p->code,UlongToPtr(p->args));
 return STATUS_SUCCESS;
}

'''

ENTRY_OLD = 'NTSTATUS pw_vk_batch_unix(void *args)\n{\n'
ENTRY_NEW = ('NTSTATUS pw_vk_batch_unix(void *args)\n{\n'
             ' if(((struct pw_vk_batch_params *)args)->version==PW_VK_BATCH_ASYNC_VERSION)return async_batch(args);\n')
CAP_OLD = '    if (!strcmp(name, PW_VK_BATCH_NAME)) return PW_VK_BATCH_CAPABILITY;'
CAP_NEW = ('    if (!strcmp(name, PW_VK_BATCH_ASYNC_NAME)) { extern int pw_vk_async_enabled(void); '
           'if (pw_vk_async_enabled()) return PW_VK_BATCH_ASYNC_CAPABILITY; }\n' + CAP_OLD)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


STUBS = BASE / 'native248-build-wine/prx/stubs'
ENV = dict(os.environ, MESA_BUILD=str(BASE / 'native248-platform'), OO_PS4_TOOLCHAIN=str(SDK), PW_PRX_LINK='1',
           PW_SHARED_LIBC='1', LLVM_OBJCOPY='llvm-objcopy-18',
           PW_PRX_STUBS='-L' + str(STUBS) + ' -Wl,--as-needed ' + ' '.join(str(STUBS / (x + '.so')) for x in ('ntdll', 'win32u', 'wineserver')))
ENV['PATH'] = str(BASE / 'private-bin') + ':' + str(BASE / 'deps/rootfs/usr/bin') + ':' + ENV['PATH']


def run(command, cwd):
    result = subprocess.run([str(a) for a in command], cwd=cwd, env=ENV, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f'failed: {command[0]}\n{result.stdout[-1500:]}\n{result.stderr[:3000]}')
    return result


def text_digest(obj):
    target = OUT / (Path(obj).name + '.' + hashlib.sha1(str(obj).encode()).hexdigest()[:8] + '.text')
    subprocess.run(['llvm-objcopy-18', '-O', 'binary', '--only-section=.text', str(obj), str(target)], check=True)
    return sha(target)


def writable(path):
    data = Path(path).read_bytes()
    offset = int.from_bytes(data[32:40], 'little')
    size = int.from_bytes(data[54:56], 'little')
    count = int.from_bytes(data[56:58], 'little')
    rows = []
    for i in range(count):
        p = data[offset + i * size:offset + (i + 1) * size]
        if int.from_bytes(p[0:4], 'little') == 1 and int.from_bytes(p[4:8], 'little') & 2:
            rows.append((int.from_bytes(p[16:24], 'little'), int.from_bytes(p[40:48], 'little')))
    return rows


# (original source, patched source, object name in the link) swapped in the candidate only, each
# after its own control compile reproduces the admitted .text. Empty for asyncbuild260/asynccb260.
EXTRA_OBJECTS = []

EXTRA_DEVICE_CALLS = ('vkDestroyFramebuffer', 'vkDestroyShaderModule', 'vkResetQueryPool', 'vkResetQueryPoolEXT')


def verified_device_forwarders():
    """Accept a device-level call only if its 64-bit thunk is one direct driver call."""
    source = (WINE / 'dlls/winevulkan/vulkan_thunks.c').read_text()
    accepted = []
    for name in EXTRA_DEVICE_CALLS:
        match = re.search(r'static NTSTATUS thunk64_' + name + r'\(void \*args\)\s*\{(.*?)\n\}', source, re.S)
        assert match, name
        body = [line.strip() for line in match[1].strip().splitlines() if line.strip()]
        assert len(body) == 4 and body[0].startswith('struct ' + name + '_params *params = args;'), (name, body)
        assert body[1].startswith('TRACE('), (name, body)
        expected = 'vulkan_device_from_handle(params->device)->p_' + name.replace('EXT', '') if name.endswith('EXT') else 'vulkan_device_from_handle(params->device)->p_' + name
        assert body[2].startswith('vulkan_device_from_handle(params->device)->p_'), (name, body)
        assert expected.split('->p_')[1].rstrip('EXT') in body[2], (name, body)
        assert body[3] == 'return STATUS_SUCCESS;', (name, body)
        accepted.append(name)
    return accepted


def generate_eligible(directory):
    generator = BASE / 'prospero-win-108194e/tools/generate_vk_replay_dispatch.py'
    generated = directory / 'dispatch'
    subprocess.run(['python3', '-I', str(generator), '--source', str(WINE), '--output', str(generated)], check=True)
    manifest = json.loads((generated / 'pw_vk_replay_dispatch.json').read_text())
    names = sorted({row if isinstance(row, str) else row['name'] for row in manifest['eligible']})
    assert names and all(name.startswith('vkCmd') for name in names), names[:5]
    names += verified_device_forwarders()
    for required in ('vkCmdDraw', 'vkCmdDrawIndexed', 'vkCmdPipelineBarrier', 'vkCmdBeginRenderPass'):
        print(required, 'eligible' if required in names else 'NOT eligible')
    body = ''.join(f'  case unix_{name}:\n' for name in names)
    (directory / 'pw_vk_async_eligible.h').write_text(
        '/* Generated by asyncbuild260.py from the exact Wine thunks. */\n'
        'static int pw_vk_async_code_eligible(unsigned code)\n{\n switch(code){\n'
        + body + '   return 1;\n  default: return 0;\n }\n}\n')
    return names


def main():
    recipe = {c['label']: c for c in json.loads((NATIVE / 'native280-decoder-config-build.json').read_text())['commands']}
    compile_row = recipe['compile actual PS4 batch object']
    link_row = recipe['candidate full PRX ELF link']
    conv_row = recipe['candidate fself conversion']
    shutil.rmtree(OUT, ignore_errors=True)  # only this script's own output
    (OUT / 'src').mkdir(parents=True)
    names = generate_eligible(OUT / 'src')
    shutil.copy(HERE / 'pw_vk_async_core.h', OUT / 'src')

    base_compile = compile_row['command']
    old_output = base_compile[base_compile.index('-o') + 1]
    source_index = next(i for i, a in enumerate(base_compile) if a.endswith('native280-decoder-config-candidate.c'))

    def compile_c(source, output):
        command = list(base_compile)
        command[command.index('-o') + 1] = str(output)
        command[source_index] = str(source)
        command[1:1] = ['-I' + str(OUT / 'src')]
        run([a for a in command if a != '-fstack-usage'], compile_row['cwd'])
    assert old_output

    text = BASE_SOURCE.read_text()
    newline = '\r\n' if '\r\n' in text else '\n'
    entry_old = ENTRY_OLD.replace('\n', newline)
    assert text.count(entry_old) == 1
    code = ASYNC_CODE.replace('\n', newline)
    text = text.replace(entry_old, code + ENTRY_NEW.replace('\n', newline))
    batch_c = OUT / 'src/pw_vk_batch_unix.c'
    batch_c.write_text(text)
    batch_o = OUT / 'pw_vk_batch_unix.o'
    compile_c(batch_c, batch_o)

    original_vulkan = WINE / 'dlls/winevulkan/vulkan.c'
    vulkan_text = original_vulkan.read_text()
    assert vulkan_text.count(CAP_OLD) == 1
    # Control: our compile of the unpatched file must produce the admitted code.
    control_o = OUT / 'control-vulkan.o'
    compile_c(original_vulkan, control_o)
    link_command = list(link_row['command'])
    deployed_vulkan = next(a for a in link_command if a.endswith('/vulkan.o') or a == 'vulkan.o' or a.endswith('dlls/winevulkan/vulkan.o'))
    deployed_path = Path(deployed_vulkan) if deployed_vulkan.startswith('/') else Path(link_row['cwd']) / deployed_vulkan
    if not deployed_path.exists():
        deployed_path = BASE / 'native248-build-wine/build' / deployed_vulkan
    control_same = text_digest(control_o) == text_digest(deployed_path)
    print('vulkan.c control reproduces admitted .text:', control_same)
    assert control_same

    extra_swaps = {}
    for original, patched_source, object_name in EXTRA_OBJECTS:
        deployed = next(a for a in link_command if a.endswith('/' + object_name) or a == object_name)
        deployed_object = Path(deployed) if deployed.startswith('/') else Path(link_row['cwd']) / deployed
        control_object = OUT / ('control-' + object_name)
        compile_c(original, control_object)
        same = text_digest(control_object) == text_digest(deployed_object)
        print(object_name, 'control reproduces admitted .text:', same)
        assert same
        candidate_object = OUT / object_name
        compile_c(patched_source, candidate_object)
        extra_swaps[deployed] = candidate_object

    patched_vulkan = OUT / 'src/vulkan.c'
    patched_vulkan.write_text(vulkan_text.replace(CAP_OLD, CAP_NEW))
    vulkan_o = OUT / 'vulkan.o'
    compile_c(patched_vulkan, vulkan_o)

    conv = conv_row['command']

    def link_and_convert(arm, batch, vulkan):
        directory = OUT / arm
        directory.mkdir()
        elf = directory / 'winevulkan.shared.elf'
        command = list(link_command)
        command[command.index('-o') + 1] = str(elf)
        if batch or vulkan:
            for i, a in enumerate(command):
                if a.endswith('pw_vk_batch_unix.o'):
                    command[i] = str(batch)
                elif a == deployed_vulkan:
                    command[i] = str(vulkan)
                elif a in extra_swaps:
                    command[i] = str(extra_swaps[a])
        run(command, directory)
        c = list(conv)
        c[c.index('-in') + 1] = str(elf)
        c[c.index('-out') + 1] = str(directory / 'winevulkan.oelf')
        c[c.index('-lib') + 1] = str(directory / 'winevulkan.prx')
        run(c, directory)
        return elf, directory / 'winevulkan.oelf', directory / 'winevulkan.prx'

    # The linked name embeds in create-fself, so each arm uses the admitted names.
    control = link_and_convert('control', None, None)
    print('control ELF exact:', sha(control[0]) == ELF_SHA, 'PRX exact:', sha(control[2]) == PRX_SHA)
    assert sha(control[0]) == ELF_SHA and sha(control[2]) == PRX_SHA

    candidate = link_and_convert('candidate', batch_o, vulkan_o)
    va, size = max(writable(candidate[0]), key=sum)
    end = max(v + m for v, m in writable(candidate[1]) if v == va)
    assert ((end + 0x3fff) & ~0x3fff) >= va + size, 'BSS would be truncated'
    undefined = subprocess.check_output(['llvm-nm-18', '-u', str(candidate[0])], text=True)
    assert 'pw_vk_async_enabled' not in undefined  # pthread_create is linked in from the compat library
    manifest = {'eligible_vkCmd_count': len(names), 'elf_sha256': sha(candidate[0]), 'elf': str(candidate[0]),
                'prx_sha256': sha(candidate[2]), 'prx': str(candidate[2]), 'prx_bytes': candidate[2].stat().st_size,
                'control_elf_exact': True, 'control_prx_exact': True, 'bss_mapped': True}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (HERE / 'asyncbuild260-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()

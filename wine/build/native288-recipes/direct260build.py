"""Build winevulkan.dll (i386) with direct batch append, PW_VK_DIRECT=0 disables it.

Run in WSL: python3 direct260build.py

The deployed PE half is native269 candidate (syswow64 sha 2507a7d8efb2e256...).
In the native269 bridge every Vulkan record is encoded, appended to a per-thread
ring (CAS on the global sequence and three copies), then at each flush taken out
again, validated twice and copied into one scratch buffer before the Unix call.
On the PS4 four-player race that collection was about 25% of DXVK's CS thread
(spsc_take 10.5%, memcpy about 10%, append 4.3%, copy_out 2.2%).

With the async backend, records are now written once, under a short spin lock,
straight into a shared double buffer in sequence order. A flush swaps the
buffers and sends the filled one. The Unix side copies or replays it before the
call returns, so the buffer is free again for the following swap. Framing,
sequence numbers and the Unix side are unchanged.
"""
from pathlib import Path
import hashlib, json, shlex, shutil, subprocess
import os  # JKFLOW_BUILD_ROOT

HERE = Path(__file__).resolve().parent
BASE = Path(os.environ['JKFLOW_BUILD_ROOT'])
BUILD = BASE / 'native248-build-wine/build'
SOURCE = BASE / 'native269-integration/source/dlls/winevulkan/pw_vk_batch_pe.c'
SOURCE_SHA = None  # pinned below after first read
ORIGINAL = BASE / 'native247-wine-source/dlls/winevulkan/pw_vk_batch_pe.c'
DEPLOYED_STRIPPED = BASE / 'native269-integration/candidate/i386-windows/stripped/winevulkan.dll'
DEPLOYED_SHA_PREFIX = '2507a7d8efb2e256'
OUT = BASE / 'native288-direct260'
ARCH = 'i386-windows'

DIRECT_CODE = r'''
/* Direct batch append (native288 direct260). See direct260build.py. */
static BOOL direct_allowed,direct_mode,query_nowait,async_cb_allowed,async_cb;
/* asynccb260: vkBeginCommandBuffer/vkEndCommandBuffer as stream records, only
 * when the Unix side answers this capability, so an old Unix half never
 * receives the new opcodes. Off with PW_VK_ASYNC_CB=0. */
#define PW260_BEGIN_CB 0x2601u
#define PW260_END_CB 0x2602u
#define PW260_CB_NAME "__wine_pw_vk_async_cb260"
#define PW260_CB_CAPABILITY 0x50572601u
static volatile LONG64 query_nowait_total;
static unsigned char *direct_buf[2];
static unsigned direct_active;
static size_t direct_used,direct_records;
static LONG direct_lock;
static void direct_acquire(void){while(InterlockedCompareExchange(&direct_lock,1,0))YieldProcessor();}
static void direct_release(void){InterlockedExchange(&direct_lock,0);}
static int direct_append(uint32_t opcode,const unsigned char *payload,uint32_t bytes)
{
 size_t total=(PW_VK_STREAM_HEADER+(size_t)bytes+7)&~(size_t)7;unsigned char *d;uint32_t word;uint64_t ticket;
 if(bytes>PW_VK_BATCH_ARENA)return PW_VK_STREAM_INVALID;
 direct_acquire();
 if(total>PW_VK_BATCH_SCRATCH-direct_used){direct_release();return PW_VK_STREAM_FULL;}
 d=direct_buf[direct_active]+direct_used;
 ticket=atomic_fetch_add_explicit(&stream_sequence.next,1,memory_order_relaxed);
 word=PW_VK_STREAM_MAGIC;memcpy(d,&word,4);
 word=PW_VK_STREAM_VERSION;memcpy(d+4,&word,4);
 word=(uint32_t)total;memcpy(d+8,&word,4);
 memcpy(d+12,&opcode,4);
 memcpy(d+16,&bytes,4);
 memset(d+20,0,4);
 memcpy(d+24,&ticket,8);
 memcpy(d+PW_VK_STREAM_HEADER,payload,bytes);
 memset(d+PW_VK_STREAM_HEADER+bytes,0,total-PW_VK_STREAM_HEADER-bytes);
 direct_used+=total;direct_records++;
 direct_release();
 return PW_VK_STREAM_OK;
}
/* Called only by the drain owner under the PE gate. */
static unsigned char *direct_take(size_t *bytes,size_t *records)
{
 unsigned char *batch;
 direct_acquire();
 batch=direct_buf[direct_active];*bytes=direct_used;*records=direct_records;
 direct_active^=1;direct_used=0;direct_records=0;
 direct_release();
 enqueued_total+=*records;
 return batch;
}
'''

EDITS = (
    ('static unsigned char *scratch;\n', 'static unsigned char *scratch;\n'),  # anchor check only
    (' scratch=heap_alloc(PW_VK_BATCH_SCRATCH);\n',
     ' scratch=heap_alloc(PW_VK_BATCH_SCRATCH);\n'
     ' direct_buf[0]=scratch;direct_buf[1]=heap_alloc(PW_VK_BATCH_SCRATCH);\n'
     ' direct_allowed=direct_buf[1]&&!(GetEnvironmentVariableA("PW_VK_DIRECT",env,sizeof(env))==1&&env[0]==\'0\');\n'
     ' query_nowait=GetEnvironmentVariableA("PW_VK_QUERY_NOWAIT",env,sizeof(env))==1&&env[0]==\'1\';\n'
     ' async_cb_allowed=!(GetEnvironmentVariableA("PW_VK_ASYNC_CB",env,sizeof(env))==1&&env[0]==\'0\');\n'),
    # asynccb260: the CS thread called vkEndCommandBuffer about 55 times per
    # frame with four players and each call drained the async worker (about
    # 150 ms of waiting per 1.5 s). As records they keep their place in the
    # global sequence: the next vkQueueSubmit is a progress call and still
    # drains everything before it, so the driver sees Begin/End before Submit.
    # Both return VK_SUCCESS at once; a later driver failure is logged once on
    # the Unix side (PW_VK_ASYNC_CB260). Begin with pNext or inheritance info
    # (not used by DXVK 1.10.3) stays synchronous.
    (' size_t n=0;int encoded=0;uint32_t op=0,selected=call_opcode(code);\n',
     ' size_t n=0;int encoded=0;uint32_t op=0,selected=call_opcode(code);\n'
     ' if(async_cb&&code==unix_vkEndCommandBuffer){\n'
     '  struct vkEndCommandBuffer_params *q=args;uint32_t h=(uint32_t)(uintptr_t)q->commandBuffer;\n'
     '  memcpy(wire,&h,4);q->result=VK_SUCCESS;*written=4;*opcode=PW260_END_CB;return 1;\n'
     ' }\n'
     ' if(async_cb&&code==unix_vkBeginCommandBuffer){\n'
     '  struct vkBeginCommandBuffer_params *q=args;const VkCommandBufferBeginInfo *b=q->pBeginInfo;\n'
     '  if(b&&b->sType==VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO&&!b->pNext&&!b->pInheritanceInfo){\n'
     '   uint32_t v[2]={(uint32_t)(uintptr_t)q->commandBuffer,(uint32_t)b->flags};\n'
     '   memcpy(wire,v,8);q->result=VK_SUCCESS;*written=8;*opcode=PW260_BEGIN_CB;return 1;\n'
     '  }\n'
     ' }\n'),
    # Opt-in only (PW_VK_QUERY_NOWAIT=1): it cut the drains from about 7000 to
    # 1350 per 1.5 s, but paired four-player races were slower (on 18.4/21.3,
    # off 22.1/23.3 FPS), likely because the game now spins on its polls.
    # vkGetQueryPoolResults without WAIT_BIT is a poll. The progress rule made
    # every poll drain the whole async worker queue (four players: about 5000
    # drains per second, roughly 300 ms of waiting per 1.45 s). Overtaking the
    # queue is safe for a poll: if the submit that writes the query is still
    # queued, the driver answers VK_NOT_READY, which DXVK already handles, and
    # query pools are created synchronously, so the handle exists.
    (' if(fallback_profile&&code<unix_count)fallback_counts[code]++;\n',
     ' if(fallback_profile&&code<unix_count)fallback_counts[code]++;\n'
     ' if(query_nowait&&async_backend&&code==unix_vkGetQueryPoolResults\n'
     '    &&!(((const struct vkGetQueryPoolResults_params *)args)->flags&VK_QUERY_RESULT_WAIT_BIT)){\n'
     '  query_nowait_total++;leave();return raw_call(code,args);\n'
     ' }\n'),
    (' if(!read_opcode_mask())enabled=FALSE; /* Invalid explicit masks fail closed. */\n',
     ' if(!read_opcode_mask())enabled=FALSE; /* Invalid explicit masks fail closed. */\n'
     ' WINE_MESSAGE("PW_VK_DIRECT260 init enabled=%u\\n",(unsigned)enabled);\n'),
    (' size_t bytes=0,records=0;struct pw_vk_batch_params p;NTSTATUS status;\n while(!collect(&bytes,&records)){\n',
     ' size_t bytes=0,records=0;struct pw_vk_batch_params p;NTSTATUS status;unsigned char *batch=scratch;\n'
     ' if(direct_mode)batch=direct_take(&bytes,&records);\n'
     ' else while(!collect(&bytes,&records)){\n'),
    ('p.version=pw247_batch_version(backend_kind);p.batch=(UINT_PTR)scratch;',
     'p.version=pw247_batch_version(backend_kind);p.batch=(UINT_PTR)batch;'),
    ('appended=bytes?pw_vk_spsc_append(&stream_sequence,&p->stream,opcode,wire,bytes):PW_VK_STREAM_INVALID;',
     'appended=bytes?(direct_mode?direct_append(opcode,wire,(uint32_t)bytes):pw_vk_spsc_append(&stream_sequence,&p->stream,opcode,wire,bytes)):PW_VK_STREAM_INVALID;'),
    ('backend_kind=kind;async_backend=kind==PW247_ASYNC;negotiated=kind!=PW247_NONE;',
     'backend_kind=kind;async_backend=kind==PW247_ASYNC;direct_mode=direct_allowed&&kind==PW247_ASYNC;'
     'WINE_MESSAGE("PW_VK_DIRECT version=1 enabled=%u\\n",(unsigned)direct_mode);negotiated=kind!=PW247_NONE;'
     'async_cb=async_cb_allowed&&kind==PW247_ASYNC&&ask_capability(*q->pInstance,PW260_CB_NAME)==PW260_CB_CAPABILITY;'
     'WINE_MESSAGE("PW_VK_ASYNC_CB260 enabled=%u\\n",(unsigned)async_cb);'),
)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args, cwd):
    p = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if p.returncode:
        print(' '.join(map(str, args))[:400])
        print(p.stdout[-3000:] + p.stderr[-3000:])
        raise SystemExit(p.returncode)
    return p.stdout


def main():
    assert sha(DEPLOYED_STRIPPED).startswith(DEPLOYED_SHA_PREFIX)
    text = SOURCE.read_text()
    assert '\r\n' not in text
    anchor = 'static unsigned char *scratch;\n'
    assert text.count(anchor) == 1
    for old, new in EDITS[1:]:
        assert text.count(old) == 1, old
        text = text.replace(old, new)
    # The helpers need stream_sequence, enqueued_total and scratch declared first.
    late_anchor = 'static BOOL CALLBACK initialize('
    assert text.count(late_anchor) == 1
    text = text.replace(late_anchor, DIRECT_CODE + late_anchor, 1)
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir()
    candidate = OUT / 'pw_vk_batch_pe.c'
    candidate.write_text(text)
    shutil.copy2(candidate, HERE / 'direct260-pw_vk_batch_pe.c')

    env_path = [str(BASE / 'private-bin'), str(BASE / 'deps/rootfs/usr/bin')]
    import os
    os.environ['PATH'] = ':'.join(env_path) + ':' + os.environ['PATH']
    (OUT / 'tmp').mkdir()
    os.environ['TMPDIR'] = str(OUT / 'tmp')

    target = 'dlls/winevulkan/' + ARCH + '/pw_vk_batch_pe.o'
    recipe = run(['make', '-s', '-n', '-o', 'Makefile', '-o', 'config.status', '-W', str(ORIGINAL), target], BUILD)
    rows = [shlex.split(x) for x in recipe.replace('\\\n', ' ').splitlines() if ' -o ' + target + ' ' in x]
    assert len(rows) == 1
    obj = OUT / 'pw_vk_batch_pe.o'
    args = rows[0]
    args[args.index(str(ORIGINAL))] = str(candidate)
    args[args.index('-o') + 1] = str(obj)
    # Headers next to the native269 source (pw_vk_disable_guard.h and friends).
    args.insert(1, '-I' + str(SOURCE.parent))
    run(args, BUILD)
    shim = OUT / 'direct260_unixshim.o'
    shim_args = list(args)
    shim_args[shim_args.index(str(candidate))] = str(HERE / 'direct260_unixshim.c')
    shim_args[shim_args.index('-o') + 1] = str(shim)
    run(shim_args, BUILD)

    dlltarget = 'dlls/winevulkan/' + ARCH + '/winevulkan.dll'
    recipe = run(['make', '-s', '-n', '-o', 'Makefile', '-o', 'config.status', '-W', str(ORIGINAL), dlltarget], BUILD)
    rows = [shlex.split(x) for x in recipe.replace('\\\n', ' ').splitlines() if ' -o ' + dlltarget + ' ' in x and 'winegcc' in x]
    assert len(rows) == 1
    results = {}
    for kind in ('control', 'candidate'):
        destination = OUT / kind
        destination.mkdir()
        dll = destination / 'winevulkan.dll'
        args = rows[0][:]
        args[args.index('-o') + 1] = str(dll)
        args[args.index('--wine-objdir') + 1] = str(BUILD)
        for i, arg in enumerate(args):
            if arg.startswith(('dlls/', 'libs/')):
                args[i] = str(obj) if kind == 'candidate' and arg == target else str(BUILD / arg)
        if kind == 'candidate':
            args.insert(args.index(str(obj)) + 1, str(shim))
        if kind == 'control':
            # The control links native269's own object to prove the recipe reproduces the deployed DLL.
            args[args.index(str(BUILD / target))] = str(BASE / 'native269-integration/i386-windows-pw_vk_batch_pe.o')
        run(args, destination)
        stripped = destination / 'stripped.dll'
        shutil.copyfile(dll, stripped)
        run(['llvm-strip-18', '--strip-debug', str(stripped)], destination)
        results[kind] = {'unstripped': str(dll), 'stripped': str(stripped), 'sha256': sha(stripped)}
    def body(path):
        data = bytearray(Path(path).read_bytes())
        nt = int.from_bytes(data[0x3c:0x40], 'little')
        data[nt + 8:nt + 12] = bytes(4)  # PE timestamp
        return hashlib.sha256(data).hexdigest()
    control_ok = body(results['control']['stripped']) == body(DEPLOYED_STRIPPED)
    shutil.copyfile(results['candidate']['stripped'], OUT / 'wvkdirect.dll')
    results['candidate']['wvkdirect'] = str(OUT / 'wvkdirect.dll')
    manifest = {'candidate': results['candidate'], 'control_matches_deployed': control_ok,
                'deployed_sha256': sha(DEPLOYED_STRIPPED), 'source_sha256': sha(candidate)}
    (HERE / 'direct260-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()

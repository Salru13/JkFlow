#!/usr/bin/env python3
"""Build winevulkan.prx with N pool-owned parallel replay workers (parwork260).

Plan: docs/superpowers/plans/2026-11-11-parallel-native-recording.md, Task 3.
Base: the asynccb260 Unix half (asyncbuild260.py + asynccb260build.py), unchanged when
PW_VK_ASYNC_WORKERS is unset or 1.

With PW_VK_ASYNC_WORKERS=2..4:
  * each batch is split by owning worker (pw_vk_async_multi.h): command-buffer records and
    BEGIN_CB/END_CB to the worker of the buffer's pool, descriptor-template updates to the
    worker of the next command record, anything else drains all workers and replays inline;
  * the private drain sentinel (unix_count+1) no longer waits, since its records are queued in order;
  * vulkan_thunks.c wrappers wait only where Vulkan requires it: vkQueueSubmit for the owners of
    the submitted buffers, vkResetCommandPool / vkAllocateCommandBuffers / vkFreeCommandBuffers /
    vkDestroyCommandPool for the pool's owner; vkQueueSubmit2 waits for all workers.
PW_VK_ASYNC_STATS=1 adds `PW_VK_ASYNC_MULTI busy_ms=... unknown_cb=... pools=...`.
PW_VK_ASYNC_CAPTURE=1 writes 64 MiB of batches and pool/submit events after 120 s to
/data/Pc/Blur/async-capture.bin ({u32 kind, u32 bytes, payload}; kind 0 batch, 1 alloc,
2 free, 3 destroy pool, 4 reset pool, 5 submit; event payload {u32 count, u32 0, u64 pool, u64 cb[count]}).

Controls: unpatched vulkan.c and vulkan_thunks.c compile to the admitted .text, and the
unpatched relink reproduces the admitted ELF and PRX byte for byte. Run in WSL.
"""
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import asynccb260build as cbb  # noqa: E402

base = cbb.base
OUT = base.BASE / 'native288-parwork260'
WORK = OUT / 'inputs'

DECLS = r'''
/* parwork260: N pool-owned replay workers, see parwork260build.py. */
#include "pw_vk_async_multi.h"
#include "pw_vk_par_cb_codes.h"
static struct pw_async_multi par;
static int par_on;
static pthread_mutex_t par_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t par_single_busy_ns;
static int async_stats_on;
static uint64_t async_now(void);
static int par_descriptor_set(void *ctx,const struct pw_vk_stream_record *r,uint64_t *set);
static unsigned par_bound_sets(void *ctx,const struct pw_vk_stream_record *r,uint64_t *sets,unsigned max);
'''

ASYNC_RUN_OLD = (' (void)context;return replay_bytes((void *)bytes,size)!=STATUS_SUCCESS;\n')
ASYNC_RUN_NEW = (' uint64_t start=async_stats_on>0?async_now():0;int failed;(void)context;\n'
                 ' failed=replay_bytes((void *)bytes,size)!=STATUS_SUCCESS;\n'
                 ' if(start)__atomic_add_fetch(&par_single_busy_ns,async_now()-start,__ATOMIC_RELAXED);\n'
                 ' return failed;\n')

START_OLD = 'static void async_start(void){async_ready=!pw_async_start(&async_queue,PW_VK_ASYNC_RING,async_run,NULL);}'
START_NEW = r'''static void async_start(void)
{
 unsigned n=pw_route_parse_workers(getenv("PW_VK_ASYNC_WORKERS"));char line[80];
 async_ready=!pw_async_start(&async_queue,PW_VK_ASYNC_RING,async_run,NULL);
 if(n>1&&async_ready&&!pw_multi_start(&par,n,PW_VK_ASYNC_RING,async_run,NULL)){par_on=1;pw_multi_set_matching(&par,par_descriptor_set,par_bound_sets);}
 snprintf(line,sizeof(line),"PW_VK_ASYNC_WORKERS version=1 workers=%u\n",par_on?n:1u);
 __wine_dbg_output(line);
}'''

WAIT_OLD_1 = ' if(!async_stats_on)return pw_async_wait_all(&async_queue);\n'
WAIT_NEW_1 = (' if(par_on&&(code==unix_count+1||code==unix_vkResetCommandPool||code==unix_vkAllocateCommandBuffers||\n'
              '            code==unix_vkFreeCommandBuffers||code==unix_vkDestroyCommandPool))return pw_multi_failed(&par);\n'
              ' if(!async_stats_on)return par_on?pw_multi_wait_all(&par):pw_async_wait_all(&async_queue);\n')
WAIT_OLD_2 = ' start=async_now();failed=pw_async_wait_all(&async_queue);spent=async_now()-start;\n'
WAIT_NEW_2 = ' start=async_now();failed=par_on?pw_multi_wait_all(&par):pw_async_wait_all(&async_queue);spent=async_now()-start;\n'

REPORT_OLD = 'static void async_report(void)\n{\n'
REPORT_NEW = r'''static uint64_t par_busy_seen[PW_ROUTE_MAX_WORKERS+1];
static void par_report(void)
{
 char line[200];uint64_t b[PW_ROUTE_MAX_WORKERS]={0};unsigned w;
 if(par_on)for(w=0;w<par.workers;w++){uint64_t t=atomic_load(&par.busy_ns[w]);b[w]=(t-par_busy_seen[w])/1000000u;par_busy_seen[w]=t;}
 else{uint64_t t=__atomic_load_n(&par_single_busy_ns,__ATOMIC_RELAXED);b[0]=(t-par_busy_seen[PW_ROUTE_MAX_WORKERS])/1000000u;par_busy_seen[PW_ROUTE_MAX_WORKERS]=t;}
 snprintf(line,sizeof(line),"PW_VK_ASYNC_MULTI workers=%u busy_ms=%llu,%llu,%llu,%llu unknown_cb=%llu pools=%llu\n",par_on?par.workers:1u,
  (unsigned long long)b[0],(unsigned long long)b[1],(unsigned long long)b[2],(unsigned long long)b[3],
  (unsigned long long)(par_on?par.route.unknown_cb:0),(unsigned long long)(par_on?par.route.pools:0));
 __wine_dbg_output(line);
}
static void async_report(void)
{
 par_report();
'''

HOOKS = r'''
/* parwork260 routing, capture and the hooks called from vulkan_thunks.c. */
#ifndef PW260_BEGIN_CB
#define PW260_BEGIN_CB 0x2601u
#define PW260_END_CB 0x2602u
#endif
static uint64_t par_batches,par_new_pool_batch=~(uint64_t)0;static int par_new_pool_worker=-1;
static FILE *par_capture;static uint64_t par_capture_bytes,par_capture_after;static int par_capture_state;
/* Pool events before the capture opens, replayed first so the checker knows every pool. */
static unsigned char par_early[256u*1024u];static uint32_t par_early_used,par_early_lost;
/* Caller holds par_lock. */
static void par_capture_write(uint32_t kind,const void *a,uint32_t an,const void *b,uint32_t bn)
{
 uint32_t h[2];
 if(!par_capture_state){const char *v=getenv("PW_VK_ASYNC_CAPTURE");par_capture_state=v&&v[0]=='1'&&!v[1]?2:1;par_capture_after=async_now()+120000000000ull;}
 if(par_capture_state==2&&kind>=1&&kind<=3){
  if(par_early_used+8u+an+bn<=sizeof(par_early)){
   h[0]=kind;h[1]=an+bn;memcpy(par_early+par_early_used,h,8);memcpy(par_early+par_early_used+8,a,an);
   if(bn)memcpy(par_early+par_early_used+8+an,b,bn);par_early_used+=8u+an+bn;
  }else par_early_lost++;
 }
 if(par_capture_state==2&&async_now()>=par_capture_after){
  par_capture=fopen("/data/Pc/Blur/async-capture.bin","wb");par_capture_state=par_capture?3:1;
  if(par_capture){char line[96];fwrite(par_early,1,par_early_used,par_capture);par_capture_bytes=par_early_used;
   snprintf(line,sizeof(line),"PW_VK_ASYNC_CAPTURE start early_bytes=%u early_lost=%u\n",par_early_used,par_early_lost);__wine_dbg_output(line);}
 }
 if(par_capture_state!=3)return;
 h[0]=kind;h[1]=an+bn;fwrite(h,4,2,par_capture);
 if(an)fwrite(a,1,an,par_capture);
 if(bn)fwrite(b,1,bn,par_capture);
 par_capture_bytes+=8u+an+bn;
 if(par_capture_bytes>=(64u<<20)){fclose(par_capture);par_capture=NULL;par_capture_state=4;__wine_dbg_output("PW_VK_ASYNC_CAPTURE done bytes=67108864\n");}
}
static void par_capture_event(uint32_t kind,uint64_t pool,const uint32_t *cbs,unsigned n)
{
 uint64_t head[2],tmp[64];unsigned done=0;
 if(par_capture_state==1||par_capture_state==4)return;
 head[0]=n;head[1]=pool;
 if(n<=64){for(unsigned i=0;i<n;i++)tmp[i]=cbs[i];par_capture_write(kind,head,16,tmp,8u*n);return;}
 /* Large lists: one event per chunk of 64 buffers. */
 while(done<n){unsigned k=n-done>64?64:n-done;head[0]=k;for(unsigned i=0;i<k;i++)tmp[i]=cbs[done+i];par_capture_write(kind,head,16,tmp,8u*k);done+=k;}
}
static int par_classify(void *ctx,const struct pw_vk_stream_record *r,uint64_t *cb)
{
 unsigned code;uint32_t v;(void)ctx;
 switch(r->opcode){
 /* Wire records (pw_vk_wire.h): the command buffer is the u32 at offset 0. */
 case PW260_BEGIN_CB:case PW260_END_CB:
 case PW_VK_DRAW_INDEXED:case PW_VK_BIND_PIPELINE:case PW_VK_BIND_INDEX:
 case PW_VK_BIND_DESCRIPTORS:case PW_VK_BIND_VERTEX2:case PW_VK_PUSH_CONSTANTS:
  if(r->payload_bytes<4)return PW_MULTI_BLOCKING;
  memcpy(&v,r->payload,4);*cb=v;return PW_MULTI_CB;
 /* A descriptor-template update runs before the next command record. */
 case PW_VK_UPDATE_TEMPLATE:return PW_MULTI_DESCRIPTOR;
 }
 if(r->opcode!=PW_VK_BATCH_GENERATED_OPCODE||r->payload_bytes<12)return PW_MULTI_BLOCKING;
 memcpy(&code,r->payload,4);
 if(code==unix_vkUpdateDescriptorSetWithTemplate||code==unix_vkUpdateDescriptorSetWithTemplateKHR)return PW_MULTI_DESCRIPTOR;
 /* Eligible device-level calls (destroy framebuffer/shader module, host query reset) run before
  * the next command record, so they precede it and its submit. */
 if(code==unix_vkDestroyFramebuffer||code==unix_vkDestroyShaderModule||code==unix_vkResetQueryPool||code==unix_vkResetQueryPoolEXT)
  return PW_MULTI_DESCRIPTOR;
 if(!pw_vk_par_cb_code(code))return PW_MULTI_BLOCKING;
 /* Proven by parwork260build.py: commandBuffer is the first, 8-byte wide field. */
 memcpy(cb,r->payload+4,8);return PW_MULTI_CB;
}
/* Exact descriptor routing: an update follows the bind of its own set. */
static int par_descriptor_set(void *ctx,const struct pw_vk_stream_record *r,uint64_t *set)
{
 unsigned code;(void)ctx;
 if(r->opcode==PW_VK_UPDATE_TEMPLATE){if(r->payload_bytes<16)return 0;memcpy(set,r->payload+8,8);return 1;}
 if(r->opcode!=PW_VK_BATCH_GENERATED_OPCODE||r->payload_bytes<20)return 0;
 memcpy(&code,r->payload,4);
 if(code!=unix_vkUpdateDescriptorSetWithTemplate&&code!=unix_vkUpdateDescriptorSetWithTemplateKHR)return 0;
 /* Proven by parwork260build.py: device (8, wide) then descriptorSet (8). */
 memcpy(set,r->payload+4+8,8);return 1;
}
static unsigned par_bound_sets(void *ctx,const struct pw_vk_stream_record *r,uint64_t *sets,unsigned max)
{
 uint32_t n;(void)ctx;
 if(r->opcode!=PW_VK_BIND_DESCRIPTORS||r->payload_bytes<32)return 0;
 memcpy(&n,r->payload+20,4);
 if(n>max)n=max;
 if(32u+8u*n>r->payload_bytes)return 0;
 memcpy(sets,r->payload+32,8u*n);return n;
}
int pw_vk_par_active(void){return par_on;}
void pw_vk_par_wait_all(void){if(par_on)pw_multi_wait_all(&par);}
void pw_vk_par_wait_pool(uint64_t pool)
{
 int w;
 if(!par_on)return;
 pthread_mutex_lock(&par_lock);w=pw_route_worker_for_pool(&par.route,pool);pthread_mutex_unlock(&par_lock);
 pw_multi_wait_worker(&par,w<0?0:w);   /* an unknown pool's buffers ran on worker 0 */
}
void pw_vk_par_reset_pool(uint64_t pool)
{
 pthread_mutex_lock(&par_lock);par_capture_event(4,pool,NULL,0);pthread_mutex_unlock(&par_lock);
 pw_vk_par_wait_pool(pool);
}
void pw_vk_par_note_alloc(uint64_t pool,const uint32_t *cbs,unsigned n)
{
 uint64_t tmp[64];unsigned done=0;int known,as=-1;
 pthread_mutex_lock(&par_lock);
 par_capture_event(1,pool,cbs,n);
 if(par_on){
  known=pw_route_worker_for_pool(&par.route,pool)>=0;
  /* The pools of one DXVK command list are created with no stream batch in between. */
  if(!known&&par_new_pool_batch==par_batches)as=par_new_pool_worker;
  while(done<n){unsigned k=n-done>64?64:n-done;for(unsigned i=0;i<k;i++)tmp[i]=cbs[done+i];pw_route_note_alloc_as(&par.route,pool,tmp,k,as);done+=k;}
  if(!n)pw_route_note_alloc_as(&par.route,pool,tmp,0,as);
  if(!known){par_new_pool_batch=par_batches;par_new_pool_worker=pw_route_worker_for_pool(&par.route,pool);}
 }
 pthread_mutex_unlock(&par_lock);
}
void pw_vk_par_note_free(uint64_t pool,const uint32_t *cbs,unsigned n)
{
 uint64_t tmp[64];unsigned done=0;
 pthread_mutex_lock(&par_lock);
 par_capture_event(2,pool,cbs,n);
 if(par_on)while(done<n){unsigned k=n-done>64?64:n-done;for(unsigned i=0;i<k;i++)tmp[i]=cbs[done+i];pw_route_note_free(&par.route,pool,tmp,k);done+=k;}
 pthread_mutex_unlock(&par_lock);
}
void pw_vk_par_note_destroy_pool(uint64_t pool)
{
 pthread_mutex_lock(&par_lock);
 par_capture_event(3,pool,NULL,0);
 if(par_on)pw_route_note_destroy_pool(&par.route,pool);
 pthread_mutex_unlock(&par_lock);
}
void pw_vk_par_submit32(const uint32_t *cbs,unsigned n)
{
 uint64_t tmp[64];unsigned mask=0,done=0;
 pthread_mutex_lock(&par_lock);
 par_capture_event(5,0,cbs,n);
 if(par_on)while(done<n){unsigned k=n-done>64?64:n-done;for(unsigned i=0;i<k;i++)tmp[i]=cbs[done+i];mask|=pw_multi_owner_mask(&par,tmp,k);done+=k;}
 pthread_mutex_unlock(&par_lock);
 if(mask)pw_multi_wait_mask(&par,mask);
}
'''

BATCH_OLD = ''' if(p->bytes){
  struct async_scan scan={0,0};size_t completed;
  int ok=pw_vk_stream_replay(bytes,p->bytes,async_scan_record,&scan,&completed)==PW_VK_STREAM_OK&&!scan.blocked;
  if(async_stats_on){async_stats.batches++;async_stats.bytes+=p->bytes;}
  if(async_ready&&ok&&pw_async_fits(&async_queue,p->bytes)){
   pw_async_submit(&async_queue,bytes,p->bytes);
   if(async_stats_on)async_stats.queued++;
  }else{
   NTSTATUS status;
   if(async_ready&&async_wait(p->code))return STATUS_UNSUCCESSFUL;
'''
BATCH_NEW = ''' if(p->bytes){
  struct async_scan scan={0,0};size_t completed;int par_queued=0;
  int ok=pw_vk_stream_replay(bytes,p->bytes,async_scan_record,&scan,&completed)==PW_VK_STREAM_OK&&!scan.blocked;
  if(async_stats_on){async_stats.batches++;async_stats.bytes+=p->bytes;}
  pthread_mutex_lock(&par_lock);
  par_capture_write(0,bytes,(uint32_t)p->bytes,NULL,0);
  par_batches++;
  if(par_on&&ok)par_queued=!pw_multi_submit_batch(&par,bytes,p->bytes,par_classify,NULL);
  pthread_mutex_unlock(&par_lock);
  if(par_queued){if(async_stats_on)async_stats.queued++;}
  else if(!par_on&&async_ready&&ok&&pw_async_fits(&async_queue,p->bytes)){
   pw_async_submit(&async_queue,bytes,p->bytes);
   if(async_stats_on)async_stats.queued++;
  }else{
   NTSTATUS status;
   if(par_on){if(pw_multi_wait_all(&par))return STATUS_UNSUCCESSFUL;}
   else if(async_ready&&async_wait(p->code))return STATUS_UNSUCCESSFUL;
'''

THUNK_HOOKS = r'''
/* parwork260 wrappers (hooks in pw_vk_batch_unix.c, see parwork260build.py). Both the
 * wow64 table and pw_vk_batch_dispatch reach these through the original names. */
extern int pw_vk_par_active(void);
extern void pw_vk_par_wait_all(void);
extern void pw_vk_par_wait_pool(uint64_t pool);
extern void pw_vk_par_reset_pool(uint64_t pool);
extern void pw_vk_par_note_alloc(uint64_t pool, const uint32_t *cbs, unsigned n);
extern void pw_vk_par_note_free(uint64_t pool, const uint32_t *cbs, unsigned n);
extern void pw_vk_par_note_destroy_pool(uint64_t pool);
extern void pw_vk_par_submit32(const uint32_t *cbs, unsigned n);
static NTSTATUS thunk32_vkQueueSubmit(void *args)
{
    struct { PTR32 queue; uint32_t submitCount; PTR32 pSubmits; VkFence DECLSPEC_ALIGN(8) fence; VkResult result; } *params = args;
    const VkSubmitInfo32 *s = (const VkSubmitInfo32 *)UlongToPtr(params->pSubmits);
    uint32_t i;
    for (i = 0; i < params->submitCount; i++)
        if (s[i].commandBufferCount)
            pw_vk_par_submit32((const uint32_t *)UlongToPtr(s[i].pCommandBuffers), s[i].commandBufferCount);
    return thunk32_vkQueueSubmit_orig(args);
}
static NTSTATUS thunk32_vkQueueSubmit2(void *args)
{
    pw_vk_par_wait_all();
    return thunk32_vkQueueSubmit2_orig(args);
}
static NTSTATUS thunk32_vkQueueSubmit2KHR(void *args)
{
    pw_vk_par_wait_all();
    return thunk32_vkQueueSubmit2KHR_orig(args);
}
static NTSTATUS thunk32_vkResetCommandPool(void *args)
{
    struct { PTR32 device; VkCommandPool DECLSPEC_ALIGN(8) commandPool; VkCommandPoolResetFlags flags; VkResult result; } *params = args;
    pw_vk_par_reset_pool(params->commandPool);
    return thunk32_vkResetCommandPool_orig(args);
}
static NTSTATUS thunk32_vkAllocateCommandBuffers(void *args)
{
    struct { PTR32 device; PTR32 pAllocateInfo; PTR32 pCommandBuffers; VkResult result; } *params = args;
    const VkCommandBufferAllocateInfo32 *info = (const VkCommandBufferAllocateInfo32 *)UlongToPtr(params->pAllocateInfo);
    VkCommandPool pool = info->commandPool;
    uint32_t count = info->commandBufferCount;
    NTSTATUS status;
    pw_vk_par_wait_pool(pool);
    status = thunk32_vkAllocateCommandBuffers_orig(args);
    if (!status && params->result == VK_SUCCESS)
        pw_vk_par_note_alloc(pool, (const uint32_t *)UlongToPtr(params->pCommandBuffers), count);
    return status;
}
static NTSTATUS thunk32_vkFreeCommandBuffers(void *args)
{
    struct { PTR32 device; VkCommandPool DECLSPEC_ALIGN(8) commandPool; uint32_t commandBufferCount; PTR32 pCommandBuffers; } *params = args;
    VkCommandPool pool = params->commandPool;
    NTSTATUS status;
    pw_vk_par_wait_pool(pool);
    status = thunk32_vkFreeCommandBuffers_orig(args);
    pw_vk_par_note_free(pool, (const uint32_t *)UlongToPtr(params->pCommandBuffers), params->commandBufferCount);
    return status;
}
static NTSTATUS thunk32_vkDestroyCommandPool(void *args)
{
    struct { PTR32 device; VkCommandPool DECLSPEC_ALIGN(8) commandPool; PTR32 pAllocator; } *params = args;
    VkCommandPool pool = params->commandPool;
    NTSTATUS status;
    pw_vk_par_wait_pool(pool);
    status = thunk32_vkDestroyCommandPool_orig(args);
    pw_vk_par_note_destroy_pool(pool);
    return status;
}
'''
WRAPPED = ('vkQueueSubmit', 'vkQueueSubmit2', 'vkQueueSubmit2KHR', 'vkResetCommandPool',
           'vkAllocateCommandBuffers', 'vkFreeCommandBuffers', 'vkDestroyCommandPool')
TABLE_ANCHOR = 'const unixlib_entry_t __wine_unix_call_wow64_funcs[] =\n'


def patched_thunks(text):
    newline = '\r\n' if '\r\n' in text else '\n'
    t = text.replace('\r\n', '\n')
    for name in WRAPPED:
        old = f'static NTSTATUS thunk32_{name}(void *args)\n{{\n'
        assert t.count(old) == 1, name
        t = t.replace(old, f'static NTSTATUS thunk32_{name}_orig(void *args)\n{{\n')
    # The parameter layouts the wrappers read must be exactly Wine's.
    for name, fields in (('vkQueueSubmit_orig', 'PTR32 queue;\n        uint32_t submitCount;\n        PTR32 pSubmits;\n        VkFence DECLSPEC_ALIGN(8) fence;'),
                         ('vkResetCommandPool_orig', 'PTR32 device;\n        VkCommandPool DECLSPEC_ALIGN(8) commandPool;\n        VkCommandPoolResetFlags flags;'),
                         ('vkAllocateCommandBuffers_orig', 'PTR32 device;\n        PTR32 pAllocateInfo;\n        PTR32 pCommandBuffers;'),
                         ('vkFreeCommandBuffers_orig', 'PTR32 device;\n        VkCommandPool DECLSPEC_ALIGN(8) commandPool;\n        uint32_t commandBufferCount;\n        PTR32 pCommandBuffers;'),
                         ('vkDestroyCommandPool_orig', 'PTR32 device;\n        VkCommandPool DECLSPEC_ALIGN(8) commandPool;\n        PTR32 pAllocator;')):
        i = t.index(f'static NTSTATUS thunk32_{name}(void *args)')
        assert fields in t[i:i + 600], name
    for struct, fields in (('VkSubmitInfo32', 'uint32_t waitSemaphoreCount;\n    PTR32 pWaitSemaphores;\n    PTR32 pWaitDstStageMask;\n    uint32_t commandBufferCount;\n    PTR32 pCommandBuffers;'),
                           ('VkCommandBufferAllocateInfo32', 'VkCommandPool DECLSPEC_ALIGN(8) commandPool;\n    VkCommandBufferLevel level;\n    uint32_t commandBufferCount;')):
        i = t.index(f'typedef struct {struct}\n')
        assert fields in t[i:i + 400], struct
    assert t.count(TABLE_ANCHOR) == 1
    t = t.replace(TABLE_ANCHOR, THUNK_HOOKS + '\n' + TABLE_ANCHOR)
    return t.replace('\n', newline)


def cb_codes(directory, eligible):
    """Every eligible vkCmd* must encode commandBuffer as its first, 8-byte wide field."""
    generated = (base.WINE / 'dlls/winevulkan/pw_vk_generated.c').read_text()
    names = []
    for name in eligible:
        if not name.startswith('vkCmd'):
            continue
        m = re.search(r'static int __attribute__\(\(unused\)\) codec_' + name + r'\(struct pw_vk_codec \*c,void \*value,uint64_t selector\)\n'
                      r'\{ struct ' + name + r'_params \*p=value;.*?\n if\(c->depth>=64\)return 0;\n c->depth\+\+;\n (.*?)\n', generated, re.S)
        assert m, name
        assert m[1] == 'CHECK(pw_vk_codec_value(c,(void *)(&p->commandBuffer),sizeof(*(&p->commandBuffer)),1));', (name, m[1])
        names.append(name)
    # par_descriptor_set reads the set of a generated template update at wire offset 8.
    template = re.search(r'codec_vkUpdateDescriptorSetWithTemplate\(struct pw_vk_codec \*c,void \*value,uint64_t selector\)\n'
                         r'.*?c->depth\+\+;\n (.*?)\n(.*?)\n', generated, re.S)
    assert template and '&p->device' in template[1] and ',1));' in template[1], template and template.groups()
    assert '&p->descriptorSet' in template[2], template.groups()
    body = ''.join(f'  case unix_{n}:\n' for n in names)
    (directory / 'pw_vk_par_cb_codes.h').write_text(
        '/* Generated by parwork260build.py: vkCmd* codes whose first wire field is the 8-byte commandBuffer. */\n'
        'static int pw_vk_par_cb_code(unsigned code)\n{\n switch(code){\n' + body + '   return 1;\n  default: return 0;\n }\n}\n')
    return names


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    WORK.mkdir(parents=True)
    # 1. The asynccb260 batch source, then the parwork changes to the async code.
    source = WORK / 'native280-decoder-config-candidate.c'
    source.write_text(cbb.patched_batch_source(base.BASE_SOURCE.read_text()))
    code = base.ASYNC_CODE
    for old, new in ((ASYNC_RUN_OLD, ASYNC_RUN_NEW), (START_OLD, START_NEW), (WAIT_OLD_1, WAIT_NEW_1),
                     (WAIT_OLD_2, WAIT_NEW_2), (REPORT_OLD, REPORT_NEW), (BATCH_OLD, BATCH_NEW)):
        assert code.count(old) == 1, old[:60]
        code = code.replace(old, new)
    anchor = 'static NTSTATUS replay_bytes(void *bytes,size_t size)\n'
    assert code.count(anchor) == 1
    code = code.replace(anchor, DECLS + anchor)
    anchor = 'static __attribute__((noinline)) NTSTATUS async_batch(void *args)\n'
    assert code.count(anchor) == 1
    code = code.replace(anchor, HOOKS + anchor)
    base.ASYNC_CODE = code
    for header in ('pw_vk_async_core.h', 'pw_vk_async_route.h', 'pw_vk_async_multi.h'):
        shutil.copy(HERE / header, WORK)
    base.BASE_SOURCE = source
    base.HERE = WORK
    base.OUT = OUT / 'build'
    base.CAP_NEW = cbb.CAP_EXTRA + base.CAP_NEW

    # 2. Extra source directory content and the patched vulkan_thunks.c, through base.main's hooks.
    original_generate = base.generate_eligible

    def generate_and_extend(directory):
        names = original_generate(directory)
        print('command-buffer codes routed:', len(cb_codes(directory, names)))
        for header in ('pw_vk_async_route.h', 'pw_vk_async_multi.h'):
            shutil.copy(HERE / header, directory)
        return names
    base.generate_eligible = generate_and_extend

    thunks = base.WINE / 'dlls/winevulkan/vulkan_thunks.c'
    patched = WORK / 'vulkan_thunks.c'
    patched.write_text(patched_thunks(thunks.read_text()))
    base.EXTRA_OBJECTS = [(thunks, patched, 'vulkan_thunks.o')]
    base.main()
    shutil.copy(WORK / 'asyncbuild260-manifest.json', HERE / 'parwork260-manifest.json')


if __name__ == '__main__':
    main()

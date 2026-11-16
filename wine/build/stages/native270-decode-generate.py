"""Create isolated bounded retained-decoder candidate from pinned actual sources."""
from pathlib import Path
import difflib,hashlib,json,sys
import os  # JKFLOW_BUILD_ROOT
base=Path(os.environ['JKFLOW_BUILD_ROOT'])
repo=Path(sys.argv[1]); reports=repo/'research/native-wine'
original=base/'native247-wine-source/dlls/winevulkan'
out=base/'native270-decode';out.mkdir(exist_ok=True)
stage=out/'source';stage.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(original/'pw_vk_batch_unix.c')=='f87f446c1731687a52b0fed6cbf2dafc3a00624de46e3cb84fbc5c12ddeef97f'
inputs={};patch=[]
def save(src,text,relative):
 inputs[str(src)]=sha(src)
 dest=stage/src.name
 if not dest.exists() or dest.read_text()!=text:dest.write_text(text)
 patch.extend(difflib.unified_diff(src.read_text().splitlines(True),text.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
 return dest
generated=(original/'pw_vk_generated.c').read_text()
decoder=generated[generated.index('int pw_vk_generated_decode('):]
extra=decoder.replace('pw_vk_generated_decode(', 'pw_vk_generated_decode_used(',1).replace('void **args)', 'void **args,size_t *used)',1)
extra=extra.replace('if(!size||','if(!used||!size||',1).replace('||c.used!=bytes)', '||c.used!=bytes||c.arena_used>capacity)',1)
extra=extra.replace('*args=arena;return 1;', '*args=arena;*used=c.arena_used;return 1;',1)
save(original/'pw_vk_generated.c',generated+extra,'dlls/winevulkan/pw_vk_generated.c')
header=(original/'pw_vk_codec.h').read_text().replace('size_t pw_vk_generated_param_size(unsigned);','/* Internal Unix staging API; successful used bytes include every owned child. */\nint pw_vk_generated_decode_used(unsigned,const void *,size_t,void *,size_t,void **,size_t *);\nsize_t pw_vk_generated_param_size(unsigned);')
save(original/'pw_vk_codec.h',header,'dlls/winevulkan/pw_vk_codec.h')
generator=base/'native248-build-bridge/tools/generate_vk_codecs.py'
text=generator.read_text();needle=' *args=arena;return 1;}\n';assert text.count(needle)==1
save(generator,text.replace(needle,needle+extra,1),'tools/generate_vk_codecs.py')
text=(original/'pw_vk_batch_unix.c').read_text()
text=text.replace('struct replay_context {','''/* One stable stack region plus at most one stable bounded heap region. Never
 * relocate decoded objects: nested native pointers refer into their region.
 * Exhausting either budget restarts the original complete preflight before
 * any effect, freeing this heap before the original path can allocate. */
#define PW_VK_RETAINED_RECORDS 128
#define PW_VK_RETAINED_BYTES (128u*1024u)
struct retained_record { void *params; uint64_t sequence; unsigned code; };
struct replay_context {''',1)
text=text.replace('void *arena; size_t capacity; unsigned version; NTSTATUS failure;','''void *arena; size_t capacity; unsigned version; NTSTATUS failure;
 struct retained_record retained[PW_VK_RETAINED_RECORDS];
 size_t used; unsigned retained_count, replay_index;''',1)
insert='''static int retain_preflight(void *context,const struct pw_vk_stream_record *r)
{
 struct replay_context *ctx=context;struct retained_record *saved;void *params;size_t used;unsigned code;
 if(r->opcode!=PW_VK_BATCH_GENERATED_OPCODE)
  return !(r->payload_bytes<=4096&&pw_vk_wire_validate(r->opcode,r->payload,r->payload_bytes));
 if(ctx->version!=PW_VK_BATCH_VERSION||r->payload_bytes<4||r->payload_bytes>PW_VK_CODEC_MAX_BYTES||ctx->retained_count==PW_VK_RETAINED_RECORDS)return 1;
 memcpy(&code,r->payload,4);if(code>=unix_pw_vk_batch)return 1;
 if(!pw_vk_generated_decode_used(code,r->payload+4,r->payload_bytes-4,(unsigned char *)ctx->arena+ctx->used,ctx->capacity-ctx->used,&params,&used)){
  if(ctx->arena!=ctx->local)return 1;
  ctx->arena=malloc(PW_VK_RETAINED_BYTES);if(!ctx->arena)return 1;
  ctx->capacity=PW_VK_RETAINED_BYTES;ctx->used=0;
  if(!pw_vk_generated_decode_used(code,r->payload+4,r->payload_bytes-4,ctx->arena,ctx->capacity,&params,&used))return 1;
 }
 /* A region ends on an 8-byte boundary, so rounding a successful decode fits. */
 ctx->used+=(used+7)&~(size_t)7;
 saved=&ctx->retained[ctx->retained_count++];saved->params=params;saved->sequence=r->sequence;saved->code=code;
 return 0;
}
static int retained_replay(void *context,const struct pw_vk_stream_record *r)
{
 struct replay_context *ctx=context;struct retained_record *saved;unsigned code;
 if(r->opcode!=PW_VK_BATCH_GENERATED_OPCODE)return !pw_wine_vk_replay(r->opcode,r->payload,r->payload_bytes);
 if(ctx->replay_index>=ctx->retained_count||r->payload_bytes<4)return 1;
 saved=&ctx->retained[ctx->replay_index++];memcpy(&code,r->payload,4);
 if(saved->sequence!=r->sequence||saved->code!=code)return 1;
 return pw_vk_batch_dispatch_native(code,saved->params)!=STATUS_SUCCESS;
}
'''
text=text.replace('NTSTATUS pw_vk_batch_unix(void *args)',insert+'NTSTATUS pw_vk_batch_unix(void *args)',1)
old=''' if(pw_vk_stream_replay(UlongToPtr(p->batch),p->bytes,preflight,&ctx,&completed)!=PW_VK_STREAM_OK)status=ctx.failure?ctx.failure:STATUS_INVALID_PARAMETER;
 else if(pw_vk_stream_replay(UlongToPtr(p->batch),p->bytes,replay,&ctx,&completed)!=PW_VK_STREAM_OK)status=STATUS_UNSUCCESSFUL;'''
new=''' ctx.used=0;ctx.retained_count=0;ctx.replay_index=0;
 if(pw_vk_stream_replay(UlongToPtr(p->batch),p->bytes,retain_preflight,&ctx,&completed)==PW_VK_STREAM_OK){
  if(pw_vk_stream_replay(UlongToPtr(p->batch),p->bytes,retained_replay,&ctx,&completed)!=PW_VK_STREAM_OK)status=STATUS_UNSUCCESSFUL;
 }else{
  /* Includes allocation failure and malformed input. Original preflight still
   * decides the result; no native dispatch has happened on the attempted path. */
  if(ctx.arena&&ctx.arena!=ctx.local)free(ctx.arena);
  ctx.arena=ctx.local;ctx.capacity=sizeof(ctx.local);
  if(pw_vk_stream_replay(UlongToPtr(p->batch),p->bytes,preflight,&ctx,&completed)!=PW_VK_STREAM_OK)status=ctx.failure?ctx.failure:STATUS_INVALID_PARAMETER;
  else if(pw_vk_stream_replay(UlongToPtr(p->batch),p->bytes,replay,&ctx,&completed)!=PW_VK_STREAM_OK)status=STATUS_UNSUCCESSFUL;
 }'''
assert old in text;text=text.replace(old,new,1)
save(original/'pw_vk_batch_unix.c',text,'dlls/winevulkan/pw_vk_batch_unix.c')
(reports/'native270-decode.patch').write_text(''.join(patch))
(reports/'native270-decode-source.json').write_text(json.dumps({'inputs':inputs,'outputs':{str(p):sha(p) for p in stage.iterdir()},'stack_region_bytes':4096,'retention_heap_budget_bytes':131072,'fallback_heap_budget_bytes':4194304,'metadata_records':128},indent=2)+'\n')
print(stage)

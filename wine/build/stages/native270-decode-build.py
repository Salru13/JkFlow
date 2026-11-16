"""Compile actual old/candidate decoder and effects fixture in private paths."""
from pathlib import Path
import hashlib,json,os,resource,subprocess,sys
repo=Path(sys.argv[1]);base=Path(os.environ['JKFLOW_BUILD_ROOT']);out=base/'native270-decode';out.mkdir(exist_ok=True)
reports=repo/'research/native-wine';source=base/'native247-wine-source';d=source/'dlls/winevulkan'
receipt=reports/'native270-decode-build-commands.json'
commands=json.loads(receipt.read_text()) if receipt.exists() else []
env=os.environ.copy();env['PATH']=str(base/'private-bin')+':'+str(base/'deps/rootfs/usr/bin')+':'+env['PATH']
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def run(args,expected=0):
 p=subprocess.run(list(map(str,args)),env=env,cwd=out,text=True,capture_output=True)
 commands.append(dict(command=list(map(str,args)),exit=p.returncode,stdout=p.stdout,stderr=p.stderr))
 (reports/'native270-decode-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 if p.returncode!=expected:print(p.stdout+p.stderr);raise SystemExit(1)
 return p
fixture=(reports/'shared-prospero-108194e/tests/lab/vk_generated_replay.c').read_text()
count='''static _Thread_local uint64_t decode_calls,effect_hash;
static _Thread_local unsigned native_failure_at,strict_lifecycle,query_available,destroyed;
int __real_pw_vk_generated_decode(unsigned,const void *,size_t,void *,size_t,void **);
int __wrap_pw_vk_generated_decode(unsigned code,const void *wire,size_t bytes,void *arena,size_t capacity,void **args)
{decode_calls++;return __real_pw_vk_generated_decode(code,wire,bytes,arena,capacity,args);}
#ifdef PW270_CANDIDATE
int __real_pw_vk_generated_decode_used(unsigned,const void *,size_t,void *,size_t,void **,size_t *);
int __wrap_pw_vk_generated_decode_used(unsigned code,const void *wire,size_t bytes,void *arena,size_t capacity,void **args,size_t *used)
{decode_calls++;return __real_pw_vk_generated_decode_used(code,wire,bytes,arena,capacity,args,used);}
#endif
'''
fixture=fixture.replace('static _Thread_local uint64_t scalar_calls;',count+'static _Thread_local uint64_t scalar_calls;')
fixture=fixture.replace('static atomic_uint allocations,releases;', 'static atomic_uint allocations,releases;\nstatic atomic_ullong allocation_bytes;')
fixture=fixture.replace('assert(bytes==PW_VK_CODEC_DECODE_BYTES);','assert(bytes==PW_VK_CODEC_DECODE_BYTES||bytes==128*1024);atomic_fetch_add(&allocation_bytes,bytes);')
fixture=fixture.replace(' if(code==unix_vkCmdPipelineBarrier2){',''' if(strict_lifecycle)assert(!destroyed);
 if(code==unix_vkResetQueryPoolEXT){
  struct vkResetQueryPoolEXT_params *p=params;assert(p->device==(VkDevice)(uintptr_t)0x5678&&p->queryPool==0x9911&&p->firstQuery==7&&p->queryCount==3);query_available=0;
 }else if(code==unix_vkGetDescriptorEXT){
  struct vkGetDescriptorEXT_params *p=params;assert(p->pDescriptorInfo->type==VK_DESCRIPTOR_TYPE_SAMPLER&&*p->pDescriptorInfo->data.pSampler==0x6655&&p->dataSize==16);
  for(unsigned i=0;i<16;i++)((unsigned char *)p->pDescriptor)[i]=0xa0+i;
 }else if(code==unix_vkCmdInsertDebugUtilsLabelEXT){
  struct vkCmdInsertDebugUtilsLabelEXT_params *p=params;assert(!strcmp(p->pLabelInfo->pLabelName,"retained label")&&p->pLabelInfo->color[3]==4);
 }else if(code==unix_vkCmdPipelineBarrier2){''',1)
fixture=fixture.replace('assert(p->descriptorWriteCount==1&&p->pDescriptorWrites[0].pBufferInfo->buffer==0x4433);','''assert(p->descriptorWriteCount==1);
  if(p->pDescriptorWrites[0].descriptorType==VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK){
   const VkWriteDescriptorSetInlineUniformBlock *q=p->pDescriptorWrites[0].pNext;assert(q&&q->sType==VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK&&q->dataSize==4);
   assert(!memcmp(q->pData,"\\1\\3\\5\\7",4));
  }else assert(p->pDescriptorWrites[0].pBufferInfo->buffer==0x4433);''')
fixture=fixture.replace('assert(p->device==(VkDevice)(uintptr_t)0x5678&&!p->pAllocator);','assert(p->device==(VkDevice)(uintptr_t)0x5678&&!p->pAllocator);destroyed=1;')
fixture=fixture.replace(' effects++;return STATUS_SUCCESS;',' effect_hash=effect_hash*UINT64_C(1099511628211)+code;effects++;return native_failure_at==effects?STATUS_UNSUCCESSFUL:STATUS_SUCCESS;',1)
fixture=fixture.replace('int main(int argc,char **argv)',(reports/'native270-decode-fixture-extra.inc').read_text()+'\nint main(int argc,char **argv)',1)
fixture=fixture.replace(' return 0;\n}', ' return 0;\n}')
pos=fixture.rindex(' return 0;\n}')
fixture=fixture[:pos]+' extra_tests(argc==2&&!strcmp(argv[1],"require-single"));\n'+fixture[pos:]
(out/'fixture.c').write_text(fixture);(reports/'native270-decode-fixture.c').write_text(fixture)
common=['-std=gnu11','-O2','-Wall','-Wextra','-Werror','-D__WINESRC__','-DWINE_UNIX_LIB','-D_WIN64','-I'+str(base/'wine-build/include'),'-I'+str(source/'include')]
for kind in sys.argv[2:] or ['baseline','candidate']:
 stage=d if kind=='baseline' else out/'source'
 exe=out/(kind+'-replay')
 inputs=[out/'fixture.c',stage/'pw_vk_batch_unix.c',d/'pw_vk_codec.c',stage/'pw_vk_generated.c',d/'pw_vk_wire.c',d/'pw_vk_command_stream.c']
 # Reuse content-identical host objects from CPU fixture; link wrappers still
 # intercept their external references. They contain both old and new APIs.
 cache=[out/(n+'-cpu.o') for n in ['pw_vk_codec','pw_vk_generated','pw_vk_wire','pw_vk_command_stream']]
 if all(p.exists() for p in cache) and all(p.stat().st_mtime>=s.stat().st_mtime for p,s in zip(cache,[d/'pw_vk_codec.c',out/'source/pw_vk_generated.c',d/'pw_vk_wire.c',d/'pw_vk_command_stream.c'])):
  inputs=[out/'fixture.c',stage/'pw_vk_batch_unix.c',*cache]
 flags=['-DPW270_CANDIDATE','-Wl,--wrap=pw_vk_generated_decode_used'] if kind=='candidate' else []
 run(['cc',*common,'-I'+str(stage),'-I'+str(d),*flags,*inputs,'-pthread','-Wl,--wrap=pw_vk_generated_decode','-Wl,--wrap=pw_vk_codec_value','-Wl,--wrap=malloc','-Wl,--wrap=free','-o',exe])
 run([exe])
 run([exe,'require-single'],0 if kind=='candidate' else -6)
 print('PASS',kind,'semantic + expected single-decode result',flush=True)

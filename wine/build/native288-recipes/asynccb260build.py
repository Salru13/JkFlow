#!/usr/bin/env python3
"""Unix half of asynccb260: vkBeginCommandBuffer/vkEndCommandBuffer as stream records.

Builds on asyncbuild260.py (async replay worker, batch version 3) and adds:

  * two wire opcodes, PW260_BEGIN_CB (0x2601, payload: u32 command buffer,
    u32 flags) and PW260_END_CB (0x2602, payload: u32 command buffer), which
    pw_wine_vk_replay runs in sequence order like the vkCmd* records. They are
    not generated records, so the async scan queues them for the worker.
  * the capability "__wine_pw_vk_async_cb260" = 0x50572601, answered only when
    async replay is on and PW_VK_ASYNC_CB is not "0". The PE half
    (direct260build.py) encodes the opcodes only after this answer, so an old
    PE half or an old Unix half never sees them.

Why: with four players DXVK's CS thread ended about 55 command buffers per
frame, and each synchronous vkEndCommandBuffer drained the whole worker queue
(about 150 ms of waiting per 1.5 s, PW_VK_ASYNC_WAITS code 463). Ordering is
kept: vkQueueSubmit is a progress call and still drains everything before it.
A driver failure is logged once (PW_VK_ASYNC_CB260 ... failed).

Run in WSL. Same controls as asyncbuild260.py (unpatched relink must reproduce
the admitted ELF and PRX byte for byte). Output: native288-asynccb260/.
"""
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import asyncbuild260 as base  # noqa: E402

OUT = base.BASE / 'native288-asynccb260'
WORK = OUT / 'inputs'

HELPER = r'''#include <stdio.h>
/* native288 asynccb260, see asynccb260build.py. */
#define PW260_BEGIN_CB 0x2601u
#define PW260_END_CB 0x2602u
static int pw260_validate(uint32_t op,const void *v,size_t n)
{
 if(op==PW260_BEGIN_CB)return v&&n==8;
 if(op==PW260_END_CB)return v&&n==4;
 return pw_vk_wire_validate(op,v,n);
}
static void pw260_cb_failed(const char *what,VkResult result)
{
 static int reported;
 if(!__atomic_exchange_n(&reported,1,__ATOMIC_RELAXED)){
  char line[96];snprintf(line,sizeof(line),"PW_VK_ASYNC_CB260 %s failed result=%d\n",what,(int)result);
  __wine_dbg_output(line);
 }
}
'''

REPLAY_CASES = r''' case PW260_BEGIN_CB:{
  VkCommandBufferBeginInfo info={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,NULL,pw_vk_wire_u32(p+4),NULL};
  VkResult result=cb->device->p_vkBeginCommandBuffer(cb->host.command_buffer,&info);
  if(result!=VK_SUCCESS)pw260_cb_failed("begin",result);
  break;}
 case PW260_END_CB:{
  VkResult result=cb->device->p_vkEndCommandBuffer(cb->host.command_buffer);
  if(result!=VK_SUCCESS)pw260_cb_failed("end",result);
  break;}
'''

ENABLED_FN = r'''
/* asynccb260 capability: only with the async worker, off with PW_VK_ASYNC_CB=0. */
int pw_vk_async_cb260_enabled(void)
{
 const char *value=getenv("PW_VK_ASYNC_CB");
 if(value&&value[0]=='0'&&!value[1])return 0;
 return pw_vk_async_enabled();
}
'''

CAP_EXTRA = ('    if (!strcmp(name, "__wine_pw_vk_async_cb260")) { extern int pw_vk_async_cb260_enabled(void); '
             'if (pw_vk_async_cb260_enabled()) return 0x50572601u; }\n')


def patched_batch_source(text):
    newline = '\r\n' if '\r\n' in text else '\n'
    t = text.replace('\r\n', '\n')
    assert t.count('pw_vk_wire_validate(') == 3, t.count('pw_vk_wire_validate(')
    t = t.replace('pw_vk_wire_validate(', 'pw260_validate(')
    anchor = 'int pw_wine_vk_replay(uint32_t op,const void *wire,size_t bytes)\n'
    assert t.count(anchor) == 1
    t = t.replace(anchor, HELPER + anchor)
    default = ' default:return 0;\n }\n return 1;\n}\n'
    assert t.count(default) == 1
    t = t.replace(default, REPLAY_CASES + default)
    t = t + ENABLED_FN
    return t.replace('\n', newline)


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    WORK.mkdir(parents=True)
    source = WORK / 'native280-decoder-config-candidate.c'
    source.write_text(patched_batch_source(base.BASE_SOURCE.read_text()))
    shutil.copy(HERE / 'pw_vk_async_core.h', WORK)
    base.BASE_SOURCE = source
    base.HERE = WORK  # manifest goes here, not over asyncbuild260-manifest.json
    base.OUT = OUT / 'build'
    base.CAP_NEW = CAP_EXTRA + base.CAP_NEW
    base.main()
    shutil.copy(WORK / 'asyncbuild260-manifest.json', HERE / 'asynccb260-manifest.json')


if __name__ == '__main__':
    main()

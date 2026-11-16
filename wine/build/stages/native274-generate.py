"""Relocate only the missing1046 guards; preserve1054 and every driver call."""
from pathlib import Path
import difflib,hashlib,json,sys
import os  # JKFLOW_BUILD_ROOT
repo=Path(sys.argv[1]);base=Path(os.environ['JKFLOW_BUILD_ROOT']);reports=repo/'research/native-wine';out=base/'native274-win32u';out.mkdir(exist_ok=True)
original=base/'native247-wine-source/dlls/win32u/vulkan.c';text=original.read_text();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
expected=json.loads((reports/'native271-upstream-diff/wine-runtime-proof.json').read_text())['source_hashes'][str(original)];assert sha(original)==expected
def body(source,name):
 start=source.index('static VkResult '+name+'(');brace=source.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return start,end,source[start:end]
for name in ['win32u_vkAcquireNextImage2KHR','win32u_vkAcquireNextImageKHR']:
 start,end,fn=body(text,name)
 decl='    struct surface *surface = swapchain->surface;\n    RECT client_rect;'
 assert fn.count(decl)==1;fn=fn.replace(decl,'#ifndef __PROSPERO__\n'+decl+'\n#endif',1)
 start_guard=fn.index('    if (!res && get_surface_rect(');end_guard=fn.index('\n\n    return res;',start_guard)
 fn=fn[:start_guard]+'#ifndef __PROSPERO__\n'+fn[start_guard:end_guard]+'\n#endif'+fn[end_guard:]
 text=text[:start]+fn+text[end:]
start,end,fn=body(text,'win32u_vkQueuePresentKHR')
start_guard=fn.index('        else if (!extents_equals(');brace=fn.index('{',start_guard);depth=1;end_guard=brace+1
while depth:depth+=(fn[end_guard]=='{')-(fn[end_guard]=='}');end_guard+=1
fn=fn[:start_guard]+'#ifndef __PROSPERO__\n'+fn[start_guard:end_guard]+'\n#endif'+fn[end_guard:]
text=text[:start]+fn+text[end:]
tail='#ifndef __PROSPERO__\n#endif\n'*5
assert text.endswith(tail);text=text[:-len(tail)]
candidate=out/'vulkan.c';candidate.write_text(text);(reports/'native274-vulkan.c').write_text(text)
(reports/'native274-1046.patch').write_text(''.join(difflib.unified_diff(original.read_text().splitlines(True),text.splitlines(True),fromfile='a/dlls/win32u/vulkan.c',tofile='b/dlls/win32u/vulkan.c')))
# Explicitly pin the capabilities region so the1054 resolution policy cannot drift.
def capabilities(s):return s[s.index('static void adjust_surface_capabilities('):s.index('static VkResult win32u_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(')]
assert capabilities(original.read_text())==capabilities(text)
(reports/'native274-source.json').write_text(json.dumps({'original':str(original),'original_sha256':sha(original),'candidate':str(candidate),'candidate_sha256':sha(candidate),'patch_sha256':sha(reports/'native274-1046.patch'),'capabilities_1054_region_identical':True,'moved_empty_eof_guard_pairs':5,'source_contract':'Only __PROSPERO__ synthetic resize SUBOPTIMAL checks guarded; driver calls/results, invalid-window checks and1054 extents retained'},indent=2)+'\n')
print('PASS isolated1046 guard relocation;1054 extent region byte-identical')

"""impname.py in out: rename the winevulkan.dll import of a PE32 to wvkdirect.dll."""
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read())
pe = struct.unpack_from('<I', d, 60)[0]; n = struct.unpack_from('<H', d, pe + 6)[0]; osz = struct.unpack_from('<H', d, pe + 20)[0]
opt = pe + 24; imp = struct.unpack_from('<I', d, opt + 104)[0]
S = []
for i in range(n):
    r = opt + osz + 40 * i; vs, va, rs, ro = struct.unpack_from('<IIII', d, r + 8); S.append((va, max(vs, rs), ro))
def o(v):
    for va, sz, ro in S:
        if va <= v < va + sz: return ro + v - va
i = o(imp); hits = 0
while True:
    name = struct.unpack_from('<I', d, i + 12)[0]
    if not name: break
    x = o(name); s = bytes(d[x:d.index(0, x)])
    if s.lower() == b'vulkan-1.dll':
        d[x:x + 12] = b"wvkdir26.dll"; hits += 1
    i += 20
assert hits == 1, hits
open(sys.argv[2], 'wb').write(d); print('patched import')

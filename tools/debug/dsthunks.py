"""dsthunks.py: lists DSOUND member thunks (this adjust, null-checked) with their ret size and inner call."""
import re, struct, os

d = open(os.path.join('assets', 'default.xbe'), 'rb').read()
base = 0x10000
ns = struct.unpack_from('<I', d, 0x11C)[0]
sh = struct.unpack_from('<I', d, 0x120)[0] - base
for i in range(ns):
    f, va, vs, ro, rs, nm = struct.unpack_from('<IIIIII', d, sh + i * 56)
    if d[nm - base:nm - base + 6] != b'DSOUND':
        continue
    sec = d[ro:ro + rs]
    for m in re.finditer(rb'\x23\xc8\x51\xe8(....)\xc2(.)\x00', sec, re.S):
        e = m.start()
        rel = struct.unpack('<i', m.group(1))[0]
        tgt = (va + m.end() - 4 - 3 + rel + 4 - 4 + 0) & 0xFFFFFFFF
        callsite = va + m.start() + 3
        tgt = (callsite + 5 + rel) & 0xFFFFFFFF
        # walk back to the thunk start: the adjust is a lea/add before neg
        s = sec.rfind(b'\x8b\x44\x24\x04', max(0, e - 40), e)
        s = (va + s) if s >= 0 else None
        print('thunk %s ret %02x inner %x' % (hex(s) if s else '?', m.group(2)[0], tgt))

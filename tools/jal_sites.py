# List the call sites (jal) of target addresses in the main segment: jal_sites.py rom.z64 syms.toml target...
import re, struct, sys
rom = open(sys.argv[1], 'rb').read()
syms = open(sys.argv[2]).read().split('[[section]]')[1]
funcs = [(int(v, 16), int(z, 16)) for v, z in re.findall(r'vram = (0x\w+), size = (0x\w+)', syms)]
for t in (int(x, 16) for x in sys.argv[3:]):
    for fva, fsz in funcs:
        for va in range(fva, fva + fsz, 4):
            w = struct.unpack('>I', rom[va - 0x80000400 + 0x1000: va - 0x80000400 + 0x1004])[0]
            if w >> 26 == 3 and (0x80000000 | (w & 0x3FFFFFF) << 2) == t:
                print('%08X called from %08X in func_%08X' % (t, va, fva))

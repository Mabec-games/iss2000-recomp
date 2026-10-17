# Find reads of field OFF of the struct pointed to by global PTR: "lw r, lo(PTR)(..)" then "l* x, OFF(r)" within a window.
import sys, re, struct, rabbitizer
d = open(sys.argv[1], 'rb').read(); s = open(sys.argv[2]).read()
ptr = int(sys.argv[3], 16); off = int(sys.argv[4], 16)
hi = (ptr + 0x8000) >> 16; lo = ptr & 0xFFFF
for sec in s.split('[[section]]')[1:]:
    name = re.search(r'name = "(\w+)"', sec).group(1); rom = int(re.search(r'rom = (0x\w+)', sec).group(1), 16)
    base = int(re.search(r'vram = (0x\w+)', sec).group(1), 16)
    for fva, fsz in [(int(a, 16), int(b, 16)) for a, b in re.findall(r'vram = (0x\w+), size = (0x\w+)', sec)]:
        luis, holders = {}, {}
        for i in range(0, fsz, 4):
            va = fva + i; w = struct.unpack('>I', d[rom + va - base:rom + va - base + 4])[0]
            ins = rabbitizer.Instruction(w, va); n = ins.getOpcodeName()
            if n == 'lui':
                luis[ins.rt.name] = (w & 0xFFFF)
                holders.pop(ins.rt.name, None); continue
            if n == 'lw' and luis.get(ins.rs.name) == hi and (w & 0xFFFF) == lo:
                holders[ins.rt.name] = va; continue
            if ins.doesLoad() or ins.doesStore():
                if ins.rs.name in holders and ins.getProcessedImmediate() == off:
                    print('%-12s %08X in func %08X: %s' % (name, va, fva, ins.disassemble()))
            if ins.hasOperandAlias(rabbitizer.OperandType.cpu_rt) and ins.doesLoad():
                holders.pop(ins.rt.name, None)

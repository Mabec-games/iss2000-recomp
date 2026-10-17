# Find code (all sections) that accesses a given data address via lui/%lo pairs. Usage: xref.py rom syms 0xADDR
import sys, re, struct, rabbitizer
d = open(sys.argv[1], 'rb').read(); s = open(sys.argv[2]).read(); target = int(sys.argv[3], 16)
hi = (target + 0x8000) >> 16; lo = target & 0xFFFF
for sec in s.split('[[section]]')[1:]:
    name = re.search(r'name = "(\w+)"', sec).group(1); rom = int(re.search(r'rom = (0x\w+)', sec).group(1), 16)
    base = int(re.search(r'vram = (0x\w+)', sec).group(1), 16)
    funcs = [(int(a, 16), int(b, 16)) for a, b in re.findall(r'vram = (0x\w+), size = (0x\w+)', sec)]
    for fva, fsz in funcs:
        luis = {}
        for i in range(0, fsz, 4):
            va = fva + i; w = struct.unpack('>I', d[rom + va - base:rom + va - base + 4])[0]
            ins = rabbitizer.Instruction(w, va); n = ins.getOpcodeName()
            if n == 'lui':
                if (w & 0xFFFF) == hi: luis[ins.rt.name] = va
                else: luis.pop(ins.rt.name, None)
            elif ins.hasOperandAlias(rabbitizer.OperandType.cpu_rs) and ins.rs.name in luis and (w & 0xFFFF) == lo and n not in ('lui',):
                print('%-12s %08X in func %08X: %s' % (name, va, fva, ins.disassemble()))

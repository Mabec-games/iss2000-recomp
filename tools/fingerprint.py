# Print a one-line fingerprint for every function in a section of the symbol TOML:
# addr size | hw regs touched | cop0/cache ops | callees
import sys, struct, re, rabbitizer
ROM = open(sys.argv[1], 'rb').read()
syms = open(sys.argv[2]).read()
section = sys.argv[3]
lo = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
hi = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0xFFFFFFFF
names = {}
if len(sys.argv) > 6:
    for l in open(sys.argv[6]):
        p = l.split()
        if len(p) >= 2: names[int(p[0], 16)] = p[1]
sec = syms.split('[[section]]')
for s in sec:
    if 'name = "%s"' % section not in s: continue
    rom = int(re.search(r'rom = (0x\w+)', s).group(1), 16)
    base = int(re.search(r'vram = (0x\w+)', s).group(1), 16)
    for va, sz in re.findall(r'vram = (0x\w+), size = (0x\w+)', s):
        va, sz = int(va, 16), int(sz, 16)
        if not (lo <= va < hi): continue
        hw, cop, calls, lui = set(), set(), [], {}
        for i in range(0, sz, 4):
            w = struct.unpack('>I', ROM[rom + va - base + i: rom + va - base + i + 4])[0]
            ins = rabbitizer.Instruction(w, va + i)
            n = ins.getOpcodeName()
            if n == 'lui':
                imm = w & 0xFFFF
                if 0xA400 <= imm < 0xA500 or imm in (0xBFC0, 0xB000, 0xBFF0): hw.add('%04X' % imm)
            if n in ('mfc0', 'mtc0', 'cache', 'eret', 'tlbwi', 'tlbp', 'tlbr', 'tlbwr', 'ctc1', 'cfc1', 'sync'):
                cop.add(n)
            if n == 'jal':
                t = ins.getInstrIndexAsVram()
                calls.append(names.get(t, '%08X' % t))
        nm = names.get(va, '')
        print('%08X %5X %-22s hw[%s] cop[%s] -> %s' % (va, sz, nm, ','.join(sorted(hw)), ','.join(sorted(cop)), ' '.join(dict.fromkeys(calls))))

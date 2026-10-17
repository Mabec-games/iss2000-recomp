# Disassemble around an address in a section: ctx.py rom syms section addr [before] [after]
import sys, re, struct, rabbitizer
d = open(sys.argv[1], 'rb').read(); s = open(sys.argv[2]).read(); name = sys.argv[3]; t = int(sys.argv[4], 16)
b = int(sys.argv[5]) if len(sys.argv) > 5 else 12; a = int(sys.argv[6]) if len(sys.argv) > 6 else 16
sec = [x for x in s.split('[[section]]') if 'name = "%s"' % name in x][0]
rom = int(re.search(r'rom = (0x\w+)', sec).group(1), 16); base = int(re.search(r'vram = (0x\w+)', sec).group(1), 16)
starts = {int(x, 16) for x in re.findall(r'vram = (0x\w+), size', sec)}
for va in range(t - 4 * b, t + 4 * a, 4):
    w = struct.unpack('>I', d[rom + va - base:rom + va - base + 4])[0]
    print('%s%08X%s %s' % ('\n' + 'func_%08X:\n' % va if va in starts else '', va, ' >' if va == t else '  ', rabbitizer.Instruction(w, va).disassemble()))

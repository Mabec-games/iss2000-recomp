# Print disassembly of functions (by vram) from a section in the symbol TOML.
import sys, struct, re, rabbitizer
ROM = open(sys.argv[1], 'rb').read(); syms = open(sys.argv[2]).read(); section = sys.argv[3]
for s in syms.split('[[section]]'):
    if 'name = "%s"' % section not in s: continue
    rom = int(re.search(r'rom = (0x\w+)', s).group(1), 16); base = int(re.search(r'vram = (0x\w+)', s).group(1), 16)
    sizes = {int(a, 16): int(b, 16) for a, b in re.findall(r'vram = (0x\w+), size = (0x\w+)', s)}
    for a in sys.argv[4:]:
        va = int(a, 16); print('== %08X (0x%X)' % (va, sizes[va]))
        for i in range(0, sizes[va], 4):
            w = struct.unpack('>I', ROM[rom + va - base + i: rom + va - base + i + 4])[0]
            print('  %04X %s' % (i, rabbitizer.Instruction(w, va + i).disassemble()))

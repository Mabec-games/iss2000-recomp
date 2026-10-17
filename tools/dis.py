import sys, struct, rabbitizer
rom = open(sys.argv[1], 'rb').read()
off = int(sys.argv[2], 16); vram = int(sys.argv[3], 16); n = int(sys.argv[4])
for i in range(n):
    w = struct.unpack('>I', rom[off+i*4:off+i*4+4])[0]
    ins = rabbitizer.Instruction(w, vram + i*4)
    print("%06X %08X %08X  %s" % (off+i*4, vram+i*4, w, ins.disassemble()))

# Scan main + all known overlays for calls to ROM read / chunk copy / bzero with constant args.
import os, sys, struct, rabbitizer
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import versions
rom = open(sys.argv[1], 'rb').read()
V = versions.detect(rom)
segs = [(versions.MAIN_ROM, versions.MAIN_VRAM, V['text_end'] - versions.MAIN_VRAM)]
for l in open(sys.argv[2]):
    r, v, s, b = (int(x, 16) for x in l.split()); segs.append((r, v, s))
TGT = {V['read']: 'READ ', V['bzero']: 'BZERO', V['chunk']: 'CHUNK', V['file']: 'FILE '}
for (romoff, base, size) in segs:
    W = lambda va: struct.unpack('>I', rom[va - base + romoff: va - base + romoff + 4])[0]
    for va in range(base, base + size, 4):
        w = W(va)
        if w >> 26 != 3: continue
        t = 0x80000000 | ((w & 0x3FFFFFF) << 2)
        if t not in TGT: continue
        r = {'zero': 0}
        for pva in list(range(max(base, va - 96), va, 4)) + [va + 4]:
            ins = rabbitizer.Instruction(W(pva), pva); n = ins.getOpcodeName()
            ops = {}
            for nm, ot in (('rs', rabbitizer.OperandType.cpu_rs), ('rt', rabbitizer.OperandType.cpu_rt), ('rd', rabbitizer.OperandType.cpu_rd)):
                if ins.hasOperandAlias(ot): ops[nm] = getattr(ins, nm).name
            imm = ins.getProcessedImmediate() if ins.hasOperandAlias(rabbitizer.OperandType.cpu_immediate) else 0
            g = lambda k: r.get(ops.get(k))
            if (ins.isBranch() or ins.isJump()) and pva < va - 4: r = {'zero': 0}; continue
            if n == 'lui': r[ops['rt']] = (imm << 16) & 0xFFFFFFFF
            elif n == 'addiu' and g('rs') is not None: r[ops['rt']] = (g('rs') + imm) & 0xFFFFFFFF
            elif n == 'ori' and g('rs') is not None: r[ops['rt']] = g('rs') | (imm & 0xFFFF)
            elif n in ('addu', 'daddu') and g('rs') is not None and g('rt') is not None: r[ops['rd']] = (g('rs') + g('rt')) & 0xFFFFFFFF
            elif n == 'subu' and g('rs') is not None and g('rt') is not None: r[ops['rd']] = (g('rs') - g('rt')) & 0xFFFFFFFF
            elif not (ins.isBranch() or ins.isJump() or ins.doesStore()):
                for k in ('rd', 'rt'):
                    if k in ops and (k == 'rd' or 'rd' not in ops): r.pop(ops[k], None); break
        f = lambda v: '%08X' % v if v is not None else '????????'
        print('%s seg=%06X %08X %s' % (TGT[t], romoff, va, ' '.join('%s=%s' % (k, f(r.get(k))) for k in ('a0', 'a1', 'a2', 'a3'))))

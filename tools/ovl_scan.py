# Symbolically evaluate a0/a1/a2 before calls to the ROM-read wrapper to recover overlay loads.
import sys, struct, rabbitizer
rom = open(sys.argv[1], 'rb').read()
BASE, ROMOFF, TEXT_END = 0x80000400, 0x1000, 0x800691D0
READ, INVAL = 0x80003964, 0x8005C5A0
import os; READ2 = int(os.environ.get("READ2","0"),16)
W = lambda va: struct.unpack('>I', rom[va - BASE + ROMOFF: va - BASE + ROMOFF + 4])[0]
def regs_before(call_va, lookback=24):
    r = {'zero': 0}
    start = call_va - lookback * 4
    for va in list(range(start, call_va, 4)) + [call_va + 4]:  # include delay slot
        ins = rabbitizer.Instruction(W(va), va)
        n = ins.getOpcodeName()
        if ins.isBranch() or ins.isJump() and va != call_va:
            if va < call_va: r = {'zero': 0}; continue
        try:
            rd = ins.rd.name if ins.hasOperandAlias(rabbitizer.OperandType.cpu_rd) else None
        except Exception: rd = None
        rs = ins.rs.name if ins.hasOperandAlias(rabbitizer.OperandType.cpu_rs) else None
        rt = ins.rt.name if ins.hasOperandAlias(rabbitizer.OperandType.cpu_rt) else None
        imm = ins.getProcessedImmediate() if ins.hasOperandAlias(rabbitizer.OperandType.cpu_immediate) else None
        g = lambda x: r.get(x)
        if n == 'lui': r[rt] = (ins.getProcessedImmediate() << 16) & 0xFFFFFFFF
        elif n == 'addiu' and g(rs) is not None: r[rt] = (g(rs) + imm) & 0xFFFFFFFF
        elif n == 'ori' and g(rs) is not None: r[rt] = g(rs) | (imm & 0xFFFF)
        elif n in ('addu','daddu') and g(rs) is not None and g(rt) is not None: r[rd] = (g(rs)+g(rt)) & 0xFFFFFFFF
        elif n == 'subu' and g(rs) is not None and g(rt) is not None: r[rd] = (g(rs)-g(rt)) & 0xFFFFFFFF
        else:
            for dst in (rd, rt):
                if dst and n not in ('sw','sb','sh','sd','beq','bne','beql','bnel'): r.pop(dst, None)
    return r
out = []
for va in range(BASE, TEXT_END, 4):
    w = W(va)
    if w >> 26 == 3:
        t = 0x80000000 | ((w & 0x3FFFFFF) << 2)
        if t in (READ, INVAL, READ2):
            r = regs_before(va)
            a = [r.get(x) for x in ('a0','a1','a2','a3')]
            f = lambda v: '%08X' % v if v is not None else '????????'
            print('%s %08X a0=%s a1=%s a2=%s a3=%s' % ({READ:'READ ',INVAL:'INVAL'}.get(t,'CHUNK'), va, *map(f, a)))

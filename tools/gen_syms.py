# Generate an N64Recomp symbol TOML (sections + function boundaries) for ISS 2000.
#
# Function discovery: linear sweep per section. A function ends at `jr $ra` (+ delay slot) once no
# earlier branch in the function targets beyond it. Seeds: every jal target from every segment.
# Text end: the sweep stops when the next candidate does not decode cleanly up to a `jr $ra`.
import os, sys, struct, rabbitizer
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import versions

ROM = open(sys.argv[1], 'rb').read()
V = versions.detect(ROM)
OVL_TSV, OUT = sys.argv[2], sys.argv[3]
NAMES = {}
if len(sys.argv) > 4:
    for l in open(sys.argv[4]):
        p = l.split('#')[0].split()
        if len(p) == 2: NAMES[int(p[0], 16)] = p[1]

MAIN = ('main', versions.MAIN_ROM, versions.MAIN_VRAM, V['bss'] - versions.MAIN_VRAM)
segs = [MAIN]
for l in open(OVL_TSV):
    r, v, s, b = (int(x, 16) for x in l.split())
    segs.append(('ovl_%06X' % r, r, v, s))

def words(seg):
    _, r, v, s = seg
    return [struct.unpack('>I', ROM[r + i:r + i + 4])[0] for i in range(0, s & ~3, 4)]

JR_RA = 0x03E00008
TRAPS = {'tge', 'tgeu', 'tlt', 'tltu', 'teq', 'tne', 'tgei', 'tgeiu', 'tlti', 'tltiu', 'teqi', 'tnei'}

def instr(w, va):
    return rabbitizer.Instruction(w, va)

def branch_target(ins):
    if ins.isBranch() or (ins.isJump() and not ins.isJumpWithAddress() is False and ins.getOpcodeName() == 'j'):
        try:
            return ins.getBranchVramGeneric()
        except Exception:
            return None
    return None

def read_word(va, cur_base=None, cur_ws=None):
    """Read a word by vram: from the segment being swept if it covers va (several overlays share
    vram slots, so this must win), else main, else the first overlay covering it."""
    if cur_ws is not None and cur_base <= va < cur_base + len(cur_ws) * 4:
        return cur_ws[(va - cur_base) // 4]
    for _, r, b, sz in segs:
        if b <= va < b + sz:
            return struct.unpack('>I', ROM[r + va - b:r + va - b + 4])[0]
    return None

def jump_table_targets(ws, base, start_idx, i, consts):
    """At a `jr $reg` (not $ra) at index i, try to recover the jump table and return its targets."""
    count, tbl = None, None
    for k in range(i - 1, max(start_idx, i - 12) - 1, -1):
        ins = instr(ws[k], base + k * 4); n = ins.getOpcodeName()
        if n == 'sltiu' and count is None:
            count = ins.getProcessedImmediate()
        if n == 'lw' and tbl is None:
            # lw rX, lo(rY): table = const(rY) + lo, or rY = addu(rA, rB) with a const operand
            off = ins.getProcessedImmediate(); ry = ins.rs.name
            for kk in range(k - 1, max(start_idx, k - 6) - 1, -1):
                j2 = instr(ws[kk], base + kk * 4)
                if j2.getOpcodeName() == 'addu' and j2.rd.name == ry:
                    for opnd in (j2.rs.name, j2.rt.name):
                        if consts.get(opnd) is not None and consts[opnd] >= 0x80000000:
                            tbl = consts[opnd] + off
                    break
                if j2.getOpcodeName() == 'lui' and j2.rt.name == ry:
                    tbl = ((j2.getProcessedImmediate() << 16) + off) & 0xFFFFFFFF
                    break
    if tbl is None or not count or count > 512:
        return []
    jr_va = base + i * 4
    out = []
    for e in range(count):
        w = read_word(tbl + e * 4, base, ws)
        # targets must be near the jr and inside the function being swept (after its start)
        if w is None or not (base + start_idx * 4 <= w < jr_va + 0x4000):
            return []
        out.append(w)
    return out

def sweep_function(ws, base, start_idx, limit_idx):
    """Return end index (exclusive) of a function starting at start_idx, or None if it doesn't look like code."""
    max_target = base + start_idx * 4
    consts = {}
    i = start_idx
    while i < limit_idx:
        va = base + i * 4
        ins = instr(ws[i], va)
        if not ins.isValid():
            return None
        n = ins.getOpcodeName()
        if n in TRAPS:
            return None  # this compiler never emits traps; it's data
        if n == 'j':
            jt = (va & 0xF0000000) | (ins.getInstrIndexAsVram() & 0x0FFFFFFF)
            if not (base <= jt < base + len(ws) * 4 or versions.MAIN_VRAM <= jt < V['text_end']):
                return None
        if n == 'lui':
            consts[ins.rt.name] = (ins.getProcessedImmediate() << 16) & 0xFFFFFFFF
        elif n in ('addiu', 'ori') and consts.get(ins.rs.name) is not None and ins.rt.name != 'zero':
            imm = ins.getProcessedImmediate()
            consts[ins.rt.name] = (consts[ins.rs.name] + imm) & 0xFFFFFFFF if n == 'addiu' else consts[ins.rs.name] | (imm & 0xFFFF)
        elif n not in ('lui', 'addiu', 'ori', 'addu'):
            # any other write makes the register's constant stale (e.g. lui v0 / lhu v0 before a jump table);
            # addu keeps it so `addu at, at, idx` still sees the table base in `at`
            for reg, writes in (('rd', ins.modifiesRd()), ('rt', ins.modifiesRt())):
                if writes:
                    consts.pop(getattr(ins, reg).name, None)
        if n == 'jr' and ins.rs.name != 'ra':
            for t in jump_table_targets(ws, base, start_idx, i, consts):
                max_target = max(max_target, t)
        if ins.isBranch() or n == 'j':
            t = ins.getBranchVramGeneric() if ins.isBranch() else ((va & 0xF0000000) | ins.getInstrIndexAsVram() & 0x0FFFFFFF)
            if base + start_idx * 4 <= t < base + limit_idx * 4:
                max_target = max(max_target, t)
            if (n == 'j' or n == 'b' or (n == 'beq' and ins.rs.name == 'zero' and ins.rt.name == 'zero')) and va + 8 > max_target:
                # unconditional jump with nothing branching past it: end unless target is past us
                if not (va < t <= base + limit_idx * 4 and t > va + 8):
                    return i + 2
        if ws[i] == JR_RA and va + 8 > max_target:
            return i + 2
        i += 1
    return None

# Known hard text ends (data that happens to decode as CPU code, e.g. RSP microcode)
TEXT_END_OVERRIDE = {'main': V['text_end']}

def find_functions(seg, seeds):
    name, r, base, size = seg
    ws = words(seg)
    n = len(ws)
    if name in TEXT_END_OVERRIDE:
        n = (TEXT_END_OVERRIDE[name] - base) // 4
    seed_idx = sorted({(s - base) // 4 for s in seeds if base <= s < base + n * 4})
    funcs = []
    i = 0
    while i < n:
        # skip nop padding
        while i < n and ws[i] == 0:
            i += 1
        if i >= n:
            break
        end = sweep_function(ws, base, i, n)
        if end is None:
            break  # reached data
        # split at any seed (jal target) strictly inside this function
        inner = [s for s in seed_idx if i < s < end]
        starts = [i] + inner
        for a, b in zip(starts, starts[1:] + [end]):
            funcs.append((base + a * 4, (b - a) * 4))
        i = end
    text_end = funcs[-1][0] + funcs[-1][1] if funcs else base
    # seeds beyond the detected text end mean the sweep stopped too early
    missed = [base + s * 4 for s in seed_idx if base + s * 4 >= text_end]
    return funcs, text_end, missed

def jal_targets(seg, text_end):
    name, r, base, size = seg
    out = set()
    for i, w in enumerate(words(seg)):
        if base + i * 4 >= text_end:
            break
        if w >> 26 == 3:
            out.add(((base + i * 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    return out

# Pass 1: rough sweep without seeds to learn text ends, then collect jal targets globally.
# Seeds are per-section: only a section's own jal targets may split its functions, because other
# overlays sharing the same vram slot would otherwise inject bogus split points.
own = {seg[0]: set() for seg in segs}
for it in range(20):
    results = {}
    changed = False
    for seg in segs:
        funcs, tend, missed = find_functions(seg, own[seg[0]])
        results[seg[0]] = (seg, funcs, tend, missed)
        t = jal_targets(seg, tend)
        if t != own[seg[0]]:
            own[seg[0]] = t; changed = True
    if not changed:
        break
seeds = set().union(*own.values())

total = 0
unresolved = []
with open(OUT, 'w') as f:
    for seg in segs:
        seg, funcs, tend, missed = results[seg[0]]
        name, r, base, size = seg
        if not funcs:
            print('%-11s data-only, skipped' % name); continue
        f.write('[[section]]\nname = "%s"\nrom = 0x%X\nvram = 0x%X\nsize = 0x%X\n\nfunctions = [\n' % (name, r, base, tend - base))
        for va, sz in funcs:
            fname = NAMES.get(va) if name == 'main' else None
            fname = fname or ('func_%08X' % va if name == 'main' else 'func_%08X_%s' % (va, name))
            f.write('    { name = "%s", vram = 0x%08X, size = 0x%X },\n' % (fname, va, sz))
        f.write(']\n\n')
        total += len(funcs)
        print('%-11s rom %06X vram %08X text %06X / %06X  funcs %4d  missed-seeds %d' % (name, r, base, tend - base, size, len(funcs), len(missed)))
        for m in missed:
            unresolved.append((name, m))

# jal targets that don't land on any function start in any section
starts = defaultdict(list)
for name, (seg, funcs, tend, missed) in results.items():
    for va, sz in funcs:
        starts[va].append(name)
nomatch = sorted(t for t in seeds if t not in starts)
print('total functions:', total)
mainstarts = {va for va, sz in results['main'][1]}
for va, nm in sorted(NAMES.items()):
    if va not in mainstarts: print('WARNING: named address is not a function start: %08X %s' % (va, nm))
print('jal targets with no function anywhere:', len(nomatch), ' '.join('%08X' % t for t in nomatch[:40]))

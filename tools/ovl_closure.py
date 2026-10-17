# Iterate ovl_scan2 until no new overlays are found; writes analysis/overlays.tsv (rom, vram, size, bss)
import subprocess, sys, re
py, rom, tsv = sys.executable, sys.argv[1], sys.argv[2]
def load():
    return {(int(a,16),int(b,16)):(int(c,16),int(d,16)) for a,b,c,d in (l.split() for l in open(tsv))}
while True:
    ovl = load()
    out = subprocess.run([py, '-I', 'tools/ovl_scan2.py', rom, tsv], capture_output=True, text=True).stdout.splitlines()
    P = [dict(re.findall(r'(a\d)=(\S+)', l), kind=l[:5].strip()) for l in out]
    new = 0
    for i, p in enumerate(P):
        if p['kind'] == 'READ': r, v, s = p['a0'], p['a1'], p['a2']
        elif p['kind'] == 'CHUNK':
            if '?' in p['a0'] + p['a1']: continue
            r, v, s = p['a0'], p['a2'], '%X' % (int(p['a1'],16) - int(p['a0'],16))
        else: continue
        if '?' in r + v + s: continue
        r, v, s = int(r,16), int(v,16), int(s,16)
        if r >= 0x1000000 or not (0x80000000 <= v < 0x80800000): continue
        bss = 0
        if i+1 < len(P) and P[i+1]['kind'] == 'BZERO' and '?' not in P[i+1]['a0']+P[i+1]['a1'] and int(P[i+1]['a0'],16) == v+s:
            bss = int(P[i+1]['a1'],16)
        k = (r, v)
        if k not in ovl: new += 1
        ovl[k] = (s, max(bss, ovl.get(k, (0,0))[1]))
    with open(tsv, 'w') as f:
        for (r, v), (s, b) in sorted(ovl.items()): f.write('%X\t%X\t%X\t%X\n' % (r, v, s, b))
    print('pass: %d overlays, %d new' % (len(ovl), new))
    if not new: break

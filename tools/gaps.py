import struct, sys
d = open(sys.argv[1], 'rb').read()
cov = [(0x1000, 0xA1200)] + [(r, r + s) for r, v, s, b in ((int(x, 16) for x in l.split()) for l in open(sys.argv[2]))]
cov.sort(); cur = 0x1000; gaps = []
for a, b in cov:
    if a > cur: gaps.append((cur, a))
    cur = max(cur, b)
gaps.append((cur, 0xC56DE0))
for a, b in gaps:
    blob = d[a:b]
    jr = sum(1 for o in range(0, len(blob) - 3, 4) if blob[o:o+4] == b'\x03\xe0\x00\x08')
    print("gap %06X-%06X size %6X  jr_ra %4d" % (a, b, b - a, jr))

# Prefix every global symbol defined in a static library (func_80000400 -> us_func_80000400, ...), in place.
# References inside the library are renamed with them; undefined symbols (runtime, host hooks) are left alone.
# This lets several versions' recompiled code, which use the same function names, link into one executable.
# usage: prefix_syms.py lib.a prefix nm objcopy
import subprocess, sys
lib, prefix, nm, objcopy = sys.argv[1:5]
out = subprocess.run([nm, '-g', '--defined-only', '-P', lib], capture_output=True, text=True, check=True).stdout
syms = set()
for line in out.splitlines():
    parts = line.split()
    # "name type [value size]"; archive member headers end with ':'
    if len(parts) >= 2 and len(parts[1]) == 1 and not parts[0].endswith(':'):
        syms.add(parts[0])
syms = sorted(s for s in syms if not s.startswith(prefix))
with open(lib + '.syms', 'w') as f:
    for s in syms:
        f.write('%s %s%s\n' % (s, prefix, s))
subprocess.run([objcopy, '--redefine-syms=' + lib + '.syms', lib], check=True)
print('%s: prefixed %d symbols with %s' % (lib, len(syms), prefix))

# Write a big-endian (.z64) copy of an N64 ROM given in any byte order (.z64 / .v64 / .n64).
import sys
rom = bytearray(open(sys.argv[1], 'rb').read())
magic = rom[:4].hex()
if magic == '37804012':
    rom[0::2], rom[1::2] = rom[1::2], rom[0::2]
elif magic == '40123780':
    for i in range(0, len(rom), 4):
        rom[i:i + 4] = rom[i:i + 4][::-1]
elif magic != '80371240':
    raise SystemExit('not an N64 ROM (magic %s)' % magic)
open(sys.argv[2], 'wb').write(rom)

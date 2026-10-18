# Per-version constants for the analysis tools, keyed by the ROM header's game code (0x3B..0x3E).
# All tools pick the version from the ROM they are given, so the same commands work for every version.
#   read   game ROM read wrapper (romAddr, ramDst, size)     bzero  bzero(addr, size), clears overlay BSS
#   chunk  chunked loader (romStart, romEnd, ramDst)          file   file-table loader (index, ...)
#   text_end  end of main .text (RSP code follows)            bss    main BSS start (= end of main ROM data)
VERSIONS = {
    b'NISE': dict(id='us', read=0x80003964, bzero=0x8005C5A0, chunk=0x8005568C, file=0x80053150,
                  text_end=0x80069180, bss=0x800A0600),
    b'NISX': dict(id='ende', read=0x80003E24, bzero=0x8005CD50, chunk=0x80055E8C, file=0x80053950,
                  text_end=0x80069A00, bss=0x8009DD20),
    b'NISY': dict(id='frit', read=0x80003E44, bzero=0x8005CD70, chunk=0x80055EAC, file=0x80053970,
                  text_end=0x80069A20, bss=0x8009DE00),
}
MAIN_ROM, MAIN_VRAM = 0x1000, 0x80000400

def detect(rom):
    """Version constants for a big-endian (.z64) ROM image."""
    code = bytes(rom[0x3B:0x3F])
    if code not in VERSIONS:
        raise SystemExit('unknown ROM game code %r (expected one of %s)' % (code, list(VERSIONS)))
    return VERSIONS[code]

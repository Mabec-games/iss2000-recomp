# Wrap a PNG (up to 256x256) in a single-image .ico, for the Windows exe icon: png_to_ico.py in.png out.ico
# Windows Vista and later read PNG-compressed icon images directly, so no conversion is needed.
import struct, sys
data = open(sys.argv[1], 'rb').read()
w, h = struct.unpack('>II', data[16:24])  # IHDR
with open(sys.argv[2], 'wb') as f:
    f.write(struct.pack('<HHH', 0, 1, 1))
    f.write(struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(data), 6 + 16))
    f.write(data)

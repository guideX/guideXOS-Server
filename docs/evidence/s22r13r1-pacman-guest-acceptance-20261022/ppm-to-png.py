import struct, sys, zlib
from pathlib import Path
source, target = map(Path, sys.argv[1:3])
data = source.read_bytes()
parts = data.split(b'\n', 3)
if len(parts) != 4 or parts[0] != b'P6':
    raise SystemExit('expected a plain P6 PPM header')
width, height = map(int, parts[1].split())
if parts[2] != b'255':
    raise SystemExit('expected 8-bit PPM channels')
pixels = parts[3]
if len(pixels) != width * height * 3:
    raise SystemExit(f'pixel payload length mismatch: {len(pixels)}')
rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
def chunk(kind, payload):
    body = kind + payload
    return struct.pack('>I', len(payload)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
target.write_bytes(png)

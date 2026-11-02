import struct, sys, zlib
from pathlib import Path

def chunks(data):
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while pos < len(data) and data[pos] in b' \t\r\n': pos += 1
        if pos < len(data) and data[pos] == 35:
            while pos < len(data) and data[pos] not in b'\r\n': pos += 1
            continue
        start = pos
        while pos < len(data) and data[pos] not in b' \t\r\n': pos += 1
        tokens.append(data[start:pos])
    if tokens[0] != b'P6': raise ValueError('expected binary PPM P6')
    width, height, maximum = map(int, tokens[1:])
    if maximum != 255: raise ValueError('expected 8-bit PPM')
    if pos < len(data) and data[pos] in b' \t\r\n':
        if data[pos:pos+2] == b'\r\n': pos += 2
        else: pos += 1
    return width, height, data[pos:]

def png_chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff)

src = Path(sys.argv[1]); dst = Path(sys.argv[2])
data = src.read_bytes(); w,h,pixels = chunks(data)
if len(pixels) != w*h*3: raise ValueError(f'pixel length {len(pixels)} != {w*h*3}')
rows = b''.join(b'\x00' + pixels[y*w*3:(y+1)*w*3] for y in range(h))
png = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>2I5B', w,h,8,2,0,0,0)) + png_chunk(b'IDAT', zlib.compress(rows, 9)) + png_chunk(b'IEND', b'')
dst.write_bytes(png)
print(f'{src} -> {dst}: {w}x{h}, {len(png)} bytes')

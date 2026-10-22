import pathlib
import struct
import sys
import zlib

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
data = src.read_bytes()
pos = 0

def token():
    global pos
    while pos < len(data):
        if data[pos] in b" \t\r\n\f\v":
            pos += 1
            continue
        if data[pos] == ord('#'):
            while pos < len(data) and data[pos] not in b"\r\n":
                pos += 1
            continue
        break
    start = pos
    while pos < len(data) and data[pos] not in b" \t\r\n\f\v#":
        pos += 1
    value = data[start:pos]
    if pos >= len(data):
        raise ValueError('truncated PPM header')
    if data[pos] == ord('\r') and pos + 1 < len(data) and data[pos + 1] == ord('\n'):
        pos += 2
    else:
        pos += 1
    return value

magic = token()
width = int(token())
height = int(token())
maxval = int(token())
if magic != b'P6' or maxval != 255:
    raise ValueError(f'unsupported PPM format: {magic!r}, maxval={maxval}')
pixels = data[pos:]
if len(pixels) != width * height * 3:
    raise ValueError(f'pixel data length {len(pixels)} != {width}*{height}*3')

def chunk(kind, payload):
    body = kind + payload
    return struct.pack('>I', len(payload)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)

raw = b''.join(b'\x00' + pixels[row * width * 3:(row + 1) * width * 3] for row in range(height))
png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw, 9))
png += chunk(b'IEND', b'')
dst.write_bytes(png)
print(f'{width}x{height} RGB {len(pixels)} bytes -> {dst} ({len(png)} bytes)')
import pathlib, struct, zlib, sys
source = pathlib.Path(sys.argv[1])
target = pathlib.Path(sys.argv[2])
data = source.read_bytes()
parts = data.split(b"\n", 3)
if len(parts) != 4 or parts[0] != b"P6": raise SystemExit("unexpected QEMU PPM header")
width, height = map(int, parts[1].split())
if parts[2] != b"255": raise SystemExit("unexpected PPM depth")
pixels = parts[3]
stride = width * 3
if len(pixels) != height * stride: raise SystemExit("PPM size mismatch")
def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
rows = b"".join(b"\x00" + pixels[y*stride:(y+1)*stride] for y in range(height))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b"")
target.write_bytes(png)
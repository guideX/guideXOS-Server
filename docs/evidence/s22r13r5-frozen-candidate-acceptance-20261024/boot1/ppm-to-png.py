import struct
import sys
import zlib

def ppm_to_png(src, dst):
    data = open(src, "rb").read()
    if not data.startswith(b"P6"):
        raise ValueError("expected binary P6 PPM")
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while pos < len(data):
            if data[pos] in b" \t\r\n":
                pos += 1
            elif data[pos] == ord("#"):
                while pos < len(data) and data[pos] not in b"\r\n":
                    pos += 1
            else:
                break
        start = pos
        while pos < len(data) and data[pos] not in b" \t\r\n#":
            pos += 1
        if pos == start:
            raise ValueError("truncated PPM header")
        tokens.append(data[start:pos])
    if tokens[0] != b"P6":
        raise ValueError("expected P6 PPM")
    width, height, maxval = map(int, tokens[1:])
    if maxval != 255:
        raise ValueError(f"unsupported PPM max value: {maxval}")
    if pos >= len(data) or data[pos] not in b" \t\r\n":
        raise ValueError("missing PPM pixel separator")
    if data[pos:pos+2] == b"\r\n":
        pos += 2
    else:
        pos += 1
    pixels = data[pos:]
    if len(pixels) != width * height * 3:
        raise ValueError(f"pixel size mismatch: {len(pixels)} vs {width*height*3}")
    rows = b"".join(b"\x00" + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    def chunk(kind, body):
        payload = kind + body
        return struct.pack(">I", len(body)) + payload + struct.pack(">I", zlib.crc32(payload) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 6))
    png += chunk(b"IEND", b"")
    with open(dst, "wb") as out:
        out.write(png)

if __name__ == "__main__":
    ppm_to_png(sys.argv[1], sys.argv[2])

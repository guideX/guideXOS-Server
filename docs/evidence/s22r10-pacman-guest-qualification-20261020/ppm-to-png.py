import pathlib, struct, sys, zlib
src=pathlib.Path(sys.argv[1]); dst=pathlib.Path(sys.argv[2]); data=src.read_bytes(); i=0

def token():
    global i
    while i<len(data):
        if data[i] in b' \t\r\n': i+=1; continue
        if data[i]==35:
            while i<len(data) and data[i] not in b'\r\n': i+=1
            continue
        break
    start=i
    while i<len(data) and data[i] not in b' \t\r\n': i+=1
    return data[start:i]
magic=token()
if magic!=b'P6': raise SystemExit(f'unsupported PPM magic {magic!r}')
w=int(token()); h=int(token()); maxval=int(token())
while i<len(data) and data[i] in b' \t\r\n': i+=1
pixels=data[i:]
if maxval!=255 or len(pixels)!=w*h*3: raise SystemExit(f'invalid PPM payload: {w}x{h}, max {maxval}, bytes {len(pixels)}')
def chunk(kind,payload):
    return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
rows=b''.join(b'\0'+pixels[y*w*3:(y+1)*w*3] for y in range(h))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,6))+chunk(b'IEND',b'')
dst.write_bytes(png)
print(f'{dst} {w}x{h} {len(png)} bytes')

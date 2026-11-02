import argparse
import json
import socket
import struct
import time
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--port', type=int, default=5949)
p.add_argument('--x', type=int, required=True)
p.add_argument('--y', type=int, required=True)
p.add_argument('--click', action='store_true')
p.add_argument('--key')
p.add_argument('--trace', required=True)
p.add_argument('--label', required=True)
a = p.parse_args()

def read_exact(sock, n):
    chunks = bytearray()
    while len(chunks) < n:
        part = sock.recv(n-len(chunks))
        if not part:
            raise RuntimeError('VNC disconnected during handshake')
        chunks.extend(part)
    return bytes(chunks)

s = socket.create_connection(('127.0.0.1', a.port), timeout=10)
s.settimeout(10)
version = read_exact(s, 12)
if not version.startswith(b'RFB '):
    raise RuntimeError(f'unexpected VNC version {version!r}')
s.sendall(version)
count = read_exact(s, 1)[0]
if not count:
    raise RuntimeError('VNC server rejected connection')
security = list(read_exact(s, count))
if 1 not in security:
    raise RuntimeError(f'no unauthenticated loopback security option: {security}')
s.sendall(b'\x01')
security_result = read_exact(s, 4)
if struct.unpack('>I', security_result)[0] != 0:
    raise RuntimeError('VNC security negotiation failed')
s.sendall(b'\x01')  # shared client
server_init = read_exact(s, 24)
width, height = struct.unpack('>HH', server_init[:4])
name_len = struct.unpack('>I', server_init[20:24])[0]
server_name = read_exact(s, name_len).decode('utf-8', 'replace')
if not (0 <= a.x < width and 0 <= a.y < height):
    raise RuntimeError(f'pointer target outside {width}x{height}: {a.x},{a.y}')
actions = []
def pointer(mask):
    s.sendall(struct.pack('>BBHH', 5, mask, a.x, a.y))
    actions.append({'type':'pointer', 'mask':mask, 'x':a.x, 'y':a.y})
    time.sleep(0.35)

pointer(0)
if a.click:
    pointer(1)
    pointer(0)
if a.key:
    # RFB KeyEvent uses X11 keysym. Support the small key set needed here.
    keysyms = {'Return':0xff0d, 'Escape':0xff1b, 'Tab':0xff09, 'Down':0xff54, 'Up':0xff52, 'Left':0xff51, 'Right':0xff53, 'F6':0xffc3, 'Alt_L':0xffe9}
    keysym = keysyms.get(a.key)
    if keysym is None:
        raise RuntimeError(f'unsupported key {a.key}')
    s.sendall(struct.pack('>BBHI', 4, 1, 0, keysym)); actions.append({'type':'key','down':True,'keysym':keysym})
    time.sleep(0.12)
    s.sendall(struct.pack('>BBHI', 4, 0, 0, keysym)); actions.append({'type':'key','down':False,'keysym':keysym})
    time.sleep(0.35)
record = {'label':a.label,'observedUtc':time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),'transport':'loopback QEMU VNC input','guestInputDevice':'QEMU PS/2 Mouse (query-mice active, absolute:false)','screen':[width,height],'server':server_name,'actions':actions}
trace = Path(a.trace)
trace.parent.mkdir(parents=True, exist_ok=True)
with trace.open('a', encoding='utf-8') as f:
    f.write(json.dumps(record)+'\n')
print(json.dumps(record, indent=2))
s.close()

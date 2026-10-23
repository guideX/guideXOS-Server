import argparse, json, socket
p = argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, required=True)
p.add_argument('--command', required=True)
p.add_argument('--arguments', default='{}')
a = p.parse_args()
with socket.create_connection((a.host, a.port), timeout=5) as s:
    s.settimeout(5)
    f = s.makefile('rwb', buffering=0)
    greeting = json.loads(f.readline())
    f.write(b'{"execute":"qmp_capabilities","id":0}\r\n')
    while True:
        reply = json.loads(f.readline())
        if reply.get('id') == 0:
            break
    args = json.loads(a.arguments)
    request = {'execute': a.command, 'id': 1}
    if args:
        request['arguments'] = args
    f.write((json.dumps(request, separators=(',', ':')) + '\r\n').encode())
    while True:
        reply = json.loads(f.readline())
        if reply.get('id') == 1:
            print(json.dumps({'greeting': greeting, 'response': reply}, indent=2))
            break

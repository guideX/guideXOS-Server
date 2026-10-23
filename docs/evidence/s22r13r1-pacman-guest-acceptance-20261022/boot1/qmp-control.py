import argparse
import json
import socket
from datetime import datetime, timezone
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--port', type=int, required=True)
p.add_argument('--log', required=True)
p.add_argument('--hmp', action='append', default=[])
a = p.parse_args()

log_path = Path(a.log)
log_path.parent.mkdir(parents=True, exist_ok=True)

def stamp():
    return datetime.now(timezone.utc).isoformat()

def log(line):
    with log_path.open('a', encoding='utf-8', newline='\n') as f:
        f.write(f'{stamp()} {line}\n')

with socket.create_connection((a.host, a.port), timeout=5) as s:
    s.settimeout(5)
    stream = s.makefile('rwb', buffering=0)
    greeting = json.loads(stream.readline())
    log('RX ' + json.dumps(greeting, separators=(',', ':')))
    next_id = 1
    def execute(name, args=None):
        global next_id
        request = {'execute': name, 'id': next_id}
        if args is not None:
            request['arguments'] = args
        encoded = (json.dumps(request, separators=(',', ':')) + '\r\n').encode()
        stream.write(encoded)
        log('TX ' + encoded.decode().strip())
        while True:
            raw = stream.readline()
            if not raw:
                raise RuntimeError('QMP closed before replying')
            response = json.loads(raw)
            log('RX ' + json.dumps(response, separators=(',', ':')))
            if response.get('id') == next_id:
                next_id += 1
                if 'error' in response:
                    raise RuntimeError(json.dumps(response['error']))
                return response.get('return')
    execute('qmp_capabilities')
    for command in a.hmp:
        result = execute('human-monitor-command', {'command-line': command})
        print(json.dumps({'hmp': command, 'return': result}, ensure_ascii=False))

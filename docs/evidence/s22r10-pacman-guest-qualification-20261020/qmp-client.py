import argparse, datetime, json, socket, time
p=argparse.ArgumentParser(); p.add_argument('--port',type=int,default=65252); p.add_argument('--log',required=True); p.add_argument('--command',action='append',default=[]); a=p.parse_args()
with socket.create_connection(('127.0.0.1',a.port),timeout=6) as s:
    s.settimeout(6); r=s.makefile('r',encoding='utf-8',newline='\n'); w=s.makefile('w',encoding='utf-8',newline='\n'); log=open(a.log,'a',encoding='utf-8')
    def record(direction,line): log.write(datetime.datetime.now().astimezone().isoformat()+f' {direction} {line.rstrip()}\n'); log.flush()
    greeting=r.readline()
    if not greeting: raise RuntimeError('QMP closed before greeting')
    record('RX',greeting); next_id=1
    def request(payload):
        global next_id
        ident=next_id; next_id+=1; payload['id']=ident; line=json.dumps(payload,separators=(',',':')); record('TX',line); w.write(line+'\n'); w.flush()
        while True:
            raw=r.readline()
            if not raw: raise RuntimeError('QMP closed before reply')
            record('RX',raw); response=json.loads(raw)
            if response.get('id')==ident:
                if 'error' in response: raise RuntimeError(json.dumps(response['error']))
                return response.get('return')
    request({'execute':'qmp_capabilities'}); status=request({'execute':'query-status'}); results=[]
    for command in a.command:
        result=request({'execute':'human-monitor-command','arguments':{'command-line':command}}); results.append({'command':command,'result':result}); time.sleep(0.12)
    print(json.dumps({'status':status,'commands':results},indent=2))

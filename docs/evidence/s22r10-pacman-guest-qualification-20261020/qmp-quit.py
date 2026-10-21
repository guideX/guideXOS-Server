import argparse,datetime,json,socket,pathlib
p=argparse.ArgumentParser(); p.add_argument('--port',type=int,default=65252); p.add_argument('--log',required=True); a=p.parse_args()
port=a.port; log=pathlib.Path(a.log)
def record(d,line):
    with log.open('a',encoding='utf-8') as f: f.write(datetime.datetime.now().astimezone().isoformat()+f' {d} {line.rstrip()}\n')
with socket.create_connection(('127.0.0.1',port),timeout=6) as s:
    s.settimeout(6); r=s.makefile('r',encoding='utf-8',newline='\n'); w=s.makefile('w',encoding='utf-8',newline='\n')
    greeting=r.readline(); record('RX',greeting)
    for ident,req in [(1,{'execute':'qmp_capabilities'}),(2,{'execute':'quit'})]:
        req['id']=ident; raw=json.dumps(req,separators=(',',':')); record('TX',raw); w.write(raw+'\n'); w.flush()
        try:
            while True:
                line=r.readline()
                if not line: break
                record('RX',line); obj=json.loads(line)
                if obj.get('id')==ident: break
        except (OSError,TimeoutError): pass
print(f'QMP quit requested on port {port}.')

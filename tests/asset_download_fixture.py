#!/usr/bin/env python3
"""Local release server for the asset downloader: Range, redirects and injected faults."""
import hashlib,http.server,importlib.util,json,os,pathlib,shutil,subprocess,sys,tempfile,threading,time
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tests'))
from pack_fixture import fixture
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
split=module('split',ROOT/'scripts/split-asset-pack.py');assembler=module('assemble',ROOT/'scripts/assemble-assets.py')
log=[];attempts={};lock=threading.Lock()
with tempfile.TemporaryDirectory(prefix='albert-download-') as directory:
 root=pathlib.Path(directory);base=root/'base.pak';fixture(base);child=root/'mercury';child.mkdir();fixture(child/'extra.pak',['mercury-day','mercury-height'])
 def manifest(payload,extensions=None):
  data=payload.read_bytes();part=payload.with_suffix('.pakpart');part.write_bytes(data);digest=hashlib.sha256(data).hexdigest()
  entry=dict(bytes=len(data),sha256=digest,parts=[dict(path=part.name,bytes=len(data),sha256=digest)])
  if extensions:entry['extensions']=extensions
  (payload.parent/'manifest.json').write_text(json.dumps(entry));return payload.parent/'manifest.json'
 top=manifest(base,[dict(manifest='mercury/manifest.json')]);manifest(child/'extra.pak');pack=root/'visuals.pak';assembler.assemble(top,pack)
 release=root/'release';catalog=split.split(pack,release,moons={'jupiter'},scale=1/1024)
 assets=root/'assets';(assets/'packs').mkdir(parents=True);shutil.copy(release/'catalog.json',assets)
 class Handler(http.server.BaseHTTPRequestHandler):
  protocol_version='HTTP/1.1'
  def log_message(self,*args):pass
  def do_GET(self):
   parts=self.path.strip('/').split('/');name=parts[-1];rng=self.headers.get('Range')
   if parts[0]==catalog['release'] and name=='moons-2.pak':
    self.send_response(302);self.send_header('Location','/blob/'+name);self.send_header('Content-Length','0');self.end_headers();log.append((name,rng,302));return
   with lock:attempt=attempts[self.path]=attempts.get(self.path,0)+1
   if name=='essential.pak' and attempt==1:
    self.send_response(503);self.send_header('Content-Length','0');self.end_headers();log.append((name,rng,503));return
   data=(release/name).read_bytes();start=int(rng.split('=')[1].split('-')[0]) if rng else 0
   ignore=name=='earth-1.pak' and attempt==2
   if name=='mercury-1.pak' and attempt==1:data=data[:-1]+bytes([data[-1]^0xff])
   body=data if ignore or not rng else data[start:]
   status=206 if rng and not ignore else 200;log.append((name,rng,status))
   self.send_response(status);self.send_header('Content-Length',str(len(body)))
   if status==206:self.send_header('Content-Range',f'bytes {start}-{len(data)-1}/{len(data)}')
   self.end_headers()
   try:
    if attempt==1 and name in('moon-1.pak','earth-1.pak'):self.wfile.write(body[:len(body)//2]);self.wfile.flush();self.close_connection=True;return
    slow=(name=='moon-1.pak' and attempt==2) or (name=='earth-1.pak' and attempt>=3)
    step=64 if slow else 4096
    for offset in range(0,len(body),step):
     self.wfile.write(body[offset:offset+step]);self.wfile.flush()
     if slow:time.sleep(.05)  # 1.25 KiB/s: slow, yet above the stall limit.
   except (BrokenPipeError,ConnectionResetError):pass
 class Server(http.server.ThreadingHTTPServer):
  def handle_error(self,request,address):pass  # Clients abort transfers on purpose.
 server=Server(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 env={k:v for k,v in os.environ.items() if k!='ALBERT_PACK_PATH'}
 windows=any(pathlib.Path(s).name=='wine' for s in sys.argv[1:]);native=lambda p:'Z:'+str(p).replace('/','\\') if windows else str(p)
 env.update(ALBERT_ASSET_DIR=native(assets),ALBERT_DATA_DIR=native(root/'data'),ALBERT_ASSET_BASE_URL=f'http://127.0.0.1:{server.server_port}')
 try:subprocess.run([*sys.argv[1:],native(pack)],check=True,env=env,timeout=90)
 finally:server.shutdown();server.server_close()
 requests=lambda name:[(r,s) for n,r,s in log if n==name]
 moon=requests('moon-1.pak');assert len(moon)>=3 and moon[0]==(None,200) and moon[1][0] and moon[1][1]==206,moon
 # First phase: truncated, resumed (slow), preempted and resumed again.
 assert all(int(r.split('=')[1][:-1])>0 for r,_ in moon[1:3]),'Resumed transfers must not restart from zero'
 assert int(moon[2][0].split('=')[1][:-1])>int(moon[1][0].split('=')[1][:-1]),'Preempted transfer did not resume where it stopped'
 earth=requests('earth-1.pak');assert earth[0]==(None,200) and earth[1][0] and earth[1][1]==200,earth
 assert requests('mercury-1.pak')[:2]==[(None,200),(None,200)],'Corrupt download was not discarded and fetched again'
 assert requests('essential.pak')[:2]==[(None,503),(None,200)]
 assert requests('moons-2.pak')[0][1]==302
 assert not list((assets/'packs').glob('*.part')),'Completed downloads left partial files'
 # Only the transfer cancelled on purpose remains, resumable.
 assert [p.name for p in (root/'second/packs').glob('*.part')]==['earth-1.pak.part']
print('Downloads: resume, ignored range, SHA-256 rejection, redirect, 503 retry and focus preemption OK')

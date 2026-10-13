#!/usr/bin/env python3
"""Resume a bounded NASA Trek/Viking imagery snapshot for offline packaging."""
import argparse,concurrent.futures,hashlib,http.client,json,pathlib,threading,time,urllib.parse
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='https://trek.nasa.gov/tiles/Mars/EQ/Mars_Viking_MDIM21_ClrMosaic_global_232m/1.0.0/default/default028mm'
def download(level=6,workers=2):
 from PIL import Image
 directory=ROOT/'asset-packs/solar/sources/mars-trek';directory.mkdir(parents=True,exist_ok=True)
 cols,rows=2**(level+1),2**level;connections=threading.local();stop=threading.Event()
 def fetch(item):
  row,col=item;path=directory/f'{level}/{row}/{col}.jpg';url=f'{BASE}/{level}/{row}/{col}.jpg'
  for attempt in range(5):
   reused=False
   try:
    if stop.is_set():raise concurrent.futures.CancelledError()
    if not path.exists():
     reused=bool(getattr(getattr(connections,'http',None),'sock',None))
     if not getattr(connections,'http',None):connections.http=http.client.HTTPSConnection('trek.nasa.gov',timeout=35)
     connections.http.request('GET',urllib.parse.urlsplit(url).path,headers={'User-Agent':'AlbertEngine/0.2 (+https://github.com/AllanOliveiraM/albert-engine)','Connection':'keep-alive'})
     response=connections.http.getresponse();raw=response.read(1024*1024+1)
     if response.will_close:connections.http.close();connections.http=None
     if response.status!=200:raise RuntimeError('WMTS HTTP '+str(response.status))
     if len(raw)>1024*1024:raise ValueError('Oversized WMTS tile')
     path.parent.mkdir(parents=True,exist_ok=True);temporary=path.with_suffix('.tmp');temporary.write_bytes(raw)
     with Image.open(temporary) as image:
      if image.size!=(256,256):raise ValueError('Unexpected WMTS tile dimensions')
      image.verify()
     temporary.replace(path)
    try:
     if path.stat().st_size>1024*1024:raise ValueError('Oversized cached WMTS tile')
     with Image.open(path) as image:
      if image.size!=(256,256) or image.format!='JPEG':raise ValueError('Invalid cached WMTS tile')
      image.verify()
    except Exception:
     path.unlink(missing_ok=True);raise
    raw=path.read_bytes();return dict(path=path.relative_to(directory).as_posix(),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
   except Exception as error:
    if stop.is_set():raise
    if getattr(connections,"http",None):connections.http.close();connections.http=None
    if attempt==4:raise
    if reused and isinstance(error,(http.client.RemoteDisconnected,ConnectionResetError)) and attempt==0:continue
    print(f'Retry Mars tile {level}/{row}/{col}: {error}',flush=True)
    if stop.wait(min(10,2**attempt)):raise concurrent.futures.CancelledError()
 records=[];items=iter((r,c) for r in range(rows) for c in range(cols));pool=concurrent.futures.ThreadPoolExecutor(max_workers=workers)
 pending=set()
 def submit():
  item=next(items,None)
  if item is not None:pending.add(pool.submit(fetch,item))
 try:
  for _ in range(workers*2):submit()
  while pending:
   done,_=concurrent.futures.wait(pending,return_when=concurrent.futures.FIRST_COMPLETED)
   for future in done:
    pending.remove(future);records.append(future.result());submit()
    if len(records)%64==0:print(f'Mars Viking: {len(records)}/{rows*cols} tiles',flush=True)
 except BaseException:
  stop.set()
  for future in pending:future.cancel()
  raise
 finally:pool.shutdown(wait=True,cancel_futures=True)
 records.sort(key=lambda record:record['path'])
 manifest=directory/'manifest.json';temporary=manifest.with_suffix('.tmp');temporary.write_text(json.dumps(dict(provider='NASA Trek',layer='Mars_Viking_MDIM21_ClrMosaic_global_232m',level=level,columns=cols,rows=rows,tile_size=256,source_bounds=[-180,-90,180,90],files=records),indent=2)+'\n');temporary.replace(manifest)
 print('Mars snapshot complete:',sum(v['bytes'] for v in records),'bytes',flush=True)
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--level',type=int,choices=range(0,8),default=6);parser.add_argument('--workers',type=int,choices=range(1,5),default=2);a=parser.parse_args();download(a.level,a.workers)

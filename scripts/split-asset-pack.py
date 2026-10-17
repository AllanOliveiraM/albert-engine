#!/usr/bin/env python3
"""Split the assembled visuals.pak into per-body/per-level download containers.

Each container is a regular albert-pack-v1 archive. The essential container holds
every payload needed to open the program (the startup resolution of each layer,
measured shapes and source records); the rest follow by resolution level.
Payloads are copied unchanged and re-verified; nothing is re-encoded.
  python3 scripts/split-asset-pack.py build/linux/imagery/visuals.pak OUTPUT
  python3 scripts/split-asset-pack.py PACK OUTPUT --catalog-only
"""
import argparse,hashlib,json,pathlib,re,struct

ROOT=pathlib.Path(__file__).resolve().parents[1]
MAGIC,COLLECTION=b'ALBRTPK1',b'ALBRTPX1'
TILE=re.compile(r'v1/([^/]+)/(\d+)/\d+/\d+\.\w+$')
FIRST=('earth','moon','sun')  # Earth/Moon/Sun detail precedes the other bodies.

def archives(stream,size):
 stream.seek(size-16);length,magic=struct.unpack('<Q8s',stream.read(16))
 if magic!=COLLECTION:return [(0,size)]
 stream.seek(size-16-length);collection=json.loads(stream.read(length))
 if collection.get('container')!='albert-pack-collection-v1':raise ValueError('Unsupported collection')
 return [(a['offset'],a['bytes']) for a in collection['archives']]

def read_index(path):
 layers,files={},[]
 with open(path,'rb') as stream:
  size=stream.seek(0,2)
  for offset,length in archives(stream,size):
   stream.seek(offset);magic,count=struct.unpack('<8sQ',stream.read(16))
   if magic!=MAGIC or count>16*1024*1024:raise ValueError('Invalid archive header')
   manifest=json.loads(stream.read(count));start=offset+16+count
   for layer in manifest['layers']:
    if layers.setdefault(layer['id'],layer)!=layer:raise ValueError('Conflicting layer '+layer['id'])
   for entry in manifest['files']:files.append(dict(path=entry['path'],bytes=entry['bytes'],sha256=entry['sha256'],source=start+entry['offset']))
 if len({f['path'] for f in files})!=len(files):raise ValueError('Duplicate payload path')
 return layers,files

def startup_level(layer,moons,scale=1):
 # Mirrors the startup requests: relief 1024 px, Sun 2048, moons 1024, others 4096.
 name=layer['id'];request=1024 if layer.get('kind')=='elevation' or name.endswith('-height') else 2048 if name=='sun' else 1024 if name.split('-')[0] in moons else 4096
 level=0
 while level<layer['max_level'] and layer['width']>>(layer['max_level']-level)<request*scale:level+=1
 return level

def groups(layers,files,moons,scale=1):
 result={}
 for entry in files:
  match=TILE.match(entry['path']);name=match and match.group(1)
  if not match or name not in layers:key=('essential',0)
  else:
   level=int(match.group(2));body=name.split('-')[0]
   key=('essential',0) if level<=startup_level(layers[name],moons,scale) else ('moons' if body in moons else body,level)
  result.setdefault(key,[]).append(entry)
 # Smallest cumulative group size first: every body improves early, and a group's
 # levels stay in order because each level adds to its predecessors.
 cumulative={key:sum(e['bytes'] for k,v in result.items() if k[0]==key[0] and k[1]<=key[1] for e in v) for key in result}
 order=lambda key:(key[0]!='essential',key[0] not in FIRST,cumulative[key],key)
 return [(key,sorted(result[key],key=lambda e:e['path'])) for key in sorted(result,key=order)]

def container(source,entries,layers,destination=None):
 used=sorted({TILE.match(e['path']).group(1) for e in entries if TILE.match(e['path']) and TILE.match(e['path']).group(1) in layers})
 offset=0;listed=[]
 for entry in entries:listed.append(dict(path=entry['path'],offset=offset,bytes=entry['bytes'],sha256=entry['sha256']));offset+=entry['bytes']
 index=json.dumps(dict(container='albert-pack-v1',layers=[layers[name] for name in used],files=listed),separators=(',',':'),ensure_ascii=False).encode()
 digest=hashlib.sha256();head=MAGIC+struct.pack('<Q',len(index))+index;digest.update(head);total=len(head)
 output=destination.with_suffix('.tmp').open('wb') if destination else None
 try:
  if output:output.write(head)
  for entry in entries:
   source.seek(entry['source']);data=source.read(entry['bytes'])
   if len(data)!=entry['bytes'] or hashlib.sha256(data).hexdigest()!=entry['sha256']:raise ValueError('Corrupt payload '+entry['path'])
   digest.update(data);total+=len(data)
   if output:output.write(data)
 finally:
  if output:output.close()
 if destination:destination.with_suffix('.tmp').replace(destination)
 return total,digest.hexdigest(),used

def split(pack,output,catalog_only=False,moons=None,scale=1):
 # scale shrinks the startup requests only for small test fixtures.
 if moons is None:moons={b['id'] for b in json.loads((ROOT/'assets/moon-sources.json').read_text())['bodies']}
 layers,files=read_index(pack);output=pathlib.Path(output);output.mkdir(parents=True,exist_ok=True);records=[]
 with open(pack,'rb') as source:
  for priority,((group,level),entries) in enumerate(groups(layers,files,moons,scale)):
   name='essential.pak' if group=='essential' else f'{group}-{level}.pak'
   total,digest,used=container(source,entries,layers,None if catalog_only else output/name)
   if total>=2**31-1:raise ValueError('Container exceeds the release attachment limit: '+name)
   records.append(dict(file=name,group=group,level=level,essential=group=='essential',priority=priority,bytes=total,sha256=digest,payloads=len(entries),layers=used))
 identity=hashlib.sha256('\n'.join(r['file']+' '+r['sha256'] for r in records).encode()).hexdigest()
 catalog=dict(schema='albert-asset-catalog-v1',id=identity,release='assets-'+identity[:12],total_bytes=sum(r['bytes'] for r in records),containers=records)
 (output/'catalog.json').write_text(json.dumps(catalog,indent=1)+'\n');return catalog

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 parser.add_argument('pack',type=pathlib.Path);parser.add_argument('output',type=pathlib.Path);parser.add_argument('--catalog-only',action='store_true')
 args=parser.parse_args();catalog=split(args.pack,args.output,args.catalog_only)
 essential=catalog['containers'][0]['bytes'];print(f"{len(catalog['containers'])} containers, essential {essential/1e6:.1f} MB, total {catalog['total_bytes']/1e6:.1f} MB, release {catalog['release']}")

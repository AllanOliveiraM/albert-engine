#!/usr/bin/env python3
"""Split a small collection into download containers and verify them independently."""
import hashlib,importlib.util,json,pathlib,struct,subprocess,sys,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tests'))
from pack_fixture import fixture
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
split=module('split',ROOT/'scripts/split-asset-pack.py');assembler=module('assemble',ROOT/'scripts/assemble-assets.py')
def files_of(path):
 data=path.read_bytes();assert data[:8]==b'ALBRTPK1';count=struct.unpack('<Q',data[8:16])[0];index=json.loads(data[16:16+count]);start=16+count;end=0;out={}
 assert index['container']=='albert-pack-v1'
 for e in index['files']:
  assert e['offset']==end;payload=data[start+e['offset']:start+e['offset']+e['bytes']];assert hashlib.sha256(payload).hexdigest()==e['sha256'];out[e['path']]=e['sha256'];end+=e['bytes']
 assert start+end==len(data),'Container has gaps or trailing bytes'
 return out,{l['id'] for l in index['layers']}
with tempfile.TemporaryDirectory(prefix='albert-split-') as directory:
 root=pathlib.Path(directory);base=root/'base.pak';fixture(base);child=root/'mercury';child.mkdir();fixture(child/'extra.pak',['mercury-day','mercury-height'])
 def manifest(payload,extensions=None):
  data=payload.read_bytes();part=payload.with_suffix('.pakpart');part.write_bytes(data);digest=hashlib.sha256(data).hexdigest()
  entry=dict(bytes=len(data),sha256=digest,parts=[dict(path=part.name,bytes=len(data),sha256=digest)])
  if extensions:entry['extensions']=extensions
  (payload.parent/'manifest.json').write_text(json.dumps(entry));return payload.parent/'manifest.json'
 top=manifest(base,[dict(manifest='mercury/manifest.json')]);manifest(child/'extra.pak');pack=root/'visuals.pak';assembler.assemble(top,pack)
 _,original=split.read_index(pack);original={f['path']:f['sha256'] for f in original}
 catalog=split.split(pack,root/'out',moons={'jupiter'},scale=1/1024)
 names=[r['file'] for r in catalog['containers']]
 assert names[0]=='essential.pak' and set(names)=={'essential.pak','earth-1.pak','moon-1.pak','mercury-1.pak','moons-1.pak','moons-2.pak'},names
 union={}
 for record in catalog['containers']:
  path=root/'out'/record['file'];assert path.stat().st_size==record['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest()==record['sha256']
  files,layers=files_of(path);assert not set(files)&set(union),'Payload in two containers';union.update(files)
  assert sorted(layers)==record['layers']
  levels={int(p.split('/')[2]) for p in files if p.count('/')==4}
  if record['essential']:assert levels=={0} and 'v1/jupiter-day/0/0/0.png' in files and 'v1/sun/0/0/0.png' in files
  else:assert levels=={record['level']},(record['file'],levels)
 assert union==original,'Split lost, changed or invented payloads'
 again=split.split(pack,root/'catalog',catalog_only=True,moons={'jupiter'},scale=1/1024)
 assert again==catalog and not list((root/'catalog').glob('*.pak')),'Catalog-only mode differs or wrote containers'
 # Same tiles as moon-1 under a different layer description must be rejected by the reader.
 data=(root/'out/moon-1.pak').read_bytes();count=struct.unpack('<Q',data[8:16])[0];index=json.loads(data[16:16+count]);index['layers'][0]['height']+=2
 encoded=json.dumps(index,separators=(',',':')).encode();(root/'out/divergent.pak').write_bytes(b'ALBRTPK1'+struct.pack('<Q',len(encoded))+encoded+data[16+count:])
 if len(sys.argv)>1:
  import os,shutil
  assets=root/'assets';(assets/'packs').mkdir(parents=True);shutil.copy(root/'out/catalog.json',assets);shutil.copy(root/'out/essential.pak',assets/'packs')
  env={k:v for k,v in os.environ.items() if k!='ALBERT_PACK_PATH'};env.update(ALBERT_ASSET_DIR=str(assets),ALBERT_DATA_DIR=str(root/'data'))
  windows=any(pathlib.Path(s).name=='wine' for s in sys.argv[1:]);native=lambda p:'Z:'+str(p).replace('/','\\') if windows else str(p)
  if windows:env.update(ALBERT_ASSET_DIR=native(assets),ALBERT_DATA_DIR=native(root/'data'))
  subprocess.run([*sys.argv[1:],'--split',native(root/'out'),native(pack)],check=True,env=env)
 data=bytearray(pack.read_bytes());data[100]^=0xff;pack.write_bytes(bytes(data))
 try:split.split(pack,root/'broken',moons={'jupiter'},scale=1/1024);raise AssertionError('Corrupt payload copied')
 except ValueError:pass
print('Split: containers by body/level, exact payload union, catalog hashes, catalog-only identity and corruption rejected')

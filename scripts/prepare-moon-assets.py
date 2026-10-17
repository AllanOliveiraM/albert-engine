#!/usr/bin/env python3
"""Reproduce the local moon surface pack from pinned NASA GLBs.

Python 3 + Pillow (also available in scripts/Dockerfile.assets).
Originals/intermediates remain in ignored asset-packs/moons/.
"""
import argparse,hashlib,importlib.util,io,json,pathlib,shutil,struct,urllib.request
from solar_imagery import file_digest,image_tiles
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[1]

def visual(raw,grid):
 if len(raw)<28 or struct.unpack_from('<4sII',raw)!=(b'glTF',2,len(raw)):
  raise ValueError('Invalid NASA GLB header')
 count,kind=struct.unpack_from('<I4s',raw,12)
 if kind!=b'JSON' or 20+count+8>len(raw):raise ValueError('Invalid GLB JSON chunk')
 doc=json.loads(raw[20:20+count]);size,kind=struct.unpack_from('<I4s',raw,20+count)
 if kind!=b'BIN\0' or 28+count+size!=len(raw):raise ValueError('Invalid GLB binary chunk')
 primitive=doc['meshes'][0]['primitives'][0];material=doc['materials'][primitive['material']]
 texture=material['pbrMetallicRoughness']['baseColorTexture']
 if texture.get('texCoord',0)!=0 or texture.get('extensions'):raise ValueError('Unsupported NASA texture coordinates')
 view=doc['bufferViews'][doc['images'][doc['textures'][texture['index']]['source']]['bufferView']]
 start=view.get('byteOffset',0);end=start+view['byteLength']
 if view.get('buffer',0)!=0 or not 0<=start<end<=size:raise ValueError('Invalid texture range')
 with Image.open(io.BytesIO(raw[28+count+start:28+count+end])) as image:
  if list(image.size)!=grid or grid[0]!=2*grid[1]:raise ValueError('Unexpected moon source grid')
  return image.convert('RGB')

def prepare(offline=False):
 plan_path=ROOT/'assets/moon-sources.json';plan=json.loads(plan_path.read_text())
 root=ROOT/'asset-packs/moons';sources=root/'sources';public=root/'public'
 sources.mkdir(parents=True,exist_ok=True)
 # Never publish a mixture of old and new sources/processing.
 if public.exists():shutil.rmtree(public)
 public.mkdir();layers=[];files=[]
 for body in plan['bodies']:
  path=sources/body['filename']
  if not path.exists():
   if offline:raise FileNotFoundError(path)
   temporary=path.with_suffix('.part')
   with urllib.request.urlopen(body['url'],timeout=90) as response,temporary.open('wb') as output:
    remaining=body['bytes']
    while block:=response.read(min(1024*1024,remaining+1)):
     remaining-=len(block)
     if remaining<0:raise ValueError('Oversized source: '+body['id'])
     output.write(block)
   if temporary.stat().st_size!=body['bytes'] or file_digest(temporary)!=body['sha256']:raise ValueError('Downloaded source mismatch')
   temporary.replace(path)
  if path.stat().st_size!=body['bytes'] or file_digest(path)!=body['sha256']:raise ValueError('Source checksum/size mismatch: '+body['id'])
  image=visual(path.read_bytes(),body['grid'])
  record=image_tiles(public,body['id']+'-day',image);image.close()
  record['layer']['lighting']='Static NASA VTAD visual reference; not calibrated reflectance or elevation'
  layers.append(record['layer']);files.extend(record['files']);print(body['id'],body['grid'],flush=True)
 source=public/'v1/moons/SOURCES.json';source.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(plan_path,source)
 files.append(dict(path=source.relative_to(public).as_posix(),bytes=source.stat().st_size,sha256=file_digest(source)))
 (public/'manifest.json').write_text(json.dumps(dict(schema='albert-tiled-assets-v1',version='v1',static=True,layers=layers,files=files),indent=2)+'\n')
 spec=importlib.util.spec_from_file_location('pack',ROOT/'scripts/pack-assets.py');pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack);pack.create_pack(root,'moons')
 metadata=ROOT/'assets/manifest.json';record=json.loads(metadata.read_text())
 record['files']=[v for v in record['files'] if v['path']!='moon-sources.json']+[dict(path='moon-sources.json',bytes=plan_path.stat().st_size,sha256=file_digest(plan_path),source='NASA VTAD visual resources',license='Credits and limits in source inventory')]
 metadata.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--offline',action='store_true');args=parser.parse_args();prepare(args.offline)

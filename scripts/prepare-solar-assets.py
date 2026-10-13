#!/usr/bin/env python3
"""Download pinned NASA visual models, remap their cube atlases, and publish a local pack.
Requires Python 3, Pillow and NumPy; originals/work products stay in asset-packs/.
"""
import argparse,hashlib,importlib.util,io,json,pathlib,struct,urllib.request
import numpy as np
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[1]
PLAN=ROOT/'assets/solar-sources.json'
def digest(data):return hashlib.sha256(data).hexdigest()
def accessor(doc,blob,index):
 a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']];components={'SCALAR':1,'VEC2':2,'VEC3':3}[a['type']];dtype={5126:'<f4',5125:'<u4',5123:'<u2'}[a['componentType']];size=np.dtype(dtype).itemsize
 return np.ndarray((a['count'],components),dtype=dtype,buffer=blob,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',size*components),size))
def remap_cube(doc,blob,image,width=4096):
 primitive=doc['meshes'][0]['primitives'][0];p=accessor(doc,blob,primitive['attributes']['POSITION']).astype(float);p/=np.max(np.abs(p),axis=0)
 uv=accessor(doc,blob,primitive['attributes']['TEXCOORD_0']);faces=accessor(doc,blob,primitive['indices']).ravel().reshape(-1,3)
 center=p[faces].mean(1);axes=np.abs(center).argmax(1);sign=np.sign(center[np.arange(len(center)),axes]);height=width//2
 lon=(np.arange(width)+.5)*2*np.pi/width-np.pi;lat=np.pi/2-(np.arange(height)+.5)*np.pi/height
 directions=np.stack(np.broadcast_arrays(np.cos(lat[:,None])*np.cos(lon),np.sin(lat[:,None]),-np.cos(lat[:,None])*np.sin(lon)),axis=-1);dominant=np.abs(directions).argmax(-1)
 source=np.asarray(image.convert('RGB'));result=np.zeros((height,width,3),dtype=np.uint8)
 for axis in range(3):
  other=[i for i in range(3) if i!=axis]
  for side in [-1,1]:
   indices=faces[(axes==axis)&(sign==side)].ravel();points=p[indices];matrix=np.column_stack((points[:,other]/np.abs(points[:,axis,None]),np.ones(len(points))))
   # Preserve the supplied mesh's UVs, including its non-linear cube mapping.
   resolution=512;lookup=np.full((resolution,resolution,2),np.nan)
   for triangle in faces[(axes==axis)&(sign==side)]:
    v=p[triangle];xy=(v[:,other]/np.abs(v[:,axis,None])+1)*resolution/2-.5
    lo=np.maximum(np.ceil(xy.min(0)).astype(int),0);hi=np.minimum(np.floor(xy.max(0)).astype(int),resolution-1)
    if np.any(hi<lo):continue
    xx,yy=np.meshgrid(np.arange(lo[0],hi[0]+1),np.arange(lo[1],hi[1]+1));q=np.stack((xx,yy),-1)-xy[0]
    inverse=np.linalg.inv(np.column_stack((xy[1]-xy[0],xy[2]-xy[0])));weights=q@inverse.T;valid=(weights.min(-1)>=-1e-7)&(weights.sum(-1)<=1+1e-7)
    sampled=uv[triangle[0]]+weights[:,:,:1]*(uv[triangle[1]]-uv[triangle[0]])+weights[:,:,1:]*(uv[triangle[2]]-uv[triangle[0]])
    lookup[yy[valid],xx[valid]]=sampled[valid]
   if not np.isfinite(lookup).all():raise ValueError('Incomplete cube mesh coverage')
   selected=(dominant==axis)&(np.sign(directions[:,:,axis])==side);d=directions[selected];coords=np.clip((d[:,other]/np.abs(d[:,axis,None])+1)*resolution/2-.5,0,resolution-1);a0=coords.astype(int);a1=np.minimum(a0+1,resolution-1);f=coords-a0
   mapped=(1-f[:,1,None])*((1-f[:,0,None])*lookup[a0[:,1],a0[:,0]]+f[:,0,None]*lookup[a0[:,1],a1[:,0]])+f[:,1,None]*((1-f[:,0,None])*lookup[a1[:,1],a0[:,0]]+f[:,0,None]*lookup[a1[:,1],a1[:,0]])
   x=np.clip(mapped[:,0]*image.width-.5,0,image.width-1);y=np.clip(mapped[:,1]*image.height-.5,0,image.height-1);x0=x.astype(int);y0=y.astype(int);x1=np.minimum(x0+1,image.width-1);y1=np.minimum(y0+1,image.height-1);a=(x-x0)[:,None];b=(y-y0)[:,None]
   result[selected]=np.rint((1-b)*((1-a)*source[y0,x0]+a*source[y0,x1])+b*((1-a)*source[y1,x0]+a*source[y1,x1])).astype(np.uint8)
 return Image.fromarray(result)
def prepare(offline=False):
 plan=json.loads(PLAN.read_text());root=ROOT/'asset-packs/solar';sources=root/'sources';public=root/'public';sources.mkdir(parents=True,exist_ok=True);public.mkdir(exist_ok=True);layers=[];files=[]
 def register(path):raw=path.read_bytes();files.append(dict(path=path.relative_to(public).as_posix(),bytes=len(raw),sha256=digest(raw)))
 def pyramid(name,image,projection='equirectangular'):
  width,height=image.size;levels=max(0,min(3,int(np.log2(max(1,width//512)))));tile=512;fmt='png' if image.mode=='RGBA' else 'jpg'
  layers.append(dict(id=name,projection=projection,bounds=[-180,-90,180,90],width=width,height=height,tile_size=tile,gutter=2,max_level=levels,format=fmt,kind='visual',source_ids=[name.split('-')[0]],lighting='NASA visualization texture; not calibrated reflectance',terrain_available=False))
  for level in range(levels+1):
   w,h=width>>(levels-level),height>>(levels-level);scaled=image.resize((w,h),Image.Resampling.LANCZOS);pixels=np.asarray(scaled);padded=np.pad(pixels,((2,2),(0,0),(0,0)),mode='edge');padded=np.pad(padded,((0,0),(2,2),(0,0)),mode='wrap')
   for row in range((h+tile-1)//tile):
    for col in range((w+tile-1)//tile):
     out=public/f'v1/{name}/{level}/{row}/{col}.{fmt}';out.parent.mkdir(parents=True,exist_ok=True);crop=Image.fromarray(padded[row*tile:min((row+1)*tile,h)+4,col*tile:min((col+1)*tile,w)+4]);crop.save(out,**(dict(format='JPEG',quality=94,subsampling=0,optimize=True,progressive=True) if fmt=='jpg' else dict(format='PNG',compress_level=9)));register(out)
 for body in plan['bodies']:
  path=sources/body['filename']
  if not path.exists():
   if offline:raise FileNotFoundError(path)
   raw=urllib.request.urlopen(body['url'],timeout=60).read();path.write_bytes(raw)
  raw=path.read_bytes()
  if digest(raw)!=body['sha256']:raise ValueError('Source checksum mismatch: '+body['id'])
  if path.suffix=='.glb':
   n=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+n]);blob=raw[28+n:]
   def image(index):view=doc['bufferViews'][doc['images'][index]['bufferView']];start=view.get('byteOffset',0);return Image.open(io.BytesIO(blob[start:start+view['byteLength']]))
   visual=image(0).convert('RGB')
   if body.get('cube_atlas'):visual=remap_cube(doc,blob,visual)
   if body['id']=='saturn':pyramid('saturn-ring',image(1).convert('RGBA'),'orthographic-solar-image')
  else:visual=Image.open(io.BytesIO(raw)).convert('RGB')
  if visual.width!=visual.height*2:raise ValueError('Expected equirectangular 2:1 visual map')
  pyramid(body['id']+'-day',visual);print(body['id'],visual.size,flush=True)
 source=public/'v1/solar/SOURCES.json';source.parent.mkdir(parents=True,exist_ok=True);source.write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n');register(source)
 (public/'manifest.json').write_text(json.dumps(dict(schema='albert-tiled-assets-v1',version='v1',static=True,layers=layers,files=files),indent=2)+'\n')
 spec=importlib.util.spec_from_file_location('pack',ROOT/'scripts/pack-assets.py');pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack);pack.create_pack(root,'solar')
 metadata=ROOT/'assets/manifest.json';record=json.loads(metadata.read_text());raw=PLAN.read_bytes();record['files']=[v for v in record['files'] if v['path']!='solar-sources.json']+[dict(path='solar-sources.json',bytes=len(raw),sha256=digest(raw),source='NASA VTAD/JPL visual resources',license='Credits and limits in source inventory')];metadata.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--offline',action='store_true');args=parser.parse_args();prepare(args.offline)

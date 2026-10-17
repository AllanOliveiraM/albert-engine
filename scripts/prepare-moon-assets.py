#!/usr/bin/env python3
"""Reproduce the local moon surface pack from pinned NASA GLBs/images.

Python 3 + Pillow + NumPy; measured DSK shapes also require SpicePy.
These authoring dependencies are declared in scripts/Dockerfile.assets.
Originals/intermediates remain in ignored asset-packs/moons/.
"""
import argparse,hashlib,importlib.util,io,json,pathlib,shutil,struct,urllib.request
from solar_imagery import file_digest,image_tiles
from PIL import Image
import numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[1]
def source_file(record,sources,offline):
 path=sources/record['filename']
 if not path.exists():
  if offline:raise FileNotFoundError(path)
  temporary=path.with_suffix('.part')
  with urllib.request.urlopen(record['url'],timeout=90) as response,temporary.open('wb') as output:
   remaining=record['bytes']
   while block:=response.read(min(1024*1024,remaining+1)):
    remaining-=len(block)
    if remaining<0:raise ValueError('Oversized source')
    output.write(block)
  if temporary.stat().st_size!=record['bytes'] or file_digest(temporary)!=record['sha256']:raise ValueError('Downloaded source mismatch')
  temporary.replace(path)
 if path.stat().st_size!=record['bytes'] or file_digest(path)!=record['sha256']:raise ValueError('Source checksum/size mismatch: '+record['filename'])
 return path

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

def cubic(values,coords,axis,wrap):
 # Keys cubic (a=-0.5) along one axis; exact at integer coordinates.
 base=np.floor(coords).astype(np.int64);t=(coords-base).astype(np.float32);shape=[1]*values.ndim;shape[axis]=-1
 weights=[((-.5*t+1)*t-.5)*t,(1.5*t-2.5)*t*t+1,((-1.5*t+2)*t+.5)*t,(.5*t-.5)*t*t];out=0
 for tap,weight in enumerate(weights):
  index=base+tap-1;index=index%values.shape[axis] if wrap else np.clip(index,0,values.shape[axis]-1)
  out=out+np.take(values,index,axis=axis)*weight.reshape(shape)
 return out

def blur(values,radius):
 # Three box passes, close to a Gaussian of sigma ~radius; longitude wraps, rows clamp.
 for axis,mode in ((0,'edge'),(1,'wrap')):
  for _ in range(3):
   pad=[(0,0)]*values.ndim;pad[axis]=(radius+1,radius);total=np.cumsum(np.pad(values,pad,mode=mode),axis=axis,dtype=np.float64);n=values.shape[axis]
   values=(np.take(total,np.arange(2*radius+1,2*radius+1+n),axis=axis)-np.take(total,np.arange(n),axis=axis))/(2*radius+1)
 return values

def no_data(image,spec):
 """Unobserved pixels of a published map: the exact fill value, near-fill noise
 connected to it (up to dark_max; isolated dark shadows stay), every row south of
 the last observed (published) latitude, and the anti-aliased fringe (fringe_px)."""
 def grow(mask):
  grown=mask|np.roll(mask,1,1)|np.roll(mask,-1,1);grown[1:]|=mask[:-1];grown[:-1]|=mask[1:];return grown
 values=np.asarray(image.convert('L'));mask=values==spec.get('value',0);dark=values<=spec.get('dark_max',spec.get('value',0))
 if 'south_of_lat' in spec:mask[int(round((90-spec['south_of_lat'])*image.height/180)):]=True
 while (grown:=grow(mask)&(dark|mask)).sum()>mask.sum():mask=grown
 for _ in range(spec.get('fringe_px',1)):mask=grow(mask)
 return mask

def published(source,projection):
 """Crop a framed map to its 2:1 grid and fill unobserved pixels with the local mean
 of the observed surroundings (about 2 deg), fading to the observed median: no
 invented detail, and no black edges for the resampling or the renderer."""
 image=source.crop(tuple(projection['crop'])) if 'crop' in projection else source.copy()
 if image.width!=2*image.height:raise ValueError('Map grid is not 2:1 after cropping')
 image=image.convert('RGB')
 if 'no_data' not in projection:return image
 gaps=no_data(image,projection['no_data']);observed=(~gaps).astype(np.float32);weight=blur(observed,max(1,image.width//180));bands=[]
 for band in image.split():
  values=np.asarray(band,dtype=np.float32);median=float(np.median(values[~gaps]))
  local=blur(values*observed,max(1,image.width//180))/np.maximum(weight,1e-6);alpha=np.clip(weight*4,0,1)
  bands.append(np.where(gaps,alpha*local+(1-alpha)*median,values))
 return Image.merge('RGB',[Image.fromarray(np.clip(np.rint(b),0,255).astype(np.uint8)) for b in bands])

def registered(image,projection):
 """Resample a published map to planetocentric latitude and east longitude -180..180.
 A feature at (lat, lon) appears in the source at tan(lat')=k*tan(lat), lon+offset;
 source columns start at west_edge_lon (east longitude, degrees)."""
 k=projection.get('tan_latitude_scale',1.);offset=projection.get('longitude_offset_deg',0.);edge=projection.get('west_edge_lon',-180.)
 width,height=image.size
 latitude=np.radians(90-(np.arange(height)+.5)*180/height)
 rows=(90-np.degrees(np.arctan(k*np.tan(latitude))))*height/180-.5
 cols=((-180+(np.arange(width)+.5)*360/width+offset-edge)%360)*width/360-.5
 channels=[cubic(cubic(np.asarray(band,dtype=np.float32),rows,0,False),cols,1,True) for band in image.split()]
 return Image.merge(image.mode,[Image.fromarray(np.clip(np.rint(c),0,255).astype(np.uint8)) for c in channels])

def srgb(linear):
 return np.where(linear<=.0031308,12.92*linear,1.055*np.power(linear,1/2.4)-.055)

def prepare(offline=False):
 plan_path=ROOT/'assets/moon-sources.json';plan=json.loads(plan_path.read_text())
 root=ROOT/'asset-packs/moons';sources=root/'sources';public=root/'public'
 sources.mkdir(parents=True,exist_ok=True)
 # Never publish a mixture of old and new sources/processing.
 if public.exists():shutil.rmtree(public)
 public.mkdir();layers=[];files=[]
 for body in plan['bodies']:
  if body.get('kind')=='uniform':
   # No observed map registered to the measured shape: one measured colour, no invented detail.
   colour=tuple(int(v) for v in np.rint(255*srgb(np.array(body['colour']['albedo_linear']))))
   image=Image.new('RGB',tuple(body['map_grid']),colour)
  elif body.get('kind')=='image':
   path=source_file(body,sources,offline)
   with Image.open(path) as source:
    if list(source.size)!=body['grid']:raise ValueError('Unexpected source image grid')
    image=published(source,body.get('projection',{}))
   if 'projection' in body:image=registered(image,body['projection'])
  else:image=visual(source_file(body,sources,offline).read_bytes(),body['grid'])
  record=image_tiles(public,body['id']+'-day',image);image.close()
  record['layer']['lighting']='Static '+body['credit']+' reference; not calibrated reflectance or elevation'
  layers.append(record['layer']);files.extend(record['files']);print(body['id'],body.get('grid',body.get('map_grid')),flush=True)
  if 'shape_source' in body:
   from moon_shapes import export_shape
   shape=body['shape_source'];source_path=source_file(shape,sources,offline);destination=public/f"v1/{body['id']}/shape.bin"
   export_shape(source_path,shape,destination)
   files.append(dict(path=destination.relative_to(public).as_posix(),bytes=destination.stat().st_size,sha256=file_digest(destination)))
 source=public/'v1/moons/SOURCES.json';source.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(plan_path,source)
 files.append(dict(path=source.relative_to(public).as_posix(),bytes=source.stat().st_size,sha256=file_digest(source)))
 (public/'manifest.json').write_text(json.dumps(dict(schema='albert-tiled-assets-v1',version='v1',static=True,layers=layers,files=files),indent=2)+'\n')
 spec=importlib.util.spec_from_file_location('pack',ROOT/'scripts/pack-assets.py');pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack);pack.create_pack(root,'moons')
 metadata=ROOT/'assets/manifest.json';record=json.loads(metadata.read_text())
 record['files']=[v for v in record['files'] if v['path']!='moon-sources.json']+[dict(path='moon-sources.json',bytes=plan_path.stat().st_size,sha256=file_digest(plan_path),source='NASA/JPL/USGS visual resources',license='Credits and limits in source inventory')]
 metadata.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--offline',action='store_true');args=parser.parse_args();prepare(args.offline)

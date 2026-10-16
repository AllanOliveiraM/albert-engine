#!/usr/bin/env python3
"""Download pinned NASA visual models, remap their cube atlases, and publish a local pack.
Requires Python 3, Pillow and NumPy; originals/work products stay in asset-packs/.
"""
import argparse,hashlib,importlib.util,io,json,os,pathlib,shutil,struct,urllib.request
os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
os.environ.setdefault('OMP_NUM_THREADS','1')
from solar_imagery import file_digest,image_tiles,snapshot_vrt
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
 source=np.asarray(image.convert('RGB'));lookups={};resolution=512
 for axis in range(3):
  other=[i for i in range(3) if i!=axis]
  for side in [-1,1]:
   lookup=np.full((resolution,resolution,2),np.nan)
   for triangle in faces[(axes==axis)&(sign==side)]:
    v=p[triangle];xy=(v[:,other]/np.abs(v[:,axis,None])+1)*resolution/2-.5
    lo=np.maximum(np.ceil(xy.min(0)).astype(int),0);hi=np.minimum(np.floor(xy.max(0)).astype(int),resolution-1)
    if np.any(hi<lo):continue
    xx,yy=np.meshgrid(np.arange(lo[0],hi[0]+1),np.arange(lo[1],hi[1]+1));q=np.stack((xx,yy),-1)-xy[0]
    inverse=np.linalg.inv(np.column_stack((xy[1]-xy[0],xy[2]-xy[0])));weights=q@inverse.T;valid=(weights.min(-1)>=-1e-7)&(weights.sum(-1)<=1+1e-7)
    sampled=uv[triangle[0]]+weights[:,:,:1]*(uv[triangle[1]]-uv[triangle[0]])+weights[:,:,1:]*(uv[triangle[2]]-uv[triangle[0]])
    lookup[yy[valid],xx[valid]]=sampled[valid]
   if not np.isfinite(lookup).all():raise ValueError('Incomplete cube mesh coverage')
   lookups[axis,side]=lookup
 result=np.empty((height,width,3),dtype=np.uint8);lon=(np.arange(width)+.5)*2*np.pi/width-np.pi;lat=np.pi/2-(np.arange(height)+.5)*np.pi/height
 # Bound all projection/interpolation temporaries to 64 rows.
 for row in range(0,height,64):
  stripe=lat[row:row+64];directions=np.stack(np.broadcast_arrays(np.cos(stripe[:,None])*np.cos(lon),np.sin(stripe[:,None]),-np.cos(stripe[:,None])*np.sin(lon)),axis=-1);dominant=np.abs(directions).argmax(-1)
  for (axis,side),lookup in lookups.items():
   other=[i for i in range(3) if i!=axis];selected=(dominant==axis)&(np.sign(directions[:,:,axis])==side);d=directions[selected]
   coords=np.clip((d[:,other]/np.abs(d[:,axis,None])+1)*resolution/2-.5,0,resolution-1);a0=coords.astype(int);a1=np.minimum(a0+1,resolution-1);f=coords-a0
   mapped=(1-f[:,1,None])*((1-f[:,0,None])*lookup[a0[:,1],a0[:,0]]+f[:,0,None]*lookup[a0[:,1],a1[:,0]])+f[:,1,None]*((1-f[:,0,None])*lookup[a1[:,1],a0[:,0]]+f[:,0,None]*lookup[a1[:,1],a1[:,0]])
   x=np.clip(mapped[:,0]*image.width-.5,0,image.width-1);y=np.clip(mapped[:,1]*image.height-.5,0,image.height-1);x0=x.astype(int);y0=y.astype(int);x1=np.minimum(x0+1,image.width-1);y1=np.minimum(y0+1,image.height-1);a=(x-x0)[:,None];b=(y-y0)[:,None]
   result[row:row+len(stripe)][selected]=np.rint((1-b)*((1-a)*source[y0,x0]+a*source[y0,x1])+b*((1-a)*source[y1,x0]+a*source[y1,x1])).astype(np.uint8)
 return Image.fromarray(result)
def prepare(offline=False,mars_snapshot=None):
 plan=json.loads(PLAN.read_text());root=ROOT/'asset-packs/solar';sources=root/'sources';public=root/'public';work=root/'work';sources.mkdir(parents=True,exist_ok=True);public.mkdir(exist_ok=True);work.mkdir(exist_ok=True);layers=[];files=[]
 # Explicit adoption validates the complete snapshot before touching publication.
 if mars_snapshot:
  source_path=pathlib.Path(mars_snapshot).resolve();_,snapshot=snapshot_vrt(source_path,work)
  if source_path!= (sources/'mars-trek/manifest.json').resolve() or snapshot['level']!=6:raise ValueError('Use the complete level-6 snapshot in asset-packs/solar/sources/mars-trek')
  body=next(v for v in plan['bodies'] if v['id']=='mars');body.update(filename='mars-trek/manifest.json',url='https://trek.nasa.gov/tiles/Mars/EQ/Mars_Viking_MDIM21_ClrMosaic_global_232m/1.0.0/',sha256=file_digest(source_path),bytes=source_path.stat().st_size,cube_atlas=False,kind='wmts-snapshot',level=6,credit='NASA/JPL/USGS Viking MDIM 2.1; NASA Trek',product='https://trek.nasa.gov/tiles/apidoc/trekAPI.html?body=mars',limits='Static colorized Viking mosaic. WMTS grid 32768 x 16384; effective resolution/coverage vary. No DEM or current weather. Longitude registration requires independent validation.')
 # A new source/processing fingerprint invalidates only generated intermediates.
 fingerprint=digest(json.dumps(plan,sort_keys=True).encode()+b'solar-strip-v2');state=work/'plan.sha256'
 if not state.exists() or state.read_text()!=fingerprint:
  if public.exists():shutil.rmtree(public)
  public.mkdir();state.write_text(fingerprint)
 def register(path):files.append(dict(path=path.relative_to(public).as_posix(),bytes=path.stat().st_size,sha256=file_digest(path)))
 def pyramid(name,image,projection='equirectangular'):
  record=image_tiles(public,name,image,projection);layers.append(record['layer']);files.extend(record['files'])
 for body in plan['bodies']:
  path=sources/body['filename']
  if body.get('kind')=='wmts-snapshot':
   if not path.exists() or json.loads(path.read_text()).get('level')!=body['level']:
    if offline:raise FileNotFoundError('Complete Mars source snapshot missing: '+str(path))
    spec=importlib.util.spec_from_file_location('mars_download',ROOT/'scripts/download-mars-tiles.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.download(body['level'],2)
   vrt,snapshot=snapshot_vrt(path,work,body['sha256'])
   from osgeo import gdal
   gdal.UseExceptions();gdal.SetCacheMax(128*1024*1024)
   for key,value in {'GDAL_NUM_THREADS':'1','GDAL_MAX_DATASET_POOL_SIZE':'16','VSI_CACHE':'FALSE','GDAL_DISABLE_READDIR_ON_OPEN':'TRUE','GDAL_PAM_ENABLED':'NO'}.items():gdal.SetConfigOption(key,value)
   spec=importlib.util.spec_from_file_location('highres',ROOT/'scripts/prepare-assets.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
   dataset=gdal.Open(str(vrt));width,height=dataset.RasterXSize,dataset.RasterYSize
   record=module.tile_pyramid(root,dataset,'mars-day',width,height,512,6,'RGB',dict(kind='visual',source_ids=['mars'],lighting='Static colorized Viking mosaic; not calibrated albedo',terrain_available=False))
   dataset=None;layers.append(record['layer']);files.extend(record['files'])
   packed=public/'v1/solar/mars-source.json';packed.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,packed);register(packed);print('mars',(width,height),flush=True);continue
  if not path.exists():
   if offline:raise FileNotFoundError(path)
   temporary=path.with_suffix(path.suffix+'.part')
   with urllib.request.urlopen(body['url'],timeout=90) as response,temporary.open('wb') as output:shutil.copyfileobj(response,output,1024*1024)
   if file_digest(temporary)!=body['sha256']:raise ValueError('Downloaded source checksum mismatch: '+body['id'])
   temporary.replace(path)
  if file_digest(path)!=body['sha256']:raise ValueError('Source checksum mismatch: '+body['id'])
  if path.suffix=='.glb':
   raw=path.read_bytes();n=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+n]);blob=raw[28+n:]
   def image(index):view=doc['bufferViews'][doc['images'][index]['bufferView']];start=view.get('byteOffset',0);return Image.open(io.BytesIO(blob[start:start+view['byteLength']]))
   visual=image(0).convert('RGB')
   if body.get('cube_atlas'):visual=remap_cube(doc,blob,visual)
   if body['id']=='saturn':pyramid('saturn-ring',image(1).convert('RGBA'),'orthographic-solar-image')
  else:visual=Image.open(path).convert('RGB')
  if visual.width!=visual.height*2:raise ValueError('Expected equirectangular 2:1 visual map')
  pyramid(body['id']+'-day',visual);print(body['id'],visual.size,flush=True)
 source=public/'v1/solar/SOURCES.json';source.parent.mkdir(parents=True,exist_ok=True);source.write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n');register(source)
 (public/'manifest.json').write_text(json.dumps(dict(schema='albert-tiled-assets-v1',version='v1',static=True,layers=layers,files=files),indent=2)+'\n')
 spec=importlib.util.spec_from_file_location('pack',ROOT/'scripts/pack-assets.py');pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack);pack.create_pack(root,'solar')
 PLAN.write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n')
 metadata=ROOT/'assets/manifest.json';record=json.loads(metadata.read_text());raw=PLAN.read_bytes();record['files']=[v for v in record['files'] if v['path']!='solar-sources.json']+[dict(path='solar-sources.json',bytes=len(raw),sha256=digest(raw),source='NASA VTAD/GSFC/JPL/USGS visual resources',license='Credits and limits in source inventory')];metadata.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--offline',action='store_true');parser.add_argument('--mars-snapshot',type=pathlib.Path,help='Validate and adopt the complete level-6 NASA Trek snapshot');args=parser.parse_args();prepare(args.offline,args.mars_snapshot)

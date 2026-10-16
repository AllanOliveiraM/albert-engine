"""Asset-authoring regression; run with Dockerfile.assets dependencies."""
import hashlib,importlib.util,io,json,pathlib,sys,tempfile
import numpy as np
from PIL import Image
from osgeo import gdal
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from solar_imagery import image_tiles,snapshot_vrt

with tempfile.TemporaryDirectory(prefix='albert-solar-') as directory:
 root=pathlib.Path(directory);public=root/'public';y,x=np.mgrid[:526,:1050]
 pixels=np.stack((x%251,y%251,(x//10+y//10)%251,np.full_like(x,255)),axis=-1).astype('uint8');source=Image.fromarray(pixels)
 record=image_tiles(public,'fixture-day',source)
 with Image.open(public/'v1/fixture-day/1/1/2.png') as edge:
  assert edge.size==(30,18),'Partial edge tile dimensions changed'
  assert edge.getpixel((2,2))==source.getpixel((1024,512)),'Partial tile origin changed'
  assert edge.getpixel((27,15))==source.getpixel((1049,525)),'Last source pixel lost'
 with Image.open(public/'v1/fixture-day/1/0/0.png') as first:
  assert first.getpixel((0,2))==source.getpixel((1048,0)),'Longitude gutter does not wrap'
  assert first.getpixel((2,0))==source.getpixel((0,0)),'Pole gutter does not clamp'
 assert len(record['files'])==8
 image_tiles(public,'profile',source,'orthographic-solar-image')
 with Image.open(public/'v1/profile/1/0/0.png') as first:assert first.getpixel((0,2))==source.getpixel((0,0)),'Non-geographic profile wrapped across its ends'
 snapshot=root/'snapshot';entries=[]
 for col,color in enumerate(((220,10,10),(10,10,220))):
  path=snapshot/f'0/0/{col}.jpg';path.parent.mkdir(parents=True,exist_ok=True);Image.new('RGB',(256,256),color).save(path,quality=98)
  raw=path.read_bytes();entries.append(dict(path=f'0/0/{col}.jpg',bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
 metadata=dict(provider='NASA Trek',layer='Mars_Viking_MDIM21_ClrMosaic_global_232m',level=0,columns=2,rows=1,tile_size=256,source_bounds=[-180,-90,180,90],files=entries)
 manifest=snapshot/'manifest.json';manifest.write_text(json.dumps(metadata));checksum=hashlib.sha256(manifest.read_bytes()).hexdigest();vrt,_=snapshot_vrt(manifest,root/'work',checksum)
 gdal.UseExceptions();gdal.SetCacheMax(16*1024*1024);dataset=gdal.Open(str(vrt));assert (dataset.RasterXSize,dataset.RasterYSize)==(512,256)
 raster=dataset.ReadAsArray();assert raster[0,100,100]>200 and raster[2,100,400]>200,'WMTS tile placement or bands changed';dataset=None
 original=vrt.read_bytes()
 def rejects(value):
  manifest.write_text(json.dumps(value))
  try:snapshot_vrt(manifest,root/'work');raise AssertionError('Invalid snapshot accepted')
  except ValueError:pass
  assert vrt.read_bytes()==original,'Rejected snapshot damaged the last valid VRT'
 rejects(dict(metadata,files=entries[:1]));rejects(dict(metadata,files=[entries[0],entries[0]]))
 rejects(dict(metadata,files=[dict(entries[0],path='../outside.jpg'),entries[1]]))
 (snapshot/entries[0]['path']).write_bytes(b'corrupt');rejects(metadata)
 print('Solar assets: partial tiles, longitude/pole gutters, profiles, WMTS placement, completeness and corruption recovery OK')

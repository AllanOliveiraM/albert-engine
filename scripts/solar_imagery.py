"""Bounded-memory preparation of planetary imagery; no network or publication."""
import hashlib,json,pathlib,xml.etree.ElementTree as ET

def file_digest(path):
 with pathlib.Path(path).open('rb') as stream:
  result=hashlib.sha256()
  while block:=stream.read(1024*1024):result.update(block)
 return result.hexdigest()

def image_tiles(public,name,image,projection='equirectangular'):
 """Small source maps: pad one strip at a time, including partial edge tiles."""
 from PIL import Image
 import math
 width,height=image.size;levels=max(0,min(8,int(math.log2(max(1,width//512))),int(math.log2(height))));tile=512;fmt='png' if image.mode=='RGBA' else 'jpg'
 layer=dict(id=name,projection=projection,bounds=[-180,-90,180,90],width=width,height=height,tile_size=tile,gutter=2,max_level=levels,format=fmt,kind='visual',source_ids=[name.split('-')[0]],lighting='Static NASA visual reference; not calibrated reflectance',terrain_available=False)
 files=[]
 for level in range(levels+1):
  w,h=width>>(levels-level),height>>(levels-level);scaled=image.resize((w,h),Image.Resampling.LANCZOS)
  for row in range((h+tile-1)//tile):
   core=min(tile,h-row*tile);start=max(0,row*tile-2);end=min(h,(row+1)*tile+2)
   strip=scaled.crop((0,start,w,end));padded=Image.new(image.mode,(w+4,core+4));offset=max(0,2-row*tile);padded.paste(strip,(2,offset))
   if offset:padded.paste(strip.crop((0,0,w,1)).resize((w,offset)),(2,0))
   used=offset+strip.height
   if used<core+4:padded.paste(strip.crop((0,strip.height-1,w,strip.height)).resize((w,core+4-used)),(2,used))
   if projection=='equirectangular':
    padded.paste(padded.crop((w,0,w+2,core+4)),(0,0));padded.paste(padded.crop((2,0,4,core+4)),(w+2,0))
   else:
    padded.paste(padded.crop((2,0,3,core+4)).resize((2,core+4)),(0,0));padded.paste(padded.crop((w+1,0,w+2,core+4)).resize((2,core+4)),(w+2,0))
   for col in range((w+tile-1)//tile):
    path=public/f'v1/{name}/{level}/{row}/{col}.{fmt}';path.parent.mkdir(parents=True,exist_ok=True)
    crop=padded.crop((col*tile,0,col*tile+min(tile,w-col*tile)+4,core+4));temporary=path.with_suffix('.tmp')
    crop.save(temporary,**(dict(format='JPEG',quality=94,subsampling=0,optimize=True,progressive=True) if fmt=='jpg' else dict(format='PNG',compress_level=9)))
    temporary.replace(path);files.append(dict(path=path.relative_to(public).as_posix(),bytes=path.stat().st_size,sha256=file_digest(path)))
 return dict(layer=layer,files=files)

def snapshot_vrt(manifest_path,work,expected_sha=None):
 """Validate every source tile before making a virtual, unstitched mosaic."""
 from PIL import Image
 manifest_path=pathlib.Path(manifest_path)
 with manifest_path.open('rb') as stream:raw=stream.read(8*1024*1024+1)
 if len(raw)>8*1024*1024 or (expected_sha and hashlib.sha256(raw).hexdigest()!=expected_sha):raise ValueError('Mars snapshot manifest checksum/size mismatch')
 record=json.loads(raw);level=record['level'];tile=record['tile_size'];cols,rows=record['columns'],record['rows']
 if any(type(v)!=int for v in (level,tile,cols,rows)) or not 0<=level<=7 or tile!=256 or cols!=2**(level+1) or rows!=2**level or record['source_bounds']!=[-180,-90,180,90] or record['provider']!='NASA Trek' or record['layer']!='Mars_Viking_MDIM21_ClrMosaic_global_232m':raise ValueError('Unsupported Mars snapshot grid')
 if len(record['files'])!=cols*rows:raise ValueError('Incomplete Mars snapshot')
 root=ET.Element('VRTDataset',rasterXSize=str(cols*tile),rasterYSize=str(rows*tile))
 ET.SubElement(root,'GeoTransform').text=f'-180,{360/(cols*tile)},0,90,0,{-180/(rows*tile)}'
 bands=[]
 for i,color in enumerate(('Red','Green','Blue'),1):
  band=ET.SubElement(root,'VRTRasterBand',dataType='Byte',band=str(i));ET.SubElement(band,'ColorInterp').text=color;bands.append(band)
 seen=set();directory=manifest_path.parent.resolve()
 for entry in record['files']:
  parts=pathlib.PurePosixPath(entry['path']).parts
  if len(parts)!=3 or parts[0]!=str(level) or not parts[1].isdigit() or not parts[2].endswith('.jpg') or not parts[2][:-4].isdigit():raise ValueError('Invalid Mars tile path')
  row,col=int(parts[1]),int(parts[2][:-4]);path=directory/entry['path']
  if not 0<=row<rows or not 0<=col<cols or (row,col) in seen or not path.resolve().is_relative_to(directory):raise ValueError('Duplicate or out-of-grid Mars tile')
  seen.add((row,col))
  if not path.is_file() or path.stat().st_size!=entry['bytes'] or entry['bytes']>1024*1024 or file_digest(path)!=entry['sha256']:raise ValueError('Mars source tile checksum/size mismatch: '+entry['path'])
  with Image.open(path) as image:
   if image.size!=(tile,tile) or image.mode!='RGB' or image.format!='JPEG':raise ValueError('Unexpected Mars source image')
   image.verify()
  for i,band in enumerate(bands,1):
   source=ET.SubElement(band,'SimpleSource');ET.SubElement(source,'SourceFilename',relativeToVRT='0').text=str(path)
   ET.SubElement(source,'SourceBand').text=str(i)
   ET.SubElement(source,'SourceProperties',RasterXSize=str(tile),RasterYSize=str(tile),DataType='Byte',BlockXSize=str(tile),BlockYSize='1')
   ET.SubElement(source,'SrcRect',xOff='0',yOff='0',xSize=str(tile),ySize=str(tile))
   ET.SubElement(source,'DstRect',xOff=str(col*tile),yOff=str(row*tile),xSize=str(tile),ySize=str(tile))
 work=pathlib.Path(work);work.mkdir(parents=True,exist_ok=True);path=work/'mars-viking.vrt';ET.ElementTree(root).write(path,encoding='utf-8',xml_declaration=True)
 return path,record

#!/usr/bin/env python3
"""Prepare the optional, versioned cloud library. Payloads never enter Git.

Download is resumable. Processing uses GDAL and Pillow from Dockerfile.assets.
"""
import argparse, concurrent.futures, hashlib, importlib.util, json, os, pathlib, shutil, subprocess, sys, time
ROOT=pathlib.Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def download(source,root):
    path=root/'sources'/source['filename'];path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file():
        part=path.with_suffix(path.suffix+'.part')
        print('Downloading:',source['id'],flush=True)
        for attempt in range(6):
            # Recalculate the resume offset on every attempt, including after
            # a partially received response. HTTP/1.1 avoids large-stream
            # HTTP/2 cancellations seen on the SVS source server.
            result=subprocess.run(['curl','--http1.1','--fail','--location','--connect-timeout','20','--max-time','1800','--continue-at','-','--silent','--show-error','--output',str(part),source['url']])
            if result.returncode==0:break
            if attempt==5:raise RuntimeError('Download failed: '+source['id'])
            print('Retrying:',source['id'],'from',part.stat().st_size if part.exists() else 0,'bytes',flush=True)
            time.sleep(2*(attempt+1))
        with part.open('rb') as stream:header=stream.read(4)
        if header not in (b'II*\x00',b'MM\x00*',b'II+\x00',b'MM\x00+') and not header.startswith(b'\xff\xd8'):
            raise RuntimeError('Invalid image header: '+str(part))
        part.replace(path)
    checksum=digest(path)
    metadata=path.with_suffix(path.suffix+'.json')
    if metadata.is_file() and json.loads(metadata.read_text())['sha256']!=checksum:
        raise RuntimeError('Existing source checksum mismatch: '+str(path))
    record=dict(source,bytes=path.stat().st_size,sha256=checksum)
    path.with_suffix(path.suffix+'.json').write_text(json.dumps(record,indent=2)+'\n')
    print('Ready:',source['id'],round(record['bytes']/1024**2,1),'MiB',flush=True)
    return record

def process(root, selected=None):
    from osgeo import gdal
    from PIL import Image
    import math
    radius=1737400.
    gdal.UseExceptions();gdal.SetCacheMax(384*1024*1024)
    gdal.SetConfigOption('GDAL_NUM_THREADS','2')
    Image.MAX_IMAGE_PIXELS=None
    work=root/'work';work.mkdir(exist_ok=True)
    public=root/'public';public.mkdir(exist_ok=True)
    source_plan=json.loads((ROOT/'assets/highres-sources.json').read_text())
    records=[]
    for entry in source_plan['sources']:
        if selected and entry['layer'] not in selected:continue
        path=root/'sources'/entry['filename']
        if not path.is_file():raise RuntimeError('Source is not ready: '+str(path))
        record=json.loads(path.with_suffix(path.suffix+'.json').read_text())
        if digest(path)!=record['sha256']:raise RuntimeError('Source checksum mismatch: '+str(path))
        records.append(record)
    files=[];layers=[]
    def register(path):
        files.append({'path':path.relative_to(public).as_posix(),'bytes':path.stat().st_size,'sha256':digest(path)})
    def pyramid(dataset,name,width,height,tile,levels,mode,info,dataset_for_level=None):
        bands=3 if mode=='RGB' else 1
        data_type=gdal.GDT_UInt16 if mode=='I;16' else gdal.GDT_Byte
        extension='png' if name.endswith('height') or name=='earth-night' else 'jpg'
        layer=dict(id=name,projection='equirectangular',bounds=[-180,-90,180,90],width=width,height=height,tile_size=tile,gutter=2,max_level=levels,format=extension)
        layer.update(info)
        start=len(files)
        layers.append(layer)
        for level in range(levels+1):
            w=width//(1<<(levels-level));h=height//(1<<(levels-level))
            if dataset_for_level:dataset=dataset_for_level(level,w,h)
            cols=math.ceil(w/tile);rows=math.ceil(h/tile)
            print('Processing:',name,'level',level,f'{w}x{h}',cols*rows,'tiles',flush=True)
            for row in range(rows):
                destinations=[public/f'v1/{name}/{level}/{row}/{col}.{extension}' for col in range(cols)]
                if all(path.is_file() for path in destinations):
                    for path in destinations:register(path)
                    continue
                y0=max(0,row*tile-2);y1=min(h,(row+1)*tile+2)
                raw=dataset.ReadRaster(0,y0*dataset.RasterYSize/h,dataset.RasterXSize,(y1-y0)*dataset.RasterYSize/h,
                    buf_xsize=w,buf_ysize=y1-y0,buf_type=data_type,band_list=list(range(1,bands+1)),
                    buf_pixel_space=bands*(2 if mode=='I;16' else 1),buf_line_space=w*bands*(2 if mode=='I;16' else 1),
                    buf_band_space=(2 if mode=='I;16' else 1),resample_alg=gdal.GRIORA_Average)
                strip=Image.frombytes(mode,(w,y1-y0),bytes(raw))
                del raw
                padded=Image.new(mode,(w+4,tile+4))
                offset=2 if row==0 else 0
                padded.paste(strip,(2,offset))
                if offset:padded.paste(strip.crop((0,0,w,1)).resize((w,2)),(2,0))
                used=offset+strip.height
                if used<tile+4:padded.paste(strip.crop((0,strip.height-1,w,strip.height)).resize((w,tile+4-used)),(2,used))
                # Repeat longitude at the antimeridian, including real gutters.
                padded.paste(padded.crop((w,0,w+2,tile+4)),(0,0))
                padded.paste(padded.crop((2,0,4,tile+4)),(w+2,0))
                for col,path in enumerate(destinations):
                    path.parent.mkdir(parents=True,exist_ok=True)
                    if not path.is_file():
                        core=min(tile,w-col*tile)
                        image=padded.crop((col*tile,0,col*tile+core+4,min(tile,h-row*tile)+4))
                        temporary=path.with_suffix('.tmp')
                        if extension=='jpg':image.save(temporary,format='JPEG',quality=94,subsampling=0,optimize=True,progressive=True)
                        else:image.save(temporary,format='PNG',compress_level=9,optimize=True)
                        temporary.replace(path)
                    register(path)
                if row%4==0 or row+1==rows:print('Rows:',name,level,row+1,'/',rows,flush=True)
        record_path=work/(name+'.json')
        record_path.write_text(json.dumps({'layer':layer,'files':files[start:]},indent=2)+'\n')
    for name in ('earth-day','earth-night'):
        if selected and name not in selected:continue
        sources=[str(root/'sources'/s['filename']) for s in records if s['layer']==name]
        dataset=gdal.BuildVRT(str(work/(name+'.vrt')),sources)
        if dataset.RasterXSize!=86400 or dataset.RasterYSize!=43200:raise RuntimeError('Unexpected Earth grid')
        pyramid(dataset,name,86400,43200,1350,4,'RGB' if name=='earth-day' else 'L',{'resolution_m_equator':500,'epoch':'2004-10' if name=='earth-day' else '2016','source_ids':[s['id'] for s in records if s['layer']==name]})
    if not selected or 'moon-day' in selected:
        # Reproject each source virtually, including stereographic polar products.
        # No enormous uncompressed global TIFF is written to disk.
        extent=math.pi*radius
        srs='+proj=eqc +lat_ts=0 +lon_0=0 +R=1737400 +units=m +no_defs'
        warped=[];lunar_sources=[]
        for entry in records:
            if entry['layer']!='moon-day':continue
            path=work/(entry['id']+'.vrt')
            source_path=root/'sources'/entry['filename']
            source=gdal.Open(str(source_path))
            if '_E300' in entry['id'] and source.GetGeoTransform()[0]+source.RasterXSize*source.GetGeoTransform()[1]*.5>extent:
                # The source EQC grid uses 0..360 degrees; the library uses -180..180.
                shifted=work/(entry['id']+'-shifted.vrt')
                source=gdal.Translate(str(shifted),source,format='VRT')
                transform=list(source.GetGeoTransform());transform[0]-=2*extent
                source.SetGeoTransform(transform);source.FlushCache()
            lunar_sources.append((entry['id'],source.GetDescription()))
            if '_E300' in entry['id']:
                center=int(entry['id'].split('_')[2][5:9])/10.
                if center>180:center-=360.
                north='E300N' in entry['id']
                bounds=((center-45)*extent/180.,0. if north else -extent/3.,(center+45)*extent/180.,extent/3. if north else 0.)
                dimensions=(27000,18000)
            else:
                north='P900N' in entry['id']
                bounds=(-extent,extent/3. if north else -extent/2.,extent,extent/2. if north else -extent/3.)
                dimensions=(108000,9000)
            # Keep each virtual image within its real footprint. Expanding all
            # ten sources to the entire globe would repeatedly warp empty areas.
            dataset=gdal.Warp(str(path),source,format='VRT',dstSRS=srs,
                outputBounds=bounds,width=dimensions[0],height=dimensions[1],
                resampleAlg='bilinear',dstNodata=0,warpMemoryLimit=128*1024*1024)
            if dataset is None:raise RuntimeError('Lunar reprojection failed')
            dataset=None;warped.append(str(path))
        dataset=gdal.BuildVRT(str(work/'moon-day.vrt'),warped,srcNodata=0,VRTNodata=0)
        if dataset.RasterXSize!=108000 or dataset.RasterYSize!=54000:raise RuntimeError('Unexpected lunar grid')
        native=dataset
        def lunar_level(level,w,h):
            if level==4:return native
            # Warp directly at the requested mip resolution. Averaging a
            # native-resolution warped globe would repeat billions of costly
            # coordinate transformations even for the smallest mip.
            sources=[]
            for source_id,source_path in lunar_sources:
                path=work/(source_id+f'-L{level}.vrt')
                reduced=gdal.Warp(str(path),source_path,format='VRT',dstSRS=srs,
                    outputBounds=(-extent,-extent/2,extent,extent/2),width=w,height=h,
                    resampleAlg='average',dstNodata=0,warpMemoryLimit=128*1024*1024)
                if reduced is None:raise RuntimeError('Lunar mip reprojection failed')
                reduced=None;sources.append(str(path))
            return gdal.BuildVRT(str(work/f'moon-day-L{level}.vrt'),sources,srcNodata=0,VRTNodata=0)
        pyramid(dataset,'moon-day',108000,54000,1125,4,'L',{'resolution_m_equator':2*math.pi*radius/108000,'source_ids':[s['id'] for s in records if s['layer']=='moon-day'],'lighting':'acquisition illumination; morphology, not a DEM','nodata':0},lunar_level)
    for name,width,height,tile,mode,encoding in (
        ('earth-height',21600,10800,675,'L','height_m = sample * 6400 / 255'),
        ('moon-height',23040,11520,720,'I;16','radius_m = 1727400 + sample * 0.5')):
        if selected and name not in selected:continue
        entry=next(s for s in records if s['layer']==name)
        dataset=gdal.Open(str(root/'sources'/entry['filename']))
        if dataset.RasterXSize!=width or dataset.RasterYSize!=height:raise RuntimeError('Unexpected elevation dimensions')
        pyramid(dataset,name,width,height,tile,4,mode,{'encoding':encoding,'source_ids':[entry['id']],'resolution_m_equator':2*math.pi*(6378137. if name=='earth-height' else radius)/width})
    if not selected or 'sun' in selected:
        entry=next(s for s in records if s['layer']=='sun')
        dataset=gdal.Open(str(root/'sources'/entry['filename']))
        pyramid(dataset,'sun',4096,4096,1024,2,'RGB',{'projection':'orthographic-solar-image','bounds':[0,0,1,1],'epoch':entry['epoch'],'source_ids':['sun'],'encoding':entry['encoding']})
    expected=('earth-day','earth-night','moon-day','earth-height','moon-height','sun')
    if any(not (work/(name+'.json')).is_file() for name in expected):
        print('Selected layers complete; remaining layers are required before publishing the manifest.',flush=True)
        return
    files=[];layers=[]
    for name in expected:
        record=json.loads((work/(name+'.json')).read_text())
        layers.append(record['layer']);files.extend(record['files'])
    records=[json.loads((root/'sources'/entry['filename']).with_suffix(pathlib.Path(entry['filename']).suffix+'.json').read_text()) for entry in source_plan['sources']]
    provenance=json.dumps({'version':1,'sources':records},indent=2)+'\n'
    (public/'SOURCES.json').write_text(provenance)
    (root/'sources.json').write_text(provenance)
    register(public/'SOURCES.json')
    manifest={'schema':'albert-tiled-assets-v1','version':'v1','orientation':'north at row 0; longitude -180 at column 0',
        'layers':layers,'files':files,'total_bytes':sum(f['bytes'] for f in files),
        'credits':'NASA Earth Observatory / NASA SDO HMI / NASA GSFC ASU LROC / NASA LOLA / GEBCO08',
        'static':True,'notes':'Reference mosaics; no weather or ephemerides. Upload the entire public directory unchanged.'}
    temporary=public/'manifest.json.tmp';temporary.write_text(json.dumps(manifest,indent=2)+'\n');temporary.replace(public/'manifest.json')
    (public/'LEIA-ME.txt').write_text('Albert Engine — biblioteca visual v1\n\nHospede TODO o conteudo desta pasta, preservando os caminhos.\nPasse o endereco publico desta pasta; manifest.json precisa estar acessivel.\nJPEGs ja comprimidos; elevacao PNG sem perdas. Nao aplique nova conversao nas imagens.\nFontes, datas, resolucoes e hashes: manifest.json e SOURCES.json.\nAs imagens sao referencias estaticas; nuvens e efemerides sao consultadas separadamente.\n',encoding='utf-8')
    print('READY:',public,len(files),'files',round(manifest['total_bytes']/1024**3,3),'GiB',flush=True)

def verify(root):
    from PIL import Image
    public=root/'public'
    manifest=json.loads((public/'manifest.json').read_text())
    expected=set()
    for layer in manifest['layers']:
        tile=layer['tile_size'];gutter=layer['gutter']
        for level in range(layer['max_level']+1):
            divisor=1<<(layer['max_level']-level)
            width=layer['width']//divisor;height=layer['height']//divisor
            for row in range((height+tile-1)//tile):
                for col in range((width+tile-1)//tile):
                    name=f"v1/{layer['id']}/{level}/{row}/{col}.{layer['format']}"
                    expected.add(name)
                    with Image.open(public/name) as image:
                        size=(min(tile,width-col*tile)+2*gutter,min(tile,height-row*tile)+2*gutter)
                        if image.size!=size:raise RuntimeError('Tile dimensions mismatch: '+name)
                        if layer['id']=='moon-height' and image.mode not in ('I','I;16'):
                            raise RuntimeError('16-bit elevation was lost: '+name)
                        image.verify()
    declared={entry['path'] for entry in manifest['files']}
    if declared!=expected|{'SOURCES.json'}:raise RuntimeError('Tile coverage mismatch')
    for entry in manifest['files']:
        path=public/entry['path']
        if path.stat().st_size!=entry['bytes'] or digest(path)!=entry['sha256']:
            raise RuntimeError('Tile checksum mismatch: '+entry['path'])
    print('Verified:',len(expected),'tiles; complete coverage, dimensions, bit depth and SHA-256.',flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'asset-packs')
    parser.add_argument('--download-only',action='store_true')
    parser.add_argument('--process-only',action='store_true')
    parser.add_argument('--verify',action='store_true',help='Validate the completed public library without downloading or changing it.')
    parser.add_argument('--pack-only',action='store_true',help='Package the completed library without downloading or reprocessing.')
    parser.add_argument('--jobs',type=int,default=3)
    parser.add_argument('--layers',nargs='+',choices=['earth-day','earth-night','moon-day','earth-height','moon-height','sun'],help='Process selected layers; final manifest is published only after all layers are complete.')
    args=parser.parse_args();root=args.output.resolve();root.mkdir(parents=True,exist_ok=True)
    if args.verify:
        verify(root);return
    if args.pack_only:
        verify(root)
        package(root);return
    if args.download_only and args.process_only:parser.error('Choose download-only or process-only')
    plan=json.loads((ROOT/'assets/highres-sources.json').read_text())
    if not args.process_only:
        if shutil.disk_usage(root).free<25*1024**3:raise RuntimeError('At least 25 GiB of free disk space is required for sources and processing.')
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.jobs,4))) as pool:
            records=list(pool.map(lambda source:download(source,root),plan['sources']))
        (root/'sources.json').write_text(json.dumps({'version':1,'sources':records},indent=2)+'\n')
    if args.download_only:return
    process(root,args.layers)
    if (root/'public/manifest.json').is_file():
        verify(root);package(root)

def package(root):
    spec=importlib.util.spec_from_file_location('pack_assets',ROOT/'scripts/pack-assets.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.create_pack(root)

if __name__=='__main__':main()

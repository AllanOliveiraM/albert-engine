#!/usr/bin/env python3
"""Reproduce a solid body's optimized, versioned local imagery archive."""
import argparse,hashlib,importlib.util,json,pathlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/file);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result

def pds_layer(root,record,datum):
    # PDS3 simple-cylindrical product area-averaged onto the target grid (pds_maps),
    # written once to a tiled GeoTIFF in work/; heights become uint16 steps above the
    # body's datum with the inventory's explicit scale and offset (0 = no data).
    from osgeo import gdal
    import numpy as np
    pds=module('pds','pds_maps.py');source=root/'sources'/record['filename'];meta,_=pds.image(source)
    if [int(meta['LINE_SAMPLES']),int(meta['LINES'])]!=record['grid']:raise RuntimeError('Unexpected source grid: '+record['layer'])
    width,height=record['target_grid'];elevation=record.get('kind')=='elevation';path=root/'work'/(record['layer']+'.tif')
    if not path.exists():
        temporary=path.with_name(path.stem+'.tmp.tif')
        output=gdal.GetDriverByName('GTiff').Create(str(temporary),width,height,1,gdal.GDT_UInt16 if elevation else gdal.GDT_Byte,['TILED=YES','COMPRESS=DEFLATE','BIGTIFF=YES'])
        for top,values,valid in pds.resample(source,width,height):
            if elevation:
                radius=values*float(meta['SCALING_FACTOR'])+float(meta['OFFSET'])
                sample=np.rint((radius-datum-record['height_offset_m'])/record['height_scale_m'])
                if valid.min()>=.5 and (sample.min()<1 or sample.max()>65535):raise RuntimeError('Heights exceed the uint16 encoding: '+record['layer'])
                sample=np.where(valid>=.5,np.clip(sample,1,65535),0).astype(np.uint16)
            else:sample=np.where(valid>=.5,np.clip(np.rint(values),0,255),0).astype(np.uint8)
            output.GetRasterBand(1).WriteArray(sample,0,top)
        if record.get('fill_gaps'):fill_gaps(output)
        output.FlushCache();output=None;temporary.replace(path)
    info=dict(kind='elevation',sample_type='uint16',height_scale_m=record['height_scale_m'],height_offset_m=record['height_offset_m'],nodata_sample=0) if elevation else dict(kind='reflectance',lighting=record['lighting'],nodata=0)
    return gdal.Open(str(path)),(width,height),info

def fill_gaps(output,factor=16):
    # Unobserved pixels (0) take the local mean of the observations around them
    # (about 2 deg), fading to the observed median, as the moon maps: computed on a
    # grid factor x coarser and interpolated into the gaps only; no detail is invented.
    import numpy as np
    blur=module('moon','prepare-moon-assets.py').blur;band=output.GetRasterBand(1);width,height=output.RasterXSize,output.RasterYSize
    coarse=(height//factor,width//factor);total=np.zeros(coarse);count=np.zeros(coarse);median=[]
    for top in range(0,coarse[0]*factor,factor):
        block=band.ReadAsArray(0,top,coarse[1]*factor,factor).astype(np.float64).reshape(factor,coarse[1],factor).transpose(1,0,2).reshape(coarse[1],-1)
        total[top//factor]=block.sum(1);count[top//factor]=(block>0).sum(1);median.append(block[block>0][::97])
    median=float(np.median(np.concatenate(median)));observed=(count>0).astype(np.float64)
    radius=max(1,coarse[1]//180*2);weight=blur(observed,radius);local=blur(np.divide(total,np.maximum(count,1))*observed,radius)/np.maximum(weight,1e-6)
    field=np.clip(weight*4,0,1)*local+(1-np.clip(weight*4,0,1))*median
    for top in range(0,height,256):
        rows=min(256,height-top);block=band.ReadAsArray(0,top,width,rows)
        if not (block==0).any():continue
        y=np.clip((top+np.arange(rows)+.5)/factor-.5,0,coarse[0]-1);x=(np.arange(width)+.5)/factor-.5
        y0=np.minimum(y.astype(int),coarse[0]-2);fy=(y-y0)[:,None];x0=np.floor(x).astype(int);fx=x-x0
        a,b=field[y0][:,x0%coarse[1]],field[y0][:,(x0+1)%coarse[1]];c,d=field[y0+1][:,x0%coarse[1]],field[y0+1][:,(x0+1)%coarse[1]]
        value=(a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy
        band.WriteArray(np.where(block==0,np.clip(np.rint(value),1,255),block).astype(np.uint8),0,top)

def relief(path,datum,scale,offset):
    # Measured extent of a height map: largest radius and the body-axis dimensions.
    from osgeo import gdal
    import numpy as np
    data=gdal.Open(str(path));width,height=data.RasterXSize,data.RasterYSize;lon=np.radians(-180+(np.arange(width)+.5)*360/width)
    largest=0;low=np.full(3,np.inf);high=np.full(3,-np.inf)
    for top in range(0,height,256):
        block=data.GetRasterBand(1).ReadAsArray(0,top,width,min(256,height-top)).astype(np.float64)
        lat=np.radians(90-(top+np.arange(block.shape[0])+.5)*180/height)[:,None];r=np.where(block>0,datum+offset+block*scale,np.nan)
        xyz=np.stack((r*np.cos(lat)*np.cos(lon),r*np.cos(lat)*np.sin(lon),r*np.sin(lat)*np.ones_like(lon)))
        largest=max(largest,np.nanmax(r));low=np.minimum(low,np.nanmin(xyz,axis=(1,2)));high=np.maximum(high,np.nanmax(xyz,axis=(1,2)))
    return dict(bounding_radius_m=round(float(largest),1),dimensions_m=[round(float(v),1) for v in high-low])

def prepare(name,download=True,selected=None,publish=True):
    from osgeo import gdal
    import math
    gdal.UseExceptions();gdal.SetCacheMax(384*1024*1024)
    plan=json.loads((ROOT/'assets/planetary-sources.json').read_text());body=next(b for b in plan['bodies'] if b['id']==name)
    root=ROOT/'asset-packs'/name;root.mkdir(parents=True,exist_ok=True);sources=[]
    fetch=module('fetch','fetch-ranges.py');tiles=module('tiles','prepare-assets.py');pack=module('pack','pack-assets.py')
    for source in body['sources']:
        if selected and source['layer'] not in selected:continue
        path=root/'sources'/source['filename']
        if download:record=fetch.fetch(source['url'],path,workers=8,expected_sha256=source.get('sha256'))
        else:
            with path.open('rb') as stream:checksum=hashlib.file_digest(stream,'sha256').hexdigest()
            if source.get('sha256') and checksum!=source['sha256']:raise RuntimeError('Source SHA-256 mismatch')
            record=dict(bytes=path.stat().st_size,sha256=checksum)
        sources.append(dict(source,**record))
    work=root/'work';work.mkdir(exist_ok=True);public=root/'public';public.mkdir(exist_ok=True)
    layers=[];files=[];radius=body['datum_radius_m'];extent=math.pi*radius
    for record in sources:
        layer=record['layer'];elevation=record.get('kind')=='elevation'
        if record.get('format')=='pds3-cylindrical':
            source,actual,encoding=pds_layer(root,record,radius)
            info=dict(body=name,source_ids=[record['id']],resolution_m_equator=2*extent/actual[0],**encoding)
            if elevation:
                measured=relief(root/'work'/(layer+'.tif'),radius,record['height_scale_m'],record['height_offset_m'])
                print('Measured relief:',json.dumps(measured),flush=True)
                if body.get('relief') and any(abs(a-b)>1 for a,b in zip([measured['bounding_radius_m']]+measured['dimensions_m'],[body['relief']['bounding_radius_m']]+body['relief']['dimensions_m'])):raise RuntimeError('Inventory relief differs from the height map')
            result=tiles.tile_pyramid(root,source,layer,*actual,record['tile_size'],4,'I;16' if elevation else 'L',info)
            layers.append(result['layer']);files.extend(result['files']);continue
        source=gdal.Open(str(root/'sources'/record['filename']))
        expected=tuple(record['grid']);actual=(source.RasterXSize,source.RasterYSize)
        if actual!=expected or source.RasterCount!=record.get('channels',1):raise RuntimeError('Unexpected source grid/bands: '+layer)
        info=dict(body=name,source_ids=[record['id']],resolution_m_equator=2*extent/actual[0])
        if elevation:
            # ISIS SignedWord values times band scale are height above the
            # product's datum, not brightness-derived terrain. Shift unsigned
            # samples losslessly and explicitly carry the physical encoding.
            band=source.GetRasterBand(1);scale=band.GetScale() or 1.;offset=band.GetOffset() or 0.
            info.update(kind='elevation',sample_type='uint16',height_scale_m=scale,height_offset_m=offset-32768*scale,nodata_sample=0)
            halves=[];width,height=actual
            for label,x,bounds in [('west',width//2,[-extent,extent/2,0,-extent/2]),('east',0,[0,extent/2,extent,-extent/2])]:
                path=work/f'{layer}-{label}.vrt'
                dataset=gdal.Translate(str(path),source,format='VRT',srcWin=[x,0,width//2,height],outputBounds=bounds,
                    outputSRS=f'+proj=eqc +lon_0=0 +lat_ts=0 +R={radius} +units=m +no_defs',
                    outputType=gdal.GDT_UInt16,scaleParams=[[-32768,32767,0,65535]],noData=0)
                dataset=None;halves.append(str(path))
            source=gdal.BuildVRT(str(work/f'{layer}.vrt'),halves,srcNodata=0,VRTNodata=0)
        else:info.update(kind='reflectance',lighting=record['lighting'],nodata=0)
        result=tiles.tile_pyramid(root,source,layer,*actual,record['tile_size'],4,'I;16' if elevation else 'RGB' if record.get('channels',1)==3 else 'L',info)
        layers.append(result['layer']);files.extend(result['files'])
    expected=[source['layer'] for source in body['sources']]
    if any(not (work/(layer+'.json')).is_file() for layer in expected):
        print('Selected layers ready; remaining sources/layers are required before packaging.',flush=True);return
    layers=[];files=[]
    for layer in expected:
        record=json.loads((work/(layer+'.json')).read_text());layers.append(record['layer']);files.extend(record['files'])
    sources=[dict(source,**json.loads((root/'sources'/source['filename']).with_suffix(pathlib.Path(source['filename']).suffix+'.download.json').read_text())) for source in body['sources']]
    provenance=public/f'v1/{name}/SOURCES.json';provenance.parent.mkdir(parents=True,exist_ok=True)
    provenance.write_text(json.dumps(dict(body=body,sources=sources),indent=2,ensure_ascii=False)+'\n')
    files.append(dict(path=provenance.relative_to(public).as_posix(),bytes=provenance.stat().st_size,sha256=hashlib.sha256(provenance.read_bytes()).hexdigest()))
    (public/'manifest.json').write_text(json.dumps(dict(schema='albert-tiled-assets-v1',version='v1',static=True,layers=layers,files=files),indent=2)+'\n')
    if publish:pack.create_pack(root,name)
    else:print("Prepared body imagery; publication deferred.",flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('body');parser.add_argument('--process-only',action='store_true');parser.add_argument('--layer',action='append');parser.add_argument('--prepare-only',action='store_true');args=parser.parse_args()
    prepare(args.body,not args.process_only,args.layer,not args.prepare_only)

#!/usr/bin/env python3
"""Reproduce a solid body's optimized, versioned local imagery archive."""
import argparse,hashlib,importlib.util,json,pathlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/file);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result

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
        source=gdal.Open(str(root/'sources'/record['filename']));layer=record['layer'];elevation=record.get('kind')=='elevation'
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

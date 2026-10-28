#!/usr/bin/env python3
"""Bake the saved OSM natural polygons into the terrain decal's built-in mask.
Requires Pillow and numpy, only at generation time. No network or runtime files.
Usage: python3 scripts/starbase_cover.py [source.json] [output.hpp]
Pixels run west to east, south to north, at 4 m; no imagery-dependent offsets.
"""
import hashlib,io,json,pathlib,sys
import numpy as np
from PIL import Image,ImageDraw
from osm_enu import enu

root=pathlib.Path(__file__).resolve().parents[1]
source=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else root/'assets/geography/boca-chica-natural.json'
output=pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else root/'src/render/starbase_cover.hpp'
elements=json.loads(source.read_text())['elements']
latitude,longitude=25.996128,-97.154517
west,south,cell,size=-8608,-6272,4,2816
def points(geometry):return [enu(p,latitude,longitude) for p in geometry]
def pixel(p):return ((p[0]-west)/cell-.5,(p[1]-south)/cell-.5)
def rings(members,role):
    pending=[points(m['geometry']) for m in members if m.get('role','outer')==role and m.get('geometry')]
    result=[]
    while pending:
        ring=pending.pop()
        while ring[0]!=ring[-1]:
            for i,p in enumerate(pending):
                if ring[-1]==p[0]:ring+=p[1:];break
                if ring[-1]==p[-1]:ring+=p[-2::-1];break
                if ring[0]==p[-1]:ring=p[:-1]+ring;break
                if ring[0]==p[0]:ring=p[:0:-1]+ring;break
            else:raise ValueError('Unclosed multipolygon ring')
            pending.pop(i)
        result.append(ring)
    return result
def kind(t):
    return {'water':1,'scrub':5,'sand':4,'beach':4}.get(t.get('natural'),{'saltmarsh':2,'tidalflat':3}.get(t.get('wetland'),0))
features=[];used=set();counts={}
for e in elements:
    k=kind(e.get('tags',{}))
    if e['type']=='relation' and k:
        features.append((k,rings(e['members'],'outer'),rings(e['members'],'inner')))
        used.update(m['ref'] for m in e['members'] if m.get('role')=='outer')
for e in elements:
    k=kind(e.get('tags',{}));g=e.get('geometry',[])
    if e['type']=='way' and e['id'] not in used and k and g and g[0]==g[-1]:features.append((k,[points(g)],[]))
cover=Image.new('L',(size,size))
# General flats below vegetation and beach, explicit inland water last. Holes reveal lower layers.
for priority in (3,2,5,4,1):
    for k,outer,inner in features:
        if k!=priority:continue
        counts[k]=counts.get(k,0)+1;mask=Image.new('L',cover.size);draw=ImageDraw.Draw(mask)
        for ring in outer:draw.polygon([pixel(p) for p in ring],fill=255)
        for ring in inner:draw.polygon([pixel(p) for p in ring],fill=0)
        cover.paste(k,mask=mask)
# Streams retain OSM geometry; widths are drawn assumptions where OSM has none.
draw=ImageDraw.Draw(cover)
for e in elements:
    t=e.get('tags',{});w=t.get('waterway');g=e.get('geometry',[])
    if w not in ('stream','canal','ditch','river') or not g:continue
    width={'stream':6,'canal':10,'ditch':3,'river':20}[w]
    try:width=float(t.get('width',str(width)).split()[0])
    except (ValueError,IndexError):pass
    draw.line([pixel(p) for p in points(g)],fill=1,width=max(1,round(width/cell)))
labels=np.array(cover)
# This saved Gulf coastline is a continuous north/south chain. Intersect each latitude,
# rather than closing each open OSM way with an invented diagonal across the land.
coast=[points(e['geometry']) for e in elements if e.get('tags',{}).get('natural')=='coastline']
xs=west+(np.arange(size)+.5)*cell;ys=south+(np.arange(size)+.5)*cell
shore=np.full(size,np.nan)
for line in coast:
    for a,b in zip(line,line[1:]):
        if a[1]==b[1]:continue
        rows=(ys>=min(a[1],b[1]))&(ys<max(a[1],b[1]))
        shore[rows]=a[0]+(ys[rows]-a[1])*(b[0]-a[0])/(b[1]-a[1])
assert np.isfinite(shore).all(),'Coastline does not span mask'
sea=xs[None,:]>shore[:,None];labels[sea]=6
distance=np.full((size,size),254.,dtype=np.float32)
for line in coast:
    for a,b in zip(line,line[1:]):
        x0=max(0,int((min(a[0],b[0])-256-west)/cell));x1=min(size,int((max(a[0],b[0])+256-west)/cell)+1)
        y0=max(0,int((min(a[1],b[1])-256-south)/cell));y1=min(size,int((max(a[1],b[1])+256-south)/cell)+1)
        if x0>=x1 or y0>=y1:continue
        dx,dy=b[0]-a[0],b[1]-a[1];length=dx*dx+dy*dy
        if length<.01:continue
        x=xs[None,x0:x1]-a[0];y=ys[y0:y1,None]-a[1];t=np.clip((x*dx+y*dy)/length,0,1)
        distance[y0:y1,x0:x1]=np.minimum(distance[y0:y1,x0:x1],np.hypot(x-t*dx,y-t*dy))
signed=np.where(sea,distance,-distance)
# Two linear bytes reconstruct a signed distance to 1/128 m. Keeping the low byte
# avoids metre-sized surf steps when a 4 m mask texel fills dozens of screen pixels.
fixed=np.round(signed*128+32768).clip(0,65535).astype(np.uint16)
encoded=np.stack((labels,(fixed>>8).astype(np.uint8),(fixed&255).astype(np.uint8)),axis=2)
buf=io.BytesIO();Image.fromarray(encoded).save(buf,format='PNG',optimize=True)
png=buf.getvalue();sha=hashlib.sha256(source.read_bytes()).hexdigest()
with output.open('w') as f:
    f.write('// Generated by scripts/starbase_cover.py; ODbL, © OpenStreetMap contributors.\n')
    f.write('// Source SHA256: '+sha+'\n#pragma once\nnamespace albert::boca {\n')
    f.write(f'inline constexpr double latitude={latitude},longitude={longitude};\ninline constexpr int west={west},south={south},cell={cell},size={size};\n')
    f.write('inline constexpr unsigned char png[]{\n')
    for i in range(0,len(png),24):f.write('    '+','.join(str(b) for b in png[i:i+24])+',\n')
    f.write('};\n}\n')
print(f'{counts}, {size}² at {cell} m, {len(png)} PNG bytes, source {sha}')
# Human-readable georeferenced diagnostic beside the generated source when requested.
if len(sys.argv)>3:
    palette=np.array([[120,121,85],[72,128,153],[104,112,61],[172,165,139],[210,199,164],[75,84,49],[39,113,143]],dtype=np.uint8)
    Image.fromarray(palette[labels][::-1]).save(sys.argv[3])

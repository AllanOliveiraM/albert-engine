#!/usr/bin/env python3
"""Regenerate independent ray intersections from the pinned raw shape models.

Authoring only: NumPy + SpicePy 8.3.0, available in Dockerfile.assets.
DSKs use CSPICE DSKX02; Gaskell vertex-facet and Thomas plate tables use a
separate float64 parser and Moller-Trumbore; Stooke radius grids are triangulated
here independently (quads split along the rising diagonal, fans at the poles). No network, renderer decoder or shape.bin is used.
"""
import hashlib,json,pathlib
import numpy as np

ROOT=pathlib.Path(__file__).resolve().parents[1]

def dsk_intersections(path,source,rays):
 import spiceypy as spice
 handle=spice.dasopr(str(path))
 try:
  segment=spice.dlabfs(handle);description=spice.dskgd(handle,segment)
  if description.dtype!=2 or description.center!=source['body'] or description.frmcde!=source['frame']:raise ValueError('DSK frame/body/type mismatch')
  for origin,ray in rays:
   result=spice.dskx02(handle,segment,origin,ray)
   if len(result)>2 and not result[2]:raise ValueError('Reference ray missed the source model')
   yield np.asarray(result[1])
 finally:spice.dascls(handle)

def stooke_grid(path):
 rows=[[float(v) for v in line.split()] for line in path.read_text().split('\n') if line.strip()]
 r={(lon,lat):radius*1000 for lon,lat,radius in rows};lons=sorted({k[0] for k in r})[:-1];lats=sorted({k[1] for k in r})[1:-1]
 xyz=lambda lon,lat:[r[(lon,lat)]*np.cos(np.radians(lat))*np.cos(np.radians(-lon)),r[(lon,lat)]*np.cos(np.radians(lat))*np.sin(np.radians(-lon)),r[(lon,lat)]*np.sin(np.radians(lat))]
 south,north=np.array(xyz(0.,-90.)),np.array(xyz(0.,90.));plates=[]
 for i,lon in enumerate(lons):
  nxt=lons[(i+1)%len(lons)];plates.append([south,xyz(nxt,lats[0]),xyz(lon,lats[0])]);plates.append([north,xyz(lon,lats[-1]),xyz(nxt,lats[-1])])
  for low,high in zip(lats[:-1],lats[1:]):plates+=[[xyz(lon,low),xyz(nxt,low),xyz(nxt,high)],[xyz(lon,low),xyz(nxt,high),xyz(lon,high)]]
 return np.array(plates,dtype=float)/1000

def table_intersections(path,rays,form):
 lines=path.read_text().split('\n')
 if form=='stooke-radius-grid':corners=stooke_grid(path)
 elif form=='thomas-plate':
  # "nv np", then "x y z" and 0-based "v1 v2 v3" rows.
  count,faces=map(int,lines[0].split());vertices=np.array([[float(v) for v in line.split()] for line in lines[1:1+count]])
  plates=np.array([[int(v) for v in line.split()] for line in lines[1+count:1+count+faces]])
 else:
  count=int(lines[0]);vertices=np.array([[float(v) for v in line.split()[1:4]] for line in lines[1:1+count]])
  plates=np.array([[int(v)-1 for v in line.split()[1:4]] for line in lines[2+count:2+count+int(lines[1+count])]])
 if form!='stooke-radius-grid':corners=vertices[plates]
 a=corners[:,0];e1=corners[:,1]-a;e2=corners[:,2]-a
 for origin,ray in rays:
  p=np.cross(ray,e2);det=(e1*p).sum(1);valid=np.abs(det)>1e-12;inv=np.where(valid,1/np.where(valid,det,1),0)
  s=origin-a;u=(s*p).sum(1)*inv;q=np.cross(s,e1);v=(q@ray)*inv;t=(e2*q).sum(1)*inv
  hit=valid&(u>=0)&(v>=0)&(u+v<=1)&(t>0)
  if not hit.any():raise ValueError('Reference ray missed the source model')
  yield origin+ray*t[hit].min()

rows=['// Independent ray intersections in the original body frame, metres: CSPICE DSKX02 for DSKs, float64 Moller-Trumbore for Gaskell/Thomas tables and Stooke grids.']
for record in json.loads((ROOT/'assets/moon-sources.json').read_text())['bodies']:
 if 'shape_source' not in record:continue
 source=record['shape_source'];path=ROOT/'asset-packs/moons/sources'/source['filename']
 if path.stat().st_size!=source['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=source['sha256']:raise ValueError('Original shape hash/size mismatch')
 rng=np.random.default_rng(source['body'])
 distance={'phobos':40,'mimas':400,'phoebe':250,'hyperion':400,'proteus':500}.get(record['id'],25);rays=[]
 for _ in range(24):
  direction=rng.normal(size=3);direction/=np.linalg.norm(direction);rays.append((direction*distance,-direction))
 form=source.get('format','dsk');hits=dsk_intersections(path,source,rays) if form=='dsk' else table_intersections(path,rays,form)
 for (origin,ray),hit in zip(rays,hits):
  vectors=[origin*1000,ray,hit*1000]
  rows.append('{"'+record['id']+'",'+','.join('{'+','.join(format(float(v),'.17g') for v in vector)+'}' for vector in vectors)+'},')
(ROOT/'tests/measured_shape_fixture.inc').write_text('\n'.join(rows)+'\n')

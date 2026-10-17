#!/usr/bin/env python3
"""Regenerate independent ray intersections from the pinned raw shape models.

Authoring only: NumPy + SpicePy 8.3.0, available in Dockerfile.assets.
DSKs use CSPICE DSKX02; Gaskell vertex-facet tables use a separate float64
parser and Moller-Trumbore. No network, renderer decoder or shape.bin is used.
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

def gaskell_intersections(path,rays):
 lines=path.read_text().split('\n');count=int(lines[0])
 vertices=np.array([[float(v) for v in line.split()[1:4]] for line in lines[1:1+count]])
 plates=np.array([[int(v)-1 for v in line.split()[1:4]] for line in lines[2+count:2+count+int(lines[1+count])]])
 a=vertices[plates[:,0]];e1=vertices[plates[:,1]]-a;e2=vertices[plates[:,2]]-a
 for origin,ray in rays:
  p=np.cross(ray,e2);det=(e1*p).sum(1);valid=np.abs(det)>1e-12;inv=np.where(valid,1/np.where(valid,det,1),0)
  s=origin-a;u=(s*p).sum(1)*inv;q=np.cross(s,e1);v=(q@ray)*inv;t=(e2*q).sum(1)*inv
  hit=valid&(u>=0)&(v>=0)&(u+v<=1)&(t>0)
  if not hit.any():raise ValueError('Reference ray missed the source model')
  yield origin+ray*t[hit].min()

rows=['// Independent ray intersections in the original body frame, metres: CSPICE DSKX02 for DSKs, float64 Moller-Trumbore for Gaskell tables.']
for record in json.loads((ROOT/'assets/moon-sources.json').read_text())['bodies']:
 if 'shape_source' not in record:continue
 source=record['shape_source'];path=ROOT/'asset-packs/moons/sources'/source['filename']
 if path.stat().st_size!=source['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=source['sha256']:raise ValueError('Original shape hash/size mismatch')
 rng=np.random.default_rng(source['body'])
 distance={'phobos':40,'mimas':400}.get(record['id'],25);rays=[]
 for _ in range(24):
  direction=rng.normal(size=3);direction/=np.linalg.norm(direction);rays.append((direction*distance,-direction))
 hits=gaskell_intersections(path,rays) if source.get('format')=='gaskell-vertex-facet' else dsk_intersections(path,source,rays)
 for (origin,ray),hit in zip(rays,hits):
  vectors=[origin*1000,ray,hit*1000]
  rows.append('{"'+record['id']+'",'+','.join('{'+','.join(format(float(v),'.17g') for v in vector)+'}' for vector in vectors)+'},')
(ROOT/'tests/measured_shape_fixture.inc').write_text('\n'.join(rows)+'\n')

#!/usr/bin/env python3
"""Regenerate independent CSPICE ray intersections from the pinned raw DSKs.

Authoring only: NumPy + SpicePy 8.3.0, available in Dockerfile.assets.
No network, renderer decoder or exported shape.bin is used.
"""
import hashlib,json,pathlib
import numpy as np
import spiceypy as spice

ROOT=pathlib.Path(__file__).resolve().parents[1]
rows=['// Independent CSPICE DSKX02 intersections in the original IAU body frame, metres.']
for record in json.loads((ROOT/'assets/moon-sources.json').read_text())['bodies']:
 if 'shape_source' not in record:continue
 source=record['shape_source'];path=ROOT/'asset-packs/moons/sources'/source['filename']
 if path.stat().st_size!=source['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=source['sha256']:raise ValueError('Original DSK hash/size mismatch')
 handle=spice.dasopr(str(path))
 try:
  segment=spice.dlabfs(handle);description=spice.dskgd(handle,segment)
  if description.dtype!=2 or description.center!=source['body'] or description.frmcde!=source['frame']:raise ValueError('DSK frame/body/type mismatch')
  rng=np.random.default_rng(source['body'])
  distance=40 if record['id']=='phobos' else 25
  for _ in range(24):
   direction=rng.normal(size=3);direction/=np.linalg.norm(direction)
   origin=direction*distance;ray=-direction;result=spice.dskx02(handle,segment,origin,ray)
   if len(result)>2 and not result[2]:raise ValueError('Reference ray missed the source model')
   vectors=[origin*1000,ray,np.asarray(result[1])*1000]
   rows.append('{"'+record['id']+'",'+','.join('{'+','.join(format(float(v),'.17g') for v in vector)+'}' for vector in vectors)+'},')
 finally:spice.dascls(handle)
(ROOT/'tests/measured_shape_fixture.inc').write_text('\n'.join(rows)+'\n')

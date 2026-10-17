"""Seal measured DSK type-2 plates without rescaling or moving the body origin."""
import json,pathlib,struct
import numpy as np
import spiceypy as spice

def export_shape(path,record,destination):
 handle=spice.dasopr(str(path))
 try:
  segment=spice.dlabfs(handle);description=spice.dskgd(handle,segment)
  if description.dtype!=2 or description.center!=record['body'] or description.frmcde!=record['frame']:raise ValueError('DSK body/frame/type mismatch')
  nv,nf=spice.dskz02(handle,segment)
  if (nv,nf)!=(record['vertices'],record['plates']) or nv>500000 or nf>1000000:raise ValueError('Unexpected DSK counts')
  vertices=spice.dskv02(handle,segment,1,nv)*1000;plates=spice.dskp02(handle,segment,1,nf)-1
  if not np.isfinite(vertices).all() or plates.min()<0 or plates.max()>=nv:raise ValueError('Invalid DSK geometry')
  bounds=np.stack((vertices.min(0),vertices.max(0)))
  if not np.allclose(bounds,record['bounds_m'],rtol=0,atol=.001) or abs(np.linalg.norm(vertices,axis=1).max()-record['bounding_radius_m'])>.001:raise ValueError('DSK bounds mismatch')
  # Indexed little-endian float32 metre positions and uint32 triangle indices.
  destination.parent.mkdir(parents=True,exist_ok=True)
  with destination.open('wb') as output:
   output.write(struct.pack('<8sII',b'ALBMSH1\0',nv,nf));output.write(vertices.astype('<f4').tobytes());output.write(plates.astype('<u4').tobytes())
 finally:spice.dascls(handle)

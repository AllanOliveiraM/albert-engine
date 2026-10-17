"""Seal measured shape plates without rescaling or moving the body origin."""
import json,pathlib,struct
import numpy as np

def write_shape(destination,vertices,plates):
 # Indexed little-endian float32 metre positions and uint32 triangle indices.
 destination.parent.mkdir(parents=True,exist_ok=True)
 with destination.open('wb') as output:
  output.write(struct.pack('<8sII',b'ALBMSH1\0',len(vertices),len(plates)));output.write(vertices.astype('<f4').tobytes());output.write(plates.astype('<u4').tobytes())

def check_bounds(vertices,record):
 bounds=np.stack((vertices.min(0),vertices.max(0)))
 if not np.allclose(bounds,record['bounds_m'],rtol=0,atol=.001) or abs(np.linalg.norm(vertices,axis=1).max()-record['bounding_radius_m'])>.001:raise ValueError('Shape bounds mismatch')

def export_dsk(path,record,destination):
 import spiceypy as spice
 handle=spice.dasopr(str(path))
 try:
  segment=spice.dlabfs(handle);description=spice.dskgd(handle,segment)
  if description.dtype!=2 or description.center!=record['body'] or description.frmcde!=record['frame']:raise ValueError('DSK body/frame/type mismatch')
  nv,nf=spice.dskz02(handle,segment)
  if (nv,nf)!=(record['vertices'],record['plates']) or nv>500000 or nf>1000000:raise ValueError('Unexpected DSK counts')
  vertices=spice.dskv02(handle,segment,1,nv)*1000;plates=spice.dskp02(handle,segment,1,nf)-1
  if not np.isfinite(vertices).all() or plates.min()<0 or plates.max()>=nv:raise ValueError('Invalid DSK geometry')
  check_bounds(vertices,record);write_shape(destination,vertices,plates)
 finally:spice.dascls(handle)

def gaskell_tables(path):
 # PDS vertex-facet table: count, "id x y z" in km, count, "id v1 v2 v3" (1-based).
 with open(path) as source:
  nv=int(source.readline());vertices=np.loadtxt(source,max_rows=nv)
  nf=int(source.readline());plates=np.loadtxt(source,max_rows=nf,dtype=np.int64)
  if source.read().strip():raise ValueError('Trailing data in Gaskell table')
 if vertices.shape!=(nv,4) or plates.shape!=(nf,4) or (vertices[:,0]!=np.arange(1,nv+1)).any() or (plates[:,0]!=np.arange(1,nf+1)).any():raise ValueError('Malformed Gaskell table')
 return vertices[:,1:]*1000,plates[:,1:]-1

def export_gaskell(path,record,destination):
 vertices,plates=gaskell_tables(path)
 if plates.min()<0 or plates.max()>=len(vertices) or not np.isfinite(vertices).all():raise ValueError('Invalid Gaskell geometry')
 # ICQ cube edges repeat some vertices that no plate references; drop only those.
 used,remap=np.unique(plates,return_inverse=True);vertices=vertices[used];plates=remap.reshape(plates.shape)
 if (len(vertices),len(plates))!=(record['vertices'],record['plates']) or len(vertices)>500000 or len(plates)>1000000:raise ValueError('Unexpected Gaskell counts')
 check_bounds(vertices,record);write_shape(destination,vertices,plates)

def export_shape(path,record,destination):
 {'dsk':export_dsk,'gaskell-vertex-facet':export_gaskell}[record.get('format','dsk')](path,record,destination)

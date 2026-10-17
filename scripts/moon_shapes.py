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

def thomas_tables(path):
 # PDS plate model (Thomas): "nv np", nv rows "x y z" in km, np rows "v1 v2 v3" (0-based).
 with open(path) as source:
  nv,nf=map(int,source.readline().split());vertices=np.loadtxt(source,max_rows=nv)
  plates=np.loadtxt(source,max_rows=nf,dtype=np.int64)
  if source.read().strip():raise ValueError('Trailing data in plate model')
 if vertices.shape!=(nv,3) or plates.shape!=(nf,3):raise ValueError('Malformed plate model')
 return vertices*1000,plates

def export_thomas(path,record,destination):
 vertices,plates=thomas_tables(path)
 if plates.min()<0 or plates.max()>=len(vertices) or not np.isfinite(vertices).all():raise ValueError('Invalid plate model geometry')
 if (len(vertices),len(plates))!=(record['vertices'],record['plates']) or len(vertices)>500000 or len(plates)>1000000:raise ValueError('Unexpected plate model counts')
 check_bounds(vertices,record);write_shape(destination,vertices,plates)

def stooke_tables(path):
 # PDS radius grid (Stooke): "west_lon lat radius_km" every 5 deg, poles and 0/360 repeated.
 # Triangulated through the published points only: rings of quads, fans at the poles.
 grid=np.loadtxt(path)
 if grid.ndim!=2 or grid.shape[1]!=3:raise ValueError('Malformed radius grid')
 lons,lats=np.unique(grid[:,0]),np.unique(grid[:,1]);step=lons[1]-lons[0]
 if len(grid)!=len(lons)*len(lats) or lons[0]!=0 or lons[-1]!=360 or lats[0]!=-90 or lats[-1]!=90 or not np.allclose(np.diff(lons),step) or not np.allclose(np.diff(lats),step):raise ValueError('Unexpected radius grid layout')
 radius={(lon,lat):r for lon,lat,r in grid}
 for lat in lats:
  if radius[(0.,lat)]!=radius[(360.,lat)]:raise ValueError('Radius grid seam mismatch')
 for lat in (-90.,90.):
  if len({radius[(lon,lat)] for lon in lons})!=1:raise ValueError('Radius grid pole mismatch')
 def point(lon,lat):
  r=radius[(lon,lat)]*1000;east,phi=np.radians(-lon),np.radians(lat)
  return [r*np.cos(phi)*np.cos(east),r*np.cos(phi)*np.sin(east),r*np.sin(phi)]
 ring=lons[:-1];inner=lats[1:-1];n=len(ring)
 vertices=[point(0.,-90.)]+[point(lon,lat) for lat in inner for lon in ring]+[point(0.,90.)]
 index=lambda j,i:1+j*n+i%n;top=len(vertices)-1;plates=[]
 for i in range(n):plates.append([0,index(0,i+1),index(0,i)])
 for j in range(len(inner)-1):
  for i in range(n):plates+=[[index(j,i),index(j,i+1),index(j+1,i+1)],[index(j,i),index(j+1,i+1),index(j+1,i)]]
 for i in range(n):plates.append([top,index(len(inner)-1,i),index(len(inner)-1,i+1)])
 vertices,plates=np.array(vertices),np.array(plates,dtype=np.int64)
 a,b,c=(vertices[plates[:,k]] for k in range(3))
 if np.einsum('ij,ij->i',a,np.cross(b,c)).sum()<0:plates=plates[:,::-1].copy()
 return vertices,plates

def export_stooke(path,record,destination):
 vertices,plates=stooke_tables(path)
 if (len(vertices),len(plates))!=(record['vertices'],record['plates']):raise ValueError('Unexpected radius grid counts')
 check_bounds(vertices,record);write_shape(destination,vertices,plates)

def export_shape(path,record,destination):
 {'dsk':export_dsk,'gaskell-vertex-facet':export_gaskell,'thomas-plate':export_thomas,'stooke-radius-grid':export_stooke}[record.get('format','dsk')](path,record,destination)

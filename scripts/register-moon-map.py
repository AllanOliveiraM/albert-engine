#!/usr/bin/env python3
"""Reproduce the registration of a published moon map against its measured shape.

Authoring only (NumPy + Pillow, in Dockerfile.assets), offline, under a minute:
  python3 scripts/register-moon-map.py mimas
  python3 scripts/register-moon-map.py phoebe --search   # wide exploratory scan
Synthetic shading of the measured plates is correlated with the packed (cropped,
gap-filled) map in up to 84 regions, skipping those that overlap gaps; no tested model
may beat the inventory projection by more than twice the paired standard error.
Large IAU gazetteer craters are also located on the shape by rim-crest fitting.
"""
import argparse,hashlib,importlib.util,json,pathlib,sys
import numpy as np
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from moon_shapes import gaskell_tables
# The packed map is compared, so cropping and gap filling are those of the pack.
spec=importlib.util.spec_from_file_location('moon_assets',ROOT/'scripts/prepare-moon-assets.py');moon_assets=importlib.util.module_from_spec(spec);spec.loader.exec_module(moon_assets)
# USGS Gazetteer of Planetary Nomenclature: craters >= 25 km on Mimas, >= 30 km on Phoebe (+west longitudes).
GAZETTEER={'mimas':[('Herschel',139,-1.38,111.76),('Arthur',64,-35.4,196.04),('Accolon',48,-70.56,184.41),('Morgan',43,24.21,244.98),('Gwynevere',42,-17.6,323.7),('Lucas',40,40.75,220.35),('Melyodas',40,-74.93,77.19),('Igraine',38,-41.99,231.21),('Ban',37,43.93,160.75),('Merlin',37,-38.43,219.01),('Pellinore',36,29.76,135.45),('Balin',35,14.71,82.51),('Dynas',35,2.35,80.71),('Bors',34,41.82,172.3),('Galahad',34,-45.32,145.31),('Uther',34,-35.16,250.17),('Launcelot',30,-9.46,328.49),('Dagonet',28,47.84,261.62),('Gawain',27,-58.54,261.08),('Modred',26,4.15,219.68),('Bedivere',25,9.57,149.42)],
 'phoebe':[('Jason',101,16.2,317.7),('Eurytus',89,-39.7,177.2),('Idmon',61,-67.1,197.8),('Admetus',58,11.4,39.1),('Oileus',56,-77.1,96.9),('Clytius',52,46.0,193.1),('Canthus',44,-69.6,342.2),('Peleus',44,20.2,192.2),('Erginus',38,31.6,337.1),('Mopsus',37,6.6,109.1),('Acastus',34,9.6,148.5),('Calais',31,-38.7,225.4),('Hylas',30,7.9,354.5)]}
N=3;W,H=360*N,180*N

def verified(record):
 path=ROOT/'asset-packs/moons/sources'/record['filename']
 if path.stat().st_size!=record['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:raise ValueError('Source hash/size mismatch: '+record['filename'])
 return path

def relief(vertices,plates):
 # Height above the best-fit centred triaxial ellipsoid on a 1/N degree planetocentric grid.
 axes=1/np.sqrt(np.linalg.lstsq(vertices**2,np.ones(len(vertices)),rcond=None)[0]);a,b,c=(vertices[plates[:,i]] for i in range(3))
 total=np.zeros((H,W));count=np.zeros((H,W))
 for u in np.linspace(0,1,5):
  for v in np.linspace(0,1-u,int(round((1-u)*4))+1):
   x=u*a+v*b+(1-u-v)*c;r=np.linalg.norm(x,axis=1);n=x/r[:,None]
   rows=np.clip(((90-np.degrees(np.arcsin(n[:,2])))*N).astype(int),0,H-1);cols=(np.degrees(np.arctan2(n[:,1],n[:,0]))%360*N).astype(int)%W
   np.add.at(total,(rows,cols),r-1/np.sqrt(((n/axes)**2).sum(1)));np.add.at(count,(rows,cols),1)
 h=total/np.maximum(count,1)
 for _ in range(20):
  empty=count==0
  if not empty.any():break
  near=sum(np.roll(h,s,a) for a in (0,1) for s in (-1,1));known=sum(np.roll(~empty,s,a) for a in (0,1) for s in (-1,1))
  h=np.where(empty&(known>0),near/np.maximum(known,1),h);count=np.where(empty&(known>0),1,count)
 return h,axes

def highpass(x,k=4):
 y=x.copy()
 for axis in (0,1):y=sum(np.roll(y,s,axis) for s in range(-k,k+1))/(2*k+1)
 return x-y

def correlation_model(h,image,radius,gaps):
 latc=90-(np.arange(H)+.5)/N;lat=np.radians(latc)[:,None];dx=radius*np.cos(lat)*np.radians(1/N);dy=radius*np.radians(1/N)
 sx=(np.roll(h,-1,1)-np.roll(h,1,1))/(2*dx);sy=np.zeros_like(h);sy[1:-1]=(h[:-2]-h[2:])/(2*dy);shades=[]
 for az in range(0,360,30):
  for el in (15,35):
   light=np.array([np.sin(np.radians(az))*np.cos(np.radians(el)),np.cos(np.radians(az))*np.cos(np.radians(el)),np.sin(np.radians(el))])
   shades.append(highpass((-sx*light[0]-sy*light[1]+light[2])/np.sqrt(1+sx*sx+sy*sy)))
 regions=[]
 for lat0 in (-55,-35,-15,0,15,35,55):
  half=15 if abs(lat0)<50 else 12
  for lon0 in range(0,360,30):
   rows=slice(int((90-lat0-half)*N),int((90-lat0+half)*N));cols=np.arange(int((lon0-half/np.cos(np.radians(lat0)))*N),int((lon0+half/np.cos(np.radians(lat0)))*N))%W
   regions.append((rows,cols,[(s[rows][:,cols]-s[rows][:,cols].mean())/s[rows][:,cols].std() for s in shades]))
 source=np.asarray(image.convert('L').resize((360*8,180*8),Image.BOX),dtype=float)
 # Filled gaps carry no relief; regions that overlap them are left out.
 gaps=np.asarray(Image.fromarray((gaps*255).astype(np.uint8)).resize((360*8,180*8),Image.BOX),dtype=float)
 keep=None
 def score(k,offset,edge):
  nonlocal keep
  # Bilinear sampling is enough at this 1/3 degree analysis scale.
  rows=(90-np.degrees(np.arctan(k*np.tan(np.radians(np.clip(latc,-89.9,89.9))))))/180*source.shape[0]-.5
  r0=np.clip(np.floor(rows).astype(int),0,source.shape[0]-2);fr=np.clip(rows-r0,0,1)[:,None];band=source[r0]*(1-fr)+source[r0+1]*fr
  cols=(((np.arange(W)+.5)/N+offset-edge)%360)/360*source.shape[1]-.5;c0=np.floor(cols).astype(int);fc=cols-c0
  sampled=highpass(band[:,c0%source.shape[1]]*(1-fc)+band[:,(c0+1)%source.shape[1]]*fc);values=[]
  if keep is None:
   # Regions are chosen once, from the first (published) mapping, so every model
   # is compared over the same regions; under 4% gap pixels qualifies.
   hole=gaps[r0][:,c0%source.shape[1]]
   keep=[hole[region][:,columns].mean()<10 for region,columns,_ in regions]
  for (rows,cols,options),used in zip(regions,keep):
   if not used:continue
   part=sampled[rows][:,cols];part=(part-part.mean())/part.std();values.append(max(float((s*part).mean()) for s in options))
  return np.array(values)
 return score

def crater_offsets(vertices,craters):
 # Kilometres, as the gazetteer diameters.
 r=np.linalg.norm(vertices,axis=1);n=vertices/r[:,None];axes=1/np.sqrt(np.linalg.lstsq(vertices**2,np.ones(len(vertices)),rcond=None)[0]);h=r-1/np.sqrt(((n/axes)**2).sum(1));R=np.cbrt(axes.prod())
 def fit(lat,lon,diameter):
  la,lo=np.radians(lat),np.radians(lon);centre=np.array([np.cos(la)*np.cos(lo),np.cos(la)*np.sin(lo),np.sin(la)]);east=np.array([-np.sin(lo),np.cos(lo),0.]);north=np.cross(centre,east)
  dot=n@centre;near=dot>np.cos(np.radians(min(80,diameter/R*57.3*1.6)));x=(n[near]@east)/dot[near]*R;y=(n[near]@north)/dot[near]*R;z=h[near];rim=[]
  for az in np.radians(np.arange(0,360,4)):
   s=x*np.cos(az)+y*np.sin(az);t=-x*np.sin(az)+y*np.cos(az);ray=(np.abs(t)<max(1.5,diameter/40))&(s>.25*diameter)&(s<.8*diameter)
   if ray.sum()>=5:i=np.argmax(z[ray]);rim.append((x[ray][i],y[ray][i]))
  rim=np.array(rim);cx,cy,k=np.linalg.lstsq(np.c_[2*rim,np.ones(len(rim))],(rim**2).sum(1),rcond=None)[0];p=centre+cx/R*east+cy/R*north;p/=np.linalg.norm(p)
  return np.degrees(np.arcsin(p[2])),np.degrees(np.arctan2(p[1],p[0]))%360
 out=[]
 for name,diameter,lat,west in craters:
  la,lo=lat,(360-west)%360
  for _ in range(5):la,lo=fit(la,lo,diameter)
  out.append((name,la-lat,((lo-(360-west))+180)%360-180))
 return out

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('body');parser.add_argument('--search',action='store_true');args=parser.parse_args()
 record=next(b for b in json.loads((ROOT/'assets/moon-sources.json').read_text())['bodies'] if b['id']==args.body)
 shape=record['shape_source']
 if shape.get('format')!='gaskell-vertex-facet':raise ValueError('Registration analysis expects a Gaskell table')
 vertices,plates=gaskell_tables(verified(shape));used,plates=np.unique(plates,return_inverse=True);vertices=vertices[used]/1000;plates=plates.reshape(-1,3)
 h,axes=relief(vertices,plates);projection=record.get('projection',{})
 k,offset,edge=projection.get('tan_latitude_scale',1.),projection.get('longitude_offset_deg',0.),projection.get('west_edge_lon',-180.)
 with Image.open(verified(record)) as source:
  image=moon_assets.published(source,projection)
  cropped=source.crop(tuple(projection['crop'])) if 'crop' in projection else source
  gaps=moon_assets.no_data(cropped,projection['no_data']) if 'no_data' in projection else np.zeros((image.height,image.width),bool)
 score=correlation_model(h,image,float(np.cbrt(axes.prod())),gaps)
 published,longitude,model=score(1,0,edge),score(1,offset,edge),score(k,offset,edge)
 print('mean correlation over %d regions: published %.3f, longitude only %.3f, inventory model %.3f; improved regions %d/%d'%(len(model),published.mean(),longitude.mean(),model.mean(),(model>published).sum(),len(model)))
 def scan(models):
  # Paired over the same regions: a better model must beat the inventory by twice
  # the standard error of the per-region differences (soft maps have flat optima).
  return sorted(((values.mean(),kk,do,bool((values-model).mean()>2*(values-model).std()/np.sqrt(len(model)))) for kk,do in models for values in [score(kk,do,edge)]),reverse=True)
 wide=scan([(kk,do) for kk in (1.,1.05,1.1,1.15,1.2) for do in np.arange(-15,15.01,1.)]) if args.search else []
 if wide:print('widest scan, best models (mean, k, longitude offset; * significantly better than the inventory): '+'; '.join('%.3f %.2f %+.0f%s'%(m[0],m[1],m[2],'*'*m[3]) for m in wide[:5]))
 local=scan([(k+dk,offset+do) for dk in (-.02,-.01,0,.01,.02) for do in (-.2,-.1,0,.1,.2)]);best=local[0]
 print('best local model: k %+.2f, longitude %+.1f deg from inventory (mean %.3f, %s)'%(best[1]-k,best[2]-offset,best[0],'significantly better' if best[3] else 'within noise'))
 craters=crater_offsets(vertices,GAZETTEER.get(args.body,[]))
 if craters:
  la,lo=np.array([c[1] for c in craters]),np.array([c[2] for c in craters]);mad=lambda x:np.median(np.abs(x-np.median(x)))
  print('shape vs IAU gazetteer, %d craters: median latitude %+.2f deg (MAD %.2f), longitude %+.2f deg (MAD %.2f)'%(len(craters),np.median(la),mad(la),np.median(lo),mad(lo)))
 corrected=(k,offset)!=(1.,0.)
 if any(m[3] for m in local+wide) or (corrected and model.mean()<2*published.mean()):raise SystemExit('Inventory projection is not the best registration model')

if __name__=='__main__':main()

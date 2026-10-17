#!/usr/bin/env python3
"""Exercise GLB texture selection, grid validation and map registration without network or real assets."""
import importlib.util,io,json,pathlib,struct,sys
from PIL import Image
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('moon_assets',ROOT/'scripts/prepare-moon-assets.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def glb(grid=(64,32),transform=False):
 binary=io.BytesIO();Image.new('RGB',grid,(180,70,20)).save(binary,format='PNG');image=binary.getvalue();data=image+b'\0'*(-len(image)%4)
 texture=dict(index=0)
 if transform:texture['extensions']={'KHR_texture_transform':{'offset':[.5,0]}}
 doc=dict(meshes=[dict(primitives=[dict(material=0)])],materials=[dict(pbrMetallicRoughness=dict(baseColorTexture=texture))],textures=[dict(source=1)],images=[dict(uri='ignored.png'),dict(bufferView=0)],bufferViews=[dict(byteLength=len(image))])
 encoded=json.dumps(doc).encode();encoded+=b' '*(-len(encoded)%4)
 return struct.pack('<4sII',b'glTF',2,28+len(encoded)+len(data))+struct.pack('<I4s',len(encoded),b'JSON')+encoded+struct.pack('<I4s',len(data),b'BIN\0')+data
image=module.visual(glb(),[64,32]);assert image.mode=='RGB' and image.getpixel((20,10))==(180,70,20)
for raw,grid in [(glb()[:-1],[64,32]),(glb(),[128,64]),(glb((64,64)),[64,64]),(glb(transform=True),[64,32])]:
 try:module.visual(raw,grid)
 except ValueError:continue
 raise AssertionError('Invalid source accepted')
import numpy as np
rng=np.random.default_rng(601);source=Image.fromarray(rng.integers(0,256,(32,64,3),dtype=np.uint8))
assert np.array_equal(np.asarray(module.registered(source,{})),np.asarray(source)),'Identity registration changed pixels'
shifted=np.asarray(module.registered(source,dict(west_edge_lon=0)))
assert np.array_equal(shifted,np.roll(np.asarray(source),32,axis=1)),'0-360 source was not recentred on 0 deg'
# Linear ramps are reproduced by the cubic kernel, so sampled positions can be checked exactly.
height,width,k=90,180,(207.8/190.6)**2;ramp=Image.fromarray(np.repeat((np.arange(height)*2).astype(np.uint8)[:,None],width,1)).convert('RGB')
out=np.asarray(module.registered(ramp,dict(tan_latitude_scale=k)))[:,0,0].astype(float)
latitude=np.radians(90-(np.arange(height)+.5)*180/height);expected=((90-np.degrees(np.arctan(k*np.tan(latitude))))*height/180-.5)*2
assert np.abs(out-expected)[3:-3].max()<=.5,'Latitude registration sampled the wrong source rows'
cols=np.asarray(module.registered(Image.fromarray(np.repeat((np.arange(width)).astype(np.uint8)[None],height,0)).convert('RGB'),dict(longitude_offset_deg=-4)))[0,:,0].astype(float)
assert np.abs(cols-(np.arange(width)-2))[3:-3].max()<=.5,'Longitude offset moved the map the wrong way'
# Framed maps: crop to 2:1; fill the black cap, near-black noise touching it and the south band, keep isolated shadows.
framed=np.zeros((40,70,3),np.uint8);framed[4:36,3:67]=120;inside=framed[4:36,3:67];inside[:8]=0;inside[8,10]=7;inside[20,30]=5
crop=dict(crop=[3,4,67,36]);spec=dict(crop,no_data=dict(value=0,dark_max=12,south_of_lat=-80,fringe_px=1))
assert np.array_equal(np.asarray(module.published(Image.fromarray(framed),crop)),inside),'Cropping changed observed pixels'
try:module.published(Image.fromarray(framed),dict(crop=[3,4,66,36]));raise AssertionError('Non 2:1 crop accepted')
except ValueError:pass
mask=module.no_data(Image.fromarray(inside),spec['no_data'])
assert mask[:9].all() and mask[9,10] and not mask[9,11] and not mask[20,30] and mask[29:].all() and not mask[10:29].any(),'Unobserved pixels misclassified'
filled=np.asarray(module.published(Image.fromarray(framed),spec))[...,0]
assert filled[20,30]==5 and (filled[~mask][filled[~mask]!=5]==120).all() and filled[mask].min()>=100 and filled[mask].max()<=120,'Gap fill invented detail or changed observations'
# Uniform maps store linear albedo as sRGB, decoded again by the shader.
assert np.abs(module.srgb(np.array([0.,.0031308,.21404114,1.]))-[0,.04044994,.5,1]).max()<1e-6,'sRGB encoding changed'
print('Moon assets: material texture selection, source grid, GLB chunks, unsupported UV transforms, map registration, cropping, gap filling and uniform colour OK')

#!/usr/bin/env python3
"""Exercise GLB material selection/grid validation without network or real assets."""
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
print('Moon assets: material texture selection, source grid, GLB chunks and unsupported UV transforms OK')

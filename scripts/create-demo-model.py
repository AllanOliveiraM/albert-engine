#!/usr/bin/env python3
"""Create a small, self-contained static GLB vehicle for the Albert RPC example.
Geometry and dimensions are illustrative; this is not a flight/mission model.
"""
import argparse,json,math,pathlib,struct

def create(destination):
    data=bytearray();views=[];accessors=[];meshes=[];nodes=[]
    materials=[dict(pbrMetallicRoughness=dict(baseColorFactor=[.65,.68,.72,1],metallicFactor=.55,roughnessFactor=.3)),dict(pbrMetallicRoughness=dict(baseColorFactor=[.025,.035,.05,1],metallicFactor=.1,roughnessFactor=.55))]
    def section(radius,height,y,cone,material):
        points=[]
        for i in range(48):
            a=i*math.tau/48;b=(i+1)*math.tau/48
            lower=(radius*math.cos(a),0,radius*math.sin(a));next_lower=(radius*math.cos(b),0,radius*math.sin(b))
            upper=(0,height,0) if cone else (radius*math.cos(a),height,radius*math.sin(a));next_upper=(0,height,0) if cone else (radius*math.cos(b),height,radius*math.sin(b))
            points.extend([lower,upper,next_lower])
            if not cone:points.extend([next_lower,upper,next_upper])
            points.extend([(0,0,0),lower,next_lower])
            if not cone:points.extend([(0,height,0),next_upper,upper])
        start=len(data)
        for point in points:data.extend(struct.pack('<3f',*point))
        view=len(views);views.append(dict(buffer=0,byteOffset=start,byteLength=len(data)-start))
        accessor=len(accessors);accessors.append(dict(bufferView=view,componentType=5126,count=len(points),type='VEC3',min=[min(p[i] for p in points) for i in range(3)],max=[max(p[i] for p in points) for i in range(3)]))
        mesh=len(meshes);meshes.append(dict(primitives=[dict(attributes=dict(POSITION=accessor),material=material)]));nodes.append(dict(mesh=mesh,translation=[0,y,0]))
    section(4.5,85,0,False,0);section(4.5,5,85,False,1);section(4.5,25,90,True,0)
    root=dict(asset=dict(version='2.0',generator='Albert illustrative model generator'),buffers=[dict(byteLength=len(data))],bufferViews=views,accessors=accessors,materials=materials,meshes=meshes,nodes=[dict(children=[1,2,3]),*nodes],scenes=[dict(nodes=[0])],scene=0)
    text=json.dumps(root,separators=(',',':')).encode()
    text+=b' '*(-len(text)%4);data+=b'\0'*(-len(data)%4)
    glb=struct.pack('<5I',0x46546c67,2,28+len(text)+len(data),len(text),0x4e4f534a)+text+struct.pack('<2I',len(data),0x004e4942)+data
    destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(glb);print(f'{destination}: {len(glb)} bytes; illustrative geometry only')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=pathlib.Path,nargs='?',default=pathlib.Path('build/demo-vehicle.glb'));create(parser.parse_args().output)

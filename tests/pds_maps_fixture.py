#!/usr/bin/env python3
"""Exercise the PDS3 simple-cylindrical resampler on synthetic labelled maps (NumPy only).

Two layouts like the Dawn Vesta products: a grid-registered 16-bit map centred at
330 deg with a missing block, and an 8-bit map at a non-integer resolution. Area
averages must reproduce a known smooth function, across the longitude seam.
"""
import pathlib,sys,tempfile
import numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import pds_maps

def write(path,res,lines,samples,lpo,spo,dtype,values,missing=None):
    record=samples*np.dtype(dtype).itemsize;kind='MSB_INTEGER' if dtype=='>i2' else 'UNSIGNED_INTEGER'
    head=(f"PDS_VERSION_ID = PDS3\r\nRECORD_BYTES = {record}\r\n^IMAGE = 2\r\nOBJECT = IMAGE\r\n  LINES = {lines}\r\n  LINE_SAMPLES = {samples}\r\n"
          f"  SAMPLE_TYPE = {kind}\r\n  SAMPLE_BITS = {np.dtype(dtype).itemsize*8}\r\n"+(f"  MISSING_CONSTANT = {missing}\r\n" if missing is not None else "")+
          f"END_OBJECT = IMAGE\r\n  CENTER_LONGITUDE = 330 <degree>\r\n  LINE_PROJECTION_OFFSET = {lpo}\r\n  SAMPLE_PROJECTION_OFFSET = {spo}\r\n"
          f"  MAP_RESOLUTION = {res} <pixel/degree>\r\nEND\r\n").encode()
    path.write_bytes(head.ljust(record,b' ')+values.astype(dtype).tobytes())

def grid(lines,samples,lpo,spo,res):
    i=np.arange(lines)[:,None];c=np.arange(samples)[None,:];return (lpo-i)/res,(330+(c-spo)/res+180)%360-180

def resampled(path,width,height):
    out=np.zeros((height,width));valid=np.zeros((height,width))
    for top,values,ok in pds_maps.resample(path,width,height,rows=37):out[top:top+len(values)]=values;valid[top:top+len(values)]=ok
    return out,valid

f=lambda lat,lon:np.round(1000*np.cos(np.radians(lat))*np.cos(np.radians(lon-40))+200*np.sin(np.radians(2*lat)))
W,H=360,180;lat=90-(np.arange(H)+.5);lon=-180+(np.arange(W)+.5);L,O=np.meshgrid(lat,lon,indexing='ij')
with tempfile.TemporaryDirectory() as folder:
    path=pathlib.Path(folder)/'dtm.img';a,b=grid(721,1440,360.,720.,4);values=f(a,b);values[400:440,300:340]=-32768
    write(path,4,721,1440,360.,720.,'>i2',values,missing=-32768)
    out,valid=resampled(path,W,H);hole=(valid<1)
    assert hole.any() and np.abs(out-f(L,O))[~hole][2:-2].max()<1,'16-bit grid-registered map misregistered'
    assert np.abs(out[:,0]-f(L,O)[:,0])[2:-2].max()<1 and np.abs(out[:,-1]-f(L,O)[:,-1])[2:-2].max()<1,'Longitude seam broken'
    g=lambda lat,lon:np.clip(128+100*np.sin(np.radians(lon))*np.cos(np.radians(lat)),0,255)
    res=4.3;samples,lines=int(360*res),int(180*res);path=pathlib.Path(folder)/'mosaic.img';a,b=grid(lines,samples,lines/2-.5,samples/2,res)
    write(path,res,lines,samples,lines/2-.5,samples/2,'u1',g(a,b))
    out,valid=resampled(path,W,H)
    assert valid.min()>.99 and np.abs(out-g(L,O))[2:-2].max()<1.5,'8-bit map at a non-integer resolution misregistered'
    shifted=pathlib.Path(folder)/'shifted.img';write(shifted,res,lines,samples,lines/2-.5,samples/2+res*5,'u1',g(a,b))
    assert np.abs(resampled(shifted,W,H)[0]-g(L,O))[2:-2].max()>5,'A 5 degree sample offset went unnoticed'
print('PDS maps: grid- and cell-registered labels, missing data, longitude seam and offsets OK')

"""PDS3 simple-cylindrical maps resampled onto Albert's equirectangular grid.

Authoring only (NumPy). Reads the attached label, memory-maps the image and
area-averages it onto cells of east longitude -180..180 and planetocentric
latitude 90..-90, in blocks of rows (no full-resolution copy in RAM). Pixel
positions follow the label (DSMAP): longitude = CENTER_LONGITUDE +
(sample - SAMPLE_PROJECTION_OFFSET) / MAP_RESOLUTION and latitude =
(LINE_PROJECTION_OFFSET - line) / MAP_RESOLUTION, 0-based, at pixel centres.
"""
import re
import numpy as np

def label(path):
    # Attached PDS3 label: KEY = value lines up to END; units in <> are dropped.
    values={};text=open(path,'rb').read(65536).decode('latin-1')
    for line in text.splitlines():
        if line.strip()=='END':break
        match=re.match(r'\s*\^?([A-Z_0-9]+)\s*=\s*(.*)',line)
        if match:values.setdefault(match[1],re.sub(r'\s*<[^>]*>','',match[2]).strip().strip('"'))
    return values

def image(path):
    meta=label(path);lines,samples=int(meta['LINES']),int(meta['LINE_SAMPLES']);bits=int(meta['SAMPLE_BITS'])
    kind={('UNSIGNED_INTEGER',8):'u1',('MSB_INTEGER',16):'>i2',('LSB_INTEGER',16):'<i2',('MSB_UNSIGNED_INTEGER',16):'>u2'}[(meta['SAMPLE_TYPE'],bits)]
    if meta.get('BANDS','1')!='1' or int(meta['RECORD_BYTES'])!=samples*bits//8:raise ValueError('Unsupported PDS3 layout')
    offset=(int(meta['IMAGE'])-1)*int(meta['RECORD_BYTES'])
    return meta,np.memmap(path,dtype=kind,mode='r',offset=offset,shape=(lines,samples))

def resample(path,width,height,rows=64):
    """Yield (first_row, values, valid) blocks of the target grid; values are float64
    area averages of the valid source pixels, valid is the covered fraction."""
    meta,data=image(path);res=float(meta['MAP_RESOLUTION']);center=float(meta['CENTER_LONGITUDE'])
    lpo,spo=float(meta['LINE_PROJECTION_OFFSET']),float(meta['SAMPLE_PROJECTION_OFFSET']);lines,samples=data.shape
    if abs(samples-360*res)>1:raise ValueError('Map does not span 360 degrees of longitude')
    missing=float(meta['MISSING_CONSTANT']) if 'MISSING_CONSTANT' in meta else None
    # Target cell edges in continuous source coordinates (pixel i covers [i-0.5, i+0.5)).
    # Unwrapped from the first edge; a span past the last sample continues on the repeated row.
    u=(spo+(-180-center)*res+0.5)%samples+np.arange(width+1)*360/width*res
    lat_edges=90-np.arange(height+1)*180/height;v=np.clip(lpo-lat_edges*res+0.5,0,lines)
    def integral(prefix,x):
        # Prefix sums over the row repeated twice, so wrapped spans stay contiguous.
        k=np.floor(x).astype(np.int64);return prefix[k]+(x-k)*(prefix[k+1]-prefix[k])
    for top in range(0,height,rows):
        block=min(rows,height-top);values=np.zeros((block,width));weights=np.zeros((block,width))
        for j in range(block):
            a,b=v[top+j],v[top+j+1];first,last=int(np.floor(a)),min(lines-1,int(np.ceil(b))-1)
            total=np.zeros(samples);count=np.zeros(samples)
            for i in range(first,last+1):
                w=min(b,i+1)-max(a,i)
                if w<=0:continue
                row=data[i].astype(np.float64);ok=np.ones(samples) if missing is None else (row!=missing).astype(np.float64)
                total+=w*row*ok;count+=w*ok
            for target,source in ((values,total),(weights,count)):
                prefix=np.concatenate(([0.],np.cumsum(np.concatenate((source,source,source[:1])))))
                target[j]=integral(prefix,u[1:])-integral(prefix,u[:-1])
        span=(u[1:]-u[:-1])*(v[top+1:top+block+1]-v[top:top+block])[:,None]
        valid=weights/np.maximum(span,1e-12);average=np.divide(values,weights,out=np.zeros_like(values),where=weights>0)
        yield top,average,valid

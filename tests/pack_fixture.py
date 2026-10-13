"""Small deterministic pack: LOD, UV orientation, wrapping and real 16-bit DEM."""
import hashlib, json, os, pathlib, struct, subprocess, sys, tempfile, zlib

def png(width, height, pixels, sixteen=False):
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value))
    stride = width * (2 if sixteen else 4)
    rows = b''.join(b'\0' + pixels[y*stride:(y+1)*stride] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 16 if sixteen else 8, 0 if sixteen else 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def fixture(path):
    layers=[]; entries=[]; payloads=[]; offset=0
    for name in ['earth-day','earth-night','earth-height','moon-day','moon-height','sun']:
        maximum=1 if name in ('earth-day','earth-night','moon-day') else 0
        width,height=(8,4) if maximum else (4,4) if name=='sun' else (4,2)
        layers.append(dict(id=name,width=width,height=height,tile_size=2,gutter=2,max_level=maximum,format='png'))
        for level in range(maximum+1):
            w,h=width>>(maximum-level),height>>(maximum-level)
            for row in range(h//2):
                for col in range(w//2):
                    raw=bytearray()
                    for y in range(6):
                        for x in range(6):
                            px=(col*2+x-2)%w;py=max(0,min(h-1,row*2+y-2))
                            if name=='moon-height':raw.extend(struct.pack('>H',19894+px*101+py*503))
                            else:raw.extend(bytes([40+40*level+px,100,20+py,255]))
                    image=png(6,6,bytes(raw),name=='moon-height')
                    entries.append(dict(path=f'v1/{name}/{level}/{row}/{col}.png',offset=offset,bytes=len(image),sha256=hashlib.sha256(image).hexdigest()))
                    payloads.append(image);offset+=len(image)
    index=json.dumps(dict(container='albert-pack-v1',layers=layers,files=entries)).encode()
    path.write_bytes(b'ALBRTPK1'+struct.pack('<Q',len(index))+index+b''.join(payloads))

with tempfile.TemporaryDirectory(prefix='albert-pack-') as directory:
    path=pathlib.Path(directory)/'visuals.pak';fixture(path)
    # Wine commands receive a Windows path, native commands keep their path.
    command=sys.argv[1:]
    argument='Z:'+str(path).replace('/','\\') if any(pathlib.Path(s).name=='wine' for s in command) else str(path)
    subprocess.run([*command,argument],check=True)

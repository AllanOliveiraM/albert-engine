"""Small deterministic pack: LOD, UV orientation, wrapping and real 16-bit DEM."""
import hashlib, json, os, pathlib, struct, subprocess, sys, tempfile, zlib

def png(width, height, pixels, sixteen=False):
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value))
    stride = width * (2 if sixteen else 4)
    rows = b''.join(b'\0' + pixels[y*stride:(y+1)*stride] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 16 if sixteen else 8, 0 if sixteen else 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def fixture(path,names=None,tint=0):
    layers=[]; entries=[]; payloads=[]; offset=0
    for name in names or ['earth-day','earth-night','earth-height','moon-day','moon-height','jupiter-day','sun']:
        maximum=1 if name in ('earth-day','earth-night','moon-day','mercury-day') else 0
        width,height=(8,4) if maximum else (4,4) if name=='sun' else (4,2)
        if name=='jupiter-day':maximum=2;width,height=12,6
        layers.append(dict(id=name,width=width,height=height,tile_size=2,gutter=2,max_level=maximum,format='png'))
        if name=='mercury-height':layers[-1].update(kind='elevation',sample_type='uint16',height_scale_m=.25,height_offset_m=-5000.)
        for level in range(maximum+1):
            w,h=width>>(maximum-level),height>>(maximum-level)
            for row in range((h+1)//2):
                for col in range((w+1)//2):
                    raw=bytearray()
                    tw,th=min(2,w-col*2)+4,min(2,h-row*2)+4
                    for y in range(th):
                        for x in range(tw):
                            px=(col*2+x-2)%w;py=max(0,min(h-1,row*2+y-2))
                            if name in ('moon-height','mercury-height'):raw.extend(struct.pack('>H',19894+px*101+py*503))
                            else:raw.extend(bytes([40+40*level+px+tint,100,20+py,255]))
                    image=png(tw,th,bytes(raw),name in ('moon-height','mercury-height'))
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

    swap=pathlib.Path(directory)/'swap.pak';replacement=pathlib.Path(directory)/'replacement.pak';fixture(swap);fixture(replacement,tint=20)
    windows=any(pathlib.Path(s).name=='wine' for s in command)
    path_arg=lambda p:'Z:'+str(p).replace('/','\\') if windows else str(p)
    subprocess.run([*command,path_arg(swap),'--replace',path_arg(replacement)],check=True)

    # Independent archives preserve the old payload and assemble into one file.
    import importlib.util
    spec=importlib.util.spec_from_file_location('assemble',pathlib.Path(__file__).parents[1]/'scripts/assemble-assets.py')
    assembler=importlib.util.module_from_spec(spec);spec.loader.exec_module(assembler)
    root=pathlib.Path(directory);base=root/'base.pak';fixture(base)
    child=root/'mercury';child.mkdir();extra=child/'extra.pak';fixture(extra,['mercury-day','mercury-height'])
    def manifest(payload,extensions=None):
        data=payload.read_bytes();part=payload.with_suffix('.pakpart');part.write_bytes(data)
        entry=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),parts=[dict(path=part.name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())])
        if extensions:entry['extensions']=extensions
        target=payload.parent/'manifest.json';target.write_text(json.dumps(entry));return target
    top=manifest(base,[dict(manifest='mercury/manifest.json')]);manifest(extra)
    combined=root/'combined.pak';assembler.assemble(top,combined)
    original=combined.read_bytes();assert original.startswith(base.read_bytes())
    argument='Z:'+str(combined).replace('/','\\') if any(pathlib.Path(s).name=='wine' for s in command) else str(combined)
    subprocess.run([*command,argument,'--collection'],check=True)
    broken=root/'broken-trailer.pak';broken.write_bytes(original[:-8]+b'BADMAGIC')
    invalid_argument='Z:'+str(broken).replace('/','\\') if any(pathlib.Path(s).name=='wine' for s in command) else str(broken)
    subprocess.run([*command,invalid_argument,'--invalid'],check=True)
    # A failed assembly leaves the previously complete library intact.
    extra.with_suffix('.pakpart').write_bytes(b'broken')
    previous=combined.read_bytes()
    try:assembler.assemble(top,combined);raise AssertionError('Corrupted extension accepted')
    except RuntimeError:pass
    assert combined.read_bytes()==previous

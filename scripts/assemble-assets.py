#!/usr/bin/env python3
"""Verify LFS archives and assemble them into one seekable visual library."""
import hashlib, json, pathlib, struct, sys

FOOTER=b'ALBRTPX1'

def records(manifest_path):
    root=manifest_path.parent.resolve()
    def visit(path,depth=0):
        if depth>1:raise RuntimeError('Nested asset extensions are unsupported')
        manifest=json.loads(path.read_text())
        yield path,manifest
        for extension in manifest.get('extensions',[]):
            relative=pathlib.PurePosixPath(extension['manifest'])
            if relative.is_absolute() or '..' in relative.parts:raise RuntimeError('Invalid extension manifest path')
            child=path.parent/relative
            if not child.resolve().is_relative_to(root):raise RuntimeError('Extension escapes imagery directory')
            yield from visit(child,depth+1)
    return list(visit(manifest_path))

def assemble(manifest_path, destination):
    archives=records(manifest_path);destination.parent.mkdir(parents=True,exist_ok=True)
    temporary=destination.with_suffix('.pak.tmp');directory=[]
    try:
        with temporary.open('wb') as output:
            for path,manifest in archives:
                start=output.tell();complete=hashlib.sha256()
                for part in manifest['parts']:
                    if pathlib.Path(part['path']).name!=part['path']:raise RuntimeError('Invalid part path')
                    checksum=hashlib.sha256();size=0
                    with (path.parent/part['path']).open('rb') as source:
                        if source.read(64).startswith(b'version https://git-lfs.github.com/spec/'):
                            raise RuntimeError('Git LFS images missing. Run: git lfs install && git lfs pull')
                        source.seek(0)
                        while data:=source.read(1024*1024):
                            checksum.update(data);complete.update(data);output.write(data);size+=len(data)
                    if size!=part['bytes'] or checksum.hexdigest()!=part['sha256']:raise RuntimeError('Asset part integrity mismatch: '+part['path'])
                size=output.tell()-start
                if size!=manifest['bytes'] or complete.hexdigest()!=manifest['sha256']:raise RuntimeError('Complete asset archive integrity mismatch: '+str(path))
                directory.append(dict(offset=start,bytes=size))
            if len(directory)>1:
                index=json.dumps(dict(container='albert-pack-collection-v1',archives=directory),separators=(',',':')).encode()
                output.write(index);output.write(struct.pack('<Q',len(index)));output.write(FOOTER)
        temporary.replace(destination)
    finally:temporary.unlink(missing_ok=True)
    print('Assets ready:',destination,destination.stat().st_size,'bytes;',len(directory),'archives',flush=True)

if __name__=='__main__':assemble(pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]))

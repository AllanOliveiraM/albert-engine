#!/usr/bin/env python3
"""Reassemble the versioned LFS parts without altering their payloads."""
import hashlib, json, pathlib, sys

def assemble(manifest_path, destination):
    manifest=json.loads(manifest_path.read_text())
    destination.parent.mkdir(parents=True,exist_ok=True)
    temporary=destination.with_suffix('.pak.tmp')
    complete=hashlib.sha256()
    try:
        with temporary.open('wb') as output:
            for part in manifest['parts']:
                path=manifest_path.parent/part['path']
                if pathlib.Path(part['path']).name!=part['path']:
                    raise RuntimeError('Invalid part path')
                checksum=hashlib.sha256();size=0
                with path.open('rb') as source:
                    if source.read(64).startswith(b'version https://git-lfs.github.com/spec/'):
                        raise RuntimeError('Git LFS images missing. Run: git lfs install && git lfs pull')
                    source.seek(0)
                    while data:=source.read(1024*1024):
                        checksum.update(data);complete.update(data);output.write(data);size+=len(data)
                if size!=part['bytes'] or checksum.hexdigest()!=part['sha256']:
                    raise RuntimeError('Asset part integrity mismatch: '+part['path'])
        if temporary.stat().st_size!=manifest['bytes'] or complete.hexdigest()!=manifest['sha256']:
            raise RuntimeError('Complete asset library integrity mismatch')
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    print('Assets ready:',destination,manifest['bytes'],'bytes',flush=True)

if __name__=='__main__':
    assemble(pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]))

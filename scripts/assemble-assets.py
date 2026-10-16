#!/usr/bin/env python3
"""Verify LFS archives and assemble them into one seekable visual library."""
import hashlib, json, os, pathlib, struct, sys

FOOTER=b'ALBRTPX1'

def replace_archive(temporary,destination):
    if os.name=='nt' and destination.exists():
        # MoveFileEx (Path.replace) cannot replace some open Windows files.
        # ReplaceFile honors the reader's FILE_SHARE_DELETE contract.
        import ctypes
        api=ctypes.WinDLL('kernel32',use_last_error=True).ReplaceFileW
        api.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_void_p]
        api.restype=ctypes.c_int
        if not api(str(destination),str(temporary),None,0,None,None):raise ctypes.WinError(ctypes.get_last_error())
    else:temporary.replace(destination)

def discard_cache(stream,start=0,length=0,write=False):
    # Multi-GB sequential copies must not displace the desktop into swap.
    # Linux advice affects only these archive files, never the global cache.
    if not hasattr(os,'posix_fadvise'):return
    if write:stream.flush();os.fdatasync(stream.fileno())
    try:os.posix_fadvise(stream.fileno(),start,length,os.POSIX_FADV_DONTNEED)
    except OSError:pass # Advisory only; checksums/durability remain mandatory.

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
            flushed=0
            for path,manifest in archives:
                start=output.tell();complete=hashlib.sha256()
                for part in manifest['parts']:
                    if pathlib.Path(part['path']).name!=part['path']:raise RuntimeError('Invalid part path')
                    checksum=hashlib.sha256();size=0;discarded=0
                    with (path.parent/part['path']).open('rb') as source:
                        if source.read(64).startswith(b'version https://git-lfs.github.com/spec/'):
                            raise RuntimeError('Git LFS images missing. Run: git lfs install && git lfs pull')
                        source.seek(0)
                        while data:=source.read(1024*1024):
                            checksum.update(data);complete.update(data);output.write(data);size+=len(data)
                            if size-discarded>=64*1024*1024:discard_cache(source,discarded,size-discarded);discarded=size
                            if output.tell()-flushed>=64*1024*1024:
                                end=output.tell();discard_cache(output,flushed,end-flushed,True);flushed=end
                        discard_cache(source)
                    if size!=part['bytes'] or checksum.hexdigest()!=part['sha256']:raise RuntimeError('Asset part integrity mismatch: '+part['path'])
                size=output.tell()-start
                if size!=manifest['bytes'] or complete.hexdigest()!=manifest['sha256']:raise RuntimeError('Complete asset archive integrity mismatch: '+str(path))
                directory.append(dict(offset=start,bytes=size))
            if len(directory)>1:
                index=json.dumps(dict(container='albert-pack-collection-v1',archives=directory),separators=(',',':')).encode()
                output.write(index);output.write(struct.pack('<Q',len(index)));output.write(FOOTER)
            discard_cache(output,write=True)
        replace_archive(temporary,destination)
    finally:temporary.unlink(missing_ok=True)
    print('Assets ready:',destination,destination.stat().st_size,'bytes;',len(directory),'archives',flush=True)

if __name__=='__main__':assemble(pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]))

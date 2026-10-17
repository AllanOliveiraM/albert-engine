#!/usr/bin/env python3
"""Resume a large HTTP asset in bounded parallel ranges, validating every chunk.

Requires HTTP byte ranges and stable ETag/Last-Modified. No credentials or system
packages. Originals, chunk journal and source records stay outside Git.
"""
import argparse, concurrent.futures, hashlib, json, os, pathlib, re, threading, time, random, urllib.request, urllib.error
# Some archives (PDS SBN) refuse Python's default agent; identify the project instead.
AGENT='AlbertEngine-assets/1 (+https://github.com/AllanOliveiraM/albert-engine)'

def fetch(url, destination, workers=8, chunk_bytes=16*1024*1024, expected_sha256=None):
    destination=pathlib.Path(destination);destination.parent.mkdir(parents=True,exist_ok=True)
    if destination.exists():
        with destination.open('rb') as stream:checksum=hashlib.file_digest(stream,'sha256').hexdigest()
        if expected_sha256 and checksum!=expected_sha256:raise RuntimeError('Existing source checksum mismatch')
        return dict(url=url,bytes=destination.stat().st_size,sha256=checksum)
    probe=urllib.request.Request(url,headers={'Range':'bytes=0-16383','Accept-Encoding':'identity','User-Agent':AGENT})
    for attempt in range(6):
        try:
            response=urllib.request.urlopen(probe,timeout=20);break
        except Exception:
            if attempt==5:raise
            time.sleep(min(15,2**attempt))
    with response:
        match=re.fullmatch(r'bytes 0-(\d+)/(\d+)',response.headers.get('Content-Range',''))
        if response.status!=206 or not match:raise RuntimeError('Source does not support byte ranges')
        size=int(match[2]);identity=response.headers.get('ETag') or response.headers.get('Last-Modified')
        if not identity:raise RuntimeError('Source lacks an immutable transfer validator')
        response.read(min(size,16384))
    partial=destination.with_suffix(destination.suffix+'.part')
    journal=destination.with_suffix(destination.suffix+'.ranges.json')
    metadata=dict(url=url,bytes=size,identity=identity,chunk_bytes=chunk_bytes,chunks={})
    if journal.exists():
        previous=json.loads(journal.read_text())
        if any(previous.get(key)!=metadata[key] for key in ('url','bytes','identity','chunk_bytes')):raise RuntimeError('Source changed; preserve/remove the old partial before retrying')
        metadata=previous
    elif partial.exists():raise RuntimeError('Unjournaled partial exists; use a separate destination')
    lock=threading.Lock();stop=threading.Event()
    with partial.open('r+b' if partial.exists() else 'w+b') as output:
        output.truncate(size);descriptor=output.fileno()
        def save():
            temporary=journal.with_suffix(journal.suffix+'.tmp');temporary.write_text(json.dumps(metadata,sort_keys=True)+'\n');temporary.replace(journal)
        def read_chunk(start,length):
            with partial.open('rb') as stream:stream.seek(start);return stream.read(length)
        def transfer(index):
            start=index*chunk_bytes;length=min(chunk_bytes,size-start);key=str(index)
            if key in metadata['chunks'] and hashlib.sha256(read_chunk(start,length)).hexdigest()==metadata['chunks'][key]:return
            digest=hashlib.sha256();written=0
            for attempt in range(12):
                if stop.is_set():return
                try:
                    while written<length:
                        range_start=start+written;range_end=min(start+length-1,range_start+2*1024*1024-1)
                        request=urllib.request.Request(url,headers={'Range':f'bytes={range_start}-{range_end}','If-Range':identity,'Accept-Encoding':'identity','User-Agent':AGENT})
                        with urllib.request.urlopen(request,timeout=30) as response:
                            if response.status!=206 or response.headers.get('Content-Range')!=f'bytes {range_start}-{range_end}/{size}':raise RuntimeError('Source changed or range response is invalid')
                            while start+written<=range_end:
                                if stop.is_set():return
                                data=response.read(min(256*1024,range_end-start-written+1))
                                if not data:raise RuntimeError('Incomplete source range')
                                digest.update(data)
                                # Independent streams are portable to Windows.
                                with partial.open('r+b',buffering=0) as stream:stream.seek(start+written);stream.write(data)
                                written+=len(data)
                    with lock:
                        metadata['chunks'][key]=digest.hexdigest();save()
                        done=sum(min(chunk_bytes,size-int(i)*chunk_bytes) for i in metadata['chunks'])
                        print(f'{destination.name}: {done/size:6.1%} ({done/1024**2:.0f}/{size/1024**2:.0f} MiB)',flush=True)
                    return
                except Exception as error:
                    if attempt==11:stop.set();raise
                    delay=min(60,2**attempt)
                    if isinstance(error,urllib.error.HTTPError):
                        try:delay=max(delay,min(300,float(error.headers.get('Retry-After','0'))))
                        except ValueError:pass
                    print(f'Retrying range {start+written}: {type(error).__name__}; partial chunk retained',flush=True)
                    time.sleep(delay+random.uniform(0,2))
        with lock:save()
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
                list(pool.map(transfer,range((size+chunk_bytes-1)//chunk_bytes)))
        except BaseException:stop.set();raise
        output.flush();os.fsync(descriptor)
    with partial.open('rb') as stream:checksum=hashlib.file_digest(stream,'sha256').hexdigest()
    if expected_sha256 and checksum!=expected_sha256:raise RuntimeError('Downloaded source SHA-256 mismatch')
    partial.replace(destination)
    record=dict(url=url,bytes=size,sha256=checksum,transfer_validator=identity)
    destination.with_suffix(destination.suffix+'.download.json').write_text(json.dumps(record,indent=2)+'\n')
    print('SOURCE READY:',destination,checksum,flush=True)
    return record

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('url');parser.add_argument('destination',type=pathlib.Path)
    parser.add_argument('--workers',type=int,default=8);parser.add_argument('--sha256');args=parser.parse_args()
    if not 1<=args.workers<=32:parser.error('--workers must be between 1 and 32')
    fetch(args.url,args.destination,args.workers,expected_sha256=args.sha256)

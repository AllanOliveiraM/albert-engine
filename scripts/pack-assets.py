#!/usr/bin/env python3
"""Store already compressed imagery in one indexed, seekable Albert container.

This is packaging, not encryption. The application reads individual payloads
without unpacking the library or loading the whole file into memory.
"""
import argparse, hashlib, json, pathlib, shutil, struct

MAGIC = b'ALBRTPK1'

def create_pack(root, name=None):
    public = root / 'public'
    manifest = json.loads((public / 'manifest.json').read_text())
    offset = 0
    for entry in manifest['files']:
        entry['offset'] = offset
        offset += entry['bytes']
    manifest['container'] = 'albert-pack-v1'
    index = json.dumps(manifest, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
    destination = root / 'visuals.pak'
    temporary = root / 'visuals.pak.tmp'
    with temporary.open('wb') as output:
        output.write(MAGIC)
        output.write(struct.pack('<Q', len(index)))
        output.write(index)
        for entry in manifest['files']:
            path = public / entry['path']
            with path.open('rb') as source:
                checksum = hashlib.file_digest(source, 'sha256').hexdigest()
                if path.stat().st_size != entry['bytes'] or checksum != entry['sha256']:
                    raise RuntimeError('Invalid payload: ' + entry['path'])
                source.seek(0)
                shutil.copyfileobj(source, output, 1024 * 1024)
    temporary.replace(destination)
    with destination.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    (root / 'visuals.pak.json').write_text(json.dumps({
        'format': 'albert-pack-v1', 'bytes': destination.stat().st_size,
        'sha256': checksum, 'files': len(manifest['files']),
    }, indent=2) + '\n')
    # GitHub LFS parts stay below the per-file limit. Distribution reads the
    # byte-identical assembled container, never these working parts.
    imagery=pathlib.Path(__file__).resolve().parents[1]/'assets/imagery'
    if name and (not name.replace('-','').isalnum() or not name.isascii()):raise RuntimeError('Invalid asset archive name')
    directory=imagery/name if name else imagery
    directory.mkdir(parents=True,exist_ok=True)
    parts=[]
    with destination.open('rb') as source:
        index=0
        while source.tell()<destination.stat().st_size:
            path=directory/f'visuals-{index:03}.pakpart'
            temporary=path.with_suffix('.pakpart.tmp')
            digest=hashlib.sha256();size=0
            with temporary.open('wb') as output:
                for _ in range(1024):
                    data=source.read(1024*1024)
                    if not data:break
                    digest.update(data);output.write(data);size+=len(data)
            temporary.replace(path)
            parts.append({'path':path.name,'bytes':size,'sha256':digest.hexdigest()})
            index+=1
    record=json.loads((root/'visuals.pak.json').read_text())
    record['parts']=parts
    part_manifest=directory/'manifest.json'
    if not name and part_manifest.exists():
        extensions=json.loads(part_manifest.read_text()).get('extensions')
        if extensions:record['extensions']=extensions
    part_manifest.write_text(json.dumps(record,indent=2)+'\n')
    if name:
        base_manifest=imagery/'manifest.json';base=json.loads(base_manifest.read_text())
        extensions=base.setdefault('extensions',[])
        if not any(e['manifest']==name+'/manifest.json' for e in extensions):extensions.append({'manifest':name+'/manifest.json'})
        base_manifest.write_text(json.dumps(base,indent=2)+'\n')
    for path in directory.glob('*.pakpart'):
        if path.name not in {entry['path'] for entry in parts}:path.unlink()
    auxiliary=imagery.parent/'manifest.json'
    metadata=json.loads(auxiliary.read_text())
    for target in ([part_manifest,imagery/'manifest.json',imagery.parent/'planetary-sources.json'] if name else [part_manifest]):
        relative=target.relative_to(imagery.parent).as_posix()
        entry=next((entry for entry in metadata['files'] if entry['path']==relative),None)
        if entry is None:
            entry={'path':relative,'source':'Generated imagery archive manifest','license':'Credits in the packed source records','processing':'SHA-256 verified Git LFS parts; one distribution container'}
            metadata['files'].append(entry)
        entry.update(bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest())
    auxiliary.write_text(json.dumps(metadata,indent=2,ensure_ascii=False)+'\n')
    print('PACK READY:', destination, destination.stat().st_size, 'bytes', flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / 'asset-packs')
    parser.add_argument("--name",help="Version a separate body archive and register it in the combined library")
    args=parser.parse_args();create_pack(args.root.resolve(),args.name)

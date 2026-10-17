#!/usr/bin/env python3
"""Validate ZIP volumes, byte reconstruction, CRC, permissions and checksums."""
import hashlib,importlib.util,io,pathlib,stat,tempfile,zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('release',ROOT/'scripts/package-release.py');release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release)
with tempfile.TemporaryDirectory(prefix='albert-release-') as directory:
 root=pathlib.Path(directory);source=root/'portable';output=root/'archives'
 (source/'assets').mkdir(parents=True);(source/'licenses').mkdir()
 executable=source/'AlbertEngine';executable.write_bytes(b'program');executable.chmod(0o755)
 (source/'assets/visuals.pak').write_bytes(bytes(range(256))*32)
 (source/'licenses/notice.txt').write_bytes(b'license\n')
 for name,limit in [('small',65536),('split',512)]:
  parts=release.package(source,output,name,limit)
  assert all(path.stat().st_size<=limit for path in parts)
  assert (len(parts)==1 and parts[0].suffix=='.zip') if name=='small' else len(parts)>1
  raw=b''.join(path.read_bytes() for path in parts)
  with zipfile.ZipFile(io.BytesIO(raw)) as archive:
   assert archive.testzip() is None
   assert set(archive.namelist())=={'AlbertEngine','assets/visuals.pak','licenses/notice.txt'}
   for path in source.rglob('*'):
    if path.is_file():assert archive.read(path.relative_to(source).as_posix())==path.read_bytes()
   assert archive.getinfo('AlbertEngine').external_attr>>16&stat.S_IXUSR
  sums=(output/(name+'.sha256')).read_text().splitlines()
  for row,path in zip(sums,parts):assert row==hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name
 try:release.package(source,output,'split',512)
 except FileExistsError:pass
 else:raise AssertionError('Existing release silently overwritten')
 try:release.package(source,source/'archives','bad')
 except ValueError:pass
 else:raise AssertionError('Archive can recursively include itself')
print('Release OK: single/split ZIP, root entrypoint, exact files, CRC, modes and SHA-256')

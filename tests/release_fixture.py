#!/usr/bin/env python3
"""Validate ZIP volumes, byte reconstruction, CRC, permissions and checksums."""
import hashlib,importlib.util,io,os,pathlib,stat,tempfile,zipfile

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
 # Packaged on Windows (no Unix mode on disk), the Linux entrypoint stays executable.
 executable.chmod(0o644);parts=release.package(source,output,'windows-host',65536);executable.chmod(0o755)
 with zipfile.ZipFile(parts[0]) as archive:assert archive.getinfo('AlbertEngine').external_attr>>16&stat.S_IXUSR,'Entrypoint lost its executable bit'
 # Light folder: same program without the library, plus its download catalogue.
 import json,sys;sys.path.insert(0,str(ROOT/'tests'));from pack_fixture import fixture
 fixture(source/'assets/visuals.pak')
 spec=importlib.util.spec_from_file_location('light',ROOT/'scripts/light-dist.py');light=importlib.util.module_from_spec(spec);spec.loader.exec_module(light)
 catalog=light.light(source,root/'light')
 assert not (root/'light/assets/visuals.pak').exists() and json.loads((root/'light/assets/catalog.json').read_text())==catalog and catalog['containers'][0]['essential']
 # Windows file systems record no Unix mode; the light ZIP below still must.
 assert (os.name=='nt' or (root/'light/AlbertEngine').stat().st_mode&stat.S_IXUSR) and (root/'light/licenses/notice.txt').read_bytes()==b'license\n'
 parts=release.package(root/'light',output,'light',65536)
 with zipfile.ZipFile(parts[0]) as archive:assert set(archive.namelist())=={'AlbertEngine','assets/catalog.json','licenses/notice.txt'} and archive.getinfo('AlbertEngine').external_attr>>16&stat.S_IXUSR
 try:light.light(source,root/'light')
 except ValueError:pass
 else:raise AssertionError('Light folder silently overwritten')
spec=importlib.util.spec_from_file_location('asset_release',ROOT/'scripts/asset-release.py');assets=importlib.util.module_from_spec(spec);spec.loader.exec_module(assets)
need=[dict(name=c['file'],size=c['bytes']) for c in catalog['containers']]+[dict(name='catalog.json',size=1)]
assert assets.missing(catalog,dict(assets=need,isDraft=False))==[],'Complete asset release reported incomplete'
assert assets.missing(catalog,dict(assets=need,isDraft=True))==['(release is a draft)'],'Draft asset release accepted'
assert assets.missing(catalog,dict(assets=need[1:],isDraft=False))==[need[0]['name']],'Missing container not reported'
assert assets.missing(catalog,dict(assets=[dict(need[0],size=1)]+need[1:],isDraft=False))==[need[0]['name']],'Truncated container not reported'
print('Release OK: single/split ZIP, root entrypoint, exact files, CRC, modes, SHA-256 and light folder')

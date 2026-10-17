#!/usr/bin/env python3
"""Verify manifest assets after a Windows-style Git checkout, without large LFS parts."""
import json,pathlib,shutil,subprocess,tempfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
def run(*args,cwd=None):
 return subprocess.run(args,cwd=cwd,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)

with tempfile.TemporaryDirectory(prefix='albert-asset-checkout-') as directory:
 root=pathlib.Path(directory);repository=root/'repository';checkout=root/'checkout'
 repository.mkdir();checkout.mkdir()
 run('git','init','--quiet',str(repository))
 run('git','config','core.autocrlf','true',cwd=repository)
 shutil.copyfile(ROOT/'.gitattributes',repository/'.gitattributes')
 manifest=json.loads((ROOT/'assets/manifest.json').read_bytes())
 for name in ['manifest.json',*[entry['path'] for entry in manifest['files']]]:
  target=repository/'assets'/name;target.parent.mkdir(parents=True,exist_ok=True)
  shutil.copyfile(ROOT/'assets'/name,target)
 run('git','add','.',cwd=repository)
 run('git','checkout-index','--all','--prefix='+checkout.as_posix()+'/',cwd=repository)
 # Run the same CMake integrity guard as the actual configure, before dependencies.
 run('cmake','-DPROJECT_SOURCE_DIR='+str(checkout),'-P',str(ROOT/'cmake/Assets.cmake'))
 for entry in manifest['files']:
  target=checkout/'assets'/entry['path']
  if target.stat().st_size!=entry['bytes']:raise AssertionError('Asset byte count changed: '+entry['path'])
 # The fix must retain strict integrity checking; corrupted payloads still fail.
 damaged=checkout/'assets/countries.geojson'
 with damaged.open('ab') as output:output.write(b' ')
 result=subprocess.run(['cmake','-DPROJECT_SOURCE_DIR='+str(checkout),'-P',str(ROOT/'cmake/Assets.cmake')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 if result.returncode==0 or 'Checksum mismatch' not in result.stderr:raise AssertionError('Asset integrity guard accepted corruption')
print('Assets OK: exact hashes/bytes after core.autocrlf=true checkout; corruption rejected')

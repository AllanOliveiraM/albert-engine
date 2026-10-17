#!/usr/bin/env python3
"""Stream a portable folder to ZIP volumes below GitHub's 2 GiB asset limit."""
import argparse,hashlib,pathlib,re,shutil,zipfile

class Volumes:
 def __init__(self,directory,name,limit):
  self.directory=directory;self.name=name;self.limit=limit;self.position=0
  self.stream=None;self.current=0;self.parts=[];self.hashes=[];self.digest=None
 def tell(self):return self.position
 def flush(self):
  if self.stream:self.stream.flush()
 def finish_part(self):
  if self.stream:
   self.stream.close();self.hashes.append(self.digest.hexdigest());self.stream=None
 def write(self,data):
  view=memoryview(data);count=len(view)
  while view:
   if self.stream is None:
    path=self.directory/(self.name+f'.zip.{len(self.parts)+1:03d}')
    self.stream=path.open('xb');self.parts.append(path);self.current=0;self.digest=hashlib.sha256()
   size=min(len(view),self.limit-self.current);self.stream.write(view[:size]);self.digest.update(view[:size])
   self.current+=size;self.position+=size;view=view[size:]
   if self.current==self.limit:self.finish_part()
  return count

def package(source,output,name,limit=1900*1024*1024):
 source=pathlib.Path(source).resolve();output=pathlib.Path(output).resolve()
 if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,100}',name):raise ValueError('Invalid archive name')
 if not source.is_dir() or output==source or source in output.parents:raise ValueError('Output must be outside the distribution folder')
 if not 1<=limit<2*1024**3:raise ValueError('Volume size must be below 2 GiB')
 if not any((source/n).is_file() for n in ('AlbertEngine','AlbertEngine.exe')):raise ValueError('Distribution executable missing at root')
 # Complete folders carry the library; light ones carry its download catalogue.
 if not ((source/'assets/visuals.pak').is_file() or (source/'assets/catalog.json').is_file()):raise ValueError('Distribution imagery missing')
 output.mkdir(parents=True,exist_ok=True)
 if list(output.glob(name+'.zip*')) or (output/(name+'.sha256')).exists():raise FileExistsError('Release output already exists')
 volumes=Volumes(output,name,limit)
 try:
  with zipfile.ZipFile(volumes,'w',allowZip64=True) as archive:
   for path in sorted(source.rglob('*')):
    if not path.is_file():continue
    if source not in path.resolve().parents:raise ValueError('Distribution symlink escapes source')
    relative=path.relative_to(source).as_posix()
    info=zipfile.ZipInfo.from_file(path,relative,strict_timestamps=False)
    # Images/pak are already compressed. Avoid another full compression pass.
    info.compress_type=zipfile.ZIP_STORED
    with path.open('rb') as incoming,archive.open(info,'w',force_zip64=info.file_size>=2*1024**3) as outgoing:
     shutil.copyfileobj(incoming,outgoing,1024*1024)
  volumes.finish_part()
  if len(volumes.parts)==1:
   path=volumes.parts[0].with_suffix('');volumes.parts[0].rename(path);volumes.parts[0]=path
  checksum=output/(name+'.sha256')
  with checksum.open('w',encoding='utf-8',newline='\n') as output:
   output.write(''.join(f'{value}  {path.name}\n' for value,path in zip(volumes.hashes,volumes.parts)))
  print(f'{name}: {len(volumes.parts)} ZIP volume(s), {volumes.position:,} bytes',flush=True)
  return volumes.parts
 except BaseException:
  volumes.finish_part()
  for path in volumes.parts:path.unlink(missing_ok=True)
  raise

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('source',type=pathlib.Path);parser.add_argument('output',type=pathlib.Path);parser.add_argument('--name',required=True)
 args=parser.parse_args();package(args.source,args.output,args.name)

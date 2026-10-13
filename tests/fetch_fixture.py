"""Exercise ranged download, resume, corrupt chunks and transfer validators."""
import hashlib,http.server,importlib.util,json,pathlib,re,tempfile,threading
spec=importlib.util.spec_from_file_location('fetch',pathlib.Path(__file__).parents[1]/'scripts/fetch-ranges.py')
fetch=importlib.util.module_from_spec(spec);spec.loader.exec_module(fetch)
data=bytes(range(256))*33;requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        match=re.fullmatch(r'bytes=(\d+)-(\d+)',self.headers.get('Range',''))
        if not match:self.send_error(400);return
        begin,end=map(int,match.groups());end=min(end,len(data)-1);requests.append((begin,end))
        self.send_response(206);self.send_header('Content-Range',f'bytes {begin}-{end}/{len(data)}');self.send_header('Content-Length',str(end-begin+1));self.send_header('ETag','"fixture-v1"');self.end_headers();self.wfile.write(data[begin:end+1])
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
thread=threading.Thread(target=server.serve_forever);thread.start()
try:
    with tempfile.TemporaryDirectory(prefix='albert-fetch-') as directory:
        target=pathlib.Path(directory)/'source.bin';url=f'http://127.0.0.1:{server.server_port}/source';checksum=hashlib.sha256(data).hexdigest()
        record=fetch.fetch(url,target,workers=3,chunk_bytes=2048,expected_sha256=checksum)
        assert record['sha256']==checksum and target.read_bytes()==data
        # A resumed partial can contain a damaged chunk, which must be redownloaded.
        partial=target.with_suffix('.bin.part');target.rename(partial)
        with partial.open('r+b') as stream:stream.seek(3000);stream.write(b'bad')
        requests.clear();fetch.fetch(url,target,workers=2,chunk_bytes=2048,expected_sha256=checksum)
        assert target.read_bytes()==data and len(requests)==2,requests
        # A changed source never gets mixed into the journaled original.
        target.rename(partial);journal=target.with_suffix('.bin.ranges.json');meta=json.loads(journal.read_text());meta['identity']='different';journal.write_text(json.dumps(meta))
        try:fetch.fetch(url,target,workers=2,chunk_bytes=2048);raise AssertionError('Changed source accepted')
        except RuntimeError:pass
        assert not target.exists() and partial.read_bytes()==data
    print('Ranged assets OK: exact coverage, SHA-256, parallel writes, resume, corrupt chunk recovery and source-change rejection')
finally:server.shutdown();server.server_close();thread.join()

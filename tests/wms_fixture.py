#!/usr/bin/env python3
import http.server,subprocess,sys,threading,urllib.parse,struct,zlib
class Handler(http.server.BaseHTTPRequestHandler):
    requests=0
    def do_GET(self):
        q=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        west,east=float(q['west'][0]),float(q['east'][0]);w,h=int(q['width'][0]),int(q['height'][0])
        assert -180<=west<east<=180
        Handler.requests+=1
        rgb=b'\xff\x00\x00\xff' if west>=0 else b'\x00\x00\xff\xff'
        def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
        image=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+rgb*w)*h))+chunk(b'IEND',b'')
        self.send_response(200);self.send_header('Content-Length',str(len(image)));self.end_headers();self.wfile.write(image)
    def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
subprocess.run(sys.argv[1:]+[f'http://127.0.0.1:{server.server_port}/'],check=True)
assert Handler.requests==4
server.shutdown()

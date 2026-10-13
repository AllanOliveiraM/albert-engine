"""Local HTTP transport checks; no public endpoints or external clock needed."""
import http.server,subprocess,sys,threading,time
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        if self.path=='/error':self.send_error(503);return
        self.send_response(200)
        total=4096 if self.path=='/ok' else 81920 if self.path=='/slow' else 1024
        self.send_header('Content-Length',str(total));self.end_headers()
        try:
            if self.path=='/truncated':self.wfile.write(b'partial');return
            for offset in range(0,total,4096):
                self.wfile.write(b'x'*min(4096,total-offset));self.wfile.flush()
                if self.path=='/slow':time.sleep(.05)
        except (BrokenPipeError,ConnectionResetError):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
thread=threading.Thread(target=server.serve_forever);thread.start()
try:subprocess.run([*sys.argv[1:],f'http://127.0.0.1:{server.server_port}'],check=True,timeout=15)
finally:server.shutdown();server.server_close();thread.join()

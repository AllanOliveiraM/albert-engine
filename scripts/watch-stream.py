#!/usr/bin/env python3
"""Watch a flight from another device (a phone over a VPN): Albert on a display, its picture as live HLS video, a page.

Standard library, ffmpeg (libx264) and a built Albert:
  python3 scripts/watch-stream.py --display 172.17.0.3:100 --bind 100.115.108.116 -- --starship
starts AlbertEngine on that X display (an Xvfb: nothing on the local screen, no sound), the flight client with the
arguments after "--", and serves http://BIND:PORT/ (only on that address): the video (a 1 s segment HLS stream,
~3-5 s behind) and, under it, the client's log. Ctrl+C or the client's end stops it all.
"""
import argparse,functools,http.server,os,pathlib,shutil,signal,subprocess,sys,tempfile,threading,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
PAGE="""<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Albert ao vivo</title><style>
body{margin:0;background:#06080b;color:#dfe5ec;font:14px system-ui,sans-serif}video{width:100%;max-height:78vh;background:#000;display:block}
header{padding:10px 14px;font-weight:600;letter-spacing:.08em}#log{margin:0;padding:10px 14px;white-space:pre-wrap;font:12px ui-monospace,monospace;
color:#aab4c0;max-height:40vh;overflow:auto}</style></head><body><header>ALBERT · AO VIVO</header>
<video id="v" autoplay muted playsinline controls></video><pre id="log">a aguardar o voo…</pre>
<script src="https://cdn.jsdelivr.net/npm/hls.js@1.5.13/dist/hls.min.js"></script><script>
const v=document.getElementById('v'),src='live.m3u8';
if(v.canPlayType('application/vnd.apple.mpegurl'))v.src=src;
else if(window.Hls&&Hls.isSupported()){const h=new Hls({liveSyncDurationCount:2,lowLatencyMode:true});h.loadSource(src);h.attachMedia(v);}
const log=document.getElementById('log');
async function tick(){try{const r=await fetch('log.txt?'+Date.now());if(r.ok){const t=await r.text();const lines=t.trim().split('\\n');
  log.textContent=lines.slice(-40).join('\\n');log.scrollTop=log.scrollHeight;}}catch(e){}setTimeout(tick,2000);}tick();
</script></body></html>"""
def main():
    a=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('--display',default=os.environ.get('DISPLAY',':0'));a.add_argument('--bind',required=True,help='address to serve on (the VPN one)')
    a.add_argument('--port',type=int,default=8080);a.add_argument('--size',default='1280x720');a.add_argument('--fps',type=int,default=30)
    a.add_argument('--rpc',type=int,default=50061);a.add_argument('client',nargs=argparse.REMAINDER)
    o=a.parse_args();args=[x for x in o.client if x!='--']
    build=ROOT/'build';out=pathlib.Path(tempfile.mkdtemp(prefix='albert-stream-'));(out/'index.html').write_text(PAGE,encoding='utf-8')
    env=dict(os.environ,DISPLAY=o.display,ALBERT_AUDIO='0',ALBERT_WINDOW=o.size,ALBERT_ASSET_DIR=str(ROOT/'assets'),
             ALBERT_PACK_PATH=str(build/'linux/imagery/visuals-v2.pak'),ALBERT_DATA_DIR=str(build/'simulation-cache'))
    env.pop('WAYLAND_DISPLAY',None);procs=[]
    def stop(*_):
        for p in reversed(procs):
            if p.poll() is None:p.terminate()
        for p in procs:
            try:p.wait(timeout=10)
            except subprocess.TimeoutExpired:p.kill()
        shutil.rmtree(out,ignore_errors=True);sys.exit(0)
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    app_log=open(out/'app.txt','w');app=subprocess.Popen([str(build/'linux/bin/AlbertEngine'),f'--rpc-port={o.rpc}'],env=env,stdout=app_log,stderr=app_log);procs.append(app)
    end=time.monotonic()+240
    while f'gRPC: 127.0.0.1:{o.rpc}' not in (out/'app.txt').read_text(errors='replace'):
        if app.poll() is not None or time.monotonic()>end:print((out/'app.txt').read_text(errors='replace')[-2000:]);stop()
        time.sleep(.5)
    video=subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','error','-f','x11grab','-draw_mouse','0','-video_size',o.size,'-framerate',str(o.fps),'-i',f'{o.display}+0,0',
        '-c:v','libx264','-preset','ultrafast','-tune','zerolatency','-pix_fmt','yuv420p','-b:v','3M','-maxrate','3M','-bufsize','3M','-g',str(o.fps),
        '-f','hls','-hls_time','1','-hls_list_size','6','-hls_flags','delete_segments+independent_segments',str(out/'live.m3u8')],stdout=subprocess.DEVNULL);procs.append(video)
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self,*_):pass
    handler=functools.partial(Quiet,directory=str(out))
    server=http.server.ThreadingHTTPServer((o.bind,o.port),handler);threading.Thread(target=server.serve_forever,daemon=True).start()
    print(f'Ao vivo em http://{o.bind}:{o.port}/',flush=True)
    log=open(out/'log.txt','w')
    client=subprocess.Popen([str(build/'linux/src/rpc/albert_flight_client'),f'127.0.0.1:{o.rpc}',*args],stdout=log,stderr=subprocess.STDOUT);procs.append(client)
    client.wait();time.sleep(20);stop() # The last of the picture out.
if __name__=='__main__':main()

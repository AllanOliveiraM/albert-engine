"""Run the download integration test against a local, deterministic HTTP server.

Usage: python3 tests/http_fixture.py build/linux/tests/download_test
Python is only needed for this optional integration test, never by the app/build.
"""

import http.server
import os
import subprocess
import sys
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        if self.path == "/error":
            self.send_error(503, "Temporarily unavailable")
            return
        self.send_response(200)
        self.send_header("Content-Length", "4096")
        self.end_headers()
        try:
            if self.path == "/truncated":
                self.wfile.write(b"x" * 64)
                self.close_connection = True
            elif self.path == "/slow":
                for _ in range(64):
                    self.wfile.write(b"x" * 64)
                    self.wfile.flush()
                    time.sleep(0.05)
            else:
                self.wfile.write(b"x" * 4096)
        except (BrokenPipeError, ConnectionResetError):
            pass


if __name__ == "__main__":
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        env = dict(os.environ, NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")
        try:
            result = subprocess.run(
                [sys.argv[1], f"http://127.0.0.1:{server.server_port}"],
                env=env, check=False, timeout=15,
            )
        finally:
            server.shutdown()
        sys.exit(result.returncode)

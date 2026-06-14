#!/usr/bin/env python3
"""
dev_server.py — static file server for the FluidDial-CYD simulator with live
reload.  Watches every file under simulator/ and pushes a reload to the browser
(over Server-Sent Events) whenever one changes, so editing any screen .js, the
CSS, or index.html refreshes the page automatically.

Zero dependencies (stdlib only).  Run:

    python3 simulator/dev_server.py [port]      # default port 8777

then open http://localhost:<port>.  The page still works when served by any
other static server — js/livereload.js just no-ops if this endpoint is absent.
"""
import http.server
import json
import os
import sys
import threading
import time

import sync  # same-folder helper: colour regen + firmware staleness report

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8777

# Monotonic "generation" bumped by the watcher thread whenever a watched file's
# mtime changes.  SSE connections compare against their last-seen value.
_generation = 0


def _firmware_mtimes():
    sig = []
    for rel in sync.PORT_MAP:
        p = os.path.join(sync.SRC, rel)
        try:
            sig.append(os.stat(p).st_mtime_ns)
        except OSError:
            pass
    return hash(tuple(sig))


def _scan_mtimes():
    sig = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for f in filenames:
            if f.endswith((".py", ".pyc")):
                continue  # don't reload the browser when the server itself changes
            try:
                sig.append(os.stat(os.path.join(dirpath, f)).st_mtime_ns)
            except OSError:
                pass
    return hash(tuple(sig))


def _watch():
    """Watch both the simulator files (reload trigger) and the firmware files
    (run sync: regen colours + refresh the staleness report)."""
    global _generation
    last_sim = _scan_mtimes()
    last_fw = _firmware_mtimes()
    while True:
        time.sleep(0.4)
        cur_fw = _firmware_mtimes()
        if cur_fw != last_fw:
            last_fw = cur_fw
            try:
                sync.regen_colors()  # may rewrite js/colors.js -> picked up below
                _generation += 1  # reload so colour changes show immediately
            except Exception as e:
                print("sync error:", e)
        cur_sim = _scan_mtimes()
        if cur_sim != last_sim:
            last_sim = cur_sim
            _generation += 1


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def log_message(self, *args):
        pass  # quiet

    def end_headers(self):
        # No caching — the whole point of this server is live editing.  Without
        # this the browser heuristically caches js/*.js and serves stale code
        # across reloads, silently defeating live-reload (and any verification).
        self.send_header("Cache-Control", "no-store, max-age=0")
        super().end_headers()

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/__livereload":
            return self._serve_sse()
        if path == "/__sync_status":
            return self._serve_json(sync.compute_report())
        return super().do_GET()

    def _serve_json(self, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        last = _generation
        try:
            self.wfile.write(b": connected\n\n")
            self.wfile.flush()
            while True:
                time.sleep(0.4)
                if _generation != last:
                    last = _generation
                    self.wfile.write(b"data: reload\n\n")
                else:
                    self.wfile.write(b": ping\n\n")  # keep-alive
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass  # browser navigated away / closed the tab


def main():
    threading.Thread(target=_watch, daemon=True).start()
    httpd = http.server.ThreadingHTTPServer(("", PORT), Handler)
    print(f"FluidDial-CYD simulator (live reload) -> http://localhost:{PORT}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

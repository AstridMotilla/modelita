# make serve: the site at http://localhost:8642, like python3 -m http.server,
# but every response says Cache-Control: no-cache, so the browser checks each
# file again and a rebuilt modelita.wasm is never hidden behind a cached one.
# (The deployed site does the same through vercel.json.)
import functools
import http.server
from pathlib import Path


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


site = Path(__file__).resolve().parent / "site"
handler = functools.partial(NoCache, directory=str(site))
http.server.ThreadingHTTPServer(("127.0.0.1", 8642), handler).serve_forever()

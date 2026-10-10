#!/usr/bin/env python3
"""usage: fake-pms.py

Serves a Plex Media Server identity handshake on 127.0.0.1:32400.
"""
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

MACHINE = "0123456789abcdef0123456789abcdef01234567"

IDENTITY = {
    "size": 0, "claimed": True,
    "machineIdentifier": MACHINE, "version": "1.41.0.8992-fake",
}

PROVIDERS = {
    "size": 1, "allowCameraUpload": False, "allowSync": False,
    "friendlyName": "FakePMS", "machineIdentifier": MACHINE,
    "myPlexUsername": "", "platform": "Linux", "platformVersion": "6.1",
    "transcoderVideo": False, "version": "1.41.0.8992-fake",
    "MediaProvider": [{
        "identifier": "com.plexapp.plugins.library",
        "title": "Library", "types": "video,audio,photo",
        "protocols": "stream,download",
        "Feature": [
            {"key": "/library/sections", "type": "content",
             "Directory": [{"hubKey": "/hubs", "id": "home", "title": "Home"}]},
            {"type": "search", "key": "/library/search"},
            {"type": "match"}, {"type": "metadata"}, {"type": "promoted"},
            {"type": "subscribe"}, {"type": "universalsearch"},
            {"type": "timeline", "key": "/:/timeline"},
            {"type": "playqueue", "key": "/playQueues"},
        ],
    }],
}

SECTIONS = {"size": 0, "allowSync": False, "title1": "Plex Library", "Directory": []}
HUBS = {"size": 0, "allowSync": False, "Hub": []}
EMPTY = {"size": 0}

ROUTES = {
    "/identity": IDENTITY,
    "/media/providers": PROVIDERS,
    "/library/sections": SECTIONS,
    "/hubs": HUBS,
    "/": PROVIDERS,
}

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Access-Control-Expose-Headers", "*")

    def do_OPTIONS(self):
        self.send_response(200)
        self._cors()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_HEAD(self):
        self.do_GET(body=False)

    def do_GET(self, body=True):
        path = urlparse(self.path).path
        payload = ROUTES.get(path, EMPTY)
        blob = json.dumps({"MediaContainer": payload}).encode()
        self.send_response(200)
        self._cors()
        self.send_header("Content-Type", "application/json")
        self.send_header("X-Plex-Protocol", "1.0")
        self.send_header("Content-Length", str(len(blob)))
        self.end_headers()
        if body:
            self.wfile.write(blob)
        print(f"  pms {path}", flush=True)

    do_POST = do_PUT = do_GET

    def log_message(self, *a):
        pass

if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 32400), Handler).serve_forever()

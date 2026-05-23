#!/usr/bin/env python3
"""Deterministic local HTTP endpoints for bare-metal Navigator smoke tests."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import gzip


class NavigatorSmokeHandler(BaseHTTPRequestHandler):
    root = Path.cwd()

    def log_message(self, fmt, *args):
        print("%s - - %s" % (self.address_string(), fmt % args), flush=True)

    def write_bytes(self, status, content_type, body, headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        if headers:
            for name, value in headers.items():
                self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def write_redirect(self, status, location):
        self.send_response(status)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/navigator-smoke/basic.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel HTTP Basic</h1><p>basic html body</p></body></html>")
            return
        if path == "/navigator-smoke/final.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel HTTP Final</h1><p>redirect target</p></body></html>")
            return
        if path == "/navigator-smoke/redirect-relative":
            self.write_redirect(302, "/navigator-smoke/final.html")
            return
        if path == "/navigator-smoke/redirect-absolute":
            self.write_redirect(301, "http://10.0.2.2:8080/navigator-smoke/final.html")
            return
        if path == "/navigator-smoke/redirect-loop":
            self.write_redirect(302, "/navigator-smoke/redirect-loop")
            return
        if path == "/navigator-smoke/chunked.html":
            chunks = [b"<html><body><h1>Chunked Kernel HTML</h1>", b"<p>decoded chunk body</p></body></html>"]
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for chunk in chunks:
                self.wfile.write(("%x\r\n" % len(chunk)).encode("ascii"))
                self.wfile.write(chunk + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
            return
        if path == "/navigator-smoke/gzip.html":
            body = gzip.compress(b"<html><body><h1>Compressed</h1></body></html>")
            self.write_bytes(200, "text/html; charset=utf-8", body, {"Content-Encoding": "gzip"})
            return
        if path == "/navigator-smoke/missing.html":
            self.write_bytes(404, "text/html; charset=utf-8",
                             b"<html><body><h1>Missing</h1><p>not found</p></body></html>")
            return

        file_path = (self.root / path.lstrip("/")).resolve()
        try:
            file_path.relative_to(self.root.resolve())
        except ValueError:
            self.write_bytes(403, "text/plain", b"Forbidden")
            return
        if file_path.is_file():
            content_type = "text/html" if file_path.suffix.lower() in (".html", ".htm") else "text/plain"
            self.write_bytes(200, content_type, file_path.read_bytes())
            return
        self.write_bytes(404, "text/html", b"<html><body><h1>Missing</h1></body></html>")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    NavigatorSmokeHandler.root = Path(args.root).resolve()
    server = ThreadingHTTPServer((args.host, args.port), NavigatorSmokeHandler)
    print(f"Navigator kernel HTTP smoke server on {args.host}:{args.port} root={NavigatorSmokeHandler.root}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

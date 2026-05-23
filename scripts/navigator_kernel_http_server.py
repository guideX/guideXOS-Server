#!/usr/bin/env python3
"""Deterministic local HTTP endpoints for bare-metal Navigator smoke tests."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import binascii
import gzip
import struct
import zlib


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)


def make_smoke_png():
    width = 2
    height = 2
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    row_a = b"\x00" + bytes([220, 40, 40, 255, 40, 160, 80, 255])
    row_b = b"\x00" + bytes([40, 100, 220, 255, 245, 210, 70, 255])
    data = zlib.compress(row_a + row_b)
    return b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", data) + png_chunk(b"IEND", b"")


SMOKE_PNG = make_smoke_png()


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
        if path == "/navigator-smoke/image-relative.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Relative PNG</h1><img src=\"logo.png\" alt=\"relative png\"></body></html>")
            return
        if path == "/navigator-smoke/image-absolute.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Absolute PNG</h1><img src=\"http://10.0.2.2:8080/navigator-smoke/logo.png\" alt=\"absolute png\"></body></html>")
            return
        if path == "/navigator-smoke/image-redirect.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Redirect PNG</h1><img src=\"redirect-png\" alt=\"redirect png\"></body></html>")
            return
        if path == "/navigator-smoke/image-chunked.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Chunked PNG</h1><img src=\"chunked.png\" alt=\"chunked png\"></body></html>")
            return
        if path == "/navigator-smoke/image-nonpng.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Non PNG</h1><img src=\"not-png.txt\" alt=\"not png\"></body></html>")
            return
        if path == "/navigator-smoke/logo.png":
            self.write_bytes(200, "image/png", SMOKE_PNG)
            return
        if path == "/navigator-smoke/redirect-png":
            self.write_redirect(302, "/navigator-smoke/logo.png")
            return
        if path == "/navigator-smoke/chunked.png":
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            midpoint = len(SMOKE_PNG) // 2
            for chunk in (SMOKE_PNG[:midpoint], SMOKE_PNG[midpoint:]):
                self.wfile.write(("%x\r\n" % len(chunk)).encode("ascii"))
                self.wfile.write(chunk + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
            return
        if path == "/navigator-smoke/not-png.txt":
            self.write_bytes(200, "text/plain", b"this is not a png")
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

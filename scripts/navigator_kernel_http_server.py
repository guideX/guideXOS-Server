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
INTERACTIVE_FORM_CONTROLS = (
    b"<input type=\"text\" name=\"q\" value=\"\">"
    b"<input type=\"text\" value=\"unnamed control omitted\">"
    b"<input type=\"checkbox\" name=\"agree\" value=\"yes\">"
    b"<input type=\"checkbox\" name=\"omit\" value=\"no\">"
    b"<input type=\"radio\" name=\"kind\" value=\"alpha\">"
    b"<input type=\"radio\" name=\"kind\" value=\"beta\" checked>"
    b"<textarea name=\"note\" rows=\"4\" cols=\"24\"></textarea>"
    b"<select name=\"size\"><option value=\"s\">Small</option><option value=\"m\">Medium</option><option value=\"l\">Large</option></select>"
    b"<input type=\"submit\" value=\"Send\">"
)
EXPECTED_FORM_BODY = b"q=posted+value&agree=yes&kind=alpha&note=hello%0Asecond+line&size=m"


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
        host = self.headers.get("Host", "")
        if path == "/navigator-smoke/basic.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel HTTP Basic</h1><p>basic html body</p></body></html>")
            return
        if path == "/navigator-smoke/forms-post.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Hosted POST Form</h1>"
                             b"<p>Exercises Forms-lite POST v0.1 hosted support.</p>"
                             b"<form method=\"POST\" action=\"/navigator-smoke/post-echo\">"
                             b"<input type=\"text\" name=\"q\" value=\"\">"
                             b"<input type=\"text\" value=\"unnamed control omitted\">"
                             b"<input type=\"checkbox\" name=\"agree\" value=\"yes\" checked>"
                             b"<input type=\"checkbox\" name=\"omit\" value=\"no\">"
                             b"<input type=\"radio\" name=\"kind\" value=\"alpha\" checked>"
                             b"<input type=\"radio\" name=\"kind\" value=\"beta\">"
                             b"<textarea name=\"note\" rows=\"4\" cols=\"24\">hello\nsecond line</textarea>"
                             b"<select name=\"size\"><option value=\"s\">Small</option><option value=\"m\" selected>Medium</option><option value=\"l\">Large</option></select>"
                             b"<input type=\"submit\" value=\"Send\"></form></body></html>")
            return
        if path == "/forms/interactive-post.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Bare-metal interactive POST form</h1>"
                             b"<form method=\"POST\" action=\"/forms/post-echo\">" +
                             INTERACTIVE_FORM_CONTROLS +
                             b"</form></body></html>")
            return
        if path == "/forms/interactive-get.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Bare-metal interactive GET form</h1>"
                             b"<form method=\"GET\" action=\"/forms/get-echo\">" +
                             INTERACTIVE_FORM_CONTROLS +
                             b"</form></body></html>")
            return
        if path == "/forms/get-echo":
            expected_path = b"/forms/get-echo?" + EXPECTED_FORM_BODY
            if self.path.encode("ascii", "replace") != expected_path:
                self.write_bytes(400, "text/plain", b"unexpected encoded GET query")
                return
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Bare-metal GET OK</h1>"
                             b"<p>Exact Forms-lite urlencoded query received.</p></body></html>")
            return
        if path == "/navigator-smoke/host-check.html":
            if host.split(":", 1)[0].lower() != "guidexos.test":
                self.write_bytes(421, "text/html; charset=utf-8",
                                 b"<html><body><h1>Wrong Host</h1><p>expected guidexos.test</p></body></html>")
                return
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel DNS Hostname</h1><p>host header preserved</p></body></html>")
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
        if path == "/navigator-smoke/redirect-hostname":
            self.write_redirect(302, "http://guidexos.test:8080/navigator-smoke/final.html")
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
        if path == "/navigator-smoke/hostname-image.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Hostname PNG</h1><img src=\"logo.png\" alt=\"hostname png\"></body></html>")
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

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length)
        if path == "/forms/post-redirect-303":
            self.write_redirect(303, "/navigator-smoke/final.html")
            return
        if path == "/forms/post-redirect-307":
            self.write_redirect(307, "/forms/post-echo")
            return
        if path == "/forms/post-redirect-hostname":
            self.write_redirect(307, "http://guidexos.test:8080/forms/post-echo")
            return
        if path == "/forms/post-echo":
            expected = EXPECTED_FORM_BODY
            content_type = self.headers.get("Content-Type", "")
            content_length = self.headers.get("Content-Length", "")
            if self.command != "POST":
                self.write_bytes(405, "text/plain", b"expected POST")
                return
            if content_type != "application/x-www-form-urlencoded":
                self.write_bytes(415, "text/plain", b"unexpected content type")
                return
            if content_length != str(len(expected)) or length != len(expected):
                self.write_bytes(400, "text/plain", b"unexpected content length")
                return
            if body != expected:
                self.write_bytes(400, "text/plain", b"unexpected encoded body")
                return
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Bare-metal POST OK</h1>"
                             b"<p>Exact Forms-lite urlencoded body received.</p></body></html>")
            return
        if path == "/navigator-smoke/post-echo":
            escaped = body.decode("utf-8", "replace").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            method = self.command.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            content_type = self.headers.get("Content-Type", "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            content_length = self.headers.get("Content-Length", "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            host = self.headers.get("Host", "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            user_agent = self.headers.get("User-Agent", "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            accept_encoding = self.headers.get("Accept-Encoding", "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            connection = self.headers.get("Connection", "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            page = ("<html><body><h1>POST OK</h1>"
                    "<p>Method: " + method + "</p>"
                    "<p>Content-Type: " + content_type + "</p>"
                    "<p>Content-Length: " + content_length + "</p>"
                    "<p>Host: " + host + "</p>"
                    "<p>User-Agent: " + user_agent + "</p>"
                    "<p>Accept-Encoding: " + accept_encoding + "</p>"
                    "<p>Connection: " + connection + "</p>"
                    "<p>Encoded body:</p><pre>" + escaped + "</pre></body></html>").encode("utf-8")
            self.write_bytes(200, "text/html; charset=utf-8", page)
            return
        self.write_bytes(404, "text/html", b"<html><body><h1>Missing POST</h1></body></html>")


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

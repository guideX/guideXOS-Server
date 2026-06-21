#!/usr/bin/env python3
"""Deterministic local HTTP endpoints for bare-metal Navigator smoke tests."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import binascii
import gzip
import zlib
import ssl
import struct


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
    https_port = None
    http_port = None
    policy_host = "dev.guidexos.test"
    policy_wrong_host = "wrong.guidexos.test"
    public_pilot_host = "public-pilot.guidexos.test"

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
        host_name = host.split(":", 1)[0].lower()
        compat_prefix = "/navigator-smoke/"
        compat_path = path
        if path.startswith("/navigator-policy/") and path not in ("/navigator-policy/ok.html", "/navigator-policy/redirect-downgrade"):
            compat_prefix = "/navigator-policy/"
            compat_path = "/navigator-smoke/" + path[len("/navigator-policy/"):]

        if compat_path == "/navigator-smoke/basic.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel HTTP Basic</h1><p>basic html body</p></body></html>")
            return
        if path == "/navigator-smoke/css-inline.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body style=\"background:#f7f3e8;color:#1f2937;font-size:18px;line-height:1.5;\">"
                             b"<h1 style=\"color:#9a3412;text-align:center;margin:12px 0;\">Inline CSS Heading</h1>"
                             b"<p style=\"background:#dbeafe;padding:8px;margin:12px 0;\">Inline CSS paragraph</p>"
                             b"<a href=\"#\" style=\"color:#1d4ed8;\">Inline CSS link</a>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-style-block.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { background:#fcfaf6; margin:16px; }"
                             b"h1 { color:#7c2d12; font-size:28px; text-align:center; margin:8px 0; }"
                             b"p { margin-top:10px; margin-bottom:14px; line-height:1.6; }"
                             b".hero { background:#fde68a; padding:8px; }"
                             b"#callout { margin-left:24px; padding-left:8px; border-bottom:1px solid #d1d5db; }"
                             b".hidden { display:none; }"
                             b".unsupported .accent { color:#2563eb; }"
                             b"</style></head><body>"
                             b"<section class=\"hero\"><h1>Style Block Heading</h1>"
                             b"<p id=\"callout\">Class and id selectors work.</p>"
                             b"<p class=\"unsupported\"><span class=\"accent\">Unsupported selector fallback stays readable.</span></p>"
                             b"<p class=\"hidden\">Hidden text</p>"
                             b"</section></body></html>")
            return
        if path == "/navigator-smoke/css-external.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><link rel=\"stylesheet\" href=\"/navigator-smoke/css-external.css\"></head><body>"
                             b"<main class=\"panel\"><h1>External CSS Heading</h1>"
                             b"<p>External CSS should load safely when supported.</p></main></body></html>")
            return
        if path == "/navigator-smoke/css-external.css":
            self.write_bytes(200, "text/css; charset=utf-8",
                             b"body { background:#eff6ff; margin:16px; }"
                             b".panel { max-width:480px; margin:0 auto; padding:16px; background:#ffffff; }"
                             b"h1 { color:#1d4ed8; text-align:center; font-size:26px; margin:8px 0 12px; }"
                             b"p { line-height:1.6; }")
            return
        if path == "/navigator-smoke/css-layout.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { background:#f3ecdf; color:#243447; margin:16px; line-height:1.6; }"
                             b"main { max-width:540px; margin-left:auto; margin-right:auto; }"
                             b".panel { background:#ffffff; padding:16px 18px; margin:12px 0; border-bottom:1px solid #d7dde6; }"
                             b"h1 { color:#8a3c1c; text-align:center; margin:12px 0 14px; }"
                             b"h2 { margin:12px 0 8px; }"
                             b"p { margin:10px 0; }"
                             b"ul { margin:10px 0; padding-left:20px; }"
                             b"li { margin:4px 0; }"
                             b"li.nobullet { list-style:none; }"
                             b".note { max-width:420px; margin-left:auto; margin-right:auto; background:#dbeafe; padding:10px 12px; }"
                             b".tight { line-height:1.2; }"
                             b".rule { border-top:1px solid #cbd5e1; padding-top:8px; }"
                             b"</style></head><body>"
                             b"<main>"
                             b"<header class=\"panel\"><h1>Phase 1B Layout</h1><p class=\"note\">Centered content and readable rhythm.</p></header>"
                             b"<section class=\"panel\"><h2>Section Backgrounds</h2><p>Padding should stay inside the colored block.</p>"
                             b"<ul><li>Bullet item one</li><li>Bullet item two</li><li class=\"nobullet\">Bullet suppression works.</li></ul>"
                             b"<p class=\"rule\">Separator line should stay visible.</p>"
                             b"<p class=\"tight\">Line-height should keep the text easy to scan.</p></section>"
                             b"<footer class=\"panel\"><p>Footer spacing should remain comfortable.</p></footer>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/css-external-safety.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head>"
                             b"<link rel=\"stylesheet\" href=\"/navigator-smoke/css-missing.css\">"
                             b"<link rel=\"stylesheet\" href=\"/navigator-smoke/css-not-css.txt\">"
                             b"<link rel=\"stylesheet\" href=\"/navigator-smoke/css-oversized.css\">"
                             b"</head><body><main class=\"safety\">"
                             b"<h1>External CSS Safety</h1>"
                             b"<p>Missing and unsupported stylesheets should not crash rendering.</p>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/css-not-css.txt":
            self.write_bytes(200, "text/plain; charset=utf-8",
                             b"body { background:#fef3c7; }")
            return
        if path == "/navigator-smoke/css-oversized.css":
            filler = (b"/* oversized filler */\n" * 1200)
            self.write_bytes(200, "text/css; charset=utf-8",
                             b"body { background:#eff6ff; margin:16px; }"
                             b".safety { max-width:480px; margin:0 auto; padding:16px; background:#ffffff; }"
                             b"h1 { color:#1d4ed8; text-align:center; font-size:26px; margin:8px 0 12px; }"
                             b"p { line-height:1.6; }"
                             b".clamp-test { margin:9999px auto; padding:9999px; line-height:0.1; width:9999px; max-width:9999px; }"
                             b".unsupported-thing { display:grid; grid-template-columns:1fr 1fr; }" + filler)
            return
        if path == "/navigator-smoke/css-unsupported.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"@media screen and (min-width: 500px) { h1 { color:#dc2626; } }"
                             b".gridish { display:grid; grid-template-columns:1fr 1fr; gap:8px; }"
                             b".nested .accent { color:#059669; }"
                             b".hidden { display:none; }"
                             b"</style></head><body>"
                             b"<div class=\"gridish\"><h1>Unsupported CSS</h1><p>Grid-like CSS should not break rendering.</p></div>"
                             b"<div class=\"nested\"><span class=\"accent\">Nested selectors are optional.</span></div>"
                             b"<p class=\"hidden\">This hidden text must stay hidden.</p>"
                             b"</body></html>")
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
            if host_name != "guidexos.test":
                self.write_bytes(421, "text/html; charset=utf-8",
                                 b"<html><body><h1>Wrong Host</h1><p>expected guidexos.test</p></body></html>")
                return
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel DNS Hostname</h1><p>host header preserved</p></body></html>")
            return
        if path == "/navigator-smoke/tls-basic.html":
            if host_name != "guidexos.test":
                self.write_bytes(421, "text/html; charset=utf-8",
                                 b"<html><body><h1>Wrong TLS Host</h1><p>expected guidexos.test</p></body></html>")
                return
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel TLS Basic</h1><p>local handshake ok</p></body></html>")
            return
        if compat_path == "/navigator-smoke/plain.txt":
            self.write_bytes(200, "text/plain; charset=utf-8",
                             b"Navigator HTTPS text fixture\nsecond line\n")
            return
        if path in ("/navigator-policy/ok.html", "/policy-validated/ok.html"):
            expected = self.policy_host.lower()
            if host_name != expected:
                self.write_bytes(421, "text/html; charset=utf-8",
                                 ("<html><body><h1>Wrong TLS Host</h1><p>expected %s</p></body></html>" % expected).encode("utf-8"))
                return
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Policy Validated TLS OK</h1><p>explicit HTTPS policy navigation exercised</p></body></html>")
            return
        if path in ("/navigator-policy/redirect-downgrade", "/policy-validated/redirect-downgrade"):
            port = self.http_port or 8080
            self.write_redirect(302, f"http://10.0.2.2:{port}/navigator-smoke/insecure-downgrade")
            return
        if path == "/navigator-public-pilot/ok.html":
            expected = self.public_pilot_host.lower()
            if host_name != expected:
                self.write_bytes(421, "text/html; charset=utf-8",
                                 ("<html><body><h1>Wrong TLS Host</h1><p>expected %s</p></body></html>" % expected).encode("utf-8"))
                return
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Public HTTPS Pilot OK</h1><p>controlled public HTTPS pilot fixture exercised</p></body></html>")
            return
        if path == "/navigator-public-pilot/redirect-downgrade":
            port = self.http_port or 8080
            self.write_redirect(302, f"http://10.0.2.2:{port}/navigator-smoke/insecure-downgrade")
            return
        if compat_path == "/navigator-smoke/final.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Kernel HTTP Final</h1><p>redirect target</p></body></html>")
            return
        if compat_path == "/navigator-smoke/redirect-relative":
            self.write_redirect(302, f"{compat_prefix}final.html")
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
        if compat_path == "/navigator-smoke/tls-redirect-relative":
            self.write_redirect(302, f"{compat_prefix}final.html")
            return
        if compat_path == "/navigator-smoke/tls-redirect-absolute":
            request_host = host.split(":", 1)[0]
            if self.https_port:
                self.write_redirect(301, f"https://{request_host}:{self.https_port}{compat_prefix}final.html")
            else:
                self.write_redirect(301, f"https://{request_host}{compat_prefix}final.html")
            return
        if compat_path == "/navigator-smoke/tls-redirect-loop":
            self.write_redirect(302, f"{compat_prefix}tls-redirect-loop")
            return
        if path == "/navigator-smoke/redirect-to-https":
            if self.https_port:
                redirect_host = "guidexos.test"
                redirect_path = "/navigator-smoke/tls-basic.html"
                request_host = host.split(":", 1)[0].lower()
                if request_host in ("127.0.0.1", "localhost"):
                    redirect_host = "localhost"
                    redirect_path = "/navigator-smoke/final.html"
                self.write_redirect(302, f"https://{redirect_host}:{self.https_port}{redirect_path}")
            else:
                self.write_redirect(302, "https://example.com/secure")
            return
        if path == "/navigator-smoke/redirect-to-policy-validated-https":
            if self.https_port:
                self.write_redirect(302, f"https://{self.policy_host}:{self.https_port}/navigator-policy/ok.html")
            else:
                self.write_redirect(302, "https://example.com/secure")
            return
        if path == "/navigator-smoke/redirect-to-policy-disallowed-https":
            if self.https_port:
                self.write_redirect(302, f"https://{self.policy_wrong_host}:{self.https_port}/navigator-policy/ok.html")
            else:
                self.write_redirect(302, "https://example.com/secure")
            return
        if path == "/navigator-smoke/redirect-to-numeric-https":
            if self.https_port:
                self.write_redirect(302, f"https://10.0.2.2:{self.https_port}/navigator-smoke/tls-basic.html")
            else:
                self.write_redirect(302, "https://10.0.2.2/secure")
            return
        if path == "/navigator-smoke/redirect-to-public-https":
            self.write_redirect(302, "https://example.com/secure")
            return
        if path == "/navigator-smoke/redirect-to-public-pilot-https":
            if self.https_port:
                self.write_redirect(302, f"https://{self.public_pilot_host}:{self.https_port}/navigator-public-pilot/ok.html")
            else:
                self.write_redirect(302, "https://example.com/secure")
            return
        if path == "/navigator-smoke/redirect-downgrade":
            port = self.http_port or 8080
            self.write_redirect(302, f"http://127.0.0.1:{port}/navigator-smoke/insecure-downgrade")
            return
        if path == "/navigator-smoke/insecure-downgrade":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Insecure downgrade target reached</h1></body></html>")
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
        if compat_path == "/navigator-smoke/gzip.html":
            body = gzip.compress(b"<html><body><h1>Compressed</h1></body></html>")
            self.write_bytes(200, "text/html; charset=utf-8", body, {"Content-Encoding": "gzip"})
            return
        if compat_path == "/navigator-smoke/br.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"not-really-brotli",
                             {"Content-Encoding": "br"})
            return
        if compat_path == "/navigator-smoke/deflate.html":
            body = zlib.compress(b"<html><body><h1>Deflate</h1></body></html>")
            self.write_bytes(200, "text/html; charset=utf-8", body, {"Content-Encoding": "deflate"})
            return
        if compat_path == "/navigator-smoke/missing.html":
            self.write_bytes(404, "text/html; charset=utf-8",
                             b"<html><body><h1>Missing</h1><p>not found</p></body></html>")
            return
        if compat_path == "/navigator-smoke/error-500.html":
            self.write_bytes(500, "text/html; charset=utf-8",
                             b"<html><body><h1>Server Error</h1><p>fixture 500</p></body></html>")
            return
        if compat_path == "/navigator-smoke/large-body.txt":
            self.write_bytes(200, "text/plain; charset=utf-8", b"A" * (264 * 1024))
            return
        if compat_path == "/navigator-smoke/large-headers.html":
            headers = {}
            for index in range(8):
                headers[f"X-Smoke-{index:02d}"] = "B" * 4096
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Large Headers</h1></body></html>", headers)
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
        if compat_path == "/navigator-smoke/download.bin":
            self.write_bytes(200, "application/octet-stream", b"guideXOS download smoke\n")
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


class NavigatorTlsSmokeServer(ThreadingHTTPServer):
    def __init__(self, server_address, request_handler_class, ssl_context):
        super().__init__(server_address, request_handler_class)
        self._ssl_context = ssl_context

    def get_request(self):
        raw_socket, client_address = super().get_request()
        print("TLS accept from %s:%s" % (client_address[0], client_address[1]), flush=True)
        try:
            tls_socket = self._ssl_context.wrap_socket(raw_socket, server_side=True)
            print(
                "TLS handshake ok from %s:%s sni=%s protocol=%s cipher=%s"
                % (
                    client_address[0],
                    client_address[1],
                    getattr(tls_socket, "_guidexos_sni", None),
                    tls_socket.version(),
                    tls_socket.cipher()[0] if tls_socket.cipher() else None,
                ),
                flush=True,
            )
            return tls_socket, client_address
        except ssl.SSLError as exc:
            print(
                "TLS handshake failed from %s:%s error=%s"
                % (client_address[0], client_address[1], exc),
                flush=True,
            )
            raw_socket.close()
            raise


def main():
    def build_ssl_context(cert_path, key_path):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.options |= getattr(ssl, "OP_NO_COMPRESSION", 0)
        context.options |= getattr(ssl, "OP_NO_TICKET", 0)
        context.set_ciphers("ECDHE-RSA-AES128-GCM-SHA256")
        context.load_cert_chain(cert_path, key_path)
        return context

    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--https-port", type=int)
    parser.add_argument("--http-port", type=int)
    parser.add_argument("--tls-cert")
    parser.add_argument("--tls-key")
    parser.add_argument("--local-tls-cert")
    parser.add_argument("--local-tls-key")
    parser.add_argument("--policy-host", default="dev.guidexos.test")
    parser.add_argument("--policy-wrong-host", default="wrong.guidexos.test")
    parser.add_argument("--policy-tls-cert")
    parser.add_argument("--policy-tls-key")
    parser.add_argument("--public-pilot-host", default="public-pilot.guidexos.test")
    parser.add_argument("--public-pilot-tls-cert")
    parser.add_argument("--public-pilot-tls-key")
    args = parser.parse_args()
    NavigatorSmokeHandler.root = Path(args.root).resolve()
    NavigatorSmokeHandler.https_port = args.https_port
    NavigatorSmokeHandler.http_port = args.http_port
    NavigatorSmokeHandler.policy_host = args.policy_host
    NavigatorSmokeHandler.policy_wrong_host = args.policy_wrong_host
    NavigatorSmokeHandler.public_pilot_host = args.public_pilot_host
    server = None
    local_tls_cert = args.local_tls_cert or args.tls_cert
    local_tls_key = args.local_tls_key or args.tls_key
    if local_tls_cert or local_tls_key or args.policy_tls_cert or args.policy_tls_key or args.public_pilot_tls_cert or args.public_pilot_tls_key:
        if not local_tls_cert or not local_tls_key:
            parser.error("--local-tls-cert/--local-tls-key (or --tls-cert/--tls-key) must be supplied together")
        if bool(args.policy_tls_cert) != bool(args.policy_tls_key):
            parser.error("--policy-tls-cert and --policy-tls-key must be supplied together")
        if bool(args.public_pilot_tls_cert) != bool(args.public_pilot_tls_key):
            parser.error("--public-pilot-tls-cert and --public-pilot-tls-key must be supplied together")
        local_context = build_ssl_context(local_tls_cert, local_tls_key)
        policy_context = build_ssl_context(args.policy_tls_cert, args.policy_tls_key) if args.policy_tls_cert else None
        public_pilot_context = build_ssl_context(args.public_pilot_tls_cert, args.public_pilot_tls_key) if args.public_pilot_tls_cert else None

        def on_server_name(sock, server_name, _ctx):
            setattr(sock, "_guidexos_sni", server_name)
            chosen = local_context
            if policy_context and server_name and server_name.lower() == args.policy_host.lower():
                chosen = policy_context
            elif public_pilot_context and server_name and server_name.lower() == args.public_pilot_host.lower():
                chosen = public_pilot_context
            sock.context = chosen
            print(f"TLS clienthello sni={server_name}", flush=True)

        local_context.set_servername_callback(on_server_name)
        server = NavigatorTlsSmokeServer((args.host, args.port), NavigatorSmokeHandler, local_context)
    else:
        server = ThreadingHTTPServer((args.host, args.port), NavigatorSmokeHandler)
    scheme = "HTTPS" if local_tls_cert else "HTTP"
    print(f"Navigator kernel {scheme} smoke server on {args.host}:{args.port} root={NavigatorSmokeHandler.root}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

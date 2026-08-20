#!/usr/bin/env python3
"""Deterministic local HTTP endpoints for bare-metal Navigator smoke tests."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import binascii
import gzip
import zlib
import ssl
import socket
import struct
import os


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)


def make_smoke_png(width, height):
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            row.extend([
                (40 + (x * 29) + (y * 13)) % 256,
                (90 + (x * 11) + (y * 31)) % 256,
                (170 + (x * 17) + (y * 19)) % 256,
                255,
            ])
        rows.append(bytes(row))
    data = zlib.compress(b"".join(rows))
    return b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", data) + png_chunk(b"IEND", b"")


SMOKE_PNG = make_smoke_png(2, 2)
SMOKE_WIDE_PNG = make_smoke_png(640, 160)
SMOKE_TALL_PNG = make_smoke_png(160, 640)
CANONICAL_TLS12_SUITES = {
    0xC02F: "TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256",
    0xC030: "TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384",
    0xC02B: "TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256",
    0xC02C: "TLS-ECDHE-ECDSA-WITH-AES-256-GCM-SHA384",
}
TLS_SIGNALING_SUITES = {0x00FF}
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

    def write_fragmented_response(self, fragments, close=False):
        """Write a deliberately segmented HTTP/1.1 response.

        The ordinary helper above exercises normal BaseHTTPRequestHandler
        buffering. These endpoints intentionally split header lines, framing
        markers, and body bytes so the kernel HTTP reader is tested against
        legal segmentation rather than a convenient local packet shape.
        """
        for fragment in fragments:
            self.connection.sendall(fragment)
        if close:
            self.close_connection = True

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
        if path == "/navigator-smoke/css-wrappers.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { background:#f4efe6; color:#243447; margin:16px; }"
                             b"main { max-width:560px; margin-left:auto; margin-right:auto; }"
                             b"header, article, section, nav, aside, footer, div { background:#ffffff; padding:12px 14px; margin:12px 0; border-top:1px solid #d8dee9; border-bottom:1px solid #d8dee9; }"
                             b".note { max-width:420px; margin-left:auto; margin-right:auto; background:#dbeafe; padding:10px 12px; }"
                             b"</style></head><body><main>"
                             b"<header><h1>Wrapper Layout</h1><p class=\"note\">Centered wrappers stay readable.</p></header>"
                             b"<article><section><p>Article and section wrappers should behave like blocks.</p></section>"
                             b"<nav><p>Navigation wrapper remains visible.</p></nav>"
                             b"<aside><p>Aside wrapper remains visible.</p></aside></article>"
                             b"<footer><p>Footer wrapper remains visible.</p></footer>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/css-table.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { margin:16px; background:#f8fafc; }"
                             b"table { max-width:520px; margin-left:auto; margin-right:auto; background:#ffffff; border-top:1px solid #94a3b8; border-bottom:1px solid #94a3b8; }"
                             b"caption { text-align:center; margin:6px 0 8px; }"
                             b"th { background:#dbeafe; padding:4px 6px; }"
                             b"td { padding:4px 6px; }"
                             b"</style></head><body><main><table>"
                             b"<caption>Simple Table</caption>"
                             b"<thead><tr><th>Name</th><th>Value</th></tr></thead>"
                             b"<tbody><tr><td>Alpha</td><td>One</td></tr>"
                             b"<tr><td>Beta</td><td>Two</td></tr></tbody>"
                             b"</table></main></body></html>")
            return
        if path == "/navigator-smoke/css-table-wide.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { margin:16px; background:#f3f4f6; }"
                             b"table { max-width:320px; margin-left:auto; margin-right:auto; background:#ffffff; border-top:1px solid #64748b; border-bottom:1px solid #64748b; }"
                             b"th, td { padding:4px 6px; }"
                             b"th { background:#e2e8f0; }"
                             b"</style></head><body><main><table>"
                             b"<thead><tr><th>Northwind</th><th>Southbound</th><th>Eastward</th><th>Westward</th></tr></thead>"
                             b"<tbody><tr><td>Alpha Beta Gamma Delta</td><td>One Two Three Four</td><td>Long Long Long Cell</td><td>Wrap Safe</td></tr>"
                             b"<tr><td>Secondary Row</td><td>More Width Pressure</td><td>Keep It Stable</td><td>Still Readable</td></tr></tbody>"
                             b"</table></main></body></html>")
            return
        if path == "/navigator-smoke/css-inline-polish.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body style=\"margin:16px;background:#f8fafc;color:#1f2937;line-height:1.5;\">"
                             b"<main style=\"max-width:520px;margin-left:auto;margin-right:auto;padding:12px 14px;background:#ffffff;\">"
                             b"<h1 style=\"margin:8px 0;color:#9a3412;text-align:center;\">Inline Text Polish</h1>"
                             b"<p><span style=\"color:#b91c1c;\">Span color</span> "
                             b"<strong>Strong text</strong> "
                             b"<b>Bold text</b> "
                             b"<em>Emphasis text</em> "
                             b"<i>Italic text</i> "
                             b"<code style=\"background:#e5e7eb;\">code sample</code> "
                             b"<a href=\"#\" style=\"color:#1d4ed8;\">Link text</a></p>"
                             b"<hr>"
                             b"<p>Below the separator.</p>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/css-inline-1d.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body style=\"margin:16px;background:#f6f4ee;color:#243447;line-height:1.55;\">"
                             b"<main style=\"max-width:540px;margin-left:auto;margin-right:auto;padding:14px 16px;background:#ffffff;\">"
                             b"<h1 style=\"margin:8px 0;color:#8a3c1c;text-align:center;\">Phase 1D Inline Text</h1>"
                             b"<p><span style=\"color:#b91c1c;\">Span color</span></p>"
                             b"<p><strong>Strong text</strong></p>"
                             b"<p><b>Bold text</b></p>"
                             b"<p><em>Emphasis text</em></p>"
                             b"<p><i>Italic text</i></p>"
                             b"<p><small>Small text</small></p>"
                             b"<p><code>code sample</code></p>"
                             b"<p><a href=\"/navigator-smoke/basic.html\">Link text</a></p>"
                             b"<p>Line one<br>Line two</p>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/text-polish.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body style=\"margin:16px;background:#f5f1e8;color:#243447;line-height:1.55;\">"
                             b"<main style=\"max-width:620px;margin-left:auto;margin-right:auto;padding:14px 16px;background:#ffffff;\">"
                             b"<article style=\"background:#f8fafc;padding:12px 14px;border-top:1px solid #d6dce8;border-bottom:1px solid #d6dce8;\">"
                             b"<h1 style=\"margin:8px 0 12px;color:#7c2d12;text-align:center;line-height:1.1;\">Text Polish g j p q y</h1>"
                             b"<p>Descenders stay readable in this sentence: g j p q y.</p>"
                             b"<p>Normal text sits beside <small>small text next to normal text g j p q y</small> in the same line.</p>"
                             b"<p><a href=\"/navigator-smoke/basic.html\">Underlined link smoke marker g j p q y</a> and <code style=\"background:#e5e7eb;\">inline code g j p q y</code>.</p>"
                             b"<p><strong>Bold g j p q y</strong> <em>Italic g j p q y</em> <strong><em>Faux bold italic g j p q y</em></strong></p>"
                             b"<table style=\"max-width:520px;margin-left:auto;margin-right:auto;background:#ffffff;border-top:1px solid #94a3b8;border-bottom:1px solid #94a3b8;\">"
                             b"<caption style=\"padding:4px 0;\">Caption descenders g j p q y</caption>"
                             b"<thead><tr><th style=\"background:#dbeafe;padding:4px 6px;\">Name</th><th style=\"background:#dbeafe;padding:4px 6px;\">Value</th><th style=\"background:#dbeafe;padding:4px 6px;\">Notes</th></tr></thead>"
                             b"<tbody><tr><td style=\"padding:4px 6px;\">Alpha g j p q y</td><td style=\"padding:4px 6px;\">One g j p q y</td><td style=\"padding:4px 6px;\">table cell g j p q y</td></tr>"
                             b"<tr><td style=\"padding:4px 6px;\">Beta g j p q y</td><td style=\"padding:4px 6px;\">Two g j p q y</td><td style=\"padding:4px 6px;\">more descenders g j p q y</td></tr></tbody></table>"
                             b"<ul><li>List item with descenders g j p q y.</li></ul>"
                             b"<hr style=\"border-top:1px solid #cbd5e1;margin:12px 0;\">"
                             b"<pre style=\"margin:0;line-height:1.4;\">pre line one g j p q y\npre line two g j p q y</pre>"
                             b"</article></main></body></html>")
            return
        if path == "/navigator-smoke/css-table-1d.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body style=\"margin:16px;background:#f4f6fb;color:#243447;line-height:1.5;\">"
                             b"<main style=\"max-width:620px;margin-left:auto;margin-right:auto;padding:12px 14px;background:#ffffff;\">"
                             b"<article style=\"background:#f8fafc;padding:12px 14px;border-top:1px solid #d6dce8;border-bottom:1px solid #d6dce8;\">"
                             b"<h1 style=\"margin:8px 0 10px;color:#7c2d12;text-align:center;\">Phase 1D Table</h1>"
                             b"<table style=\"max-width:520px;margin-left:auto;margin-right:auto;background:#ffffff;border-top:1px solid #94a3b8;border-bottom:1px solid #94a3b8;\">"
                             b"<caption>Navigator Table Caption</caption>"
                             b"<thead><tr><th style=\"background:#dbeafe;\">Name</th><th style=\"background:#dbeafe;\">Value</th><th style=\"background:#dbeafe;\">Notes</th></tr></thead>"
                             b"<tbody>"
                             b"<tr><td>Alpha</td><td>One</td><td><a href=\"/navigator-smoke/basic.html\">linked cell</a></td></tr>"
                             b"<tr><td>Beta</td><td>Two</td><td><code>code sample</code></td></tr>"
                             b"<tr><td>Gamma</td><td>Three</td><td><small>small note</small></td></tr>"
                             b"</tbody></table>"
                             b"<hr>"
                             b"<section style=\"background:#fff7ed;padding:10px 12px;\">"
                             b"<p>Wrapper spacing stays readable.</p>"
                             b"</section>"
                             b"</article></main></body></html>")
            return
        if path == "/navigator-smoke/css-hr.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { margin:16px; background:#f8fafc; line-height:1.5; }"
                             b"main { max-width:520px; margin-left:auto; margin-right:auto; padding:12px 14px; background:#ffffff; }"
                             b"hr { border-top:1px solid #cbd5e1; margin:12px 0; }"
                             b"</style></head><body><main>"
                             b"<p>Above the horizontal rule.</p>"
                             b"<hr>"
                             b"<p>Below the horizontal rule.</p>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/css-phase1e.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { margin:16px; background:#f4efe6; color:#243447; line-height:1.55; }"
                             b"main { max-width:560px; margin-left:auto; margin-right:auto; padding:12px 14px; background:#ffffff; }"
                             b"figure, blockquote, dl, section, article, div { background:#f8fafc; padding:10px 12px; margin:12px 0; border-top:1px solid #d6dce8; border-bottom:1px solid #d6dce8; }"
                             b"figure { max-width:100%; margin-left:auto; margin-right:auto; text-align:center; }"
                             b"figcaption { color:#5b6472; font-style:italic; font-size:13px; margin:4px 0 6px; }"
                             b"blockquote { margin-left:18px; padding-left:12px; background:#f1f5f9; }"
                             b"dl { margin:8px 0 10px; }"
                             b"dt { font-weight:bold; margin-top:6px; margin-bottom:2px; }"
                             b"dd { margin-left:18px; margin-bottom:6px; }"
                             b".fit { max-width:100%; margin-left:auto; margin-right:auto; }"
                             b".width-only { margin-left:auto; margin-right:auto; }"
                             b".height-only { margin-left:auto; margin-right:auto; }"
                             b".clamped { margin-left:auto; margin-right:auto; }"
                             b".missing { margin-left:auto; margin-right:auto; }"
                             b".wrapsafe { overflow-wrap:break-word; word-wrap:break-word; white-space:normal; }"
                             b".breakall { word-break:break-all; }"
                             b".prewrap { white-space:pre-wrap; }"
                             b".narrow { max-width:60px; margin-left:auto; margin-right:auto; background:#fff7ed; padding:8px 10px; }"
                             b".narrow .clamp { max-width:48px; margin-left:auto; margin-right:auto; background:#eef2ff; padding:6px; }"
                             b".unsupported { display:grid; grid-template-columns:1fr 1fr; position:absolute; transform:translateX(10px); transition:all 1s; }"
                             b"</style></head><body><main>"
                             b"<h1>Phase 1E Media and Text</h1>"
                             b"<p class=\"wrapsafe\">Long URL marker https://example.com/really/long/path/that/should/wrap/safely/because/it/is/way/too/long/for/the/content/box.</p>"
                             b"<p class=\"breakall\">Break-all marker: abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789</p>"
                             b"<p class=\"prewrap\">Pre-wrap marker line one\nline two with long token supercalifragilisticexpialidocioussupercalifragilisticexpialidocious</p>"
                             b"<figure><figcaption>Max-width 100 percent figure marker</figcaption><img class=\"fit\" src=\"/navigator-smoke/wide.png\" alt=\"Max-width 100 percent figure marker\"></figure>"
                             b"<figure><figcaption>Width-only aspect ratio marker</figcaption><img class=\"width-only\" src=\"/navigator-smoke/wide.png\" width=\"180\" alt=\"Width-only aspect ratio marker\"></figure>"
                             b"<figure><figcaption>Height-only aspect ratio marker</figcaption><img class=\"height-only\" src=\"/navigator-smoke/tall.png\" height=\"180\" alt=\"Height-only aspect ratio marker\"></figure>"
                             b"<figure><figcaption>Max-height aspect marker</figcaption><img class=\"height-only\" src=\"/navigator-smoke/tall.png\" style=\"max-height:180px;\" alt=\"Max-height aspect marker\"></figure>"
                             b"<figure><figcaption>Oversized image clamped to content width marker</figcaption><img class=\"clamped\" src=\"/navigator-smoke/wide.png\" width=\"4096\" height=\"1024\" alt=\"Oversized image clamped to content width marker\"></figure>"
                             b"<figure><figcaption>Malformed or huge dimensions are clamped marker</figcaption><img class=\"clamped\" src=\"/navigator-smoke/wide.png\" width=\"99999\" height=\"99999\" alt=\"Malformed or huge dimensions are clamped marker\"></figure>"
                             b"<figure><figcaption>Missing image alt fallback marker</figcaption><img class=\"missing\" src=\"/navigator-smoke/missing.png\" width=\"360\" height=\"120\" alt=\"Missing image alt fallback marker\"></figure>"
                             b"<blockquote><p>Blockquote marker line.</p><blockquote><p>Nested blockquote marker line.</p></blockquote><cite><p>Citation marker.</p></cite><q><p>q marker.</p></q></blockquote>"
                             b"<dl><dt>Definition term alpha</dt><dd>Definition detail one with <a href=\"/navigator-smoke/basic.html\">a link</a>.</dd><dt>Definition term beta</dt><dd>Definition detail two.</dd></dl>"
                             b"<section class=\"narrow\"><div class=\"narrow\"><article class=\"narrow\"><div class=\"clamp\">Nested wrapper backgrounds and padding marker.</div></article></div></section>"
                             b"<p class=\"unsupported\">Unsupported properties remain nonfatal.</p>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/css-phase1f.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><style>"
                             b"body { margin:16px; background:#f4efe6; color:#243447; line-height:1.55; }"
                             b"main { max-width:620px; margin-left:auto; margin-right:auto; }"
                             b".shorthand-a { border: 1px solid #8ea2b5; padding:8px 10px; margin:10px 0; background:#ffffff; }"
                             b".shorthand-b { border: solid 1px #4b5563; padding:8px 10px; margin:10px 0; background:#f8fafc; }"
                             b".shorthand-c { border: #7b8794 solid 1px; padding:8px 10px; margin:10px 0; background:#ffffff; }"
                             b".per-side { border-top: 3px dashed #0f766e; border-right: 2px dotted #b91c1c; border-bottom: 4px solid #1d4ed8; border-left: 2px solid #7c3aed; padding:8px 10px; margin:10px 0; background:#f8fafc; }"
                             b".thick-clamp { border: 999px solid #111827; padding:4px 8px; margin:10px 0; background:#ffffff; }"
                             b".malformed { border: 3px solid #64748b bogus-token; padding:4px 8px; margin:10px 0; background:#ffffff; }"
                             b"blockquote { border-left:4px solid #94a3b8; margin:12px 0; padding:8px 12px; background:#f8fafc; }"
                             b"figure { border:2px solid #cbd5e1; margin:12px 0; padding:8px 10px; background:#ffffff; }"
                             b"figcaption { border-top:1px solid #cbd5e1; margin-top:6px; padding-top:4px; text-align:center; color:#5b6472; font-style:italic; }"
                             b"pre { border:2px solid #cbd5e1; margin:0; padding:8px 10px; background:#f8fafc; font-family: monospace; }"
                             b"code { border:2px solid #cbd5e1; margin:0; padding:2px 4px; background:#f8fafc; font-family: monospace; }"
                             b"table.collapse { border-collapse: collapse; border:2px solid #64748b; width:100%; margin:12px 0; }"
                             b"table.collapse th, table.collapse td { border:1px solid #94a3b8; padding:4px 6px; }"
                             b"table.separate { border-collapse: separate; border-spacing: 999px 4px; border:2px solid #64748b; width:100%; margin:12px 0; background:#ffffff; }"
                             b"table.separate th, table.separate td { border:1px solid #94a3b8; padding:4px 6px; background:#f8fafc; }"
                             b"table.wide { border-collapse: separate; border-spacing: 8px 4px; border:1px solid #64748b; max-width:320px; margin:12px 0; background:#ffffff; }"
                             b"table.wide th, table.wide td { border:1px solid #94a3b8; padding:4px 6px; background:#ffffff; }"
                             b"ul.disc { list-style: disc; padding-left:28px; margin:8px 0; }"
                             b"ul.circle { list-style: circle; padding-left:28px; margin:8px 0; }"
                             b"ul.square { list-style: square; padding-left:28px; margin:8px 0; }"
                             b"ol.decimal { list-style: decimal; padding-left:28px; margin:8px 0; }"
                             b"ol.alpha { list-style: lower-alpha; padding-left:28px; margin:8px 0; }"
                             b"ol.upper { list-style: upper-alpha; padding-left:28px; margin:8px 0; }"
                             b"ul.none { list-style: none; padding-left:18px; margin:8px 0; }"
                             b"li.wrap { max-width:280px; }"
                             b".underline { text-decoration: underline; }"
                             b".strike { text-decoration: line-through; }"
                             b".sans { font-family: Arial, sans-serif; }"
                             b".mono { font-family: \"Unavailable Font\", monospace; }"
                             b".serif { font-family: Georgia, serif; }"
                             b"</style></head><body><main>"
                             b"<h1>Phase 1F Box and Text Fidelity</h1>"
                             b"<p class=\"shorthand-a\">Wrapper with border shorthand 1px solid #888 marker.</p>"
                             b"<p class=\"shorthand-b\">Wrapper with border shorthand solid 1px black marker.</p>"
                             b"<p class=\"shorthand-c\">Wrapper with border shorthand #888 solid 1px marker.</p>"
                             b"<div class=\"per-side\">Per-side border marker with dashed and dotted edges.</div>"
                             b"<div class=\"thick-clamp\">Oversized border width clamp marker.</div>"
                             b"<div class=\"malformed\">Malformed border declaration ignored safely marker.</div>"
                             b"<blockquote>Bordered blockquote marker with wrapped text that should stay readable across lines.</blockquote>"
                             b"<figure><div>Figure marker content.</div><figcaption>Figcaption marker stays spaced away from the figure border.</figcaption></figure>"
                             b"<pre>Preformatted border marker with g j p q y and code sample.</pre>"
                             b"<code>Code block border marker with monospace fallback.</code>"
                             b"<table class=\"collapse\"><tr><th>Collapse</th><th>Grid</th><th>Marker</th></tr><tr><td>Collapse border cell</td><td>Second cell</td><td>Third cell</td></tr></table>"
                             b"<table class=\"separate\"><caption>Caption spacing marker</caption><tr><th>Separate</th><th>Spacing</th><th>Marker</th></tr><tr><td>Separate border cell</td><td>Second cell</td><td class=\"strike\">Decorated cell marker</td></tr></table>"
                             b"<table class=\"wide\"><tr><th>Wide</th><th>Table</th><th>Fallback</th><th>Bounded</th></tr><tr><td>Northwind Example</td><td>Southbound Example</td><td>Eastward Example</td><td>Westward Example</td></tr></table>"
                             b"<ul class=\"disc\"><li class=\"wrap\">Disc list marker text wraps across lines so the next line stays aligned with the content.</li><li><ul class=\"circle\"><li class=\"wrap\">Circle nested marker text wraps across lines and keeps bounded indentation.</li><li><ul class=\"square\"><li class=\"wrap\">Square nested marker text wraps across lines and keeps bounded indentation.</li></ul></li></ul></li></ul>"
                             b"<ol class=\"decimal\"><li class=\"wrap\">Decimal list marker wraps across lines and keeps numbering stable.</li><li class=\"wrap\">Second decimal item to show stable numbering.</li></ol>"
                             b"<ol class=\"alpha\"><li class=\"wrap\">Lower alpha marker wraps across lines and stays readable.</li><li class=\"wrap\">Second lower alpha item remains bounded.</li></ol>"
                             b"<ol class=\"upper\"><li class=\"wrap\">Upper alpha marker wraps across lines and stays readable.</li></ol>"
                             b"<ul class=\"none\"><li class=\"wrap\">List style none item stays readable without markers.</li></ul>"
                             b"<p class=\"underline\">Explicit underline marker text.</p>"
                             b"<p class=\"strike\">Line-through marker text.</p>"
                             b"<p><a href=\"/navigator-smoke/basic.html\" style=\"text-decoration:none;\">Link without underline marker</a> and <a href=\"/navigator-smoke/basic.html\">Default underlined link marker</a>.</p>"
                             b"<table class=\"collapse\"><tr><td>Decorated cell marker</td><td class=\"strike\">Strike inside cell marker</td><td class=\"mono\">Monospace cell marker</td></tr></table>"
                             b"<p class=\"sans\">Sans-serif font family marker.</p>"
                             b"<p class=\"mono\">Unavailable font followed by monospace marker.</p>"
                             b"<p class=\"serif\">Serif fallback marker.</p>"
                             b"</main></body></html>")
            return
        if path == "/navigator-smoke/css-phase3b.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3B Inline Layout</title><style>"
                             b"body { color:#243447; background:#f8fafc; font-size:16px; line-height:1.35; }"
                             b"p { width:300px; margin:4px 0; padding:2px; }"
                             b"#phase3b-basic { border:1px solid #64748b; }"
                             b"#phase3b-link { width:190px; } #phase3b-link a { color:#1e5cb8; background:#dbeafe; padding:1px; border:1px solid #2563eb; }"
                             b"#phase3b-mixed strong { font-weight:bold; } #phase3b-mixed em { font-style:italic; } #phase3b-mixed code { font-family:monospace; }"
                             b"#phase3b-middle img { vertical-align:middle; } #phase3b-top img { vertical-align:top; } #phase3b-bottom img { vertical-align:bottom; }"
                             b"#phase3b-text-top img { vertical-align:text-top; } #phase3b-text-bottom img { vertical-align:text-bottom; }"
                             b"#phase3b-super { vertical-align:super; } #phase3b-sub { vertical-align:sub; } #phase3b-offset { vertical-align:4px; }"
                             b"#phase3b-normal { white-space:normal; width:170px; } #phase3b-nowrap { white-space:nowrap; width:120px; }"
                             b"#phase3b-pre-wrap { white-space:pre-wrap; width:170px; } #phase3b-pre-line { white-space:pre-line; width:170px; }"
                             b"#phase3b-clip { width:120px; height:24px; overflow:hidden; border:2px solid #dc2626; }"
                             b"</style></head><body>"
                             b"<p id='phase3b-basic'>g j p q y descenders, UPPERCASE, punctuation: !?; one line becomes several bounded lines when the inline width is reduced.</p>"
                             b"<p id='phase3b-mixed'>Multiple   spaces\tand source\n indentation <span id='phase3b-span' style='background:#fef3c7;padding:2px;border:1px solid #b45309;'>span <strong>bold</strong> <em>italic</em> <code>code</code></span> around <a id='phase3b-inline-link' href='/navigator-smoke/basic.html'>a wrapped inline link with a second fragment</a>.</p>"
                             b"<p id='phase3b-middle'>Text <img id='phase3b-image-middle' src='/navigator-smoke/wide.png' width='40' height='24' alt='middle image'> middle <input id='phase3b-input' type='text' value='input'> <button id='phase3b-button' type='button'>Button</button>.</p>"
                             b"<p id='phase3b-top'>top <img id='phase3b-image-top' src='/navigator-smoke/wide.png' width='28' height='20' alt='top image'> bottom</p>"
                             b"<p id='phase3b-bottom'>bottom <img id='phase3b-image-bottom' src='/navigator-smoke/wide.png' width='28' height='20' alt='bottom image'> end</p>"
                             b"<p id='phase3b-text-top'>text top <img src='/navigator-smoke/wide.png' width='28' height='20' alt='text top image'> end</p>"
                             b"<p id='phase3b-text-bottom'>text bottom <img src='/navigator-smoke/wide.png' width='28' height='20' alt='text bottom image'> end</p>"
                             b"<p id='phase3b-align'>base <span id='phase3b-super'>super</span> <span id='phase3b-sub'>sub</span> <span id='phase3b-offset'>offset</span><br>empty line follows break<br>then text.</p>"
                             b"<p id='phase3b-normal'>  normal   whitespace wraps at safe spaces across this deliberately narrow paragraph.  </p>"
                             b"<p id='phase3b-nowrap'>nowrap text remains one unbroken inline run even when it exceeds the bounded width.</p>"
                             b"<p id='phase3b-pre-wrap' style='white-space:pre-wrap;'>pre  wrap\nsecond line with spaces</p>"
                             b"<p id='phase3b-pre-line' style='white-space:pre-line;'>pre   line\nsecond line collapses spaces</p>"
                             b"<pre id='phase3b-pre'>pre  formatted\n  preserved spaces\nthird line</pre>"
                             b"<p id='phase3b-controls'>Checkbox <input id='phase3b-check' type='checkbox' checked> Radio <input id='phase3b-radio' type='radio' checked> Select <select id='phase3b-select'><option>One</option><option>Two</option></select></p>"
                             b"<p id='phase3b-clip'><a id='phase3b-clipped-link' href='/navigator-smoke/basic.html'>clipped link fragment must not hit beyond its ancestor clip</a></p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase3c.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3C Atomic Inline Blocks</title><style>"
                             b"body { color:#243447; background:#f8fafc; font-size:16px; line-height:1.35; }"
                             b"p.fixture-line { width:420px; margin:4px 0; padding:2px; }"
                             b".ib { display:inline-block; margin:2px 3px; padding:3px; border:1px solid #2563eb; background:#dbeafe; }"
                             b".ib-wide { width:150px; } .ib-tall { height:38px; } .ib-auto { width:auto; }"
                             b".ib-min { width:8px; min-width:96px; } .ib-max { width:260px; max-width:110px; }"
                             b".ib-content-box { box-sizing:content-box; width:50px; padding:8px; border:3px solid #0f766e; }"
                             b".ib-border-box { box-sizing:border-box; width:72px; padding:8px; border:3px solid #7c3aed; }"
                             b".ib-percent { width:25%; } .ib-percent-height { height:50%; }"
                             b".ib-short { width:auto; } .ib-long { width:auto; } .ib-token { width:auto; }"
                             b".ib-fixed-child { width:auto; } .ib-fixed-child > p { width:84px; }"
                             b".ib-card { width:210px; padding:6px; border:2px solid #1d4ed8; background:#eff6ff; }"
                             b".ib-card h2 { margin:2px 0; } .ib-card p { margin:3px 0; } .ib-card ul { margin:3px 0; }"
                             b".ib-nested { width:auto; padding:2px; border:1px solid #b45309; background:#fef3c7; }"
                             b".ib-clip { width:130px; height:34px; overflow:hidden; padding:4px; border:2px solid #dc2626; }"
                             b".ib-opacity { opacity:.5; background:#bbf7d0; } .ib-hidden { visibility:hidden; }"
                             b".ib-baseline { vertical-align:baseline; } .ib-middle { vertical-align:middle; }"
                             b".ib-top { vertical-align:top; } .ib-bottom { vertical-align:bottom; }"
                             b".ib-text-top { vertical-align:text-top; } .ib-text-bottom { vertical-align:text-bottom; }"
                             b".ib-sub { vertical-align:sub; } .ib-super { vertical-align:super; }"
                             b".ib-px { vertical-align:4px; } .ib-percent-align { vertical-align:25%; }"
                             b".ib-important { display:inline-block !important; } .ib-block { display:block; }"
                             b"#phase3c-none { display:none; } #phase3c-invalid { display:not-a-display; }"
                             b"#phase3c-parent-clip { width:260px; height:72px; overflow:hidden; padding:4px; border:2px solid #7f1d1d; }"
                             b"#phase3c-fixed-height { height:46px; } #phase3c-extreme { width:9999%; min-width:9999px; max-width:140px; }"
                             b"#phase3c-empty { width:18px; height:18px; } #phase3c-fixed { width:58px; height:24px; }"
                             b"</style></head><body>"
                             b"<h1 id='phase3c-title'>Phase 3C Atomic Inline Block Fixture</h1>"
                             b"<p id='phase3c-basic' class='fixture-line'>before <span id='phase3c-simple' class='ib'>inline block</span> after <span id='phase3c-empty' class='ib'></span>.</p>"
                             b"<p id='phase3c-two' class='fixture-line'><span id='phase3c-one' class='ib ib-wide'>one</span><span id='phase3c-two-a' class='ib ib-wide'>two</span><span id='phase3c-wrap' class='ib ib-wide'>wrap as a whole</span></p>"
                             b"<p id='phase3c-states' class='fixture-line'><span id='phase3c-hidden' class='ib ib-hidden'>hidden inline block</span><span id='phase3c-opacity' class='ib ib-opacity'>opacity inline block</span><span id='phase3c-fixed' class='ib'>fixed size</span></p>"
                             b"<p id='phase3c-sizing' class='fixture-line'><span id='phase3c-tall' class='ib ib-tall'>fixed height</span><span id='phase3c-auto' class='ib ib-auto'>auto width from short content</span><span id='phase3c-min' class='ib ib-min'>minimum</span><span id='phase3c-max' class='ib ib-max'>maximum clamp</span></p>"
                             b"<p id='phase3c-box-sizing' class='fixture-line'><span id='phase3c-content-box' class='ib ib-content-box'>content box</span><span id='phase3c-border-box' class='ib ib-border-box'>border box</span><span id='phase3c-percent' class='ib ib-percent'>percent</span><span id='phase3c-percent-height' class='ib ib-percent-height'>indefinite percent height</span></p>"
                             b"<p id='phase3c-shrink' class='fixture-line'><span id='phase3c-short' class='ib ib-short'>short</span><span id='phase3c-long' class='ib ib-long'>long text with spaces uses bounded shrink to fit</span><span id='phase3c-token' class='ib ib-token'>supercalifragilisticunbreakabletoken</span><span id='phase3c-fixed-child' class='ib ib-fixed-child'><p>fixed child width</p></span><img id='phase3c-image' class='ib' src='/navigator-smoke/wide.png' width='42' height='20' alt='intrinsic image'><input id='phase3c-input' class='ib' type='text' value='control'></p>"
                             b"<div id='phase3c-card' class='ib ib-card'><h2 id='phase3c-heading'>Nested heading</h2><p id='phase3c-paragraph'>Nested paragraph with wrapped internal text and <a id='phase3c-link' href='/navigator-smoke/basic.html'>nested child link</a>.</p><ul id='phase3c-list'><li>Nested list item</li><li>Second list item</li></ul><p>Internal margin and <span id='phase3c-nested' class='ib ib-nested'>nested inline block</span>.</p><img id='phase3c-card-image' src='/navigator-smoke/wide.png' width='36' height='18' alt='card image'><button id='phase3c-button' type='button'>Card button</button></div>"
                             b"<p id='phase3c-baselines' class='fixture-line'>base <span id='phase3c-baseline' class='ib ib-baseline'>text baseline</span><span id='phase3c-middle' class='ib ib-middle'>middle</span><span id='phase3c-top' class='ib ib-top'>top</span><span id='phase3c-bottom' class='ib ib-bottom'>bottom</span><span id='phase3c-text-top' class='ib ib-text-top'>text top</span><span id='phase3c-text-bottom' class='ib ib-text-bottom'>text bottom</span><span id='phase3c-sub' class='ib ib-sub'>sub</span><span id='phase3c-super' class='ib ib-super'>super</span><span id='phase3c-px' class='ib ib-px'>px</span><span id='phase3c-percent-align' class='ib ib-percent-align'>percent align</span></p>"
                             b"<p id='phase3c-multiline' class='fixture-line'>multi <span id='phase3c-multiline-box' class='ib ib-wide'>multiple lines inside atomic box with text</span> following text <span id='phase3c-fixed-height' class='ib'>fixed height overflow baseline</span>.</p>"
                             b"<p id='phase3c-paint' class='fixture-line'><span id='phase3c-clip' class='ib ib-clip'>overflow hidden internal content must clip</span><span id='phase3c-important' class='ib-important'>important</span><span id='phase3c-block-override' class='ib ib-block'>block override</span><span id='phase3c-invalid' class='ib'>invalid keeps winner</span><span id='phase3c-none' class='ib'>none override</span></p>"
                             b"<div id='phase3c-parent-clip'><span id='phase3c-parent-clipped' class='ib ib-wide'>parent clipping blocks this descendant target</span></div>"
                             b"<p id='phase3c-extreme-line' class='fixture-line'><span id='phase3c-extreme' class='ib'>extreme clamp</span><span id='phase3c-fallback' class='ib' style='height:24px;'>baseline fallback</span></p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase3f.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3F Float Containment</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".case{width:300px;margin:8px 0;padding:4px;border:1px solid #64748b;background:#f1f5f9}"
                             b".left{float:left;width:118px;height:82px;margin:0 12px 8px 0;padding:3px;border:2px solid #2563eb;background:#dbeafe;box-sizing:border-box}"
                             b".right{float:right;width:108px;height:72px;margin:0 0 8px 12px;padding:3px;border:2px solid #b45309;background:#fef3c7;box-sizing:border-box}"
                             b".only{overflow:hidden}.auto{overflow:auto}.scroll{overflow:scroll}.fixed{overflow:hidden;height:34px}.minimum{overflow:hidden;min-height:120px}.maximum{overflow:hidden;max-height:46px}"
                             b".short{height:86px}.tall{height:168px}.fit{overflow:hidden;width:160px}.wide{overflow:hidden;width:270px}.percent{overflow:hidden;width:55%}.minwidth{overflow:hidden;width:80px;min-width:240px}"
                             b".outer-left{float:left;width:132px;height:96px;margin-right:12px;background:#bfdbfe}.outer-right{float:right;width:122px;height:86px;margin-left:12px;background:#fde68a}.document-float{height:8192px}"
                             b".atomic{display:inline-block;width:238px;padding:4px;border:2px solid #be123c;vertical-align:top;background:#fff1f2}.atomic .inner{overflow:hidden;padding:3px;border:1px solid #be123c}.atomic .innerfloat{float:left;width:86px;height:74px;margin-right:8px;background:#fecdd3}"
                             b".listcase{overflow:hidden;width:300px;border:1px solid #15803d;padding:4px}.tablecase{overflow:hidden;width:300px;border:1px solid #7c3aed;padding:4px}.tablecase table{width:280px;border:1px solid #7c3aed}.tablecase td{border:1px solid #a78bfa;padding:4px}.cellfloat{float:left;width:56px;height:44px;margin-right:6px;background:#ddd6fe}.clear{clear:both}.clip{overflow:hidden;height:28px}.opacity{opacity:0}"
                             b"</style></head><body>"
                             b"<h1 id='phase3f-title'>Phase 3F Float Containment Fixture</h1>"
                             b"<p id='phase3f-only-left' class='case only'><img class='left' src='/navigator-smoke/wide.png' alt='only left float'>left float is contained by this BFC.</p>"
                             b"<p id='phase3f-only-right' class='case only'><img class='right' src='/navigator-smoke/tall.png' alt='only right float'>right float is contained by this BFC.</p>"
                             b"<p id='phase3f-auto' class='case auto'><img class='left' src='/navigator-smoke/wide.png' alt='auto float'>auto-height BFC uses its owned float extent.</p>"
                             b"<p id='phase3f-scroll' class='case scroll'><img class='right' src='/navigator-smoke/tall.png' alt='scroll float'>scroll BFC remains a bounded formatting context.</p>"
                             b"<p id='phase3f-short' class='case only short'><img class='left' src='/navigator-smoke/wide.png' alt='float taller than normal box'>short normal content.</p>"
                             b"<p id='phase3f-tall' class='case only tall'><img class='right' src='/navigator-smoke/tall.png' alt='normal box taller than float'>tall normal content continues below the float and supplies the larger extent.</p>"
                             b"<p id='phase3f-fixed' class='case fixed'><img class='left' src='/navigator-smoke/wide.png' alt='fixed height clips this float'></p>"
                             b"<p id='phase3f-minimum' class='case minimum'><img class='right' src='/navigator-smoke/tall.png' alt='minimum height remains authoritative'></p>"
                             b"<p id='phase3f-maximum' class='case maximum'><img class='left' src='/navigator-smoke/wide.png' alt='maximum height constrains this BFC'></p>"
                             b"<p class='outer-left'>external left</p><p id='phase3f-fit' class='case fit'>BFC fits beside the active outer float.</p>"
                             b"<p class='outer-left'>external left</p><p id='phase3f-wide' class='case wide'>explicit BFC too wide moves below the active float.</p>"
                             b"<p class='outer-right'>external right</p><p id='phase3f-percent' class='case percent'>percentage width BFC avoidance remains bounded.</p>"
                             b"<p class='outer-left'>min width downshift</p><p id='phase3f-minwidth' class='case minwidth'>min width forces a bounded move below.</p>"
                             b"<div id='phase3f-nested' class='case only'><div class='case only'><p><img class='left' src='/navigator-smoke/wide.png' alt='inner owned float'>inner BFC content</p></div><p>outer sibling remains outside inner float scope.</p></div>"
                             b"<div id='phase3f-atomic' class='atomic'><div class='inner'><p><img class='innerfloat' src='/navigator-smoke/wide.png' alt='atomic float'>inline-block internal text wraps beside its float.</p><div class='clear'>atomic clear</div></div></div><span>outer line after atomic context</span>"
                             b"<ul id='phase3f-list' class='listcase'><li><img class='left' src='/navigator-smoke/wide.png' alt='list floated image'>list text wraps beside a floated image and keeps its marker.</li><li>second list item follows the contained float.</li></ul>"
                             b"<div id='phase3f-table' class='tablecase'><table><tr><td><img class='cellfloat' src='/navigator-smoke/wide.png' alt='cell floated image'>cell text beside float</td><td>second cell</td></tr></table></div>"
                             b"<p id='phase3f-clip' class='case clip'><img class='left opacity' src='/navigator-smoke/wide.png' alt='hidden layout float'></p>"
                             b"<p id='phase3f-document-float' class='left document-float'>top-level document float remains paint-extending</p><p id='phase3f-tail'>tail content remains reachable after the visible top-level float.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase3g.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3G Positioning</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".flow{position:relative;width:330px;height:170px;margin:14px 0;padding:10px;border:3px solid #1d4ed8;background:#dbeafe;overflow:hidden}"
                             b".flow-relative{position:relative;left:12px;top:8px;width:170px;padding:4px;border:2px solid #15803d;background:#dcfce7}"
                             b".percent-relative{position:relative;left:10%;top:5%;width:120px;height:24px;background:#bbf7d0}"
                             b".abs-left{position:absolute;left:24px;top:20px;width:112px;height:28px;padding:3px;border:2px solid #7c3aed;background:#ede9fe;z-index:2}"
                             b".abs-right{position:absolute;right:12px;top:58px;width:104px;height:26px;padding:3px;border:2px solid #b45309;background:#fef3c7;z-index:0}"
                             b".abs-both{position:absolute;left:150px;right:14px;top:100px;height:24px;background:#fecdd3}"
                             b".abs-auto{position:absolute;right:10px;bottom:8px;padding:3px;border:1px solid #be123c;background:#ffe4e6}"
                             b".abs-negative{position:absolute;left:-24px;top:128px;width:80px;height:20px;background:#94a3b8;z-index:-1}"
                             b".root-abs{position:absolute;left:42px;top:1530px;width:120px;height:22px;background:#fca5a5;z-index:1}"
                             b".inline-abs{position:absolute;left:200px;top:18px;padding:3px;background:#fef08a}"
                             b".relative-image{position:relative;left:8px;top:-4px;width:54px;height:30px;background:#bfdbfe}"
                             b".relative-control{position:relative;left:6px;top:3px}"
                             b".hidden-abs{position:absolute;left:20px;top:600px;width:40px;height:18px;visibility:hidden}"
                             b".zero-abs{position:absolute;left:80px;top:620px;width:40px;height:18px;opacity:0}"
                             b".unsupported-fixed{position:fixed;left:1px;top:1px}.unsupported-sticky{position:sticky;top:2px}"
                             b".overlap-a{position:absolute;left:430px;top:80px;width:90px;height:48px;background:#93c5fd;z-index:0}"
                             b".overlap-b{position:absolute;left:450px;top:96px;width:90px;height:48px;background:#fda4af;z-index:4}"
                             b"</style></head><body>"
                             b"<h1 id='phase3g-title'>Phase 3G Positioning Fixture</h1>"
                             b"<p id='phase3g-before'>Normal flow before the positioned cases remains in place.</p>"
                             b"<div id='phase3g-flow' class='flow'>"
                             b"<div id='phase3g-relative' class='flow-relative'>Relative block shifts visually without moving its sibling.</div>"
                             b"<div id='phase3g-percent' class='percent-relative'>Relative percentage offsets.</div>"
                             b"<a id='phase3g-abs-left' class='abs-left' href='/navigator-smoke/basic.html'>absolute left top link</a>"
                             b"<button id='phase3g-abs-right' class='abs-right' type='button'>absolute right control</button>"
                             b"<div id='phase3g-abs-both' class='abs-both'>left plus right fills the bounded width</div>"
                             b"<span id='phase3g-abs-auto' class='abs-auto'>shrink to fit</span>"
                             b"<div id='phase3g-abs-negative' class='abs-negative'>negative z</div>"
                             b"<span id='phase3g-inline-abs' class='inline-abs'>absolute inline text is blockified</span>"
                             b"<img id='phase3g-relative-image' class='relative-image' src='/navigator-smoke/wide.png' alt='relative positioned image'>"
                             b"<input id='phase3g-relative-control' class='relative-control' type='checkbox' checked>"
                             b"</div>"
                             b"<p id='phase3g-root-abs' class='root-abs'>root fallback below flow</p>"
                             b"<div id='phase3g-hidden' class='hidden-abs'>hidden positioned box</div>"
                             b"<div id='phase3g-zero' class='zero-abs'>opacity zero box</div>"
                             b"<p id='phase3g-unsupported-fixed' class='unsupported-fixed' style='position:fixed;top:1px;'>fixed stays explicit unsupported diagnostics.</p>"
                             b"<p id='phase3g-unsupported-sticky' class='unsupported-sticky' style='position:sticky;top:2px;'>sticky stays explicit unsupported diagnostics.</p>"
                             b"<div id='phase3g-overlap-a' class='overlap-a'>lower overlap</div><div id='phase3g-overlap-b' class='overlap-b'>higher overlap</div>"
                             b"<p id='phase3g-after'>Following normal flow remains unaffected and reachable.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase4a.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 4A Bounded Single-Line Flexbox</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".fixture{margin:8px 0;padding:6px;border:1px solid #94a3b8;background:#fff}"
                             b".row{display:flex;flex-direction:row;flex-wrap:nowrap;width:720px;height:64px;gap:12px;align-items:center;justify-content:space-between;box-sizing:border-box}"
                             b".row>div{width:96px;height:24px;padding:3px;border:1px solid #2563eb;box-sizing:border-box;background:#dbeafe}"
                             b".grow{display:flex;width:600px;height:52px;flex-direction:row;justify-content:flex-start;align-items:stretch;box-sizing:border-box}"
                             b".grow-a{flex:1 1 0;min-width:0;background:#bfdbfe}.grow-b{flex:2 1 0;min-width:0;background:#c7d2fe}.grow-c{flex:0 0 120px;background:#ddd6fe}"
                             b".shrink{display:flex;width:300px;height:42px;flex-direction:row;flex-shrink:1;gap:4px;box-sizing:border-box}.shrink>div{flex:0 1 180px;min-width:0;background:#fee2e2}"
                             b".column{display:flex;flex-direction:column;width:160px;height:126px;gap:5px;align-items:center;justify-content:space-around;box-sizing:border-box}.column>div{width:80px;height:20px;background:#dcfce7}"
                             b".reverse{display:flex;flex-direction:row-reverse;width:280px;height:36px;justify-content:flex-start;gap:6px}.reverse>div{width:48px;height:20px;background:#fef3c7}"
                             b".order{display:flex;width:280px;height:36px;gap:5px}.order>div{width:48px;height:20px;background:#fce7f3}.order-b{order:-1}.order-c{order:2}"
                             b".auto{display:flex;width:360px;height:36px;gap:4px}.auto-a{width:64px;height:20px;background:#e0e7ff}.auto-b{width:64px;height:20px;margin-left:auto;margin-right:auto;background:#e0f2fe}"
                             b".align{display:flex;width:320px;height:72px;align-items:baseline;gap:8px}.align>div{width:60px;background:#ede9fe}.align-a{height:18px}.align-b{height:34px}.align-c{height:24px;align-self:flex-end}"
                             b".nested{display:flex;width:600px;height:54px;gap:8px;align-items:stretch}.nested-inner{display:flex;flex:1 1 0;min-width:0;gap:6px;align-items:center;background:#ecfccb;padding:4px}.nested-inner>div{flex:1 1 0;min-width:0;height:20px;background:#d9f99d}.nested-fixed{flex:0 0 120px;height:28px;background:#fef08a}"
                             b".anonymous{display:flex;width:360px;height:40px;gap:8px;align-items:center}.anonymous>span{width:90px;height:20px;background:#f1f5f9}.anonymous-text{color:#0f172a}"
                             b".intrinsic{display:flex;width:440px;height:42px;gap:8px;align-items:center}.intrinsic img{width:48px;height:24px}.intrinsic input{width:120px;height:24px}.intrinsic button{width:88px;height:24px}"
                             b".excluded{display:flex;position:relative;width:360px;height:44px;gap:6px}.excluded-none{display:none}.excluded-abs{position:absolute;left:10px;top:5px;width:40px;height:20px;background:#fecaca}.excluded-flow{width:80px;height:20px;background:#bbf7d0}"
                             b".wrap-unsupported{display:flex;flex-wrap:wrap-reverse;width:220px;height:36px;gap:4px;background:#f1f5f9}.wrap-unsupported>div{width:100px;height:18px;background:#cbd5e1}"
                             b".percent-gap{display:flex;width:400px;height:38px;gap:5%;align-items:center}.percent-gap>div{flex:1 1 0;min-width:0;height:20px;background:#e0f2fe}"
                             b".content-basis{display:flex;width:320px;height:40px}.content-basis>div{flex:0 0 content;width:80px;height:20px;background:#f5d0fe}"
                             b"</style></head><body>"
                             b"<h1 id='phase4a-title'>CSS Phase 4A Bounded Single-Line Flexbox</h1>"
                             b"<div id='phase4a-row' class='fixture row'><div id='phase4a-row-a'>row A</div><div id='phase4a-row-b'>row B</div><div id='phase4a-row-c'>row C</div></div>"
                             b"<div id='phase4a-grow' class='fixture grow'><div id='phase4a-grow-a' class='grow-a'>grow 1</div><div id='phase4a-grow-b' class='grow-b'>grow 2</div><div id='phase4a-grow-c' class='grow-c'>basis 120</div></div>"
                             b"<div id='phase4a-shrink' class='fixture shrink'><div id='phase4a-shrink-a'>shrink A</div><div id='phase4a-shrink-b'>shrink B</div></div>"
                             b"<div id='phase4a-column' class='fixture column'><div id='phase4a-column-a'>column A</div><div id='phase4a-column-b'>column B</div><div id='phase4a-column-c'>column C</div></div>"
                             b"<div id='phase4a-reverse' class='fixture reverse'><div id='phase4a-reverse-a'>one</div><div id='phase4a-reverse-b'>two</div><div id='phase4a-reverse-c'>three</div></div>"
                             b"<div id='phase4a-order' class='fixture order'><div id='phase4a-order-a'>source A</div><div id='phase4a-order-b' class='order-b'>order -1</div><div id='phase4a-order-c' class='order-c'>order 2</div></div>"
                             b"<div id='phase4a-auto' class='fixture auto'><div id='phase4a-auto-a' class='auto-a'>left</div><div id='phase4a-auto-b' class='auto-b'>auto margins</div></div>"
                             b"<div id='phase4a-align' class='fixture align'><div id='phase4a-align-a' class='align-a'>base</div><div id='phase4a-align-b' class='align-b'>baseline</div><div id='phase4a-align-c' class='align-c'>end</div></div>"
                             b"<div id='phase4a-nested' class='fixture nested'><div id='phase4a-nested-inner' class='nested-inner'><div id='phase4a-nested-a'>nested A</div><div id='phase4a-nested-b'>nested B</div></div><div id='phase4a-nested-fixed' class='nested-fixed'>fixed</div></div>"
                             b"<div id='phase4a-anonymous' class='fixture anonymous'><span id='phase4a-anon-a'>A</span> anonymous text item <span id='phase4a-anon-b'>B</span></div>"
                             b"<div id='phase4a-intrinsic' class='fixture intrinsic'><img id='phase4a-image' src='/navigator-smoke/wide.png' alt='intrinsic image'><input id='phase4a-control' type='text' value='control'><button id='phase4a-button' type='button'>control</button></div>"
                             b"<div id='phase4a-excluded' class='fixture excluded'><div id='phase4a-flow' class='excluded-flow'>flow</div><div id='phase4a-none' class='excluded-none'>none</div><div id='phase4a-absolute' class='excluded-abs'>absolute</div></div>"
                             b"<div id='phase4a-wrap' class='fixture wrap-unsupported'><div id='phase4a-wrap-a'>wrap A</div><div id='phase4a-wrap-b'>wrap B</div></div>"
                             b"<div id='phase4a-percent-gap' class='fixture percent-gap'><div id='phase4a-gap-a'>gap A</div><div id='phase4a-gap-b'>gap B</div></div>"
                             b"<div id='phase4a-content-basis' class='fixture content-basis'><div id='phase4a-content-item'>content basis</div></div>"
                             b"<p id='phase4a-regression'>Phase 4A coverage includes row, row-reverse, column, grow, shrink, basis, order, justify, align, baseline, auto margins, gap, nested flex, anonymous text, intrinsic replaced/control items, display none, absolute exclusion, and readable wrap fallback.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase4b.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 4B Multiline Flexbox</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".fixture{margin:8px 0;padding:6px;border:1px solid #94a3b8;background:#fff}"
                             b".nowrap{display:flex;flex-wrap:nowrap;width:248px;height:54px;gap:8px;justify-content:space-between;align-items:center;box-sizing:border-box;background:#eff6ff}"
                             b".nowrap>div{flex:0 0 100px;height:24px;box-sizing:border-box;background:#bfdbfe;border:1px solid #2563eb}"
                             b".wrap{display:flex;flex-wrap:wrap;width:248px;padding:8px;border:2px solid #0f766e;column-gap:8px;row-gap:7px;justify-content:space-between;align-items:flex-start;box-sizing:border-box;background:#ecfeff}"
                             b".wrap>div{flex:0 0 108px;box-sizing:border-box;background:#a7f3d0;border:1px solid #047857;padding:3px}"
                             b".wrap-a{height:24px}.wrap-b{height:42px}.wrap-c{height:30px}.wrap-wide{flex-basis:236px;height:22px;background:#fde68a!important;border-color:#b45309!important}"
                             b".center{display:flex;flex-wrap:wrap;width:248px;padding:8px;border:2px solid #7c3aed;column-gap:8px;row-gap:6px;justify-content:center;box-sizing:border-box;background:#f5f3ff}"
                             b".center>div{flex:0 0 100px;height:22px;box-sizing:border-box;background:#ddd6fe;border:1px solid #6d28d9}"
                             b"</style></head><body>"
                             b"<h1 id='phase4b-title'>CSS Phase 4B Multiline Flexbox</h1>"
                             b"<div id='phase4b-nowrap' class='fixture nowrap'><div id='phase4b-nowrap-a'>nowrap A</div><div id='phase4b-nowrap-b'>nowrap B</div><div id='phase4b-nowrap-c'>nowrap C</div></div>"
                             b"<div id='phase4b-wrap' class='fixture wrap'><div id='phase4b-wrap-a' class='wrap-a'>line one A</div><div id='phase4b-wrap-b' class='wrap-b'>line one B tall</div><div id='phase4b-wrap-c' class='wrap-c'>line two C</div><div id='phase4b-wrap-d' class='wrap-wide'>oversized item</div><div id='phase4b-wrap-e' class='wrap-a'>last line</div></div>"
                             b"<div id='phase4b-center' class='fixture center'><div id='phase4b-center-a'>center A</div><div id='phase4b-center-b'>center B</div><div id='phase4b-center-c'>center C</div></div>"
                             b"<p id='phase4b-after'>Following content is below the complete wrapped container extent.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase4c.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 4C Flexbox Cross-Axis Completion</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".fixture{margin:8px 0;padding:6px;border:2px solid #64748b;background:#fff;box-sizing:border-box}"
                             b".cross{display:flex;flex-direction:row;flex-wrap:wrap;width:300px;height:112px;gap:7px 8px;align-items:flex-start;box-sizing:border-box}"
                             b".cross>div{flex:0 0 132px;box-sizing:border-box;padding:3px;border:1px solid #2563eb;background:#dbeafe}"
                             b".cross-a{height:20px}.cross-b{height:34px}.cross-c{height:24px}.cross-d{height:28px}"
                             b".baseline{height:auto;align-content:flex-start;border-color:#0f766e;background:#ecfeff}"
                             b".end{align-content:flex-end;border-color:#c2410c;background:#fff7ed}"
                             b".center{align-content:center;border-color:#7c3aed;background:#f5f3ff}"
                             b".between{align-content:space-between;border-color:#be123c;background:#fff1f2}"
                             b".around{align-content:space-around;border-color:#0369a1;background:#f0f9ff}"
                             b".stretch{align-content:stretch;border-color:#15803d;background:#f0fdf4}"
                             b".reverse-center{flex-wrap:wrap-reverse;align-content:center;border-color:#9333ea;background:#faf5ff}"
                             b".reverse-center>div{background:#e9d5ff;border-color:#7e22ce}"
                             b".column{display:flex;flex-direction:column;flex-wrap:wrap;width:126px;height:170px;gap:7px 9px;align-content:space-around;border-color:#0369a1;background:#eff6ff}"
                             b".column>div{flex:0 0 70px;width:38px;height:70px;box-sizing:border-box;padding:2px;border:1px solid #1d4ed8;background:#bfdbfe}"
                             b".column>div:nth-child(2){width:50px;height:70px}.column>div:nth-child(4){width:42px;height:70px}"
                             b".nested-outer{display:flex;flex-direction:row;flex-wrap:wrap;width:322px;height:180px;gap:8px;align-content:flex-start;align-items:flex-start;border-color:#92400e;background:#fffbeb}"
                             b".nested-outer>div{flex:0 0 132px;box-sizing:border-box;border:1px solid #b45309;background:#fef3c7}"
                             b".nested-fixed{height:42px;padding:4px}.nested-inner{display:flex;flex-direction:row;flex-wrap:wrap;width:132px;height:94px;padding:4px;gap:5px;align-content:space-between;align-items:flex-start;border-color:#166534;background:#dcfce7}"
                             b".nested-inner>div{flex:0 0 40px;box-sizing:border-box;border:1px solid #15803d;background:#bbf7d0;padding:2px}.inner-a{height:24px}.inner-b{height:36px}.inner-c{height:18px}.inner-d{height:30px}"
                             b"</style></head><body>"
                             b"<h1 id='phase4c-title'>CSS Phase 4C Flexbox Cross-Axis Completion</h1>"
                             b"<div id='phase4c-wrap' class='fixture cross baseline'><div id='phase4c-wrap-a' class='cross-a'>ordinary wrap baseline A</div><div id='phase4c-wrap-b' class='cross-b'>ordinary wrap baseline B</div><div id='phase4c-wrap-c' class='cross-c'>ordinary wrap baseline C</div><div id='phase4c-wrap-d' class='cross-d'>ordinary wrap baseline D</div></div>"
                             b"<div id='phase4c-start' class='fixture cross baseline'><div id='phase4c-start-a' class='cross-a'>align-content flex-start</div><div id='phase4c-start-b' class='cross-b'>line two flex-start</div><div id='phase4c-start-c' class='cross-c'>explicit cross gap</div></div>"
                             b"<div id='phase4c-end' class='fixture cross end'><div id='phase4c-end-a' class='cross-a'>align-content flex-end</div><div id='phase4c-end-b' class='cross-b'>different line size</div><div id='phase4c-end-c' class='cross-c'>flex-end line two</div></div>"
                             b"<div id='phase4c-center' class='fixture cross center'><div id='phase4c-center-a' class='cross-a'>align-content center</div><div id='phase4c-center-b' class='cross-b'>center line two</div><div id='phase4c-center-c' class='cross-c'>center gap</div></div>"
                             b"<div id='phase4c-between' class='fixture cross between'><div id='phase4c-between-a' class='cross-a'>align-content space-between</div><div id='phase4c-between-b' class='cross-b'>between line two</div><div id='phase4c-between-c' class='cross-c'>between explicit gap</div></div>"
                             b"<div id='phase4c-around' class='fixture cross around'><div id='phase4c-around-a' class='cross-a'>align-content space-around</div><div id='phase4c-around-b' class='cross-b'>around line two</div><div id='phase4c-around-c' class='cross-c'>around edge spacing</div></div>"
                             b"<div id='phase4c-stretch' class='fixture cross stretch'><div id='phase4c-stretch-a' class='cross-a'>align-content stretch</div><div id='phase4c-stretch-b' class='cross-b'>stretched line two</div><div id='phase4c-stretch-c' class='cross-c'>stretch keeps item alignment</div></div>"
                             b"<div id='phase4c-reverse-center' class='fixture cross reverse-center'><div id='phase4c-reverse-center-a' class='cross-a'>wrap-reverse + center</div><div id='phase4c-reverse-center-b' class='cross-b'>cross-start is reversed</div><div id='phase4c-reverse-center-c' class='cross-c'>item order stays stable</div></div>"
                             b"<div id='phase4c-column' class='fixture column'><div id='phase4c-column-a'>column wrap A</div><div id='phase4c-column-b'>column wrap B</div><div id='phase4c-column-c'>column wrap C</div><div id='phase4c-column-d'>column wrap D</div></div>"
                             b"<div id='phase4c-nested-outer' class='fixture nested-outer'><div id='phase4c-inner' class='nested-inner'><div id='phase4c-inner-a' class='inner-a'>nested inner line one</div><div id='phase4c-inner-b' class='inner-b'>nested inner line two</div><div id='phase4c-inner-c' class='inner-c'>nested inner line three</div><div id='phase4c-inner-d' class='inner-d'>nested inner line four</div></div><div id='phase4c-nested-fixed' class='nested-fixed'>outer second-line item</div><div id='phase4c-nested-tail' class='nested-fixed'>outer wrapped tail</div></div>"
                             b"<p id='phase4c-after'>Following block content remains below the flex containers.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase5a.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 5A Positioned Layout Foundation</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".fixture{margin:8px 0;padding:6px;border:2px solid #64748b;background:#fff;box-sizing:border-box}"
                             b".relative-flow{position:relative;left:18px;top:10px;width:210px;height:28px;padding:4px;border:2px solid #15803d;background:#dcfce7}"
                             b".relative-right{position:relative;right:12px;bottom:6px;width:190px;height:24px;background:#bbf7d0}"
                             b".relative-both{position:relative;left:14px;right:60px;top:7px;bottom:40px;width:176px;height:24px;background:#d9f99d}"
                             b".static-with-insets{position:static;left:99px;top:99px;width:220px;padding:6px;background:#f1f5f9;border:2px solid #64748b}"
                             b".cb-root{position:relative;width:340px;height:142px;padding:10px;border:3px solid #1d4ed8;background:#dbeafe;overflow:visible}"
                             b".cb-abs{position:absolute;left:22px;top:18px;width:132px;height:26px;padding:3px;border:2px solid #7c3aed;background:#ede9fe;z-index:3}"
                             b".cb-grand-parent{position:relative;left:8px;top:4px;width:190px;height:38px;background:#bfdbfe}"
                             b".cb-grand{position:absolute;right:8px;bottom:5px;width:78px;height:18px;background:#c4b5fd;z-index:4}"
                             b".abs-right-bottom{position:absolute;right:14px;bottom:10px;width:126px;height:28px;padding:3px;border:2px solid #b45309;background:#fef3c7;z-index:2}"
                             b".abs-between{position:absolute;left:280px;top:22px;width:116px;height:24px;background:#fecdd3;z-index:1}"
                             b".flex-host{display:flex;position:relative;width:380px;height:74px;gap:8px;padding:6px;border:2px solid #0369a1;background:#e0f2fe;box-sizing:border-box}"
                             b".flex-item{position:relative;flex:1 1 0;min-width:0;height:48px;padding:4px;background:#bae6fd;border:1px solid #0284c7}"
                             b".flex-abs{position:absolute;left:12px;top:8px;width:76px;height:18px;background:#7dd3fc;z-index:5}"
                             b".flex-direct-abs{position:absolute;left:8px;top:4px;width:92px;height:16px;background:#38bdf8;z-index:6}"
                             b".flex-item-relative{position:relative;left:10px;top:5px;flex:0 0 116px;height:38px;background:#a7f3d0}"
                             b".overlap-a{position:absolute;left:430px;top:24px;width:118px;height:46px;background:#93c5fd;z-index:1}"
                             b".overlap-b{position:absolute;left:454px;top:38px;width:118px;height:46px;background:#fda4af;z-index:7}"
                             b".equal-a{position:absolute;left:430px;top:98px;width:118px;height:34px;background:#fde68a;z-index:4}"
                             b".equal-b{position:absolute;left:454px;top:112px;width:118px;height:34px;background:#fb7185;z-index:4}"
                             b".abs-container{position:absolute;left:24px;top:640px;width:260px;height:86px;padding:8px;border:2px solid #be123c;background:#ffe4e6;z-index:2}"
                             b".abs-left-right{position:absolute;left:610px;right:54px;top:176px;height:18px;background:#c4b5fd;z-index:2}.abs-top-bottom{position:absolute;left:610px;top:205px;bottom:300px;width:160px;background:#a5b4fc;z-index:2}"
                             b".abs-container p{margin:4px 0;padding:4px;background:#fecdd3}.edge-abs{position:absolute;left:28px;top:1080px;width:120px;height:24px;background:#fca5a5}"
                             b".unsupported-fixed{position:fixed;left:1px;top:1px}.unsupported-sticky{position:sticky;top:2px}"
                             b"</style></head><body>"
                             b"<h1 id='phase5a-title'>CSS Phase 5A Positioned Layout Foundation</h1>"
                             b"<p id='phase5a-static-before' class='fixture'>Static baseline remains in normal flow.</p>"
                             b"<p id='phase5a-relative' class='relative-flow'>Relative left plus top reserves its original flow space.</p>"
                             b"<p id='phase5a-relative-right' class='relative-right'>Relative right and bottom use the opposite direction.</p>"
                             b"<p id='phase5a-relative-both' class='relative-both'>Both relative insets use left and top precedence.</p>"
                             b"<p id='phase5a-static-insets' class='static-with-insets'>Static ignores physical insets and stays in normal flow.</p>"
                             b"<div id='phase5a-cb-root' class='fixture cb-root'><a id='phase5a-cb-link' class='cb-abs' href='/navigator-smoke/basic.html'>absolute child link uses relative parent</a><p id='phase5a-grand-parent' class='cb-grand-parent'>nested positioned parent<span id='phase5a-grand' class='cb-grand'>grandchild</span></p><div id='phase5a-right-bottom' class='abs-right-bottom'>right and bottom</div></div>"
                             b"<p id='phase5a-flow-a' class='fixture'>A normal sibling before an absolute sibling.</p><p id='phase5a-abs-between' class='abs-between'>ABS between siblings</p><p id='phase5a-flow-b' class='fixture'>B follows without an artificial absolute-flow gap.</p><p id='phase5a-abs-left-right' class='abs-left-right'>left plus right bounded size</p><p id='phase5a-abs-top-bottom' class='abs-top-bottom'>top plus bottom bounded fallback</p>"
                             b"<div id='phase5a-flex-host' class='flex-host'><p id='phase5a-flex-item' class='flex-item'>relative flex item<span id='phase5a-flex-abs' class='flex-abs'>flex absolute child</span></p><p id='phase5a-flex-item-relative' class='flex-item-relative'>relative allocation</p><p id='phase5a-flex-abs-direct' class='flex-direct-abs'>direct flex absolute excluded</p></div>"
                             b"<p id='phase5a-overlap-a' class='overlap-a'>lower z-index</p><p id='phase5a-overlap-b' class='overlap-b'>higher z-index</p><p id='phase5a-equal-a' class='equal-a'>equal z first</p><p id='phase5a-equal-b' class='equal-b'>equal z later paints on top</p>"
                             b"<div id='phase5a-abs-container' class='abs-container'><p id='phase5a-abs-child'>Absolute container content includes a normal block child.</p></div>"
                             b"<p id='phase5a-following'>Ordinary following content remains in normal flow after positioned cases.</p>"
                             b"<p id='phase5a-edge' class='edge-abs'>positioned content extends document extent</p>"
                             b"<p id='phase5a-unsupported-fixed' class='unsupported-fixed'>fixed remains unsupported.</p><p id='phase5a-unsupported-sticky' class='unsupported-sticky'>sticky remains unsupported.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase5b.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 5B Fixed Positioning and Viewport Layer</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".fixture{margin:8px 0;padding:6px;border:2px solid #64748b;background:#fff;box-sizing:border-box}"
                             b".fixed-tl{position:fixed;left:8px;top:8px;width:150px;height:32px;padding:4px;border:3px solid #be123c;background:#fecdd3;z-index:12;display:block}"
                             b".fixed-tr{position:fixed;right:12px;top:8px;width:180px;height:34px;padding:4px;border:3px solid #1d4ed8;background:#bfdbfe;z-index:8}"
                             b".fixed-bl{position:fixed;left:10px;bottom:10px;padding:6px;border:3px solid #047857;background:#bbf7d0;z-index:10}"
                             b".fixed-br{position:fixed;right:14px;bottom:10px;width:158px;height:30px;padding:4px;border:3px solid #b45309;background:#fef3c7;z-index:11}"
                             b".fixed-panel{position:fixed;left:214px;top:18px;width:244px;padding:10px;border:3px solid #7c3aed;background:#ede9fe;z-index:9}"
                             b".fixed-panel p{margin:2px 0;padding:3px;background:#ddd6fe}.fixed-panel a{display:block;margin:2px 0;padding:3px;background:#c4b5fd}"
                             b".fixed-abs-child{position:absolute;right:4px;bottom:4px;width:92px;height:18px;padding:2px;border:2px solid #6d28d9;background:#c4b5fd;z-index:2}"
                             b".fixed-relative-child{position:relative;left:8px;top:3px;padding:2px;border:1px solid #4338ca;background:#e0e7ff}"
                             b".nested-fixed{position:fixed;left:478px;top:150px;width:136px;height:24px;padding:3px;border:2px solid #0f766e;background:#99f6e4;z-index:13}"
                             b".deep-fixed{position:fixed;left:486px;top:190px;width:126px;height:24px;padding:3px;border:2px solid #0e7490;background:#a5f3fc;z-index:6}"
                             b".flex-host{display:flex;position:relative;width:450px;height:76px;gap:8px;padding:6px;border:2px solid #0369a1;background:#e0f2fe;box-sizing:border-box}"
                             b".flex-item{position:relative;flex:1 1 0;min-width:0;height:44px;padding:4px;border:1px solid #0284c7;background:#bae6fd}"
                             b".flex-fixed{position:fixed;left:650px;top:120px;width:100px;height:22px;padding:3px;border:2px solid #0369a1;background:#7dd3fc;z-index:7}"
                             b".item-nested-fixed{position:fixed;left:650px;top:154px;width:120px;height:22px;padding:3px;border:2px solid #155e75;background:#67e8f9;z-index:7}"
                             b".doc-absolute{position:absolute;left:8px;top:8px;width:190px;height:34px;padding:4px;border:2px solid #475569;background:#cbd5e1;z-index:2}"
                             b".equal-a{position:fixed;left:350px;top:86px;width:124px;height:26px;padding:3px;border:2px solid #a16207;background:#fde68a;z-index:4}"
                             b".equal-b{position:fixed;left:370px;top:98px;width:124px;height:26px;padding:3px;border:2px solid #be123c;background:#fb7185;z-index:4}"
                             b".fixed-stretch{position:fixed;left:350px;right:250px;top:128px;padding:4px;border:2px solid #4338ca;background:#c7d2fe;z-index:5}"
                             b".partial-fixed{position:fixed;left:-18px;top:-8px;width:82px;height:30px;padding:3px;border:2px solid #dc2626;background:#fca5a5;z-index:14}"
                             b".sticky-probe{position:sticky;top:2px;border:1px solid #94a3b8}"
                             b".doc-content{min-height:1800px;padding-bottom:40px}.doc-content p{max-width:620px;margin:10px 0;padding:6px;border-left:3px solid #cbd5e1;background:#fff}"
                             b"</style></head><body>"
                             b"<a id='phase5b-fixed-link' class='fixed-tl' href='/navigator-smoke/basic.html'>fixed top-left link</a>"
                             b"<p id='phase5b-top-right' class='fixed-tr'>fixed top-right panel</p>"
                             b"<p id='phase5b-bottom-left' class='fixed-bl'>content-sized fixed bottom-left status</p>"
                             b"<div id='phase5b-bottom-right' class='fixed-br'><a id='phase5b-bottom-link' href='/navigator-smoke/basic.html'>fixed bottom-right action</a></div>"
                             b"<div id='phase5b-panel' class='fixed-panel'><p id='phase5b-panel-text'>fixed descendants lay out normally</p><a id='phase5b-panel-link' href='/navigator-smoke/basic.html'>fixed panel link</a><button id='phase5b-fixed-button' type='button'>fixed control</button><p id='phase5b-absolute-child' class='fixed-abs-child'>absolute child</p><p id='phase5b-relative-child' class='fixed-relative-child'>relative child</p><p id='phase5b-nested-fixed' class='nested-fixed'>nested fixed viewport</p></div>"
                             b"<div id='phase5b-deep-one'><div id='phase5b-deep-two'><div id='phase5b-deep-three'><p id='phase5b-deep-fixed' class='deep-fixed'>deep DOM fixed</p></div></div></div>"
                             b"<div id='phase5b-flex-host' class='flex-host'><div id='phase5b-flex-item-a' class='flex-item'>ordinary flex item A</div><div id='phase5b-flex-item-b' class='flex-item'>ordinary flex item B<p id='phase5b-item-nested-fixed' class='item-nested-fixed'>nested fixed in flex item</p></div><p id='phase5b-flex-direct-fixed' class='flex-fixed'>direct fixed flex child</p></div>"
                             b"<div id='phase5b-overlap-owner' class='fixture' style='position:relative;height:96px'><div id='phase5b-document-absolute' class='doc-absolute'>document absolute underneath fixed</div></div>"
                             b"<p id='phase5b-equal-a' class='equal-a'>equal-z fixed first</p><p id='phase5b-equal-b' class='equal-b'>equal-z fixed later</p>"
                             b"<p id='phase5b-stretch' class='fixed-stretch'>left and right auto width; top and content height</p>"
                             b"<p id='phase5b-partial' class='partial-fixed'>partially offscreen fixed</p>"
                             b"<p id='phase5b-before' class='fixture'>Following flow remains below fixed boxes; fixed boxes do not consume ordinary flow space.</p>"
                             b"<div class='doc-content'><h1 id='phase5b-title'>CSS Phase 5B Fixed Positioning and Viewport Layer</h1><p id='phase5b-sticky' class='sticky-probe'>sticky remains unsupported and is not approximated by fixed.</p>"
                             b"<p id='phase5b-scroll-a'>Document content scrolls normally behind the fixed viewport layer. Scroll marker A.</p><p id='phase5b-scroll-b'>Moderate scroll marker B remains ordinary document content beneath fixed controls.</p>"
                             b"<p id='phase5b-scroll-c'>Near maximum scroll marker C proves the document has real scroll range.</p><p id='phase5b-following'>Following document content proves fixed positioning does not add document extent.</p>"
                             b"<p>Long document filler 01. Long document filler 02. Long document filler 03. Long document filler 04. Long document filler 05.</p>"
                             b"<p>Long document filler 06. Long document filler 07. Long document filler 08. Long document filler 09. Long document filler 10.</p>"
                             b"<p>Long document filler 11. Long document filler 12. Long document filler 13. Long document filler 14. Long document filler 15.</p>"
                             b"<p>Long document filler 16. Long document filler 17. Long document filler 18. Long document filler 19. Long document filler 20.</p>"
                             b"<p>Long document filler 21. Long document filler 22. Long document filler 23. Long document filler 24. Long document filler 25.</p>"
                             b"</div></body></html>")
            return
        if path == "/navigator-smoke/css-phase6a.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 6A Overflow and Scroll Containers</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.2;background:#f8fafc;color:#1e293b}"
                             b"h1{font-size:22px;margin:8px 0;padding:6px;background:#dbeafe;border:2px solid #2563eb}"
                             b".case{width:300px;height:62px;margin:8px 0;padding:6px;border:3px solid #475569;background:#fff;box-sizing:border-box}"
                             b".case p{margin:2px 0}.marker{padding:3px;border:2px solid #0f766e;background:#ccfbf1}"
                             b".visible{overflow:visible;border-color:#16a34a}.visible-wide{width:420px;height:44px;background:#dcfce7}"
                             b".hidden{position:relative;overflow:hidden;border-color:#dc2626}.hidden-tall{height:120px;background:#fee2e2}.hidden-tall a{position:absolute;left:8px;top:86px;padding:4px;background:#fecaca}"
                             b".fit{overflow:auto;height:62px;border-color:#ca8a04;background:#fef9c3}.fit p{margin:0}"
                             b".auto-overflow{position:relative;overflow:auto;height:58px;border-color:#2563eb;background:#dbeafe}.auto-overflow .tall{height:190px;padding:4px;background:#bfdbfe}.auto-overflow a{position:absolute;left:8px;top:205px;padding:4px;background:#93c5fd}"
                             b".scroll-always{overflow:scroll;height:54px;border-color:#7c3aed;background:#ede9fe}.scroll-always .tall{height:90px;padding:4px;background:#ddd6fe}"
                             b".horizontal{overflow-x:scroll;overflow-y:hidden;width:220px;height:48px;white-space:nowrap;border-color:#0891b2;background:#cffafe}.wide-line{display:inline-block;width:620px;padding:5px;background:#a5f3fc}"
                             b".relative-clip{position:relative;overflow:hidden;height:58px;border-color:#be123c}.relative-shift{position:relative;left:10px;top:18px;padding:5px;background:#fbcfe8}.relative-clip a{display:block;margin-top:42px;background:#fda4af;padding:4px}"
                             b".absolute-clip{position:relative;overflow:hidden;height:58px;border-color:#c2410c}.absolute-high{position:absolute;left:230px;top:38px;width:150px;height:50px;padding:4px;background:#fed7aa;border:3px solid #ea580c;z-index:90}"
                             b".flex-host{display:flex;width:430px;height:94px;gap:8px;padding:6px;border:3px solid #0369a1;background:#e0f2fe;box-sizing:border-box}.flex-item-scroll{flex:1 1 0;min-width:0;height:70px;overflow:auto;padding:4px;border:2px solid #0284c7;background:#bae6fd}.flex-item-scroll .tall{height:140px;background:#7dd3fc}.flex-scroll{overflow:auto;width:310px;height:76px;border-color:#0369a1}.wrapped{display:flex;flex-wrap:wrap;width:450px;height:145px;gap:4px;background:#dbeafe}.wrapped i{display:block;width:130px;height:42px;padding:4px;background:#93c5fd;border:2px solid #1d4ed8;box-sizing:border-box}"
                             b".fixed-host{overflow:auto;height:58px;border-color:#0f766e}.fixed-inside{position:fixed;left:620px;top:210px;width:150px;height:26px;padding:4px;background:#99f6e4;border:2px solid #0f766e;z-index:30}"
                             b".nested-outer{overflow:auto;height:86px;border-color:#9333ea;background:#f3e8ff}.nested-inner{position:relative;overflow:auto;height:58px;margin:4px;padding:4px;border:2px solid #7e22ce;background:#e9d5ff}.nested-inner .tall{height:150px;background:#d8b4fe}.nested-inner>a{position:absolute;left:8px;top:0;width:160px;height:25px;padding:4px;background:#c4b5fd}.following{height:42px;background:#dcfce7;border-color:#16a34a}"
                             b"</style></head><body><h1 id='phase6a-title'>CSS Phase 6A Overflow, Clipping and Scroll Containers</h1>"
                             b"<div id='phase6a-visible' class='case visible'><b>overflow: visible</b><p class='visible-wide'>Visible descendant paints beyond its box.</p></div>"
                             b"<div id='phase6a-hidden' class='case hidden' style='position:relative'><b>overflow: hidden</b><div class='hidden-tall'>Clipped box<a id='phase6a-hidden-link' style='position:absolute;left:8px;top:86px;width:192px;height:25px;padding:4px' href='/navigator-smoke/basic.html'>hidden link must not hit</a></div></div>"
                             b"<div id='phase6a-fit' class='case fit'><b>overflow: auto fitting</b><p>Content fits; no active range.</p></div>"
                             b"<div id='phase6a-auto-overflow' class='case auto-overflow' style='position:relative'><b>overflow: auto overflowing</b><div class='tall'>Scrollable content<a id='phase6a-revealed-link' style='position:absolute;left:8px;top:205px;width:216px;height:25px;padding:4px' href='/navigator-smoke/basic.html'>revealed after local scroll</a></div></div>"
                             b"<div id='phase6a-scroll' class='case scroll-always'><b>overflow: scroll</b><div class='tall'>Always-recorded scroll container.</div></div>"
                             b"<div id='phase6a-horizontal' class='case horizontal'><b>overflow-x scroll</b><span class='wide-line'>Horizontal content extends beyond the local viewport.</span></div>"
                             b"<div id='phase6a-relative' class='case relative-clip'><b>relative descendant</b><span class='relative-shift'>relative visual offset</span><a id='phase6a-relative-link' href='/navigator-smoke/basic.html'>partially clipped relative link</a></div>"
                             b"<div id='phase6a-absolute' class='case absolute-clip'><b>absolute high-z child is still clipped</b><span class='absolute-high'>outside portion stays clipped</span></div>"
                             b"<div id='phase6a-flex-host' class='flex-host'><div id='phase6a-flex-item' class='flex-item-scroll'><b>flex item overflow</b><div class='tall'>flex item internal overflow</div></div><div id='phase6a-flex-scroll' class='flex-scroll'><div class='wrapped'><i>wrap A</i><i>wrap B</i><i>wrap C</i><i>wrap D</i><i>wrap E</i><i>wrap F</i></div></div></div>"
                             b"<div id='phase6a-fixed-host' class='case fixed-host'><b>local scroller owns content</b><div class='tall'>fixed descendant remains viewport-relative</div><a id='phase6a-fixed-link' class='fixed-inside' href='/navigator-smoke/basic.html'>fixed inside scrolled DOM ancestor</a></div>"
                             b"<div id='phase6a-nested-outer' class='case nested-outer'><b>outer scroll container</b><div id='phase6a-nested-inner' class='nested-inner' style='position:relative'><b>inner scroll container</b><div class='tall'>nested content</div><a id='phase6a-nested-link' style='position:absolute;left:8px;top:200px;width:160px;height:25px;padding:4px;background:#c4b5fd' href='/navigator-smoke/basic.html'>nested revealed link</a></div></div>"
                             b"<div id='phase6a-following' class='case following'>Following normal-flow content proves sibling placement uses the container box.</div>"
                             b"<div style='height:300px'></div></body></html>")
            return
        if path == "/navigator-smoke/css-phase6b.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 6B Sticky Positioning</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.2;background:#f8fafc;color:#172033}"
                             b"h1{font-size:22px;margin:8px 0;padding:6px;background:#dbeafe;border:2px solid #2563eb}"
                             b".owner{position:relative;width:620px;height:520px;margin:12px 0;padding:8px;border:3px solid #334155;background:#fff;box-sizing:border-box}"
                             b".top{position:sticky;top:6px;height:34px;padding:5px;border:2px solid #0f766e;background:#99f6e4;z-index:20;box-sizing:border-box}"
                             b".top-link{position:sticky;top:48px;height:30px;padding:4px;border:2px solid #1d4ed8;background:#bfdbfe;z-index:25;box-sizing:border-box}"
                             b".both{position:sticky;top:4px;bottom:8px;height:28px;padding:4px;border:2px solid #7c3aed;background:#ddd6fe;z-index:12;box-sizing:border-box}"
                             b".bottom{position:sticky;bottom:6px;height:28px;padding:4px;border:2px solid #b45309;background:#fde68a;z-index:14;box-sizing:border-box}"
                             b".abs-child{position:absolute;left:210px;top:4px;width:120px;height:20px;padding:2px;background:#fecdd3;border:2px solid #be123c;z-index:40;box-sizing:border-box}"
                             b".fixed-child{position:fixed;left:650px;top:176px;width:170px;height:26px;padding:4px;background:#a5f3fc;border:2px solid #0e7490;z-index:60;box-sizing:border-box}"
                             b".auto{overflow:auto;width:430px;height:140px;margin:12px 0;padding:6px;border:3px solid #2563eb;background:#dbeafe;box-sizing:border-box}.auto .fill{height:360px;padding:4px;background:#bfdbfe}"
                             b".scroll{overflow:scroll;width:430px;height:126px;margin:12px 0;padding:6px;border:3px solid #7c3aed;background:#ede9fe;box-sizing:border-box}.scroll .fill{height:320px;padding:4px;background:#ddd6fe}"
                             b".nested-outer{overflow:auto;width:500px;height:190px;margin:12px 0;padding:6px;border:3px solid #9333ea;background:#f3e8ff;box-sizing:border-box}.nested-inner{overflow:auto;width:410px;height:128px;padding:5px;border:3px solid #6d28d9;background:#e9d5ff;box-sizing:border-box}.nested-inner .fill{height:330px;background:#d8b4fe}"
                             b".horizontal{overflow-x:scroll;overflow-y:hidden;width:420px;height:58px;margin:12px 0;padding:5px;border:3px solid #0891b2;background:#cffafe;white-space:nowrap;box-sizing:border-box}.wide{display:inline-block;width:900px;height:34px;padding:4px;background:#a5f3fc;box-sizing:border-box}.left{position:sticky;left:6px;display:inline-block;width:118px;height:26px;padding:4px;background:#bbf7d0;border:2px solid #15803d;z-index:10;box-sizing:border-box}.right{position:sticky;right:6px;display:inline-block;width:118px;height:26px;padding:4px;background:#fed7aa;border:2px solid #c2410c;z-index:11;box-sizing:border-box}"
                             b".flex{display:flex;width:560px;height:110px;gap:8px;margin:12px 0;padding:6px;border:3px solid #0369a1;background:#e0f2fe;box-sizing:border-box}.flex-item{flex:1 1 0;min-width:0;height:76px;padding:4px;border:2px solid #0284c7;background:#bae6fd;box-sizing:border-box}.flex-sticky{position:sticky;top:4px;flex:1 1 0;height:30px;padding:4px;border:2px solid #0f766e;background:#86efac;z-index:18;box-sizing:border-box}.flex-scroll{flex:1 1 0;min-width:0;height:76px;overflow:auto;padding:4px;border:2px solid #7e22ce;background:#e9d5ff;box-sizing:border-box}.flex-scroll .fill{height:220px}"
                             b".hidden-host{overflow:hidden;width:420px;height:80px;margin:12px 0;padding:5px;border:3px solid #dc2626;background:#fee2e2;box-sizing:border-box}.hidden-host .hidden-sticky{position:sticky;top:4px;height:28px;padding:4px;background:#fecaca;border:2px solid #b91c1c}"
                             b".equal-a{position:sticky;top:10px;height:24px;padding:3px;background:#fda4af;border:2px solid #be123c;z-index:7;box-sizing:border-box}.equal-b{position:sticky;top:10px;height:24px;padding:3px;background:#fb7185;border:2px solid #9f1239;z-index:7;box-sizing:border-box}"
                             b".filler{height:260px;margin:8px 0;padding:8px;border:2px dashed #94a3b8;background:#f1f5f9}"
                             b"</style></head><body><h1 id='phase6b-title'>CSS Phase 6B Sticky Positioning</h1>"
                             b"<div id='phase6b-root-owner' class='owner'><h2 id='phase6b-doc-sticky' class='top'>document sticky top; normal flow space remains reserved</h2><a id='phase6b-sticky-link' class='top-link' href='/navigator-smoke/basic.html'>sticky hyperlink hit target</a><div id='phase6b-both' class='both'>both top and bottom; top precedence</div><div id='phase6b-abs-owner' class='top'>sticky parent with positioned child<div id='phase6b-absolute-child' class='abs-child'>absolute child follows sticky</div><div id='phase6b-fixed-descendant' class='fixed-child'>fixed descendant stays viewport-fixed</div></div><div class='filler'>owner content before the release edge</div><div id='phase6b-bottom-sticky' class='bottom'>bottom sticky</div><div id='phase6b-following'>following normal-flow content proves sticky space is reserved</div></div>"
                             b"<div id='phase6b-auto' class='auto'><div id='phase6b-auto-sticky' class='top'>sticky inside overflow:auto</div><div class='fill'>auto scroll content</div></div>"
                             b"<div id='phase6b-scroll' class='scroll'><div id='phase6b-scroll-sticky' class='top'>sticky inside overflow:scroll</div><div class='fill'>scroll content</div></div>"
                             b"<div id='phase6b-nested-outer' class='nested-outer'><div id='phase6b-nested-inner' class='nested-inner'><div id='phase6b-nested-sticky' class='top'>nearest nested scrollport sticky</div><div class='fill'>nested inner content</div></div></div>"
                             b"<div id='phase6b-horizontal' class='horizontal'><span id='phase6b-left-sticky' class='left'>left sticky</span><span class='wide'>horizontal overflow content</span><span id='phase6b-right-sticky' class='right'>right sticky</span></div>"
                             b"<div id='phase6b-flex' class='flex'><div id='phase6b-flex-sticky' class='flex-sticky'>sticky flex item</div><div class='flex-item'>ordinary flex item</div><div id='phase6b-flex-scroll' class='flex-scroll'><div id='phase6b-flex-child-sticky' class='top'>sticky descendant in scrollable flex item</div><div class='fill'>flex scroll content</div></div></div>"
                             b"<div id='phase6b-hidden' class='hidden-host'><div id='phase6b-hidden-sticky' class='hidden-sticky'>hidden clips; no local scroll selection</div></div>"
                             b"<div id='phase6b-equal-a' class='equal-a'>equal z source order A</div><div id='phase6b-equal-b' class='equal-b'>equal z source order B</div><div style='height:520px'></div></body></html>")
            return
        if path == "/navigator-smoke/css-phase6c.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 6C Element Scrollbar UI and Direct Interaction</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.2;background:#f8fafc;color:#172033}"
                             b"h1{font-size:22px;margin:8px 0;padding:6px;background:#dbeafe;border:2px solid #2563eb}"
                             b".case{position:relative;width:300px;height:94px;margin:8px 0;padding:6px;border:3px solid #475569;background:#fff;box-sizing:border-box}"
                             b".fit{overflow:auto;height:58px;border-color:#ca8a04;background:#fef9c3}.fit p{margin:0}"
                             b".vertical{overflow-y:auto;overflow-x:hidden;border-color:#2563eb;background:#dbeafe}.vertical .tall{height:420px;padding:4px;background:#bfdbfe}.vertical a{display:block;margin:4px 0;padding:4px;background:#93c5fd}.under-scrollbar{position:absolute;right:0;top:8px;width:80px;height:70px;padding:2px;background:#fecaca;border:2px solid #dc2626;z-index:80}"
                             b".horizontal{overflow-x:auto;overflow-y:hidden;width:300px;height:58px;border-color:#0891b2;background:#cffafe;white-space:nowrap}.horizontal .wide{display:inline-block;width:760px;height:34px;padding:4px;background:#a5f3fc;box-sizing:border-box}.horizontal a{display:inline-block;margin-left:8px;padding:4px;background:#67e8f9}"
                             b".both{overflow:auto;width:300px;height:112px;border-color:#7c3aed;background:#ede9fe}.both .large{width:720px;height:420px;padding:4px;background:#ddd6fe}.both a{display:block;width:150px;padding:4px;background:#c4b5fd}"
                             b".scroll-fit{overflow:scroll;width:300px;height:58px;border-color:#9333ea;background:#f3e8ff}.scroll-fit p{margin:0}"
                             b".nested-outer{overflow:auto;width:340px;height:152px;border-color:#9333ea;background:#f3e8ff}.nested-inner{overflow:auto;width:270px;height:94px;margin:4px;padding:4px;border:3px solid #6d28d9;background:#e9d5ff}.nested-inner .fill{height:280px;background:#d8b4fe}.nested-inner a{display:block;padding:4px;background:#c4b5fd}"
                             b".sticky-host{overflow:auto;width:300px;height:122px;border-color:#0f766e;background:#ccfbf1}.sticky-host .sticky{position:sticky;top:4px;height:28px;padding:4px;border:2px solid #047857;background:#86efac}.sticky-host .fill{height:300px}"
                             b".flex-host{display:flex;width:620px;height:112px;gap:8px;padding:6px;border:3px solid #0369a1;background:#e0f2fe;box-sizing:border-box}.flex-item-scroll{flex:1 1 0;min-width:0;height:82px;overflow:auto;padding:4px;border:2px solid #0284c7;background:#bae6fd}.flex-item-scroll .fill{height:220px}.flex-wrap{display:flex;flex-wrap:wrap;width:460px;height:220px;gap:4px;background:#dbeafe}.flex-wrap i{display:block;width:140px;height:44px;padding:4px;background:#93c5fd;border:2px solid #1d4ed8;box-sizing:border-box}"
                             b".position-host{position:relative;overflow:auto;width:300px;height:104px;border-color:#b45309;background:#fef3c7}.position-host .absolute{position:absolute;left:190px;top:160px;width:92px;height:24px;padding:3px;background:#fdba74}.position-host .fixed{position:fixed;left:650px;top:160px;width:120px;height:24px;padding:3px;background:#99f6e4;border:2px solid #0f766e;z-index:90}"
                             b".clip-outer{overflow:hidden;width:330px;height:92px;border-color:#dc2626;background:#fee2e2}.clip-inner{position:relative;top:50px;overflow:scroll;width:290px;height:72px;padding:4px;background:#fecaca}.clip-inner .large{height:240px;width:620px;background:#fca5a5}.following{height:42px;background:#dcfce7;border-color:#16a34a}.tiny{overflow:scroll;width:8px;height:8px;padding:0;border:1px solid #64748b}"
                             b"</style></head><body><h1 id='phase6c-title'>CSS Phase 6C Element Scrollbar UI and Direct Interaction</h1>"
                             b"<div id='phase6c-fit' class='case fit'><b>overflow:auto fitting content - no unnecessary scrollbar</b><p>Fits safely.</p></div>"
                             b"<div id='phase6c-vertical' class='case vertical'><b>vertical overflow:auto</b><a id='phase6c-under-scrollbar' class='under-scrollbar' href='/navigator-smoke/basic.html'>link under scrollbar</a><div class='tall'><a id='phase6c-visible-link' href='/navigator-smoke/basic.html'>visible content link</a><p>tall vertical content.</p></div></div>"
                             b"<div id='phase6c-horizontal' class='horizontal'><b>horizontal overflow-x:auto</b><span class='wide'><a id='phase6c-horizontal-link' href='/navigator-smoke/basic.html'>wide horizontal link</a></span></div>"
                             b"<div id='phase6c-both' class='both'><b>both axes and clean corner</b><div class='large'><a id='phase6c-both-link' href='/navigator-smoke/basic.html'>both-axis content link</a></div></div>"
                             b"<div id='phase6c-scroll-fit' class='scroll-fit'><b>overflow:scroll fitting content</b><p>zero-range UI remains present.</p></div>"
                             b"<div id='phase6c-nested-outer' class='nested-outer'><b>outer scrollbar owner</b><div id='phase6c-nested-inner' class='nested-inner'><b>inner scrollbar owner</b><div class='fill'><a id='phase6c-nested-link' href='/navigator-smoke/basic.html'>nested content link</a></div></div></div>"
                             b"<div id='phase6c-sticky' class='sticky-host'><div id='phase6c-sticky-child' class='sticky'>sticky inside scrollable container</div><div class='fill'>sticky filler</div></div>"
                             b"<div id='phase6c-flex' class='flex-host'><div id='phase6c-flex-item' class='flex-item-scroll'><b>flex item overflow:auto</b><div class='fill'>flex item scrolling content</div></div><div id='phase6c-flex-wrap' class='flex-wrap'><i>wrap A</i><i>wrap B</i><i>wrap C</i><i>wrap D</i><i>wrap E</i><i>wrap F</i></div></div>"
                             b"<div id='phase6c-position' class='position-host'><b>absolute descendant scrolls and clips</b><div class='absolute'>absolute descendant</div><div id='phase6c-fixed' class='fixed'>fixed descendant remains viewport invariant</div></div>"
                             b"<div id='phase6c-clip' class='clip-outer'><div id='phase6c-clipped-scroll' class='clip-inner'><div class='large'>scrollbar is clipped by overflow:hidden ancestor</div></div></div>"
                             b"<div id='phase6c-tiny' class='tiny'>tiny</div><div id='phase6c-following' class='case following'>following normal-flow content remains outside scrollbar extent.</div><div style='height:260px'></div></body></html>")
            return
        if path == "/navigator-smoke/typography-phase7a.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Navigator Typography Phase 7A Roboto Integration</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:16px;line-height:1.35;background:#f8fafc;color:#172033}"
                             b"h1{font-size:28px;margin:8px 0;background:#dbeafe} h2{font-size:22px;margin:8px 0} h3{font-size:18px;margin:8px 0}"
                             b"p{margin:8px 0}.narrow{width:190px;border:2px solid #2563eb;padding:6px}.family{margin:8px 0;padding:6px;border:2px solid #64748b;background:#fff}"
                             b".mono{font-family:monospace}.unknown{font-family:DefinitelyNotInstalled}.flex{display:flex;flex-wrap:wrap;gap:8px;width:520px;padding:8px;border:2px solid #0891b2;background:#cffafe;box-sizing:border-box}.flex>div{flex:1 1 0;min-width:0;padding:6px;background:#a5f3fc}"
                             b".relative{position:relative;left:8px;padding:6px;background:#dcfce7}.absolute-host{position:relative;height:46px;background:#fef3c7}.absolute{position:absolute;left:160px;top:8px;padding:5px;background:#fdba74}.fixed{position:fixed;left:650px;top:160px;padding:5px;background:#99f6e4;z-index:20}.sticky-host{position:sticky;top:4px;padding:6px;background:#bbf7d0;z-index:10}"
                             b".hidden{width:250px;height:32px;overflow:hidden;padding:4px;border:2px solid #dc2626}.auto{position:relative;width:280px;height:74px;overflow:auto;padding:6px;border:2px solid #7c3aed;background:#ede9fe}.auto .tall{width:620px;height:210px;padding:4px}.auto .scroll-link{position:absolute;left:8px;top:230px;width:220px;height:24px;padding:4px}.following{margin:12px 0;padding:8px;background:#dcfce7}"
                             b"</style></head><body><h1 id='phase7a-heading'>Typography Phase 7A</h1>"
                             b"<p id='phase7a-body'>Normal body text with punctuation, digits 0123456789, and supported extended bytes: caf\xc3\xa9 \xe2\x80\x94 na\xc3\xafve.</p>"
                             b"<p id='phase7a-paragraphs'>Multiple paragraphs keep ordinary flow readable. A second sentence exercises wrapping and vertical spacing after the first paragraph.</p>"
                             b"<h2 id='phase7a-heading-small'>Smaller heading</h2><h3 id='phase7a-heading-large'>Heading size and baseline check</h3>"
                             b"<p><b id='phase7a-bold'>Bold text</b> and <strong id='phase7a-strong'>strong text</strong> with <a id='phase7a-link' href='/navigator-smoke/basic.html'>a wrapped link whose measured width must remain clickable across line breaks</a>.</p>"
                             b"<div class='narrow' id='phase7a-narrow'>Narrow container text wraps according to the selected proportional face and continues on the following line.</div>"
                             b"<div class='family' id='phase7a-family'><p style='font-family:sans-serif'>sans-serif preferred proportional text</p><p style='font-family:Roboto, sans-serif'>Roboto family request</p><p class='unknown'>unknown family falls back safely</p><p class='mono'>monospace family remains distinct</p></div>"
                             b"<pre id='phase7a-pre'>preformatted 0123 !?[]{} g j p q y\nsecond pre line keeps its monospace metrics</pre>"
                             b"<p id='phase7a-code'>Mixed proportional text <code id='phase7a-inline-code'>inline code 1234 g j p q y</code> returns to normal text after code.</p>"
                             b"<div class='flex' id='phase7a-flex'><div>Flex item one wraps with Roboto text.</div><div>Flex item two has constrained intrinsic width and wraps.</div></div>"
                             b"<div class='relative' id='phase7a-relative'>Relative positioned text remains measurable.</div><div class='absolute-host'><span class='absolute' id='phase7a-absolute'>Absolute text</span></div>"
                             b"<div class='fixed' id='phase7a-fixed'>Fixed text link <a id='phase7a-fixed-link' href='/navigator-smoke/basic.html'>fixed link</a></div><div class='sticky-host' id='phase7a-sticky'>Sticky text and <a id='phase7a-sticky-link' href='/navigator-smoke/basic.html'>sticky link</a></div>"
                             b"<div class='hidden' id='phase7a-hidden'>Overflow hidden text must clip without corrupting layout.</div><div class='auto' id='phase7a-auto'><div class='tall'>Scrollable proportional text.<a id='phase7a-scroll-link' class='scroll-link' href='/navigator-smoke/basic.html'>a link after scrolling</a></div></div>"
                             b"<div class='following' id='phase7a-following'>Following normal block flow remains intact after typography cases.</div></body></html>")
            return
        if path == "/navigator-smoke/positioned-link-phase7b.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Navigator Phase 7B Positioned Link Scroll Clip</title><style>"
                             b"html,body{margin:0;padding:0} body{font-family:Roboto,sans-serif;font-size:16px;line-height:1.3;background:#f8fafc;color:#172033}"
                             b"a{color:#1d4ed8}.phase7b-spacer{height:260px}.overlap-spacer{height:180px}.case{margin:8px 0;padding:6px;border:2px solid #64748b;background:#fff}.baseline-link{display:block;width:120px;height:24px;padding:3px;background:#dbeafe}.relative-link{display:block;position:relative;left:18px;top:2px;width:220px;height:24px;background:#dcfce7;border:1px solid #16a34a;padding:3px}.absolute-host{position:relative;width:320px;height:48px;background:#fef3c7;border:2px solid #b45309}.absolute-link{position:absolute;left:150px;top:8px;width:120px;height:24px;padding:4px;background:#fdba74}.scroll-shell{position:relative;width:280px;height:78px;overflow:auto;padding:6px;border:3px solid #7c3aed;background:#ede9fe}.scroll-content{position:relative;width:560px;height:90px;background:#ddd6fe}.scroll-reveal-content{height:260px}.scroll-clip-content{height:150px}.scroll-content a{padding:3px;background:#c4b5fd}.scroll-auto-link{display:block;position:absolute;left:8px;top:66px;width:170px;height:24px}.scroll-partial-link{display:block;position:absolute;left:210px;top:-20px;width:180px;height:30px}.scroll-clipped-link{display:block;position:absolute;left:8px;top:218px;width:180px;height:24px}.scroll-revealed-link{display:block;position:absolute;left:8px;top:250px;width:180px;height:24px}.nested-outer{position:relative;width:330px;height:112px;overflow:auto;padding:6px;border:3px solid #0f766e;background:#ccfbf1}.nested-inner{position:relative;width:300px;height:82px;overflow:scroll;padding:4px;border:2px solid #0891b2;background:#cffafe}.nested-content{position:relative;width:520px;height:360px}.nested-link{display:block;position:absolute;left:180px;top:280px;width:170px;height:24px;background:#67e8f9;padding:3px}.wrapped-host{position:relative;width:300px;height:72px;border:2px solid #be185d;background:#fce7f3}.wrapped-link{display:block;position:absolute;left:8px;top:8px;width:142px;height:48px;padding:3px;background:#f9a8d4}.sticky-host{position:relative;width:300px;height:130px;overflow:auto;border:2px solid #15803d;background:#dcfce7}.sticky-fill{height:300px;padding:4px}.sticky-link{display:block;position:sticky;top:4px;width:220px;height:24px;background:#86efac;padding:3px}.fixed-link{position:fixed;left:700px;top:120px;width:150px;height:24px;padding:4px;background:#99f6e4;z-index:40}.scrollbar-host{position:relative;width:240px;height:72px;overflow:scroll;border:3px solid #475569;background:#e2e8f0}.scrollbar-fill{position:relative;width:480px;height:220px}.under-scrollbar-link{display:block;position:absolute;left:205px;top:8px;width:130px;height:24px;background:#fda4af;padding:3px;z-index:1}.overlap{position:fixed;left:400px;top:300px;width:300px;height:76px;margin:0;border:2px solid #334155;background:#f1f5f9;z-index:30}.overlap-link{display:block;position:absolute;left:20px;top:12px;width:150px;height:24px;padding:3px}.z-low{background:#fecdd3;z-index:1}.z-high{background:#86efac;z-index:3}.equal-a{left:52px;top:44px;background:#bfdbfe;z-index:2}.equal-b{left:52px;top:44px;background:#c4b5fd;z-index:2}.absolute-wrapper{position:absolute;left:12px;top:8px;width:240px;height:30px;background:#e0e7ff;padding:4px}.proportional-link{display:block;font-family:Roboto,sans-serif;width:220px;height:24px;padding:3px;background:#dbeafe}.mono-link{display:block;font-family:monospace;width:220px;height:24px;padding:4px;background:#e5e7eb}.following{margin:12px 0;padding:8px;background:#dcfce7}"
                              b"</style></head><body><div class='phase7b-spacer'></div><div class='case' id='phase7b-baseline'>Baseline <a id='phase7b-ordinary' class='baseline-link' href='/navigator-smoke/basic.html'>ordinary link</a>.</div>"
                             b"<div class='case'>Relative <a id='phase7b-relative' class='relative-link' href='/navigator-smoke/basic.html'>relative positioned link</a>.</div>"
                             b"<div class='case'><div class='absolute-host' id='phase7b-absolute-host'>Absolute <a id='phase7b-absolute' class='absolute-link' href='/navigator-smoke/basic.html'>absolute link</a></div></div>"
                              b"<div class='case'><div class='scroll-shell' id='phase7b-auto'><div class='scroll-content'><a id='phase7b-auto-visible' class='scroll-auto-link' href='/navigator-smoke/basic.html'>auto visible link</a></div></div></div>"
                              b"<div class='case'><div class='scroll-shell' id='phase7b-auto-partial-host'><div class='scroll-content scroll-clip-content'><a id='phase7b-auto-partial' class='scroll-partial-link' href='/navigator-smoke/basic.html'>partially clipped auto link</a></div></div></div>"
                              b"<div class='case'><div class='scroll-shell' id='phase7b-auto-clipped-host'><div class='scroll-content scroll-clip-content'><a id='phase7b-auto-clipped' class='scroll-clipped-link' href='/navigator-smoke/basic.html'>fully clipped auto link</a></div></div></div>"
                              b"<div class='case'><div class='scroll-shell' id='phase7b-auto-revealed-host'><div class='scroll-content scroll-reveal-content'><a id='phase7b-auto-revealed' class='scroll-revealed-link' href='/navigator-smoke/basic.html'>revealed after more scroll</a></div></div></div>"
                              b"<div class='case'><div class='scroll-shell' id='phase7b-scroll'><div class='scroll-content'><a id='phase7b-scroll-link' class='scroll-auto-link' href='/navigator-smoke/basic.html'>scroll mode positioned link</a></div></div></div>"
                             b"<div class='case'><div class='nested-outer' id='phase7b-outer'><div class='nested-inner' id='phase7b-inner'><div class='nested-content'><a id='phase7b-nested-link' class='nested-link' href='/navigator-smoke/basic.html'>nested positioned link</a></div></div></div></div>"
                             b"<div class='case'><div class='wrapped-host'><a id='phase7b-wrapped-link' class='wrapped-link' href='/navigator-smoke/basic.html'>wrapped positioned link keeps each visible multiline fragment independently clickable</a></div></div>"
                             b"<div class='case'><div class='sticky-host' id='phase7b-sticky-host'><div class='sticky-fill'><a id='phase7b-sticky-link' class='sticky-link' href='/navigator-smoke/basic.html'>sticky hyperlink regression</a><div style='height:220px'>sticky scroll content</div></div></div></div>"
                             b"<a id='phase7b-fixed-link' class='fixed-link' href='/navigator-smoke/basic.html'>fixed hyperlink</a>"
                             b"<div class='case'><div class='scrollbar-host' id='phase7b-scrollbar'><div class='scrollbar-fill'><a id='phase7b-under-scrollbar' class='under-scrollbar-link' href='/navigator-smoke/basic.html'>under scrollbar</a></div></div></div>"
                             b"<div class='overlap-spacer'></div><div class='case'><div class='overlap'><a id='phase7b-z-low' class='overlap-link z-low' href='/navigator-smoke/basic.html'>low link</a><a id='phase7b-z-high' class='overlap-link z-high' href='/navigator-smoke/basic.html'>high link</a><a id='phase7b-equal-a' class='overlap-link equal-a' href='/navigator-smoke/basic.html'>equal A</a><a id='phase7b-equal-b' class='overlap-link equal-b' href='/navigator-smoke/basic.html'>equal B</a></div></div>"
                             b"<div class='case' style='height:52px;position:relative'><div class='absolute-wrapper'><a id='phase7b-wrapper-link' href='/navigator-smoke/basic.html'>descendant inside absolute wrapper</a></div></div>"
                             b"<div class='case'><a id='phase7b-proportional' class='proportional-link' href='/navigator-smoke/basic.html'>Roboto proportional geometry</a> <a id='phase7b-mono' class='mono-link' href='/navigator-smoke/basic.html'>monospace geometry</a></div>"
                             b"<div class='following' id='phase7b-following'>Following ordinary content remains after the positioned-link cases.</div></body></html>")
            return
        if path == "/navigator-smoke/css-phase3h.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3H Traditional Positioning Completion</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".stack-root{position:relative;width:360px;height:150px;margin:12px 0;padding:8px;border:3px solid #1d4ed8;background:#dbeafe;overflow:hidden;z-index:1}"
                             b".stack-child-high{position:absolute;left:22px;top:22px;width:116px;height:28px;background:#fecdd3;z-index:999}"
                             b".stack-child-negative{position:absolute;left:38px;top:58px;width:118px;height:24px;background:#94a3b8;z-index:-10}"
                             b".stack-external{position:absolute;left:72px;top:30px;width:130px;height:30px;background:#bbf7d0;z-index:2}"
                             b".stack-auto{position:absolute;left:232px;top:12px;width:92px;height:22px;background:#fef3c7;z-index:auto}"
                             b".stack-zero{position:absolute;left:232px;top:42px;width:92px;height:22px;background:#fde68a;z-index:0}"
                             b".stack-negative{position:absolute;left:232px;top:72px;width:92px;height:22px;background:#cbd5e1;z-index:-1}"
                             b".stack-positive-a{position:absolute;left:232px;top:102px;width:92px;height:22px;background:#fda4af;z-index:4}"
                             b".stack-positive-b{position:absolute;left:232px;top:120px;width:92px;height:22px;background:#fb7185;z-index:4}"
                             b".nested-owner{position:relative;z-index:1;width:240px;height:36px;padding:4px;background:#e0e7ff;border:1px solid #4338ca}"
                             b".nested-record{position:absolute;left:18px;top:6px;width:104px;height:18px;background:#c4b5fd;z-index:999}"
                             b".inline-lane{width:250px;border:1px solid #15803d;padding:6px;overflow:hidden}"
                             b".relative-inline{position:relative;left:12px;top:3px;background:#bbf7d0;border:1px solid #16a34a}"
                             b".relative-link{position:relative;left:-6px;top:2px;background:#dbeafe;border:1px solid #2563eb;z-index:1}"
                             b".nested-inline{position:relative;left:5px;top:-2px;background:#fef3c7;border:1px solid #d97706;z-index:999}"
                             b".relative-label{position:relative;left:8px;top:2px;background:#fce7f3;border:1px solid #db2777}"
                             b".inline-cb{position:relative;left:7px;top:1px;background:#e0e7ff;border:1px solid #4f46e5}"
                             b".inline-cb-abs{position:absolute;left:22px;top:8px;width:92px;height:18px;background:#ddd6fe;z-index:3}"
                             b".static-auto{position:absolute;width:90px;height:18px;background:#e2e8f0}"
                             b".static-inline{position:absolute;padding:2px;background:#f1f5f9}"
                             b".float-relative-left{float:left;position:relative;left:18px;width:74px;height:34px;margin-right:6px;background:#bfdbfe}"
                             b".float-relative-right{float:right;position:relative;right:12px;width:74px;height:34px;margin-left:6px;background:#fed7aa}"
                             b".bfc-relative{position:relative;overflow:hidden;width:250px;height:38px;border:1px solid #0f766e;background:#ecfdf5}"
                             b".opacity-owner{position:relative;z-index:3;opacity:.5;background:#c4b5fd;padding:4px}"
                             b".opacity-child{position:absolute;left:10px;top:4px;z-index:99;background:#a7f3d0;width:90px;height:18px}"
                             b".rel-image{position:relative;left:9px;top:-2px;width:48px;height:24px} .rel-control{position:relative;left:5px;top:2px}"
                             b".abs-button{position:absolute;left:320px;top:12px;width:88px;height:24px;background:#fde68a;z-index:5}"
                             b".rel-table{position:relative;left:4px;border:1px solid #7c3aed}.rel-cell{position:relative;left:3px}"
                             b".clip-image{position:absolute;left:-10px;top:4px;width:52px;height:24px;background:#fca5a5;z-index:7}"
                             b"</style></head><body>"
                             b"<h1 id='phase3h-title'>Phase 3H Traditional Positioning Completion Fixture</h1>"
                             b"<p id='phase3h-order'>auto versus auto source order; zero versus auto; positive siblings; negative siblings; equal positive source order.</p>"
                             b"<div id='phase3h-stack' class='stack-root'>stacking owner content "
                             b"<div id='phase3h-child-high' class='stack-child-high'>nested child z 999 stays inside parent z 1</div>"
                             b"<div id='phase3h-child-negative' class='stack-child-negative'>negative child inside positive owner</div><img id='phase3h-negative-record' class='stack-child-negative' src='/navigator-smoke/wide.png' alt='negative positioned record'>"
                             b"<div id='phase3h-external' class='stack-external'>external z 2 stays above parent owner</div>"
                             b"<div id='phase3h-auto' class='stack-auto'>auto owner</div><div id='phase3h-zero' class='stack-zero'>zero owner</div>"
                             b"<div id='phase3h-negative' class='stack-negative'>negative sibling</div><div id='phase3h-positive-a' class='stack-positive-a'>positive A</div><div id='phase3h-positive-b' class='stack-positive-b'>positive B source order</div>"
                             b"</div>"
                             b"<p id='phase3h-inline-wrap' class='inline-lane'><span class='relative-inline'>wrapped relative inline owner moves every fragment across lines with background and border.</span> <a id='phase3h-link' class='relative-link' href='/navigator-smoke/basic.html'>wrapped relative link remains fragment-hit-testable with focus geometry. <strong class='nested-inline'>nested relative strong inside relative link.</strong> <img id='phase3h-nested-record' class='nested-record' src='/navigator-smoke/wide.png' alt='nested record z 999'></a> <label id='phase3h-label' class='relative-label' for='phase3h-check'>relative label shifts with its fragments</label> <img id='phase3h-image' class='rel-image' src='/navigator-smoke/wide.png' alt='relative inline image'> <input id='phase3h-check' class='rel-control' type='checkbox' checked></p>"
                             b"<p id='phase3h-inline-cb' class='inline-lane'><span id='phase3h-cb-owner' class='inline-cb'>wrapped relative inline containing block with an <span id='phase3h-cb-child' class='inline-cb-abs'>absolute child</span> and enough text to create several fragments for bounded first-last LTR geometry.</span></p>"
                             b"<div id='phase3h-static-auto' class='static-auto'>absolute all offsets auto uses static block snapshot</div><span id='phase3h-static-inline' class='static-inline'>absolute inline-source static snapshot</span>"
                             b"<p id='phase3h-offsets'><span id='phase3h-horizontal' style='position:absolute;width:84px;left:auto;right:auto'>auto horizontal offsets</span><span id='phase3h-vertical' style='position:absolute;height:18px;top:auto;bottom:auto'>auto vertical offsets</span></p>"
                             b"<p id='phase3h-floats'><span id='phase3h-float-left' class='float-relative-left'>relative left float</span><span id='phase3h-float-right' class='float-relative-right'>relative right float</span>relative float exclusion uses normal-flow geometry while visual offsets move the painted boxes.</p>"
                             b"<div id='phase3h-bfc' class='bfc-relative'>relative overflow BFC remains owned and clipped without changing exclusion geometry.</div>"
                             b"<div id='phase3h-opacity' class='opacity-owner'><div id='phase3h-opacity-child' class='opacity-child'>opacity alpha only; no opacity stacking owner</div>positioned opacity parent and child z-index stay in bounded model.</div>"
                             b"<button id='phase3h-button' class='abs-button' type='button'>absolute button</button><p id='phase3h-controls'><label for='phase3h-check'>linked control label</label> <img src='/navigator-smoke/wide.png' alt='linked positioned image'></p>"
                             b"<ul id='phase3h-list'><li id='phase3h-list-item' style='position:relative;left:6px'>relative list item with absolute child <span style='position:absolute;left:30px;top:3px'>list absolute child</span></li></ul>"
                             b"<table id='phase3h-table' class='rel-table'><tr><td id='phase3h-cell' class='rel-cell'>relative table and cell classified safely</td></tr></table>"
                             b"<div id='phase3h-clip' class='stack-root' style='height:30px'><img id='phase3h-clipped-image' class='clip-image' src='/navigator-smoke/wide.png' alt='clipped positioned image'></div>"
                             b"<p id='phase3h-lifecycle'>recomputation reload history generated page error page stale positioned hit blocked.</p>"
                             b"<p id='phase3h-regression'>CSS 1A through 3G, forms focus, Save Page Text, Page Info, hosted paint, and TLS contract remain regression rails.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase3e.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3E Floats and Clear</title><style>"
                             b"html,body{margin:0;padding:0} body{font-size:14px;line-height:1.25;background:#f8fafc;color:#243447}"
                             b".left{float:left;width:132px;height:76px;margin:0 14px 8px 0;padding:4px;border:2px solid #2563eb;background:#dbeafe;box-sizing:border-box}"
                             b".right{float:right;width:118px;height:64px;margin:0 0 8px 14px;padding:4px;border:2px solid #b45309;background:#fef3c7;box-sizing:border-box}"
                             b".bfc{overflow:hidden;width:260px;margin:8px 0;padding:4px;border:1px solid #0f766e;background:#ecfdf5}"
                             b".clear-left{clear:left}.clear-right{clear:right}.clear-both{clear:both}"
                             b".hidden{visibility:hidden}.zero{opacity:0}.invalid{float:inline-start}"
                             b".cascade{float:left}.cascade{float:right}.cascade-important{float:left !important}.cascade-inline{float:right}"
                             b"</style></head><body>"
                             b"<h1 id='phase3e-title'>Phase 3E Float and Clear Fixture</h1>"
                             b"<p id='phase3e-left-para'><img id='phase3e-left-image' class='left' src='/navigator-smoke/wide.png' alt='left floated image'>Left float text wraps beside the image and returns to the full content width after the float bottom. This sentence is long enough to exercise multiple line intervals beside a margin-box exclusion.</p>"
                             b"<p id='phase3e-right-para'><img id='phase3e-right-image' class='right' src='/navigator-smoke/tall.png' alt='right floated image'>Right float text wraps from the opposite edge. The center interval remains authoritative while the right margin box is active and returns below it.</p>"
                             b"<p id='phase3e-mixed'><span id='phase3e-inline-left' class='left'>inline blockified float</span><span id='phase3e-inline-right' class='right'>right box</span>Mixed left and right float text with a narrow center interval and a forced<br>break marker.</p>"
                             b"<p id='phase3e-clear-left' class='clear-left'>clear left moves below the left float.</p>"
                             b"<p id='phase3e-clear-right' class='clear-right'>clear right moves below the right float.</p>"
                             b"<p id='phase3e-clear-both' class='clear-both'>clear both moves below both floats.</p>"
                             b"<div id='phase3e-bfc' class='bfc'><p id='phase3e-bfc-text'>overflow hidden BFC avoids an active float and keeps its internal flow bounded.</p></div>"
                             b"<p id='phase3e-hidden' class='left hidden'>hidden float still excludes</p><p id='phase3e-opacity' class='right zero'>opacity zero float still excludes</p>"
                             b"<p id='phase3e-cascade' class='cascade'>cascade winner</p><p id='phase3e-important' class='cascade-important'>important winner</p><p id='phase3e-inline' class='cascade-inline' style='float:left'>inline winner</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase3d.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3D Margin Collapse</title><style>"
                             b"html, body { margin:0; padding:0; } body { color:#243447; background:#f8fafc; font-size:14px; line-height:1.25; }"
                             b".fixture-block { width:300px; min-height:10px; background:#eef2ff; }"
                             b".fixture-block > div, .fixture-block > p { height:12px; background:#dbeafe; }"
                             b"#phase3d-sibling-a { margin:10px 0 20px; } #phase3d-sibling-b { margin:20px 0 10px; }"
                             b"#phase3d-mixed-a { margin:5px 0 20px; } #phase3d-mixed-b { margin:-5px 0 10px; }"
                             b"#phase3d-negative-a { margin:-10px 0 -20px; } #phase3d-negative-b { margin:-20px 0 0; }"
                             b"#phase3d-zero-negative-a { margin:0 0 0; } #phase3d-zero-negative-b { margin:-10px 0 0; }"
                             b"#phase3d-chain-a { margin:10px 0 20px; } #phase3d-chain-b { margin:30px 0 40px; } #phase3d-chain-c { margin:40px 0 0; }"
                             b"#phase3d-parent-top { margin:10px 0; background:#e0f2fe; } #phase3d-parent-top > p { margin-top:20px; }"
                             b"#phase3d-border-top { margin:10px 0; border-top:2px solid #7c3aed; } #phase3d-border-top > p { margin-top:20px; }"
                             b"#phase3d-padding-top { margin:10px 0; padding-top:3px; } #phase3d-padding-top > p { margin-top:20px; }"
                             b"#phase3d-inline-top { margin:10px 0; } #phase3d-inline-top > p { margin-top:20px; }"
                             b"#phase3d-parent-bottom { margin:10px 0 20px; } #phase3d-parent-bottom > p { margin-bottom:30px; }"
                             b"#phase3d-border-bottom { margin:10px 0 20px; border-bottom:2px solid #dc2626; } #phase3d-border-bottom > p { margin-bottom:30px; }"
                             b"#phase3d-padding-bottom { margin:10px 0 20px; padding-bottom:3px; } #phase3d-padding-bottom > p { margin-bottom:30px; }"
                             b"#phase3d-height-bottom { height:30px; margin:10px 0 20px; } #phase3d-height-bottom > p { margin-bottom:30px; }"
                             b"#phase3d-min-height-bottom { min-height:30px; margin:10px 0 20px; } #phase3d-min-height-bottom > p { margin-bottom:30px; }"
                             b"#phase3d-empty-one { margin:12px 0 18px; } #phase3d-empty-two { margin:-6px 0 9px; }"
                             b"#phase3d-empty-bordered { margin:8px 0; border:1px solid #0f766e; } #phase3d-empty-padded { margin:8px 0; padding:2px; }"
                             b"#phase3d-empty-sized { height:8px; margin:8px 0; } #phase3d-empty-min { min-height:8px; margin:8px 0; }"
                             b"#phase3d-overflow-hidden { overflow:hidden; margin:10px 0; padding:2px; border:1px solid #1d4ed8; } #phase3d-overflow-hidden > p { margin-top:20px; margin-bottom:20px; }"
                             b"#phase3d-overflow-auto { overflow:auto; margin:10px 0; } #phase3d-overflow-auto > p { margin-top:12px; }"
                             b"#phase3d-overflow-scroll { overflow:scroll; margin:10px 0; } #phase3d-overflow-scroll > p { margin-top:12px; }"
                             b"#phase3d-overflow-visible { overflow:visible; margin:10px 0; } #phase3d-overflow-visible > p { margin-top:12px; }"
                             b"#phase3d-inline-block { display:inline-block; width:220px; margin:10px; padding:4px; border:2px solid #b45309; vertical-align:top; } #phase3d-inline-block > p { margin:20px 0 30px; }"
                             b"#phase3d-inline-block > div { margin:8px 0 16px; }"
                             b"#phase3d-nested-atomic { display:inline-block; width:180px; padding:3px; border:1px solid #be123c; } #phase3d-nested-atomic > div { display:inline-block; margin:12px; padding:2px; border:1px solid #be123c; }"
                             b"#phase3d-link { margin:20px 0; padding:2px; border:1px solid #2563eb; background:#dbeafe; } #phase3d-control { margin:15px 0; }"
                             b"#phase3d-extreme { margin-top:-999999px; margin-bottom:-999999px; }"
                             b"</style></head><body>"
                             b"<h1 id='phase3d-title'>Phase 3D Margin Collapse Fixture</h1>"
                             b"<section id='phase3d-siblings' class='fixture-block'><div id='phase3d-sibling-a'>sibling positive ten and twenty</div><div id='phase3d-sibling-b'>sibling twenty and ten</div><div id='phase3d-mixed-a'>mixed positive margin</div><div id='phase3d-mixed-b'>mixed negative margin</div><div id='phase3d-negative-a'>negative ten and twenty</div><div id='phase3d-negative-b'>negative continuation</div><div id='phase3d-zero-negative-a'>zero margin</div><div id='phase3d-zero-negative-b'>zero and negative</div><div id='phase3d-chain-a'>three margin chain one</div><div id='phase3d-chain-b'>three margin chain two</div><div id='phase3d-chain-c'>three margin chain three</div></section>"
                             b"<section id='phase3d-parent-top' class='fixture-block'><p id='phase3d-parent-top-child'>parent top and first child top collapse</p></section>"
                             b"<section id='phase3d-border-top' class='fixture-block'><p id='phase3d-border-top-child'>top border blocks collapse</p></section>"
                             b"<section id='phase3d-padding-top' class='fixture-block'><p id='phase3d-padding-top-child'>top padding blocks collapse</p></section>"
                             b"<section id='phase3d-inline-top' class='fixture-block'>inline lead before child<p id='phase3d-inline-top-child'>inline content blocks collapse</p></section>"
                             b"<section id='phase3d-parent-bottom' class='fixture-block'><p id='phase3d-parent-bottom-child'>parent bottom collapse</p></section>"
                             b"<section id='phase3d-border-bottom' class='fixture-block'><p id='phase3d-border-bottom-child'>bottom border blocks collapse</p></section>"
                             b"<section id='phase3d-padding-bottom' class='fixture-block'><p id='phase3d-padding-bottom-child'>bottom padding blocks collapse</p></section>"
                             b"<section id='phase3d-height-bottom' class='fixture-block'><p id='phase3d-height-bottom-child'>definite height blocks bottom collapse</p></section>"
                             b"<section id='phase3d-min-height-bottom' class='fixture-block'><p id='phase3d-min-height-bottom-child'>minimum height interaction</p></section>"
                             b"<section id='phase3d-empty-chain' class='fixture-block'><div id='phase3d-empty-one'></div><div id='phase3d-empty-two'></div><p id='phase3d-empty-follow'>empty blocks join the margin chain</p></section>"
                             b"<div id='phase3d-empty-bordered'></div><div id='phase3d-empty-padded'></div><div id='phase3d-empty-sized'></div><div id='phase3d-empty-min'></div>"
                             b"<section id='phase3d-overflow-hidden' class='fixture-block'><p id='phase3d-overflow-hidden-child'>overflow hidden establishes a bounded BFC</p></section>"
                             b"<section id='phase3d-overflow-auto' class='fixture-block'><p id='phase3d-overflow-auto-child'>overflow auto establishes a bounded BFC</p></section>"
                             b"<section id='phase3d-overflow-scroll' class='fixture-block'><p id='phase3d-overflow-scroll-child'>overflow scroll establishes a bounded BFC</p></section>"
                             b"<section id='phase3d-overflow-visible' class='fixture-block'><p id='phase3d-overflow-visible-child'>overflow visible remains normal flow</p></section>"
                             b"<div id='phase3d-inline-block'><p id='phase3d-inline-block-child'>inline block contains internal margins</p><div id='phase3d-inline-block-inner'>internal sibling collapse</div></div>"
                             b"<div id='phase3d-nested-atomic'><div id='phase3d-nested-atomic-child'>nested atomic context</div></div>"
                             b"<a id='phase3d-link' href='/navigator-smoke/basic.html'>link follows collapsed geometry</a><button id='phase3d-control' type='button'>Phase 3D control focus target</button>"
                             b"<div id='phase3d-extreme'>extreme negative margin remains bounded</div>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase3a.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>CSS Phase 3A Box Constraints</title><style>"
                             b"body { color:#243447; background:#f8fafc; }"
                             b"#phase3a-parent { box-sizing:border-box; width:70%; height:180px; padding:10px; border:2px solid #334155; overflow:hidden; }"
                             b"#phase3a-percent { width:50%; height:50%; padding:4px; border:1px solid #2563eb; box-sizing:content-box; }"
                             b"#phase3a-indefinite { height:50%; min-height:24px; padding:2px; }"
                             b"#phase3a-indefinite-parent { padding:2px; border:1px solid #64748b; } #phase3a-indefinite-child { height:50%; min-height:24px; }"
                             b"#phase3a-min { width:10px; min-width:180px; padding:3px; border:2px solid #0f766e; }"
                             b"#phase3a-max { width:90%; max-width:160px; padding:3px; border:2px solid #b45309; }"
                             b"#phase3a-conflict { width:20px; min-width:130px; max-width:40px; padding:2px; }"
                             b"#phase3a-max-height { height:90px; max-height:32px; padding:2px; border:1px solid #be123c; }"
                             b"#phase3a-small-border { box-sizing:border-box; width:12px; height:12px; padding:10px; border:4px solid #7c3aed; }"
                             b"#phase3a-overflow { width:120px; height:26px; padding:3px; overflow:hidden; border:1px solid #dc2626; }"
                             b"#phase3a-axis { width:120px; height:28px; overflow-x:hidden; overflow-y:visible; border:1px solid #0891b2; }"
                             b"#phase3a-auto { width:auto; height:auto; max-width:220px; min-height:30px; overflow:auto; border:1px solid #64748b; }"
                             b"#phase3a-scroll { width:110px; height:24px; overflow:scroll; border:1px solid #7c3aed; }"
                             b"#phase3a-hidden { visibility:hidden; padding:4px; border:2px solid #ef4444; }"
                             b"#phase3a-opacity-parent { opacity:.5; border:1px solid #9333ea; padding:3px; }"
                             b"#phase3a-opacity-child { opacity:50%; padding:3px; }"
                             b"#phase3a-zero { opacity:0; }"
                             b"#phase3a-important { overflow:scroll; overflow:visible !important; overflow-x:hidden; width:110px; height:24px; }"
                             b"#phase3a-explicit-content { box-sizing:content-box; width:80px; height:24px; padding:6px; border:2px solid #0f766e; }"
                             b"#phase3a-border-box-height { box-sizing:border-box; width:100px; height:40px; padding:10px; border:4px solid #1d4ed8; }"
                             b"#phase3a-auto-max { width:auto; max-width:180px; padding:4px; border:1px solid #0369a1; }"
                             b"#phase3a-auto-min-height { height:auto; min-height:46px; padding:3px; border:1px solid #0369a1; }"
                             b"#phase3a-min-height { height:8px; min-height:44px; padding:2px; border:1px solid #0f766e; }"
                             b"#phase3a-max-width { width:320px; max-width:140px; padding:2px; border:1px solid #b45309; }"
                             b"#phase3a-max-height { height:90px; max-height:32px; padding:2px; border:1px solid #be123c; }"
                             b"#phase3a-max-none { width:160px; max-width:none; padding:2px; }"
                             b"#phase3a-border-box-min { box-sizing:border-box; width:40px; min-width:120px; padding:12px; border:3px solid #9333ea; }"
                             b"#phase3a-nested-percent-parent { width:80%; padding:4px; border:1px solid #64748b; } #phase3a-nested-percent-child { width:50%; padding:2px; border:1px solid #2563eb; } #phase3a-nested-percent-grandchild { width:50%; padding:2px; border:1px solid #0891b2; }"
                             b"#phase3a-parent-padding-percent { width:60%; padding:12px; border:2px solid #64748b; } #phase3a-parent-padding-child { box-sizing:border-box; width:50%; padding:4px; border:2px solid #2563eb; }"
                             b"#phase3a-definite-height { height:120px; padding:4px; border:1px solid #64748b; } #phase3a-definite-height-child { height:50%; min-height:20px; border:1px solid #2563eb; }"
                             b"#phase3a-percent-cycle-a { width:50%; } #phase3a-percent-cycle-b { width:50%; } #phase3a-extreme-percent { width:9999%; min-width:9999%; max-width:9999%; }"
                             b"#phase3a-overflow-visible { width:70px; height:24px; overflow:visible; border:1px solid #16a34a; background:#dcfce7; }"
                             b"#phase3a-overflow-y-hidden { width:140px; height:24px; overflow-x:visible; overflow-y:hidden; border:1px solid #0891b2; background:#cffafe; }"
                             b"#phase3a-nested-clip { width:160px; height:46px; overflow:hidden; padding:3px; border:2px solid #dc2626; } #phase3a-inner-clip { width:130px; height:60px; overflow:hidden; padding:3px; border:2px solid #7c3aed; background:#ede9fe; }"
                             b"#phase3a-clipped-border { width:100px; height:22px; overflow:hidden; padding:5px; border:8px solid #dc2626; background:#fee2e2; }"
                             b"#phase3a-auto-scroll { width:110px; height:24px; overflow:auto; border:1px solid #7c3aed; }"
                             b"#phase3a-scroll-deferred { width:110px; height:24px; overflow:scroll; border:1px solid #7c3aed; }"
                             b"#phase3a-visibility-parent { visibility:hidden; padding:3px; border:1px solid #ef4444; } #phase3a-visibility-restored { visibility:visible; }"
                             b"#phase3a-display-none { display:none; }"
                             b"#phase3a-opacity-one { opacity:1; } #phase3a-opacity-half { opacity:0.5; } #phase3a-opacity-high { opacity:150%; } #phase3a-opacity-invalid { opacity:not-a-number; } #phase3a-opacity-control { opacity:0; }"
                             b"#phase3a-cascade-width { width:40px; } #phase3a-cascade-width { width:80px; } .phase3a-class-width { width:100px; } p.phase3a-class-width { width:120px; } #phase3a-inline-cascade { box-sizing:content-box; }"
                             b"#phase3a-partial-cascade { min-width:80px; } #phase3a-partial-cascade { max-width:150px; } #phase3a-invalid-preserve { width:90px; width:not-a-length; }"
                             b"#phase3a-valign-middle { vertical-align:middle; } #phase3a-valign-top { vertical-align:top; } #phase3a-valign-bottom { vertical-align:bottom; } #phase3a-valign-text-top { vertical-align:text-top; } #phase3a-valign-text-bottom { vertical-align:text-bottom; } #phase3a-valign-sub { vertical-align:sub; } #phase3a-valign-super { vertical-align:super; } #phase3a-valign-px { vertical-align:4px; } #phase3a-valign-percent { vertical-align:25%; }"
                             b"#phase3a-table { border:1px solid #475569; } #phase3a-table td { height:42px; vertical-align:middle; padding:3px; border:1px solid #94a3b8; }"
                             b"#phase3a-table .top { vertical-align:top; } #phase3a-table .bottom { vertical-align:bottom; }"
                             b"#phase3a-image { width:96px; max-width:120px; height:auto; border:2px solid #1d4ed8; box-sizing:border-box; }"
                             b"#phase3a-form { width:220px; min-width:180px; box-sizing:border-box; }"
                             b"</style></head><body>"
                             b"<h1 id='phase3a-heading'>Phase 3A Box Constraint Fixture</h1>"
                             b"<div id='phase3a-parent'><p id='phase3a-percent'>50 percent nested child marker.</p><p id='phase3a-indefinite'>Definite parent percentage height resolves.</p><p id='phase3a-overflow'>Hidden overflow text must be clipped beyond the fixed box.</p></div>"
                             b"<div id='phase3a-indefinite-parent'><p id='phase3a-indefinite-child'>Indefinite percentage height becomes auto.</p></div><p id='phase3a-min'>Minimum width enlarges this box.</p><p id='phase3a-max'>Maximum width constrains this box.</p><p id='phase3a-conflict'>Minimum wins over maximum marker.</p><p id='phase3a-max-height'>Maximum height constrains this box.</p><p id='phase3a-small-border'>Padding and border exceed border-box marker.</p>"
                             b"<p id='phase3a-axis'>Axis clipping marker with a deliberately long line.</p><p id='phase3a-auto'>Auto width and content-derived auto height marker.</p><p id='phase3a-scroll'>Deferred scroll marker.</p>"
                             b"<p id='phase3a-hidden'>Hidden visibility marker must retain space but not paint or extract.</p><p id='phase3a-opacity-parent'>Opacity group <span id='phase3a-opacity-child'>nested opacity multiplication</span>.</p><p id='phase3a-zero'>Zero opacity remains layout-present.</p><p id='phase3a-important'>Important overflow cascade marker.</p>"
                             b"<p id='phase3a-explicit-content'>Explicit content-box marker.</p><p id='phase3a-border-box-height'>Border-box height includes padding and border.</p><p id='phase3a-auto-max'>Auto width constrained by max-width marker.</p><p id='phase3a-auto-min-height'>Auto height enlarged by min-height marker.</p><p id='phase3a-min-height'>Explicit min-height enlarges marker.</p><p id='phase3a-max-width'>Explicit max-width constrains marker.</p><p id='phase3a-max-none'>Max-width none marker.</p><p id='phase3a-border-box-min'>Border-box min constraint marker.</p>"
                             b"<div id='phase3a-nested-percent-parent'><p id='phase3a-nested-percent-child'>Nested percentage child marker.</p></div><div id='phase3a-nested-percent-grandchild'>Nested percentage grandchild marker.</div><div id='phase3a-parent-padding-percent'><p id='phase3a-parent-padding-child'>Percentage with parent padding marker.</p></div><div id='phase3a-definite-height'><p id='phase3a-definite-height-child'>Definite parent height percentage marker.</p></div><p id='phase3a-percent-cycle-a'>Percentage cycle guard A.</p><p id='phase3a-percent-cycle-b'>Percentage cycle guard B.</p><p id='phase3a-extreme-percent'>Extreme percentage clamp marker.</p>"
                             b"<div id='phase3a-depth-01'><div id='phase3a-depth-02'><div id='phase3a-depth-03'><div id='phase3a-depth-04'><div id='phase3a-depth-05'><div id='phase3a-depth-06'><div id='phase3a-depth-07'><div id='phase3a-depth-08'><div id='phase3a-depth-09'><div id='phase3a-depth-10'><div id='phase3a-depth-11'><div id='phase3a-depth-12'><div id='phase3a-depth-13'>Percentage depth clamp marker.</div></div></div></div></div></div></div></div></div></div></div></div></div>"
                             b"<p id='phase3a-overflow-visible'>Visible overflow paint marker with long content outside the fixed box.</p><p id='phase3a-overflow-y-hidden'>Y hidden and X visible axis marker with long content.</p><div id='phase3a-nested-clip'><div id='phase3a-inner-clip'>Nested clipping text marker that exceeds both clips.</div></div><p id='phase3a-clipped-border'>Clipped border and background marker.</p><p id='phase3a-auto-scroll'>Auto overflow bounded clipping marker.</p><p id='phase3a-scroll-deferred'>Scroll overflow deferred marker.</p>"
                             b"<p id='phase3a-visibility-parent'>Hidden parent retains layout.<span id='phase3a-visibility-restored' style='visibility:visible;'>Descendant visibility restoration marker.</span></p><p id='phase3a-display-none'>Display none removes layout marker.</p><p id='phase3a-opacity-one'>Opacity one marker.</p><p id='phase3a-opacity-half'>Opacity one-half marker.</p><p id='phase3a-opacity-high'>Out of range opacity clamp marker.</p><p id='phase3a-opacity-invalid'>Invalid opacity preserves prior style marker.</p>"
                             b"<p id='phase3a-cascade-width'>Later equal-specificity width marker.</p><p id='phase3a-class-width' class='phase3a-class-width'>Class and element specificity marker.</p><p id='phase3a-inline-cascade' style='box-sizing:border-box; width:130px;'>Inline box-sizing cascade marker.</p><p id='phase3a-partial-cascade'>Partial cascade preserves min/max marker.</p><p id='phase3a-invalid-preserve'>Invalid width preserves valid winner marker.</p>"
                             b"<table id='phase3a-table'><tr><td class='top' id='phase3a-top'>Top</td><td id='phase3a-middle'>Middle</td><td class='bottom' id='phase3a-bottom'>Bottom</td></tr></table>"
                             b"<table id='phase3a-valign-table'><tr><td id='phase3a-valign-middle'>Middle</td><td id='phase3a-valign-top'>Top</td><td id='phase3a-valign-bottom'>Bottom</td><td id='phase3a-valign-text-top'>Text top</td><td id='phase3a-valign-text-bottom'>Text bottom</td><td id='phase3a-valign-sub'>Sub</td><td id='phase3a-valign-super'>Super</td><td id='phase3a-valign-px'>Pixels</td><td id='phase3a-valign-percent'>Percent</td></tr></table>"
                             b"<img id='phase3a-image' src='/navigator-smoke/wide.png' alt='Intrinsic ratio image marker'><form id='phase3a-form'><input id='phase3a-control' type='text' value='Readable constrained control'><input id='phase3a-hidden-control' type='checkbox' style='visibility:hidden'><button id='phase3a-button' type='button'>Visible control</button></form>"
                             b"<div id='phase3a-control-clip' style='width:100px;height:24px;overflow:hidden;border:1px solid #dc2626;'><input id='phase3a-clipped-control' type='text' value='Clipped control target'></div><input id='phase3a-opacity-control' type='checkbox' style='opacity:0' checked>"
                             b"<p id='phase3a-inline' style='box-sizing:border-box; width:160px; padding:8px; border:2px solid #16a34a;'>Inline box-sizing marker.</p></body></html>")
            return
        if path == "/navigator-smoke/css-phase2a.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><link rel=\"stylesheet\" href=\"/navigator-smoke/css-phase2a-linked.css\"><style>"
                             b"* { color:#111827; }"
                             b"body { background:#f8fafc; color:#334155; font-size:16px; line-height:1.4; }"
                             b"p { color:#475569; padding:2px; margin:2px 0; }"
                             b".note { color:#2563eb; padding:4px; background:#e0f2fe; }"
                             b".note.important { color:#be123c; }"
                             b"p.note { border-top:1px solid #64748b; }"
                             b"#phase2a-id { color:#7c3aed; }"
                             b"article p { padding:6px; background:#fef3c7; }"
                             b"article > p { padding:10px; }"
                             b"h1, h2, h3 { margin:3px 0; }"
                             b"#phase2a-order { color:#c2410c; }"
                             b"#phase2a-partial { color:#0f766e; background:#dcfce7; padding:8px; border:2px solid #166534; }"
                             b"#phase2a-partial { background:#fef3c7; }"
                             b"#phase2a-duplicate { padding:2px; padding:11px; }"
                             b"#phase2a-inline { color:#991b1b; background:#dbeafe; padding:4px; border:2px solid #1d4ed8; }"
                             b".inherit-wrap { color:#0369a1; font-size:20px; line-height:1.8; background:#fce7f3; padding:12px; }"
                             b".inherit-wrap .child-override { color:#15803d; }"
                             b".deep-one { color:#7e22ce; } .deep-two { color:#7e22ce; }"
                             b".table-parent { color:#92400e; font-size:18px; line-height:1.5; }"
                             b".warning { background:#fee2e2; }"
                             b"p + p { color:#dc2626; } p[data-phase2a='bad'] { color:#dc2626; }"
                             b"#phase2a-important { color:#111827; } .important-class { color:#9333ea !important; }"
                             b"#phase2a-malformed { color:#dc2626 !importantx; }"
                             b".g1, .g2, .g3, .g4, .g5, .g6, .g7, .g8, .g9, .g10, .g11, .g12, .g13, .g14, .g15, .g16, .g17, .g18 { color:#0f766e; }"
                             b"article section div p span em strong code kbd { color:#dc2626; }"
                             b"</style></head><body>"
                             b"<h1 id=\"phase2a-heading\">Phase 2A Selector Cascade</h1>"
                             b"<p id=\"phase2a-universal\" class=\"notebook\">Universal and exact class token marker.</p>"
                             b"<p id=\"phase2a-multi\" class=\"wide note important\">Multiple class matching marker.</p>"
                             b"<p id=\"phase2a-id\" class=\"note\">Exact ID and compound selector marker.</p>"
                             b"<article class=\"article-wrap\"><div class=\"nested-wrap\"><p id=\"phase2a-desc\" class=\"warning\">Descendant selector marker.</p></div><p id=\"phase2a-child\">Direct child selector marker.</p></article>"
                             b"<p id=\"phase2a-order\">Equal specificity source-order marker.</p>"
                             b"<p id=\"phase2a-partial\">Property-level partial cascade marker.</p>"
                             b"<p id=\"phase2a-duplicate\">Duplicate declaration marker.</p>"
                             b"<p id=\"phase2a-inline\" style=\"color:#1d4ed8;padding:12px;\">Inline precedence marker.</p>"
                             b"<div class=\"inherit-wrap\"><section class=\"deep-one\"><article class=\"deep-two\"><p id=\"phase2a-inherited\">Wrapper inheritance marker.</p><p id=\"phase2a-inherit-override\" class=\"child-override\">Child inheritance override marker.</p></article></section></div>"
                             b"<table class=\"table-parent\"><tr><td id=\"phase2a-cell\">Table cell text inheritance marker.</td></tr></table>"
                             b"<p id=\"phase2a-important\" class=\"important-class\">Important precedence marker.</p>"
                             b"<p id=\"phase2a-malformed\">Malformed important marker.</p>"
                             b"<p id=\"phase2a-group\" class=\"g17\">Selector group cap marker.</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase2a-linked.css":
            self.write_bytes(200, "text/css; charset=utf-8", b"#phase2a-order { color:#166534; }")
            return
        if path == "/navigator-smoke/css-phase2b.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2B Structural Selectors</title><style>"
                             b":root { color:#334155; } body { color:#475569; font-size:16px; }"
                             b"ul#phase2b-list > li { color:#64748b; }"
                             b"#phase2b-list > li:first-child { color:#b91c1c; background:#fee2e2; }"
                             b"#phase2b-list > li:last-child { color:#1d4ed8; }"
                             b"#phase2b-only > p:only-child { color:#7c3aed; }"
                             b"#phase2b-list > li:nth-child(1) { background:#fef3c7; }"
                             b"#phase2b-list > li:nth-child(2) { background:#dcfce7; }"
                             b"#phase2b-list > li:nth-child(odd) { border-top:1px solid #ef4444; }"
                             b"#phase2b-list > li:nth-child(even) { border-bottom:1px solid #2563eb; }"
                             b"#phase2b-list > li:nth-child(2n+1) { font-weight:bold; }"
                             b"#phase2b-list > li:nth-child(3n+2) { text-decoration:underline; }"
                             b"#phase2b-list > li:nth-child(99) { color:#000000; }"
                             b"#phase2b-list > li:nth-child(what) { color:#000000; }"
                             b"#phase2b-list > li:nth-child(999999999999999999999999n) { color:#000000; }"
                             b"#phase2b-type > p:first-of-type { color:#047857; }"
                             b"#phase2b-type > p:last-of-type { background:#d1fae5; }"
                             b"#phase2b-type > p:nth-of-type(2) { border-left:2px solid #059669; }"
                             b"#phase2b-only-type > p:only-of-type { color:#9333ea; }"
                             b"p:not(.hidden) { font-size:18px; }"
                             b"p:not(p.notice) { color:#0f766e; }"
                             b"p:not(.a.b) { line-height:1.5; }"
                             b".malformed:not(p .notice), p:has(.nope), p:hover, p:is(.nope), p#phase2b-group-valid { background:#e0f2fe; }"
                             b"p:first-child:last-child:only-child:nth-child(1):not(.too-many) { color:#000000; }"
                             b"a:link { color:#1d4ed8; text-decoration:none; }"
                             b"a:visited { color:#7c3aed; font-weight:bold; background:#ff0000; }"
                             b"</style></head><body>"
                             b"<h1 id=\"phase2b-heading\">Phase 2B Structural Selectors</h1>"
                             b"<ul id=\"phase2b-list\"><li id=\"phase2b-first\">First child marker</li>\n  <li id=\"phase2b-second\">Second child marker</li><li id=\"phase2b-third\">Third child marker</li></ul>"
                             b"<div id=\"phase2b-only\"><p id=\"phase2b-only-child\">Only child marker</p></div>"
                             b"<div id=\"phase2b-type\"><p id=\"phase2b-type-first\">First of type marker</p><span>Mixed tag marker</span><p id=\"phase2b-type-second\">Second of type marker</p></div>"
                             b"<div id=\"phase2b-only-type\"><h2>Mixed heading marker</h2><p id=\"phase2b-only-type-p\">Only of type marker</p></div>"
                             b"<p id=\"phase2b-not-visible\">Not hidden marker</p><p id=\"phase2b-hidden\" class=\"hidden\">Hidden class marker</p><p id=\"phase2b-not-notice\" class=\"notice\">Not notice marker</p><p id=\"phase2b-not-classes\" class=\"a b\">Multiple class marker</p>"
                             b"<p id=\"phase2b-group-valid\">Valid selector-group member marker</p>"
                             b"<a id=\"phase2b-visited\" href=\"/navigator-smoke/basic.html\">Visited link marker</a><a id=\"phase2b-link\" href=\"/navigator-smoke/css-phase2b-never-visited.html\">Unvisited link marker</a>"
                             b"<img id=\"phase2b-image-url\" src=\"/navigator-smoke/tiny.png\" alt=\"Non-anchor URL marker\">"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase2c.html":
            scan_items = b"".join(
                b"<p>bounded scan item %d</p>" % index for index in range(70)
            )
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2C Sibling Combinators</title><style>"
                             b"body { color:#334155; font-size:16px; }"
                             b"h2 + p { color:#dc2626; background:#fef3c7; }"
                             b"h2+p { border-top:1px solid #ef4444; }"
                             b"section > h2 + p { padding:6px; }"
                             b"h2 ~ p { color:#0f766e; background:#fef3c7; }"
                             b"h2:first-of-type ~ p { border-left:2px solid #2563eb; }"
                             b".a + .b { color:#7c3aed; }"
                             b"li + li { text-decoration:underline; }"
                             b"li:first-child + li { background:#dcfce7; }"
                             b"li:nth-child(2) + li { border-bottom:1px solid #16a34a; }"
                             b"li.selected ~ li { color:#0369a1; }"
                             b"p:not(.hidden) + p { font-size:18px; }"
                             b"p.warning ~ p.action { color:#b45309; }"
                             b"a:link + a:visited { color:#7c3aed; }"
                             b"article h2 ~ p.note { background:#dbeafe; }"
                             b"#phase2c-cascade-wrap p { padding:7px; border-top:1px solid #64748b; }"
                             b"h2 + p { color:#b91c1c; }"
                             b"h2.phase2c-important-heading + p { color:#166534 !important; }"
                             b".phase2c-group-valid, .bad + { color:#0f766e; }"
                             b"h2 ++ p { color:#000000; } h2 ~~ p { color:#000000; } h2 + ~ p { color:#000000; }"
                             b"* p { color:#000000; }"
                             b"p[data-phase2c='bad'] + p { color:#000000; }"
                             b"h2 + p + span + em + b + strong + code + i + small { color:#000000; }"
                             b".phase2c-scan-start ~ p { color:#000000; }"
                             b"</style></head><body>"
                             b"<h1 id=\"phase2c-heading\">Phase 2C Sibling Combinators</h1>"
                             b"<section id=\"phase2c-adj-wrap\"><h2 id=\"phase2c-adj-heading\">Adjacent heading</h2>\n  <p id=\"phase2c-adj-immediate\" class=\"a\">Immediate paragraph</p>\n  <p id=\"phase2c-adj-later\">Later paragraph</p></section>"
                             b"<h2 class=\"phase2c-cross-source\">Cross parent source</h2><div><p id=\"phase2c-cross-parent\">Cross parent paragraph</p></div>"
                             b"<ul id=\"phase2c-list\"><li id=\"phase2c-list-first\" class=\"selected a\">First item</li>\n  <li id=\"phase2c-list-second\" class=\"b\">Second item</li><li id=\"phase2c-list-third\">Third item</li></ul>"
                             b"<article id=\"phase2c-article\"><p id=\"phase2c-general-before\">Earlier paragraph</p><h2 id=\"phase2c-general-heading\">General heading</h2><div>Intervening element</div><p id=\"phase2c-general-note\" class=\"note\">Later note</p><p id=\"phase2c-general-action\" class=\"action\">Later action</p></article>"
                             b"<div id=\"phase2c-warning-wrap\"><p class=\"warning\">Warning source</p><span>Intervening span</span><p id=\"phase2c-warning-action\" class=\"action\">Warning action</p></div>"
                             b"<div id=\"phase2c-not-wrap\"><p class=\"source\">Visible source</p><p id=\"phase2c-not-target\">Not hidden target</p><p class=\"hidden\">Hidden source</p><p id=\"phase2c-not-blocked\">Blocked target</p></div>"
                             b"<div id=\"phase2c-link-wrap\"><a class=\"phase2c-link-source\" href=\"/navigator-smoke/css-phase2c-never-visited.html\">Unvisited source</a><a id=\"phase2c-visited\" href=\"/navigator-smoke/basic.html\">Visited sibling</a></div>"
                             b"<div id=\"phase2c-cascade-wrap\"><h2>Source</h2><p id=\"phase2c-cascade-target\">Cascade target</p><h2>Inline source</h2><p id=\"phase2c-inline\" style=\"color:#1d4ed8;\">Inline target</p><h2 class=\"phase2c-important-heading\">Important source</h2><p id=\"phase2c-important\">Important target</p></div>"
                             b"<p id=\"phase2c-group-valid\">Valid group member</p>"
                             b"<div id=\"phase2c-scan-wrap\"><p class=\"phase2c-scan-start\">Scan source</p>" + scan_items +
                             b"<p id=\"phase2c-scan-last\">Scan last</p></div>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase2d.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2D Empty and Parser Recovery</title><style>"
                             b"body { color:#334155; font-size:16px; }"
                             b"#phase2d-empty:empty { color:#166534; }"
                             b"#phase2d-whitespace:empty { color:#166534; }"
                             b"#phase2d-text:empty { color:#dc2626; }"
                             b"#phase2d-child:empty { color:#dc2626; }"
                             b"#phase2d-empty-child:empty { color:#dc2626; }"
                             b"#phase2d-br:empty { color:#dc2626; }"
                             b"#phase2d-image:empty { color:#dc2626; }"
                             b"#phase2d-broken:empty { color:#dc2626; }"
                             b"#phase2d-hr:empty { color:#dc2626; }"
                             b"#phase2d-figure:empty { color:#dc2626; }"
                             b"#phase2d-cell:empty { color:#dc2626; }"
                             b"#phase2d-empty:empty + p { border-top:1px solid #16a34a; }"
                             b".box:not(.keep):empty { border-left:2px solid #2563eb; }"
                             b"section/*x*/>/*y*/ div:empty { border-right:2px solid #7c3aed; }"
                             b"#phase2d-duplicate:empty { color:#dc2626; }"
                             b"#phase2d-multiline-source\n +\n #phase2d-multiline-target { color:#0ea5e9; }"
                             b"#phase2d-depth-good, h1 h2 h3 h4 h5 h6 h7 h8 { color:#15803d; }"
                             b"#phase2d-cascade-wrap p { padding:7px; border-top:1px solid #64748b; }"
                             b"#phase2d-cascade-empty:empty + #phase2d-cascade-target { color:#166534; }"
                             b"#phase2d-cascade-target { color:#dc2626; }"
                             b"#phase2d-inline { color:#1d4ed8; }"
                             b"#phase2d-important { color:#166534 !important; }"
                             b"#phase2d-comment-source/*x*/ + /*y*/ #phase2d-comment-target { color:#0f766e; }"
                             b"#phase2d-general-source/*x*/ ~ /*y*/ #phase2d-general-target { color:#b45309; }"
                             b"#phase2d-comment-group/*x*/, #phase2d-comment-group-two { color:#0891b2; }"
                             b"#phase2d-declaration { color:red /* comment */; padding:5px /* between tokens */; border-top:1px solid #64748b; }"
                             b"#phase2d-quoted { font-family:\"a;b,not-a-selector\"; color:#7c3aed; }"
                             b"#phase2d-group-good, [bad], #phase2d-group-also { color:#15803d; }"
                             b"#phase2d-pseudo-good, :hover, #phase2d-pseudo-also { color:#15803d; }"
                             b"#phase2d-not-good, div:not(.a > .b), #phase2d-not-also { color:#15803d; }"
                             b"#phase2d-attr-good, [data-x='a,b'], #phase2d-attr-also { color:#15803d; }"
                             b"#phase2d-functional-good, :is(.a,.b), #phase2d-functional-also { color:#15803d; }"
                             b"h1,,h2 { text-decoration:underline; }"
                             b", #phase2d-leading { color:#15803d; }"
                             b"#phase2d-trailing, { color:#15803d; }"
                             b"#phase2d-combinator-source/*x*/+/*y*/#phase2d-combinator-target { color:#9333ea; }"
                             b"#phase2d-general-source ~ #phase2d-general-target { background:#fef3c7; }"
                             b"+ p, #phase2d-invalid-right + { color:#000000; }"
                             b"#phase2d-invalid-repeat ++ p, #phase2d-invalid-mixed + ~ p { color:#000000; }"
                             b"#phase2d-escape\\,bad, #phase2d-escape-good { color:#15803d; }"
                             b"#phase2d-delimiter-good, div:not(.unclosed { color:#15803d; }"
                             b"#phase2d-bracket-good, [data-bad { color:#15803d; }"
                             b"#phase2d-incomplete:empty + #phase2d-incomplete-next { color:#dc2626; }"
                             b"#phase2d-unclosed-string { font-family:\"oops }"
                             b"</style><style>#phase2d-unterminated { color:#000000 /* unterminated"
                             b"</style></head><body>"
                             b"<h1 id=\"phase2d-heading\">Phase 2D Empty and Parser Recovery</h1>"
                             b"<div id=\"phase2d-empty\"></div><p id=\"phase2d-empty-next\">Empty adjacent marker</p>"
                             b"<div id=\"phase2d-whitespace\">   </div>"
                             b"<p id=\"phase2d-duplicate\">duplicate first<span>duplicate second</span></p>"
                             b"<p id=\"phase2d-multiline-source\">multiline source</p><p id=\"phase2d-multiline-target\">multiline target</p>"
                             b"<div id=\"phase2d-text\">text marker</div>"
                             b"<div id=\"phase2d-child\"><span>child marker</span></div>"
                             b"<div id=\"phase2d-empty-child\"><span></span></div>"
                             b"<div id=\"phase2d-br\"><br></div>"
                             b"<div id=\"phase2d-image\"><img src=\"/navigator-smoke/tiny.png\" alt=\"image marker\"></div>"
                             b"<div id=\"phase2d-broken\"><img src=\"/navigator-smoke/missing-phase2d.png\" alt=\"broken image fallback\"></div>"
                             b"<div id=\"phase2d-hr\"><hr></div>"
                             b"<figure id=\"phase2d-figure\"><img src=\"/navigator-smoke/tiny.png\" alt=\"figure image\"><figcaption>figure caption</figcaption></figure>"
                             b"<table><tr><td id=\"phase2d-cell\">cell text</td></tr></table>"
                             b"<div id=\"phase2d-cascade-wrap\"><div id=\"phase2d-cascade-empty\"></div><p id=\"phase2d-cascade-target\">cascade target</p><p id=\"phase2d-inline\" style=\"color:#1d4ed8;\">inline target</p><p id=\"phase2d-important\">important target</p></div>"
                             b"<p id=\"phase2d-comment-source\">comment source</p><p id=\"phase2d-comment-target\">comment adjacent target</p>"
                             b"<p id=\"phase2d-general-source\">general source</p><span>intervening</span><p id=\"phase2d-general-target\">general target</p>"
                             b"<p id=\"phase2d-combinator-source\">combinator source</p><p id=\"phase2d-combinator-target\">combinator target</p>"
                             b"<p id=\"phase2d-declaration\">declaration comment marker</p><p id=\"phase2d-quoted\">quoted value marker</p>"
                             b"<div class=\"box\" id=\"phase2d-box\"></div><div class=\"box keep\" id=\"phase2d-keep\"></div>"
                             b"<section id=\"phase2d-section\"><div id=\"phase2d-section-empty\"></div><div id=\"phase2d-section-text\">not empty</div></section>"
                             b"<p id=\"phase2d-comment-group\">comment group one</p><p id=\"phase2d-comment-group-two\">comment group two</p>"
                             b"<p id=\"phase2d-group-good\">group good</p><p id=\"phase2d-group-also\">group also</p>"
                             b"<p id=\"phase2d-pseudo-good\">pseudo good</p><p id=\"phase2d-pseudo-also\">pseudo also</p>"
                             b"<p id=\"phase2d-not-good\">not good</p><p id=\"phase2d-not-also\">not also</p>"
                             b"<p id=\"phase2d-attr-good\">attribute good</p><p id=\"phase2d-attr-also\">attribute also</p>"
                             b"<p id=\"phase2d-functional-good\">functional good</p><p id=\"phase2d-functional-also\">functional also</p>"
                             b"<h1>empty member recovery heading</h1><h2>empty member recovery subheading</h2>"
                             b"<p id=\"phase2d-leading\">leading comma marker</p><p class=\"phase2d-trailing\" id=\"phase2d-trailing\">trailing comma marker</p>"
                             b"<p id=\"phase2d-invalid-right\">invalid right marker</p><p id=\"phase2d-invalid-repeat\">invalid repeat marker</p><p id=\"phase2d-invalid-mixed\">invalid mixed marker</p>"
                             b"<p id=\"phase2d-escape-good\">escape recovery good</p>"
                             b"<div id=\"phase2d-incomplete\"><x-uncertain></x-uncertain></div><p id=\"phase2d-incomplete-next\">incomplete metadata marker</p>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase2e.html":
            oversized_value = b"oversized-value-" + (b"x" * 320)
            oversized_button = b"Button label clamp " + (b"y" * 180)
            option_clamp = b"".join(
                b"<option>Clamped option %02d</option>" % index for index in range(70)
            )
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2E Bounded Forms</title><style>"
                             b"body { color:#334155; font-size:16px; background:#f8fafc; }"
                             b"form { margin:8px; padding:8px; background:#ffffff; }"
                             b"fieldset { margin:8px 0; padding:10px; border:2px solid #64748b; background:#f8fafc; }"
                             b"legend { font-weight:bold; color:#1e3a8a; }"
                             b"label { color:#334155; margin:3px 0; }"
                             b"input, textarea, select { color:#111827; background:#ffffff; border:1px solid #94a3b8; padding:3px; }"
                             b"button { color:#111827; background:#e2e8f0; border:1px solid #475569; padding:4px; }"
                             b"input:checked, .choice:checked, option:checked { color:#166534; background:#dcfce7; font-weight:bold; }"
                             b"button:disabled, input:disabled, textarea:disabled, select:disabled, fieldset :disabled { color:#64748b; background:#e2e8f0; }"
                             b"input:enabled { border-color:#2563eb; }"
                             b"textarea:required { border:2px solid #dc2626; }"
                             b"input:read-only, textarea:read-only { background:#f1f5f9; }"
                             b"input:read-write { background:#eff6ff; }"
                             b"label + input:checked { border:3px solid #7c3aed; }"
                             b"#phase2e-source-order { color:#b91c1c; } #phase2e-source-order { color:#166534; }"
                             b"#phase2e-inline { color:#b91c1c; }"
                             b"#phase2e-important { color:#b91c1c; } #phase2e-important { color:#166534 !important; }"
                             b"input:hover, button:focus, input:active, input:has(.nope) { color:#000000; }"
                             b"</style></head><body>"
                             b"<h1 id=\"phase2e-heading\">Phase 2E Bounded Static Forms</h1>"
                             b"<form id=\"phase2e-form\" action=\"/navigator-smoke/phase2e-submit\" method=\"post\">"
                             b"<fieldset id=\"phase2e-fieldset\"><legend id=\"phase2e-legend\">Account controls</legend>"
                             b"<button id=\"phase2e-disabled-button\" type=\"button\" disabled>Disabled button</button>"
                             b"<p id=\"phase2e-source-order\">Source-order tie marker</p>"
                             b"<p id=\"phase2e-inline\" style=\"color:#1d4ed8;\">Inline precedence marker</p>"
                             b"<p id=\"phase2e-important\">Important precedence marker</p>"
                             b"<select id=\"phase2e-option-clamp\">" + option_clamp + b"</select>"
                             b"<label id=\"phase2e-wrap-label\"><input id=\"phase2e-wrap-choice\" class=\"choice\" type=\"checkbox\"> Wrapping choice</label>"
                             b"<label for=\"phase2e-associated\">Associated checked choice</label><input id=\"phase2e-associated\" class=\"choice\" type=\"checkbox\" checked name=\"choice\" value=\"yes\">"
                             b"<label for=\"phase2e-missing\">Missing association remains text</label>"
                             b"<label for=\"phase2e-duplicate\">Duplicate association remains bounded</label>"
                             b"<input id=\"phase2e-duplicate\" type=\"text\" value=\"duplicate one\"><input id=\"phase2e-duplicate\" type=\"text\" value=\"duplicate two\">"
                             b"<input id=\"phase2e-text\" type=\"text\" size=\"9999\" value=\"regular text\">"
                             b"<input id=\"phase2e-placeholder\" type=\"text\" placeholder=\"Placeholder marker\">"
                             b"<input id=\"phase2e-password\" type=\"password\" value=\"secret-phase2e-value\">"
                             b"<input id=\"phase2e-search\" type=\"search\" value=\"Search marker\">"
                             b"<input id=\"phase2e-email\" type=\"email\" placeholder=\"Email fallback\">"
                             b"<input id=\"phase2e-url\" type=\"url\" placeholder=\"URL fallback\">"
                             b"<input id=\"phase2e-number\" type=\"number\" value=\"42\">"
                             b"<input id=\"phase2e-readonly\" type=\"text\" readonly value=\"Read-only marker\">"
                             b"<input id=\"phase2e-disabled\" type=\"text\" disabled value=\"Disabled marker\">"
                             b"<input id=\"phase2e-required\" type=\"text\" required placeholder=\"Required marker\">"
                             b"<input id=\"phase2e-oversized\" type=\"text\" value=\"" + oversized_value + b"\">"
                             b"<input id=\"phase2e-hidden\" type=\"hidden\" value=\"hidden-secret-must-not-render\">"
                             b"<input id=\"phase2e-unsupported\" type=\"date\" value=\"unsupported fallback\">"
                             b"<input id=\"phase2e-unchecked\" class=\"choice\" type=\"checkbox\">"
                             b"<input id=\"phase2e-disabled-checked\" class=\"choice\" type=\"checkbox\" checked disabled>"
                             b"<input id=\"phase2e-radio-a\" class=\"choice\" type=\"radio\" name=\"same-choice\" checked>"
                             b"<input id=\"phase2e-radio-b\" class=\"choice\" type=\"radio\" name=\"same-choice\">"
                             b"<textarea id=\"phase2e-textarea\" rows=\"99\" cols=\"999\" required>Static textarea marker with bounded wrapping text.</textarea>"
                             b"<textarea id=\"phase2e-readonly-textarea\" rows=\"3\" cols=\"24\" readonly>Readonly textarea marker.</textarea>"
                             b"<textarea id=\"phase2e-disabled-textarea\" disabled>Disabled textarea marker.</textarea>"
                             b"<select id=\"phase2e-selected\" name=\"selected\"><option disabled>Unavailable first</option><option selected>Selected option marker</option><option>Later option</option></select>"
                             b"<select id=\"phase2e-default\"><option disabled>Disabled first</option><option>Default first enabled option</option><option>Later default option</option></select>"
                             b"<select id=\"phase2e-disabled-select\" disabled><option>Disabled select option</option></select>"
                             b"<select id=\"phase2e-multiple\" multiple size=\"3\"><option selected>Multiple selected one</option><option disabled>Multiple disabled option</option><option>Multiple option three</option></select>"
                             b"<button id=\"phase2e-button\" type=\"button\">Element button</button>"
                             b"<input id=\"phase2e-input-button\" type=\"button\" value=\"Input button\">"
                             b"<input id=\"phase2e-submit\" type=\"submit\" value=\"Visual submit only\">"
                             b"<input id=\"phase2e-reset\" type=\"reset\" value=\"Visual reset only\">"
                             b"<button id=\"phase2e-overflow-button\" type=\"button\">" + oversized_button + b"</button>"
                             b"</fieldset></form></body></html>")
            return
        if path == "/navigator-smoke/css-phase2f.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2F Session Local Forms</title><style>"
                             b"body { color:#334155; background:#f8fafc; }"
                             b"form { padding:6px; } fieldset { padding:8px; border:2px solid #64748b; }"
                             b"input, button { color:#111827; background:#ffffff; border:1px solid #94a3b8; padding:3px; }"
                             b"input:checked { color:#166534; background:#dcfce7; font-weight:bold; }"
                             b"fieldset input:checked { border-color:#2563eb; }"
                             b"input:checked + label { color:#7c3aed; }"
                             b"#phase2f-important { color:#b91c1c; } #phase2f-important { color:#166534 !important; }"
                             b"</style></head><body><h1>Phase 2F Session Local Forms</h1>"
                             b"<form id=\"phase2f-form-one\" action=\"/navigator-smoke/phase2f-submit\" method=\"post\"><fieldset>"
                             b"<input id=\"phase2f-checkbox\" type=\"checkbox\"><label id=\"phase2f-checkbox-label\" for=\"phase2f-checkbox\">Phase 2F checkbox</label>"
                             b"<input id=\"phase2f-disabled-checkbox\" type=\"checkbox\" checked disabled><label id=\"phase2f-disabled-checkbox-label\" for=\"phase2f-disabled-checkbox\">Disabled checkbox</label>"
                             b"<input id=\"phase2f-radio-a\" type=\"radio\" name=\"choice\" checked><label id=\"phase2f-radio-a-label\" for=\"phase2f-radio-a\">Radio A</label>"
                             b"<input id=\"phase2f-radio-b\" type=\"radio\" name=\"choice\"><label id=\"phase2f-radio-b-label\" for=\"phase2f-radio-b\">Radio B</label>"
                             b"<input id=\"phase2f-disabled-radio\" type=\"radio\" name=\"choice\" checked disabled><label id=\"phase2f-disabled-radio-label\" for=\"phase2f-disabled-radio\">Disabled radio</label>"
                             b"<input id=\"phase2f-nameless-a\" type=\"radio\"><input id=\"phase2f-nameless-b\" type=\"radio\">"
                             b"<label id=\"phase2f-wrapping-label\"><input id=\"phase2f-wrapped-checkbox\" type=\"checkbox\"> Wrapping label checkbox</label>"
                             b"<label id=\"phase2f-missing-label\" for=\"phase2f-missing\">Malformed association</label><label id=\"phase2f-duplicate-label\" for=\"phase2f-duplicate\">Duplicate association</label>"
                             b"<input id=\"phase2f-duplicate\" type=\"text\" value=\"one\"><input id=\"phase2f-duplicate\" type=\"text\" value=\"two\">"
                             b"<label id=\"phase2f-unrelated-label\" for=\"phase2f-unrelated\">Unrelated label</label>"
                             b"<input id=\"phase2f-hidden\" type=\"hidden\" value=\"phase2f-secret\">"
                             b"<button id=\"phase2f-button\" type=\"button\">Inert button</button>"
                             b"<input id=\"phase2f-input-button\" type=\"button\" value=\"Input inert button\">"
                             b"<input id=\"phase2f-submit\" type=\"submit\" value=\"Visual submit\"><input id=\"phase2f-reset\" type=\"reset\" value=\"Visual reset\">"
                             b"<button id=\"phase2f-disabled-button\" type=\"button\" disabled>Disabled button</button>"
                             b"<p id=\"phase2f-important\">Important cascade marker</p>"
                             b"</fieldset></form>"
                             b"<form id=\"phase2f-form-two\"><input id=\"phase2f-other-form-radio\" type=\"radio\" name=\"choice\" checked><label for=\"phase2f-other-form-radio\">Other form radio</label></form>"
                             b"</body></html>")
            return
        if path == "/navigator-smoke/css-phase2g.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2G Bounded Keyboard Focus</title><style>"
                             b"body { color:#334155; background:#f8fafc; }"
                             b"form { padding:6px; } fieldset { padding:8px; border:2px solid #64748b; }"
                             b"input, textarea, select, button { color:#111827; background:#ffffff; border:1px solid #94a3b8; padding:3px; }"
                             b"input:focus { color:#1d4ed8; background:#dbeafe; padding:5px; }"
                             b"input:focus-visible { background:#e0f2fe; }"
                             b"button:focus { color:#be123c; background:#fce7f3; }"
                             b"input:checked:focus { border:3px solid #7c3aed; }"
                             b"fieldset input:focus { border-color:#2563eb; }"
                             b"label + input:focus { color:#166534; }"
                             b"input:focus + label { color:#7c3aed; }"
                             b".phase2g-choice:focus { border-color:#c026d3; }"
                             b"#phase2g-source-order:focus { color:#f59e0b; } #phase2g-source-order:focus { color:#0f766e; }"
                             b"#phase2g-inline:focus { color:#b91c1c; }"
                             b"#phase2g-important:focus { color:#b91c1c !important; }"
                             b"#phase2g-css-hidden { display:none; }"
                             b"</style></head><body><h1>Phase 2G Bounded Keyboard Focus</h1>"
                             b"<form id=\"phase2g-form\" action=\"/navigator-smoke/phase2g-submit\" method=\"post\"><fieldset id=\"phase2g-fieldset\">"
                             b"<input id=\"phase2g-checkbox\" class=\"phase2g-choice\" type=\"checkbox\"><label id=\"phase2g-checkbox-label\" for=\"phase2g-checkbox\">Phase 2G checkbox</label>"
                             b"<label id=\"phase2g-radio-a-label\" for=\"phase2g-radio-a\">Radio A</label><input id=\"phase2g-radio-a\" class=\"phase2g-choice\" type=\"radio\" name=\"phase2g-choice\">"
                             b"<input id=\"phase2g-radio-b\" class=\"phase2g-choice\" type=\"radio\" name=\"phase2g-choice\"><label id=\"phase2g-radio-b-label\" for=\"phase2g-radio-b\">Radio B</label>"
                             b"<input id=\"phase2g-disabled\" type=\"checkbox\" disabled><label id=\"phase2g-disabled-label\" for=\"phase2g-disabled\">Disabled checkbox</label>"
                             b"<input id=\"phase2g-css-hidden\" type=\"checkbox\"><input id=\"phase2g-hidden\" type=\"hidden\" value=\"phase2g-hidden-marker\">"
                             b"<input id=\"phase2g-malformed\" type=\"bogus\" value=\"unsupported marker\">"
                             b"<input id=\"phase2g-text\" type=\"text\" value=\"stable text marker\"><textarea id=\"phase2g-textarea\">Stable textarea marker</textarea>"
                             b"<select id=\"phase2g-select\"><option>First select option</option><option>Second select option</option></select>"
                             b"<button id=\"phase2g-button\" type=\"button\">Inert button</button>"
                             b"<input id=\"phase2g-submit\" type=\"submit\" value=\"Visual submit only\"><input id=\"phase2g-reset\" type=\"reset\" value=\"Visual reset only\">"
                             b"<button id=\"phase2g-disabled-button\" type=\"button\" disabled>Disabled button</button>"
                             b"<input id=\"phase2g-source-order\" type=\"checkbox\"><input id=\"phase2g-inline\" type=\"checkbox\" style=\"color:#1d4ed8;\"><input id=\"phase2g-important\" type=\"checkbox\">"
                             b"</fieldset></form></body></html>")
            return
        if path == "/navigator-smoke/css-phase2h.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2H Bounded Focus and Accessibility</title><style>"
                             b"body { color:#334155; background:#f8fafc; }"
                             b"form { padding:6px; } fieldset { padding:8px; border:2px solid #64748b; }"
                             b"input, textarea, select, button { color:#111827; background:#ffffff; border:1px solid #94a3b8; padding:3px; }"
                             b"input:focus, textarea:focus, select:focus, button:focus { background:#dbeafe; }"
                             b"input:focus-visible, textarea:focus-visible, select:focus-visible, button:focus-visible { color:#1d4ed8; }"
                             b"#phase2h-thick:focus { border:4px solid #7c3aed; }"
                             b"#phase2h-tiny:focus { width:1px; height:1px; padding:0; }"
                             b"#phase2h-hidden { display:none; }"
                             b".phase2h-nested { padding:12px; border:2px solid #cbd5e1; }"
                             b".phase2h-table { border:2px solid #94a3b8; padding:4px; }"
                             b"</style></head><body><h1>Phase 2H Bounded Focus and Accessibility</h1>"
                             b'<form id="phase2h-form"><fieldset id="phase2h-fieldset"><legend>Bounded controls</legend>'
                             b'<input id="phase2h-checkbox" type="checkbox"><label id="phase2h-checkbox-label" for="phase2h-checkbox">Phase 2H checkbox</label>'
                             b'<label id="phase2h-wrapping-label"><input id="phase2h-wrapped" type="checkbox"> Wrapped checkbox</label>'
                             b'<label id="phase2h-radio-label" for="phase2h-radio">Phase 2H radio</label><input id="phase2h-radio" type="radio" name="phase2h-choice">'
                             b'<button id="phase2h-button" type="button">Phase 2H button</button>'
                             b'<input id="phase2h-input-button" type="button" value="Input button">'
                             b'<input id="phase2h-text" type="text" placeholder="Text placeholder" required readonly>'
                             b'<input id="phase2h-password" type="password" placeholder="Password placeholder" value="do-not-log">'
                             b'<textarea id="phase2h-textarea" placeholder="Textarea placeholder" required readonly>Stable textarea</textarea>'
                             b'<select id="phase2h-select" required><option>First option</option><option selected>Second option</option></select>'
                             b'<input id="phase2h-thick" type="checkbox"><label for="phase2h-thick">Thick border checkbox</label>'
                             b'<div class="phase2h-nested"><input id="phase2h-tiny" type="checkbox"><label for="phase2h-tiny">Tiny control</label></div>'
                             b'<table class="phase2h-table"><tr><td><input id="phase2h-table-control" type="radio" name="phase2h-table-choice"></td></tr></table>'
                             b'<input id="phase2h-hidden" type="checkbox"><input id="phase2h-disabled" type="checkbox" disabled>'
                             b'<label id="phase2h-missing-label" for="phase2h-missing">Missing association</label>'
                             b'<label id="phase2h-duplicate-label" for="phase2h-duplicate">Duplicate association</label>'
                             b'<input id="phase2h-duplicate" type="text" value="one"><input id="phase2h-duplicate" type="text" value="two">'
                             b'<input id="phase2h-unnamed" type="checkbox">'
                             b'</fieldset></form></body></html>')
            return
        if path == "/navigator-smoke/css-phase2i.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2I Lifecycle Fixture</title><style>"
                             b"body { color:#334155; background:#f8fafc; }"
                             b"input, button { color:#111827; background:#ffffff; border:1px solid #94a3b8; padding:3px; }"
                             b"input:focus { background:#dbeafe; } input:focus-visible { color:#1d4ed8; }"
                             b"input:checked:focus { border:3px solid #7c3aed; }"
                             b"#phase2i-hidden { display:none; }"
                             b"</style></head><body><h1>Phase 2I Lifecycle Fixture</h1>"
                             b'<form id="phase2i-form"><input id="phase2i-checkbox" type="checkbox">'
                             b'<label for="phase2i-checkbox">Phase 2I checkbox</label>'
                             b'<input id="phase2i-radio-a" type="radio" name="phase2i-choice">'
                             b'<label for="phase2i-radio-a">Phase 2I radio A</label>'
                             b'<input id="phase2i-radio-b" type="radio" name="phase2i-choice">'
                             b'<label for="phase2i-radio-b">Phase 2I radio B</label>'
                             b'<button id="phase2i-button" type="button">Phase 2I button</button>'
                             b'<input id="phase2i-password" type="password" value="phase2i-secret">'
                             b'<input id="phase2i-hidden" type="hidden" value="phase2i-hidden-marker">'
                             b'<a href="/navigator-smoke/css-phase2i-alt.html">Phase 2I alternate</a>'
                             b'</form></body></html>')
            return
        if path == "/navigator-smoke/css-phase2i-alt.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><head><title>Phase 2I Alternate</title></head><body>"
                             b"<h1>Phase 2I Alternate</h1><p>alternate lifecycle document</p></body></html>")
            return
        if path == "/navigator-smoke/css-phase2i-malformed.html":
            self.write_bytes(200, "text/html; charset=utf-8",
                             b"<html><body><h1>Phase 2I Malformed Recovery</h1>"
                             b"<form><input id='phase2i-incomplete' type='checkbox'><label>")
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
        if path == "/navigator-smoke/stream-split-content-length.html":
            body = b"<html><body><h1>Split Content-Length</h1><p>body arrives over several writes.</p></body></html>"
            body_length = str(len(body)).encode("ascii")
            self.write_fragmented_response((
                b"HTTP/1.1 200 OK\r\nContent-",
                b"Type: text/html; charset=utf-8\r\nContent-Length: ",
                body_length[:1], body_length[1:], b"\r\nConnection: close\r\n\r",
                b"\n", body[:13], body[13:47], body[47:]
            ), close=True)
            return
        if path == "/navigator-smoke/stream-split-chunked.html":
            chunks = (
                b"<html><body><h1>Split chunked response</h1>",
                b"<p>chunk framing and body boundaries are arbitrary.</p></body></html>",
            )
            body_fragments = [
                b"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nTransfer-Encoding: ",
                b"chunked\r\nConnection: keep-alive\r\n\r\n",
            ]
            for index, chunk in enumerate(chunks):
                size = ("%x;fixture=split" % len(chunk)).encode("ascii")
                body_fragments.extend((size[:2], size[2:] + b"\r", b"\n", chunk[:9], chunk[9:], b"\r", b"\n"))
            body_fragments.extend((b"0\r", b"\nX-Split-Trailer: yes\r\n", b"\r", b"\n"))
            self.write_fragmented_response(tuple(body_fragments))
            return
        if path == "/navigator-smoke/stream-connection-close.html":
            body = b"<html><body><h1>Connection-close response</h1><p>EOF completes this body.</p></body></html>"
            self.write_fragmented_response((
                b"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n",
                body[:7], body[7:31], body[31:]
            ), close=True)
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
        if path == "/navigator-smoke/wide.png":
            self.write_bytes(200, "image/png", SMOKE_WIDE_PNG)
            return
        if path == "/navigator-smoke/tall.png":
            self.write_bytes(200, "image/png", SMOKE_TALL_PNG)
            return
        if path == "/navigator-smoke/missing.png":
            self.write_bytes(404, "text/plain; charset=utf-8", b"missing png")
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

    @staticmethod
    def _log_client_hello(raw_socket):
        if not os.environ.get("GXOS_NAVIGATOR_TLS_DIAGNOSTICS"):
            return

        old_timeout = raw_socket.gettimeout()
        try:
            raw_socket.settimeout(1.0)
            header = raw_socket.recv(5, socket.MSG_PEEK)
            if len(header) < 5:
                print("TLS clienthello metadata=unavailable short_record_header", flush=True)
                return
            record_length = struct.unpack(">H", header[3:5])[0]
            record = raw_socket.recv(5 + record_length, socket.MSG_PEEK)
            if len(record) < 9 or record[0] != 0x16 or record[5] != 0x01:
                print("TLS clienthello metadata=unavailable unexpected_record", flush=True)
                return
            hello_length = int.from_bytes(record[6:9], "big")
            if len(record) < 9 + hello_length:
                print("TLS clienthello metadata=unavailable short_clienthello", flush=True)
                return

            body = record[9:9 + hello_length]
            if len(body) < 34:
                print("TLS clienthello metadata=unavailable short_clienthello_body", flush=True)
                return
            legacy_version = "0x%04x" % struct.unpack(">H", body[0:2])[0]
            offset = 34
            session_id_length = body[offset]
            offset += 1 + session_id_length
            cipher_length = struct.unpack(">H", body[offset:offset + 2])[0]
            offset += 2
            cipher_suites = [
                "0x%04x" % struct.unpack(">H", body[index:index + 2])[0]
                for index in range(offset, offset + cipher_length, 2)
            ]
            cipher_suite_ids = [int(value, 16) for value in cipher_suites]
            real_suite_ids = [value for value in cipher_suite_ids if value not in TLS_SIGNALING_SUITES]
            canonical_offer = any(value in CANONICAL_TLS12_SUITES for value in real_suite_ids)
            scsv_only = bool(cipher_suite_ids) and not real_suite_ids
            offset += cipher_length
            compression_length = body[offset]
            offset += 1 + compression_length
            extension_length = struct.unpack(">H", body[offset:offset + 2])[0]
            offset += 2
            extensions_end = min(offset + extension_length, len(body))
            supported_versions = []
            signature_algorithms = []
            while offset + 4 <= extensions_end:
                extension_type = struct.unpack(">H", body[offset:offset + 2])[0]
                extension_size = struct.unpack(">H", body[offset + 2:offset + 4])[0]
                extension_data = body[offset + 4:offset + 4 + extension_size]
                if extension_type == 43 and len(extension_data) >= 1:
                    versions_length = extension_data[0]
                    supported_versions = [
                        "0x%04x" % struct.unpack(">H", extension_data[index:index + 2])[0]
                        for index in range(1, min(1 + versions_length, len(extension_data)), 2)
                    ]
                elif extension_type == 13 and len(extension_data) >= 2:
                    algorithms_length = struct.unpack(">H", extension_data[0:2])[0]
                    signature_algorithms = [
                        "0x%04x" % struct.unpack(">H", extension_data[index:index + 2])[0]
                        for index in range(2, min(2 + algorithms_length, len(extension_data)), 2)
                    ]
                offset += 4 + extension_size

            print(
                "TLS clienthello metadata record_version=0x%04x legacy_version=%s "
                "cipher_suites=%s real_suite_count=%d scsv_only=%s canonical_offer=%s "
                "supported_versions=%s signature_algorithms=%s"
                % (
                    struct.unpack(">H", header[1:3])[0],
                    legacy_version,
                    ",".join(cipher_suites) or "(none)",
                    len(real_suite_ids),
                    "yes" if scsv_only else "no",
                    "yes" if canonical_offer else "no",
                    ",".join(supported_versions) or "(none)",
                    ",".join(signature_algorithms) or "(none)",
                ),
                flush=True,
            )
        except (OSError, IndexError, struct.error) as exc:
            print("TLS clienthello metadata=unavailable error=%s" % exc, flush=True)
        finally:
            raw_socket.settimeout(old_timeout)

    def get_request(self):
        raw_socket, client_address = super().get_request()
        print("TLS accept from %s:%s" % (client_address[0], client_address[1]), flush=True)
        try:
            self._log_client_hello(raw_socket)
            tls_socket = self._ssl_context.wrap_socket(raw_socket, server_side=True)
            negotiated_suite = tls_socket.cipher()[0] if tls_socket.cipher() else None
            negotiated_contract_name = {
                "ECDHE-RSA-AES128-GCM-SHA256": "TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256",
                "ECDHE-RSA-AES256-GCM-SHA384": "TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384",
                "ECDHE-ECDSA-AES128-GCM-SHA256": "TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256",
                "ECDHE-ECDSA-AES256-GCM-SHA384": "TLS-ECDHE-ECDSA-WITH-AES-256-GCM-SHA384",
            }.get(negotiated_suite, "(not-in-contract)")
            print(
                "TLS handshake ok from %s:%s sni=%s protocol=%s cipher=%s "
                "negotiated_suite=%s fixture_suite_contract=%s"
                % (
                    client_address[0],
                    client_address[1],
                    getattr(tls_socket, "_guidexos_sni", None),
                    tls_socket.version(),
                    negotiated_suite,
                    negotiated_contract_name,
                    "yes" if negotiated_contract_name in CANONICAL_TLS12_SUITES.values() else "no",
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

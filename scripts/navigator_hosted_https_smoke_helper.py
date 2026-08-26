#!/usr/bin/env python3
"""Smoke-only hosted HTTPS helper for localhost when Schannel credential acquisition is unavailable."""

from __future__ import annotations

import argparse
import base64
import ssl
import sys
import urllib.error
import urllib.request


def b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def response_socket(response):
    candidates = [
        lambda: response.fp.raw._sock,
        lambda: response.fp.raw._sock.sock,
    ]
    for candidate in candidates:
        try:
            sock = candidate()
            if sock is not None:
                return sock
        except Exception:
            continue
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--method", required=True)
    parser.add_argument("--host-header", required=True)
    parser.add_argument("--content-type", default="")
    parser.add_argument("--body-base64", default="")
    args = parser.parse_args()

    body = base64.b64decode(args.body_base64) if args.body_base64 else b""
    request = urllib.request.Request(args.url, data=body if args.method.upper() == "POST" else None,
                                     method=args.method.upper())
    request.add_header("Host", args.host_header)
    request.add_header("Accept-Encoding", "gzip, deflate")
    request.add_header("Connection", "close")
    if args.method.upper() == "POST":
        request.add_header("Content-Type", args.content_type or "application/x-www-form-urlencoded")

    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE

    opener = urllib.request.build_opener(NoRedirectHandler(), urllib.request.HTTPSHandler(context=context))
    try:
        response = opener.open(request, timeout=5.0)
    except urllib.error.HTTPError as exc:
        response = exc
    except Exception as exc:
        print("ERROR")
        print(b64(str(exc).encode("utf-8")))
        return 1

    body_bytes = response.read()
    sock = response_socket(response)
    tls_version = ""
    tls_cipher = ""
    if sock is not None:
        try:
            tls_version = sock.version() or ""
        except Exception:
            tls_version = ""
        try:
            cipher = sock.cipher()
            if cipher:
                tls_cipher = cipher[0]
        except Exception:
            tls_cipher = ""

    headers = list(response.headers.items())
    print(response.status)
    print(b64((response.reason or "").encode("utf-8")))
    print(b64(tls_version.encode("utf-8")))
    print(b64(tls_cipher.encode("utf-8")))
    print(len(headers))
    for name, value in headers:
        print(f"{b64(name.encode('utf-8'))}\t{b64(value.encode('utf-8'))}")
    print(b64(body_bytes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

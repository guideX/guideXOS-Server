#!/usr/bin/env python3
"""Small authoritative UDP fixture for the QEMU Navigator HTTPS qualification lane."""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import signal
import socket
import struct
import sys
from datetime import datetime, timezone


def log(event: str, **fields: object) -> None:
    print(json.dumps({"event": event, "time_utc": datetime.now(timezone.utc).isoformat(),
                      "pid": __import__("os").getpid(), **fields}, sort_keys=True), flush=True)


def decode_question(packet: bytes) -> tuple[int, int, str, int, int, int]:
    if len(packet) < 17:
        raise ValueError("short DNS query")
    ident, flags, qdcount = struct.unpack_from("!HHH", packet)
    if flags & 0x8000 or ((flags >> 11) & 0xF) != 0 or qdcount != 1:
        raise ValueError("unsupported DNS header")
    labels: list[str] = []
    offset = 12
    while True:
        if offset >= len(packet):
            raise ValueError("truncated DNS name")
        size = packet[offset]
        offset += 1
        if size == 0:
            break
        if size > 63 or offset + size > len(packet):
            raise ValueError("invalid DNS label")
        labels.append(packet[offset:offset + size].decode("ascii").lower())
        offset += size
    if offset + 4 != len(packet):
        raise ValueError("unexpected DNS query trailing bytes")
    qtype, qclass = struct.unpack_from("!HH", packet, offset)
    return ident, flags, ".".join(labels), qtype, qclass, offset + 4


def make_response(packet: bytes, expected_name: str, address: str) -> tuple[bytes, str, int]:
    ident, flags, name, qtype, qclass, question_end = decode_question(packet)
    question = packet[12:question_end]
    if name != expected_name:
        rcode = 3
        answer = b""
        answer_name = ""
    elif qtype != 1 or qclass != 1:
        rcode = 5
        answer = b""
        answer_name = ""
    else:
        rcode = 0
        answer_name = address
        answer = (b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 30, 4) +
                  ipaddress.IPv4Address(address).packed)
    # Authoritative response; echo RD only. The query ID and original question
    # are retained byte-for-byte so the guest's parser can verify both.
    response_flags = 0x8400 | (flags & 0x0100) | rcode
    response = (struct.pack("!HHHHHH", ident, response_flags, 1,
                            1 if answer else 0, 0, 0) + question + answer)
    return response, answer_name, rcode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--hostname", required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--stop-file", default="")
    args = parser.parse_args()

    bind_ip = str(ipaddress.IPv4Address(args.bind))
    address = str(ipaddress.IPv4Address(args.address))
    hostname = args.hostname.rstrip(".").lower()
    if not hostname or len(hostname) > 253 or not (1 <= args.port <= 65535):
        parser.error("hostname and UDP port are out of range")

    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
        server.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    server.settimeout(0.25)
    try:
        server.bind((bind_ip, args.port))
    except OSError as exc:
        log("bind_failed", bind=bind_ip, port=args.port, error=str(exc))
        return 2

    stopping = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    log("ready", bind=bind_ip, port=args.port, hostname=hostname, address=address)
    try:
        while not stopping:
            if args.stop_file and os.path.exists(args.stop_file):
                stopping = True
                continue
            try:
                packet, peer = server.recvfrom(2048)
            except socket.timeout:
                continue
            peer_text = f"{peer[0]}:{peer[1]}"
            try:
                ident, _flags, qname, qtype, qclass, _ = decode_question(packet)
                response, response_name, rcode = make_response(packet, hostname, address)
            except (ValueError, UnicodeError, struct.error) as exc:
                log("invalid_query", peer=peer_text, query_bytes=len(packet), error=str(exc))
                continue
            log("query", peer=peer_text, id=ident, qname=qname, qtype=qtype,
                qclass=qclass, query_bytes=len(packet))
            server.sendto(response, peer)
            log("response", peer=peer_text, id=ident, qname=qname, qtype=qtype,
                qclass=qclass, rcode=rcode, answer=response_name,
                response_bytes=len(response))
    finally:
        server.close()
        log("stopped", bind=bind_ip, port=args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""One-shot RFC 10049 v1 UDP signer for the NAVHTTPS13 QEMU fixture only."""

from __future__ import annotations

import argparse
import hashlib
import socket
import struct
import time

from test_roughtime_v1 import ROOT_KEY, make_response, parse_request_nonce
import roughtime_v1 as rt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="0.0.0.0", help="bind address; use 0.0.0.0 for QEMU user networking")
    parser.add_argument("--port", type=int, default=5319)
    parser.add_argument("--max-packets", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="bounded idle timeout for the one-shot host fixture")
    args = parser.parse_args()
    if (not 1 <= args.port <= 65535 or not 1 <= args.max_packets <= 256 or
            not 0 < args.timeout <= 300):
        parser.error("port must be 1..65535, max-packets 1..256, and timeout 0..300 seconds")

    server_key = ROOT_KEY.public_key().public_bytes_raw()
    server_id = hashlib.sha512(b"\xff" + server_key).digest()[:32]
    print(f"NAVHTTPS13_TEST_ONLY key={server_key.hex()} key_id={server_id.hex()}", flush=True)
    print(f"NAVHTTPS13_TEST_ONLY listen={args.listen}:{args.port} max_packets={args.max_packets}", flush=True)

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.bind((args.listen, args.port))
        endpoint.settimeout(args.timeout)
        for _ in range(args.max_packets):
            try:
                request, source = endpoint.recvfrom(1472)
            except TimeoutError:
                print("NAVHTTPS13_TEST_ONLY result=timeout", flush=True)
                return 2
            try:
                fields = rt._parse_message(
                    rt._unwrap_packet(request, max_packet_bytes=rt.MAX_REQUEST_PACKET_BYTES)
                )
                versions = fields.get("VER", b"")
                if (
                    fields.get("TYPE") != b"\0\0\0\0"
                    or not versions
                    or len(versions) % 4 != 0
                    or 1 not in tuple(struct.unpack_from("<I", versions, offset)[0]
                                      for offset in range(0, len(versions), 4))
                    or fields.get("SRV") != server_id
                ):
                    continue
                nonce = parse_request_nonce(request)
                if len(nonce) != 32:
                    continue
                response = make_response(request, nonce, midpoint=int(time.time()), radius=2)
            except (rt.RoughtimeError, ValueError, KeyError):
                continue

            endpoint.sendto(response, source)
            print(
                f"NAVHTTPS13_TEST_ONLY result=signed source={source[0]}:{source[1]} "
                f"request_bytes={len(request)} response_bytes={len(response)} nonce={nonce.hex()}",
                flush=True,
            )
            return 0

    print("NAVHTTPS13_TEST_ONLY result=invalid-request-limit", flush=True)
    return 3


if __name__ == "__main__":
    raise SystemExit(main())

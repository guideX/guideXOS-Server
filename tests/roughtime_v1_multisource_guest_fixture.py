"""Three separately keyed, test-only UDP signers for NAVHTTPS15 QEMU runs."""

from __future__ import annotations

import argparse
import hashlib
import socket
import threading
import time

from test_roughtime_v1 import DELEGATED_KEY, OTHER_KEY, ROOT_KEY, make_response, parse_request_nonce
from test_roughtime_portable import THIRD_ROOT_KEY
import roughtime_v1 as rt


FIXTURES = (
    (401, ROOT_KEY, 5319, 2),
    (402, OTHER_KEY, 5320, 1),
    (403, THIRD_ROOT_KEY, 5321, 1),
)


def serve_one_operator(operator_id: int, root_key, port: int, packet_count: int,
                       listen: str, timeout: float, errors: list[str]) -> None:
    public_key = root_key.public_key().public_bytes_raw()
    key_identity = hashlib.sha512(b"\xff" + public_key).digest()[:32]
    print(
        f"NAVHTTPS15_TEST_ONLY operator={operator_id} listen={listen}:{port} "
        f"server_key_id={key_identity.hex()} packets={packet_count}",
        flush=True,
    )
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
            endpoint.bind((listen, port))
            endpoint.settimeout(timeout)
            for sequence in range(1, packet_count + 1):
                request, client = endpoint.recvfrom(1472)
                fields = rt._parse_message(
                    rt._unwrap_packet(request, max_packet_bytes=rt.MAX_REQUEST_PACKET_BYTES)
                )
                versions = fields.get("VER", b"")
                if (
                    fields.get("TYPE") != b"\0\0\0\0"
                    or fields.get("SRV") != key_identity
                    or len(versions) == 0
                    or len(versions) % 4 != 0
                    or 1 not in tuple(
                        int.from_bytes(versions[offset:offset + 4], "little")
                        for offset in range(0, len(versions), 4)
                    )
                ):
                    raise ValueError("request protocol metadata or pinned key identity mismatch")
                nonce = parse_request_nonce(request)
                if len(nonce) != 32:
                    raise ValueError("request nonce length is invalid")
                response = make_response(
                    request, nonce, root_key=root_key, delegated_key=DELEGATED_KEY,
                    midpoint=int(time.time()), radius=2,
                )
                endpoint.sendto(response, client)
                print(
                    f"NAVHTTPS15_TEST_ONLY result=signed operator={operator_id} "
                    f"sequence={sequence} source={client[0]}:{client[1]} "
                    f"request_bytes={len(request)} response_bytes={len(response)} "
                    f"nonce={nonce.hex()}",
                    flush=True,
                )
    except BaseException as error:
        errors.append(f"operator={operator_id} {type(error).__name__}: {error}")
        print(f"NAVHTTPS15_TEST_ONLY result=error operator={operator_id} error={error}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="0.0.0.0")
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 180:
        parser.error("timeout must be greater than 0 and no more than 180 seconds")

    errors: list[str] = []
    threads = [
        threading.Thread(
            target=serve_one_operator,
            args=(operator_id, root_key, port, packet_count, args.listen, args.timeout, errors),
            daemon=True,
        )
        for operator_id, root_key, port, packet_count in FIXTURES
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(args.timeout + 2)

    if any(thread.is_alive() for thread in threads):
        errors.append("one or more signer threads did not stop before the bounded timeout")
    if errors:
        print("NAVHTTPS15_TEST_ONLY result=FAIL " + " | ".join(errors), flush=True)
        return 2
    print("NAVHTTPS15_TEST_ONLY result=PASS operators=3 simulated_independence=yes", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

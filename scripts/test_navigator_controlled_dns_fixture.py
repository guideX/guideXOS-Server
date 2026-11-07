#!/usr/bin/env python3
"""Hosted loopback checks for the controlled Navigator UDP DNS fixture."""

from __future__ import annotations

import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


PORT = 55353
HOSTNAME = "prod.guidexos.test"
ADDRESS = "10.0.2.2"
FIXTURE = Path(__file__).with_name("navigator_controlled_dns_fixture.py")


def encode_query(name: str, ident: int) -> bytes:
    labels = b"".join(bytes((len(part),)) + part.encode("ascii")
                      for part in name.split(".")) + b"\0"
    return struct.pack("!HHHHHH", ident, 0x0100, 1, 0, 0, 0) + labels + struct.pack("!HH", 1, 1)


def disable_udp_connreset(sock: socket.socket) -> None:
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        sock.ioctl(socket.SIO_UDP_CONNRESET, False)


def parse_answer(packet: bytes) -> tuple[int, int, str, str | None]:
    if len(packet) < 12:
        raise AssertionError("short response")
    ident, flags, qdcount, ancount = struct.unpack_from("!HHHH", packet)
    if qdcount != 1:
        raise AssertionError("unexpected question count")
    offset = 12
    labels: list[str] = []
    while packet[offset]:
        size = packet[offset]
        offset += 1
        labels.append(packet[offset:offset + size].decode("ascii").lower())
        offset += size
    offset += 1
    qname = ".".join(labels)
    offset += 4
    answer = None
    for _ in range(ancount):
        if packet[offset] & 0xC0 == 0xC0:
            offset += 2
        else:
            raise AssertionError("fixture answer owner was not compressed")
        rtype, rclass, _ttl, rdlength = struct.unpack_from("!HHIH", packet, offset)
        offset += 10
        rdata = packet[offset:offset + rdlength]
        offset += rdlength
        if rtype == 1 and rclass == 1 and rdlength == 4:
            answer = socket.inet_ntoa(rdata)
    return ident, flags, qname, answer


def query(name: str, ident: int, timeout: float = 0.7) -> tuple[bytes, tuple[str, int]]:
    client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    disable_udp_connreset(client)
    client.settimeout(timeout)
    try:
        client.sendto(encode_query(name, ident), ("127.0.0.1", PORT))
        return client.recvfrom(2048)
    finally:
        client.close()


def wait_ready(process: subprocess.Popen[bytes], log_path: Path) -> None:
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"fixture exited during startup: {log_path.read_text(errors='replace')}")
        try:
            if any(json.loads(line).get("event") == "ready" and
                   json.loads(line).get("pid") == process.pid
                   for line in log_path.read_text(errors="replace").splitlines() if line.strip()):
                return
        except (OSError, json.JSONDecodeError):
            pass
        time.sleep(0.025)
    raise AssertionError("fixture did not become ready within 4 seconds")


def start_fixture(log_path: Path, stop_path: Path):
    log_file = log_path.open("ab", buffering=0)
    process = subprocess.Popen(
        [sys.executable, str(FIXTURE), "--bind", "127.0.0.1", "--port", str(PORT),
         "--hostname", HOSTNAME, "--address", ADDRESS, "--stop-file", str(stop_path)],
        stdout=log_file, stderr=subprocess.STDOUT,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    wait_ready(process, log_path)
    return process, log_file


def stop_fixture(process: subprocess.Popen[bytes], log_file, stop_path: Path) -> None:
    if process.poll() is None:
        stop_path.write_text("stop\n")
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)
    log_file.close()
    if process.poll() is None:
        raise AssertionError("fixture process did not exit")


def assert_port_free() -> None:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(("127.0.0.1", PORT))
    finally:
        probe.close()


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="navigator-dns-fixture-") as temp_dir:
        log_path = Path(temp_dir) / "fixture.jsonl"
        stop_path = Path(temp_dir) / "fixture.stop"
        assert_port_free()
        process, log_file = start_fixture(log_path, stop_path)
        try:
            response, peer = query(HOSTNAME, 0x45A7)
            ident, flags, qname, answer = parse_answer(response)
            assert peer == ("127.0.0.1", PORT), f"unexpected response source {peer}"
            assert ident == 0x45A7, "transaction ID was not preserved"
            assert flags & 0x8400 == 0x8400 and flags & 0x000F == 0
            assert qname == HOSTNAME and answer == ADDRESS

            response, _ = query("unmapped.guidexos.test", 0x5A12)
            ident, flags, qname, answer = parse_answer(response)
            assert ident == 0x5A12 and qname == "unmapped.guidexos.test"
            assert flags & 0x000F == 3 and answer is None, "fixture acted as a wildcard mapping"

            client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            disable_udp_connreset(client)
            client.settimeout(0.2)
            try:
                client.sendto(b"\x12\x34\x01", ("127.0.0.1", PORT))
                try:
                    client.recvfrom(2048)
                    raise AssertionError("fixture returned a response to an invalid packet")
                except (socket.timeout, ConnectionResetError):
                    if process.poll() is not None:
                        raise AssertionError("fixture exited while ignoring an invalid packet")
                    pass
            finally:
                client.close()
        finally:
            stop_fixture(process, log_file, stop_path)
        assert_port_free()

        stop_path.unlink(missing_ok=True)
        process, log_file = start_fixture(log_path, stop_path)
        try:
            response, peer = query(HOSTNAME, 0x6B39)
            ident, flags, qname, answer = parse_answer(response)
            assert peer == ("127.0.0.1", PORT)
            assert ident == 0x6B39 and qname == HOSTNAME and answer == ADDRESS
            assert flags & 0x000F == 0
        finally:
            stop_fixture(process, log_file, stop_path)
        assert_port_free()

        events = [json.loads(line) for line in log_path.read_text().splitlines() if line.strip()]
        queries = [entry for entry in events if entry.get("event") == "query"]
        responses = [entry for entry in events if entry.get("event") == "response"]
        assert [entry["id"] for entry in queries if entry["qname"] == HOSTNAME] == [0x45A7, 0x6B39]
        hostname_responses = [entry for entry in responses if entry["qname"] == HOSTNAME]
        assert [entry["id"] for entry in hostname_responses] == [0x45A7, 0x6B39]
        assert all(entry["answer"] == ADDRESS and entry["rcode"] == 0
                   for entry in hostname_responses)
        assert len([entry for entry in queries if entry["qname"] == "unmapped.guidexos.test"]) == 1
        assert len([entry for entry in events if entry.get("event") == "invalid_query"]) == 1
        assert len([entry for entry in events if entry.get("event") == "stopped"]) == 2

    print("[CONTROLLED-DNS-FIXTURE] PASS cases=6 address=10.0.2.2 port=55353 shutdown_restart=yes")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, subprocess.SubprocessError, ValueError) as error:
        print(f"[CONTROLLED-DNS-FIXTURE] FAIL {error}", file=sys.stderr)
        raise SystemExit(1)

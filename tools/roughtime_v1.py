"""Bounded RFC 10049 Roughtime v1 UDP client and response verifier.

This hosted tool verifies one pinned server response. It deliberately does not
promote a single valid response to guideXOS production clock authorization.
"""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import math
import secrets
import socket
import struct
import sys
import time
from dataclasses import asdict, dataclass
from typing import Callable, Mapping, Sequence

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey


MAGIC = b"ROUGHTIM"
VERSION = 1
MAX_PACKET_BYTES = 1024
MAX_MAP_PAIRS = 16
MAX_MERKLE_HEIGHT = 32
MIN_TIME_SECONDS = 1_704_067_200  # guideXOS clock contract: 2024-01-01 UTC
MAX_TIME_SECONDS = 4_133_980_800  # exclusive: 2101-01-01 UTC
RESPONSE_CONTEXT = b"Roughtime v1 response signature\x00"
DELEGATION_CONTEXT = b"Roughtime v1 delegation signature\x00"


class RoughtimeError(ValueError):
    """The packet, transport, or time evidence did not meet the contract."""


@dataclass(frozen=True)
class TimeEvidence:
    source: str
    server_key_id: str
    protocol_version: int
    midpoint_unix_seconds: int
    server_earliest_unix_seconds: int
    server_latest_unix_seconds: int
    earliest_unix_seconds: int
    latest_unix_seconds: int
    estimated_uncertainty_seconds: int
    measured_transport_delay_ns: int
    received_monotonic_ns: int
    freshness_deadline_monotonic_ns: int
    signature_verified: bool
    nonce_matched: bool
    merkle_proof_verified: bool
    provenance_status: str
    consistency_status: str
    rollback_status: str
    production_policy_accepted: bool


def _tag_number(tag: str) -> int:
    try:
        encoded = tag.encode("ascii")
    except UnicodeEncodeError as exc:
        raise RoughtimeError("tag is not ASCII") from exc
    if not 1 <= len(encoded) <= 4 or any(byte < 65 or byte > 90 for byte in encoded):
        raise RoughtimeError("tag must contain one to four uppercase ASCII letters")
    return int.from_bytes(encoded.ljust(4, b"\x00"), "little")


def _tag_text(number: int) -> str:
    encoded = number.to_bytes(4, "little")
    zero_at = encoded.find(b"\x00")
    if zero_at == -1:
        text = encoded
    else:
        text = encoded[:zero_at]
        if any(encoded[zero_at:]):
            raise RoughtimeError("tag has nonzero bytes after padding")
    if not 1 <= len(text) <= 4 or any(byte < 65 or byte > 90 for byte in text):
        raise RoughtimeError("tag is not one to four uppercase ASCII letters")
    return text.decode("ascii")


def encode_message(fields: Mapping[str, bytes]) -> bytes:
    """Encode a sorted RFC 10049 tag map; used for requests and test fixtures."""
    if not fields or len(fields) > MAX_MAP_PAIRS:
        raise RoughtimeError("invalid tag count")
    ordered = sorted((_tag_number(tag), bytes(value)) for tag, value in fields.items())
    if len({number for number, _ in ordered}) != len(ordered):
        raise RoughtimeError("duplicate tags")
    count = len(ordered)
    offsets: list[int] = []
    values = bytearray()
    for index, (_, value) in enumerate(ordered):
        if len(values) % 4:
            raise RoughtimeError("tag values must begin at four-byte offsets")
        if index:
            offsets.append(len(values))
        values.extend(value)
    header = struct.pack("<I", count)
    if offsets:
        header += struct.pack("<" + "I" * len(offsets), *offsets)
    header += struct.pack("<" + "I" * count, *(number for number, _ in ordered))
    return header + bytes(values)


def _parse_message(message: bytes, *, max_pairs: int = MAX_MAP_PAIRS) -> dict[str, bytes]:
    if len(message) < 4:
        raise RoughtimeError("truncated message header")
    count = struct.unpack_from("<I", message, 0)[0]
    if count == 0 or count > max_pairs:
        raise RoughtimeError("invalid or excessive tag count")
    header_bytes = 4 + 4 * (count - 1) + 4 * count
    if header_bytes > len(message):
        raise RoughtimeError("truncated message header arrays")
    offsets_start = 4
    offsets = list(struct.unpack_from("<" + "I" * (count - 1), message, offsets_start)) if count > 1 else []
    tags_start = 4 + 4 * (count - 1)
    tag_numbers = list(struct.unpack_from("<" + "I" * count, message, tags_start))
    if tag_numbers != sorted(tag_numbers) or len(set(tag_numbers)) != count:
        raise RoughtimeError("tags are not uniquely sorted")
    tags = [_tag_text(number) for number in tag_numbers]
    value_bytes = message[header_bytes:]
    if offsets != sorted(offsets) or any(offset % 4 for offset in offsets):
        raise RoughtimeError("value offsets are not sorted four-byte boundaries")
    if any(offset > len(value_bytes) for offset in offsets):
        raise RoughtimeError("value offset exceeds message length")
    starts = [0, *offsets]
    ends = [*offsets, len(value_bytes)]
    if any(end < start for start, end in zip(starts, ends)):
        raise RoughtimeError("overlapping tag values")
    return {tag: value_bytes[start:end] for tag, start, end in zip(tags, starts, ends)}


def _wrap_packet(message: bytes) -> bytes:
    return MAGIC + struct.pack("<I", len(message)) + message


def _unwrap_packet(packet: bytes) -> bytes:
    if not isinstance(packet, bytes) or not 12 <= len(packet) <= MAX_PACKET_BYTES:
        raise RoughtimeError("packet is truncated or exceeds the packet limit")
    if packet[:8] != MAGIC:
        raise RoughtimeError("invalid Roughtime packet magic")
    message_length = struct.unpack_from("<I", packet, 8)[0]
    if message_length != len(packet) - 12:
        raise RoughtimeError("packet message length does not match datagram")
    return packet[12:]


def build_request(
    server_public_key: bytes,
    *,
    nonce_source: Callable[[int], bytes] = secrets.token_bytes,
) -> tuple[bytes, bytes]:
    """Build one padded v1 UDP request and return its packet and fresh nonce."""
    if len(server_public_key) != 32:
        raise RoughtimeError("server Ed25519 public key must be 32 bytes")
    try:
        nonce = nonce_source(32)
    except Exception as exc:
        raise RoughtimeError("secure randomness is unavailable") from exc
    if not isinstance(nonce, bytes) or len(nonce) != 32:
        raise RoughtimeError("secure randomness did not return a 32-byte nonce")
    server_id = hashlib.sha512(b"\xff" + server_public_key).digest()[:32]
    fields = {
        "SRV": server_id,
        "VER": struct.pack("<I", VERSION),
        "NONC": nonce,
        "TYPE": struct.pack("<I", 0),
        "ZZZZ": b"",
    }
    message = encode_message(fields)
    padding_length = MAX_PACKET_BYTES - 12 - len(message)
    if padding_length < 0 or padding_length % 4:
        raise RoughtimeError("request fields cannot fit the bounded UDP packet")
    fields["ZZZZ"] = bytes(padding_length)
    packet = _wrap_packet(encode_message(fields))
    if len(packet) != MAX_PACKET_BYTES:
        raise RoughtimeError("request did not meet the RFC UDP size recommendation")
    return packet, nonce


def _required(fields: Mapping[str, bytes], names: Sequence[str], context: str) -> None:
    missing = [name for name in names if name not in fields]
    if missing:
        raise RoughtimeError(f"{context} is missing required tag(s): {', '.join(missing)}")


def _uint32(fields: Mapping[str, bytes], name: str, context: str) -> int:
    value = fields[name]
    if len(value) != 4:
        raise RoughtimeError(f"{context} {name} must be a uint32")
    return struct.unpack("<I", value)[0]


def _uint64(fields: Mapping[str, bytes], name: str, context: str) -> int:
    value = fields[name]
    if len(value) != 8:
        raise RoughtimeError(f"{context} {name} must be a uint64")
    return struct.unpack("<Q", value)[0]


def _verify_signature(public_key: bytes, signature: bytes, message: bytes, label: str) -> None:
    if len(public_key) != 32 or len(signature) != 64:
        raise RoughtimeError(f"invalid {label} key or signature length")
    try:
        Ed25519PublicKey.from_public_bytes(public_key).verify(signature, message)
    except (InvalidSignature, ValueError) as exc:
        raise RoughtimeError(f"{label} signature verification failed") from exc


def verify_response(
    response_packet: bytes,
    request_packet: bytes,
    expected_nonce: bytes,
    server_public_key: bytes,
    *,
    source: str,
    sent_monotonic_ns: int,
    received_monotonic_ns: int,
    max_radius_seconds: int = 300,
    max_transport_delay_seconds: float = 2.0,
    evidence_lifetime_seconds: int = 300,
) -> TimeEvidence:
    """Verify all RFC 10049 v1 signatures, nonce binding, and Merkle proof."""
    if len(server_public_key) != 32 or len(expected_nonce) != 32:
        raise RoughtimeError("invalid expected key or nonce length")
    if not isinstance(max_radius_seconds, int) or max_radius_seconds <= 0:
        raise RoughtimeError("maximum time radius must be a positive integer")
    if not math.isfinite(max_transport_delay_seconds) or max_transport_delay_seconds <= 0:
        raise RoughtimeError("maximum transport delay must be finite and positive")
    if not isinstance(evidence_lifetime_seconds, int) or evidence_lifetime_seconds <= 0:
        raise RoughtimeError("evidence lifetime must be a positive integer")
    if received_monotonic_ns < sent_monotonic_ns:
        raise RoughtimeError("monotonic clock moved backwards during measurement")
    delay_ns = received_monotonic_ns - sent_monotonic_ns
    if delay_ns > int(max_transport_delay_seconds * 1_000_000_000):
        raise RoughtimeError("response exceeded the maximum transport delay")

    if len(response_packet) > len(request_packet):
        raise RoughtimeError("response exceeds the request datagram size")
    request_fields = _parse_message(_unwrap_packet(request_packet))
    _required(request_fields, ("VER", "NONC", "TYPE", "SRV"), "request")
    if request_fields["NONC"] != expected_nonce or len(expected_nonce) != 32:
        raise RoughtimeError("expected nonce does not match the transmitted request")
    if _uint32(request_fields, "TYPE", "request") != 0:
        raise RoughtimeError("request TYPE is not 0")
    expected_server_id = hashlib.sha512(b"\xff" + server_public_key).digest()[:32]
    if request_fields["SRV"] != expected_server_id:
        raise RoughtimeError("request is not bound to the expected long-term key")
    request_versions = request_fields["VER"]
    if len(request_versions) == 0 or len(request_versions) % 4 or len(request_versions) > 128:
        raise RoughtimeError("request version list is malformed")
    advertised_versions = struct.unpack("<" + "I" * (len(request_versions) // 4), request_versions)
    if tuple(advertised_versions) != tuple(sorted(set(advertised_versions))) or VERSION not in advertised_versions:
        raise RoughtimeError("request did not advertise Roughtime v1")
    if "ZZZZ" not in request_fields or any(request_fields["ZZZZ"]):
        raise RoughtimeError("request padding tag is missing or nonzero")

    top = _parse_message(_unwrap_packet(response_packet))
    required_top = ("SIG", "NONC", "TYPE", "PATH", "SREP", "CERT", "INDX")
    _required(top, required_top, "response")
    if len(top["SIG"]) != 64 or len(top["NONC"]) != 32 or len(top["TYPE"]) != 4:
        raise RoughtimeError("response contains an invalid signature, nonce, or type length")
    if struct.unpack("<I", top["TYPE"])[0] != 1:
        raise RoughtimeError("response TYPE is not 1")
    if top["NONC"] != expected_nonce:
        raise RoughtimeError("response nonce does not match the fresh request")
    path = top["PATH"]
    if len(path) % 32 or len(path) > MAX_MERKLE_HEIGHT * 32:
        raise RoughtimeError("Merkle path length is invalid")
    index = _uint32(top, "INDX", "response")

    cert = _parse_message(top["CERT"])
    _required(cert, ("SIG", "DELE"), "delegation certificate")
    if len(cert["SIG"]) != 64:
        raise RoughtimeError("delegation signature must be 64 bytes")
    _verify_signature(server_public_key, cert["SIG"], DELEGATION_CONTEXT + cert["DELE"], "delegation")

    delegation = _parse_message(cert["DELE"])
    _required(delegation, ("PUBK", "MINT", "MAXT"), "delegated key")
    if len(delegation["PUBK"]) != 32:
        raise RoughtimeError("delegated Ed25519 key must be 32 bytes")
    minimum_time = _uint64(delegation, "MINT", "delegated key")
    maximum_time = _uint64(delegation, "MAXT", "delegated key")
    if minimum_time > maximum_time:
        raise RoughtimeError("delegated key validity interval is inverted")

    srep = _parse_message(top["SREP"])
    _required(srep, ("VER", "RADI", "MIDP", "VERS", "ROOT"), "signed response")
    if len(srep["ROOT"]) != 32:
        raise RoughtimeError("signed response root must be 32 bytes")
    protocol_version = _uint32(srep, "VER", "signed response")
    if protocol_version != VERSION:
        raise RoughtimeError("unsupported signed response protocol version")
    supported_versions = srep["VERS"]
    if not supported_versions or len(supported_versions) % 4 or len(supported_versions) > 128:
        raise RoughtimeError("server version list is malformed")
    versions = struct.unpack("<" + "I" * (len(supported_versions) // 4), supported_versions)
    if tuple(versions) != tuple(sorted(set(versions))) or VERSION not in versions:
        raise RoughtimeError("server version list is unsorted, duplicated, or excludes v1")

    midpoint = _uint64(srep, "MIDP", "signed response")
    radius = _uint32(srep, "RADI", "signed response")
    if radius == 0 or radius > max_radius_seconds:
        raise RoughtimeError("server time radius is zero or exceeds policy")
    if not minimum_time <= midpoint <= maximum_time:
        raise RoughtimeError("signed timestamp is outside delegated-key validity")
    server_earliest = midpoint - radius
    server_latest = midpoint + radius
    if server_earliest < MIN_TIME_SECONDS or server_latest >= MAX_TIME_SECONDS:
        raise RoughtimeError("signed timestamp interval is outside supported clock bounds")

    _verify_signature(delegation["PUBK"], top["SIG"], RESPONSE_CONTEXT + top["SREP"], "response")

    if index >> (len(path) // 32):
        raise RoughtimeError("Merkle index has nonzero bits beyond the proof path")
    leaf = hashlib.sha512(b"\x00" + request_packet).digest()[:32]
    for level in range(len(path) // 32):
        sibling = path[level * 32:(level + 1) * 32]
        if (index >> level) & 1:
            leaf = hashlib.sha512(b"\x01" + sibling + leaf).digest()[:32]
        else:
            leaf = hashlib.sha512(b"\x01" + leaf + sibling).digest()[:32]
    if leaf != srep["ROOT"]:
        raise RoughtimeError("Merkle inclusion proof does not match the signed root")

    transport_slack_seconds = (delay_ns + 999_999_999) // 1_000_000_000
    earliest = server_earliest
    latest = server_latest + transport_slack_seconds
    if earliest < MIN_TIME_SECONDS or latest >= MAX_TIME_SECONDS:
        raise RoughtimeError("time interval including transport delay exceeds supported bounds")
    width = latest - earliest
    estimate = earliest + width // 2
    uncertainty = (width + 1) // 2
    deadline = received_monotonic_ns + evidence_lifetime_seconds * 1_000_000_000
    if deadline > 0xFFFFFFFFFFFFFFFF:
        raise RoughtimeError("freshness deadline arithmetic overflow")

    key_id = hashlib.sha512(b"\xff" + server_public_key).digest()[:32].hex()
    return TimeEvidence(
        source=source,
        server_key_id=key_id,
        protocol_version=protocol_version,
        midpoint_unix_seconds=estimate,
        server_earliest_unix_seconds=server_earliest,
        server_latest_unix_seconds=server_latest,
        earliest_unix_seconds=earliest,
        latest_unix_seconds=latest,
        estimated_uncertainty_seconds=uncertainty,
        measured_transport_delay_ns=delay_ns,
        received_monotonic_ns=received_monotonic_ns,
        freshness_deadline_monotonic_ns=deadline,
        signature_verified=True,
        nonce_matched=True,
        merkle_proof_verified=True,
        provenance_status="cryptographically-verified-single-source",
        consistency_status="not-assessed-single-source",
        rollback_status="not-assessed-no-persistent-floor",
        production_policy_accepted=False,
    )


def evidence_is_fresh(evidence: TimeEvidence, now_monotonic_ns: int) -> bool:
    return (
        evidence.signature_verified
        and evidence.nonce_matched
        and evidence.merkle_proof_verified
        and evidence.received_monotonic_ns <= now_monotonic_ns <= evidence.freshness_deadline_monotonic_ns
    )


def intervals_are_causally_consistent(measurements: Sequence[TimeEvidence]) -> bool:
    """Apply RFC 10049's pairwise interval check to responses in receive order."""
    if not measurements:
        return False
    if any(not item.signature_verified or not item.nonce_matched or not item.merkle_proof_verified for item in measurements):
        return False
    for earlier_index, earlier in enumerate(measurements):
        for later in measurements[earlier_index + 1:]:
            if earlier.server_earliest_unix_seconds > later.server_latest_unix_seconds:
                return False
    return True


def _source_text(address: tuple) -> str:
    ip = ipaddress.ip_address(address[0])
    return f"[{ip}]:{address[1]}" if ip.version == 6 else f"{ip}:{address[1]}"


def query_server(
    server_ip: str,
    port: int,
    server_public_key: bytes,
    *,
    timeout_seconds: float = 2.0,
    max_radius_seconds: int = 300,
    max_transport_delay_seconds: float = 2.0,
    evidence_lifetime_seconds: int = 300,
    nonce_source: Callable[[int], bytes] = secrets.token_bytes,
    monotonic_ns: Callable[[], int] = time.monotonic_ns,
    socket_factory: Callable = socket.socket,
) -> TimeEvidence:
    """Send one bounded UDP request to a numeric IP and verify its reply."""
    try:
        ip = ipaddress.ip_address(server_ip)
    except ValueError as exc:
        raise RoughtimeError("server must be a numeric IPv4 or IPv6 address") from exc
    if not 1 <= port <= 65535:
        raise RoughtimeError("server port must be in the range 1..65535")
    if not math.isfinite(timeout_seconds) or not 0 < timeout_seconds <= 30:
        raise RoughtimeError("timeout must be finite and between 0 and 30 seconds")
    family = socket.AF_INET6 if ip.version == 6 else socket.AF_INET
    destination = (str(ip), port, 0, 0) if ip.version == 6 else (str(ip), port)
    request_packet, nonce = build_request(server_public_key, nonce_source=nonce_source)
    sock = None
    try:
        sock = socket_factory(family, socket.SOCK_DGRAM)
        sock.settimeout(timeout_seconds)
        sent_at = monotonic_ns()
        sent_bytes = sock.sendto(request_packet, destination)
        if sent_bytes != len(request_packet):
            raise RoughtimeError("UDP transport did not send the complete request")
        try:
            response_packet, response_source = sock.recvfrom(MAX_PACKET_BYTES + 1)
        except socket.timeout as exc:
            raise RoughtimeError("Roughtime UDP request timed out") from exc
        received_at = monotonic_ns()
        source_ip = ipaddress.ip_address(response_source[0])
        if source_ip != ip or response_source[1] != port:
            raise RoughtimeError("UDP response source does not match the configured server")
        if len(response_packet) > MAX_PACKET_BYTES:
            raise RoughtimeError("UDP response exceeds the bounded packet limit")
        return verify_response(
            response_packet,
            request_packet,
            nonce,
            server_public_key,
            source=_source_text(response_source),
            sent_monotonic_ns=sent_at,
            received_monotonic_ns=received_at,
            max_radius_seconds=max_radius_seconds,
            max_transport_delay_seconds=max_transport_delay_seconds,
            evidence_lifetime_seconds=evidence_lifetime_seconds,
        )
    except RoughtimeError:
        raise
    except OSError as exc:
        raise RoughtimeError(f"Roughtime UDP transport failed: {exc}") from exc
    finally:
        if sock is not None:
            sock.close()


def _main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Verify one RFC 10049 Roughtime v1 UDP response")
    parser.add_argument("--server-ip", required=True, help="numeric IPv4 or IPv6 address")
    parser.add_argument("--port", type=int, default=5319)
    parser.add_argument("--server-key-hex", required=True, help="32-byte pinned Ed25519 public key in hex")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--max-radius", type=int, default=300)
    parser.add_argument("--max-age", type=int, default=300)
    args = parser.parse_args(argv)
    try:
        key = bytes.fromhex(args.server_key_hex)
        evidence = query_server(
            args.server_ip,
            args.port,
            key,
            timeout_seconds=args.timeout,
            max_radius_seconds=args.max_radius,
            evidence_lifetime_seconds=args.max_age,
        )
    except (ValueError, RoughtimeError) as exc:
        print(json.dumps({"status": "rejected", "reason": str(exc)}, sort_keys=True), file=sys.stderr)
        return 2
    print(json.dumps(asdict(evidence), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())

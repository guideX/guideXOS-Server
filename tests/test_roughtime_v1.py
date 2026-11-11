import hashlib
import socket
import struct
import sys
import threading
import unittest
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import roughtime_v1 as rt


ROOT_SEED = bytes.fromhex("11" * 32)
DELEGATED_SEED = bytes.fromhex("22" * 32)
OTHER_SEED = bytes.fromhex("33" * 32)
ROOT_KEY = Ed25519PrivateKey.from_private_bytes(ROOT_SEED)
DELEGATED_KEY = Ed25519PrivateKey.from_private_bytes(DELEGATED_SEED)
OTHER_KEY = Ed25519PrivateKey.from_private_bytes(OTHER_SEED)
FIXTURE_TIME = 1_800_000_000


def u32(value):
    return struct.pack("<I", value)


def u64(value):
    return struct.pack("<Q", value)


def parse_request_nonce(request_packet):
    fields = rt._parse_message(rt._unwrap_packet(request_packet))
    return fields["NONC"]


def make_response(
    request_packet,
    nonce,
    *,
    root_key=ROOT_KEY,
    delegated_key=DELEGATED_KEY,
    midpoint=FIXTURE_TIME,
    radius=3,
    response_nonce=None,
    response_version=1,
    server_versions=(1,),
    merkle_root_override=None,
    path=b"",
    index=0,
    cert_signature_corrupt=False,
    response_signature_corrupt=False,
    mint=None,
    maxt=None,
):
    if mint is None:
        mint = midpoint - 1000 if midpoint >= 1000 else 0
    if maxt is None:
        maxt = min(midpoint + 1000, 0xFFFFFFFFFFFFFFFF)
    leaf = hashlib.sha512(b"\x00" + request_packet).digest()[:32]
    root = merkle_root_override if merkle_root_override is not None else leaf
    srep = rt.encode_message({
        "VER": u32(response_version),
        "RADI": u32(radius),
        "MIDP": u64(midpoint),
        "VERS": b"".join(u32(version) for version in server_versions),
        "ROOT": root,
    })
    dele = rt.encode_message({"PUBK": delegated_key.public_key().public_bytes_raw(), "MINT": u64(mint), "MAXT": u64(maxt)})
    cert_signature = bytearray(root_key.sign(rt.DELEGATION_CONTEXT + dele))
    if cert_signature_corrupt:
        cert_signature[0] ^= 1
    cert = rt.encode_message({"SIG": bytes(cert_signature), "DELE": dele})
    response_signature = bytearray(delegated_key.sign(rt.RESPONSE_CONTEXT + srep))
    if response_signature_corrupt:
        response_signature[-1] ^= 1
    response = rt.encode_message({
        "SIG": bytes(response_signature),
        "NONC": nonce if response_nonce is None else response_nonce,
        "TYPE": u32(1),
        "PATH": path,
        "SREP": srep,
        "CERT": cert,
        "INDX": u32(index),
    })
    return rt._wrap_packet(response)


def make_evidence(midpoint, root_key=ROOT_KEY):
    server_key = root_key.public_key().public_bytes_raw()
    request, nonce = rt.build_request(server_key, nonce_source=lambda _: b"N" * 32)
    response = make_response(request, nonce, root_key=root_key, midpoint=midpoint, radius=2)
    return rt.verify_response(
        response, request, nonce, server_key, source="127.0.0.1:5319",
        sent_monotonic_ns=10_000_000_000, received_monotonic_ns=10_100_000_000,
    )


class FakeSocket:
    def __init__(self, response_factory=None, source=("127.0.0.1", 5319), timeout=False):
        self.response_factory = response_factory
        self.source = source
        self.timeout = timeout
        self.closed = False
        self.timeout_value = None
        self.request = None

    def settimeout(self, value):
        self.timeout_value = value

    def sendto(self, packet, address):
        self.request = packet
        self.destination = address
        return len(packet)

    def recvfrom(self, max_bytes):
        if self.timeout:
            raise socket.timeout("fixture timeout")
        return self.response_factory(self.request), self.source

    def close(self):
        self.closed = True


class RoughtimeV1Tests(unittest.TestCase):
    def setUp(self):
        self.server_public_key = ROOT_KEY.public_key().public_bytes_raw()
        self.request, self.nonce = rt.build_request(
            self.server_public_key, nonce_source=lambda count: b"R" * count
        )

    def verify(self, response, **kwargs):
        options = {
            "source": "127.0.0.1:5319",
            "sent_monotonic_ns": 10_000_000_000,
            "received_monotonic_ns": 10_100_000_000,
        }
        options.update(kwargs)
        return rt.verify_response(response, self.request, self.nonce, self.server_public_key, **options)

    def test_request_is_fresh_nonce_bound_and_udp_bounded(self):
        self.assertEqual(len(self.request), 1024)
        fields = rt._parse_message(rt._unwrap_packet(self.request))
        self.assertEqual(fields["NONC"], b"R" * 32)
        self.assertEqual(fields["TYPE"], u32(0))
        self.assertEqual(fields["VER"], u32(1))
        self.assertTrue(all(byte == 0 for byte in fields["ZZZZ"]))

    def test_valid_signed_response_produces_interval_and_fresh_provenance(self):
        response = make_response(self.request, self.nonce)
        evidence = self.verify(response)
        self.assertTrue(evidence.signature_verified)
        self.assertTrue(evidence.nonce_matched)
        self.assertTrue(evidence.merkle_proof_verified)
        self.assertEqual(evidence.server_key_id, hashlib.sha512(b"\xff" + self.server_public_key).digest()[:32].hex())
        self.assertEqual(evidence.measured_transport_delay_ns, 100_000_000)
        self.assertEqual(evidence.earliest_unix_seconds, FIXTURE_TIME - 3)
        self.assertEqual(evidence.latest_unix_seconds, FIXTURE_TIME + 4)
        self.assertEqual(evidence.estimated_uncertainty_seconds, 4)
        self.assertTrue(rt.evidence_is_fresh(evidence, evidence.received_monotonic_ns))
        self.assertFalse(evidence.production_policy_accepted)

    def test_wrong_long_term_server_key_rejects(self):
        wrong_public_key = OTHER_KEY.public_key().public_bytes_raw()
        request, nonce = rt.build_request(wrong_public_key, nonce_source=lambda _: b"W" * 32)
        response = make_response(request, nonce, root_key=ROOT_KEY)
        with self.assertRaises(rt.RoughtimeError):
            rt.verify_response(
                response, request, nonce, wrong_public_key, source="127.0.0.1:5319",
                sent_monotonic_ns=10, received_monotonic_ns=10,
            )

    def test_corrupted_response_signature_rejects(self):
        response = make_response(self.request, self.nonce, response_signature_corrupt=True)
        with self.assertRaisesRegex(rt.RoughtimeError, "response signature"):
            self.verify(response)

    def test_invalid_delegated_certificate_signature_rejects(self):
        response = make_response(self.request, self.nonce, cert_signature_corrupt=True)
        with self.assertRaisesRegex(rt.RoughtimeError, "delegation signature"):
            self.verify(response)

    def test_inverted_delegated_key_validity_rejects(self):
        response = make_response(self.request, self.nonce, mint=FIXTURE_TIME + 1, maxt=FIXTURE_TIME)
        with self.assertRaisesRegex(rt.RoughtimeError, "validity interval"):
            self.verify(response)

    def test_wrong_nonce_rejects(self):
        response = make_response(self.request, self.nonce, response_nonce=b"X" * 32)
        with self.assertRaisesRegex(rt.RoughtimeError, "nonce"):
            self.verify(response)

    def test_replay_of_previous_response_rejects_for_fresh_request(self):
        prior_response = make_response(self.request, self.nonce)
        fresh_request, fresh_nonce = rt.build_request(
            self.server_public_key, nonce_source=lambda _: b"S" * 32
        )
        self.assertNotEqual(fresh_nonce, self.nonce)
        with self.assertRaises(rt.RoughtimeError):
            rt.verify_response(
                prior_response, fresh_request, fresh_nonce, self.server_public_key,
                source="127.0.0.1:5319", sent_monotonic_ns=10, received_monotonic_ns=10,
            )

    def test_invalid_merkle_path_rejects(self):
        response = make_response(
            self.request, self.nonce,
            merkle_root_override=b"M" * 32,
        )
        with self.assertRaisesRegex(rt.RoughtimeError, "Merkle inclusion"):
            self.verify(response)

    def test_merkle_index_with_missing_levels_rejects(self):
        response = make_response(self.request, self.nonce, index=1)
        with self.assertRaisesRegex(rt.RoughtimeError, "Merkle index"):
            self.verify(response)

    def test_unsupported_protocol_version_rejects(self):
        response = make_response(
            self.request, self.nonce, response_version=2, server_versions=(1, 2)
        )
        with self.assertRaisesRegex(rt.RoughtimeError, "unsupported"):
            self.verify(response)

    def test_malformed_packet_rejects(self):
        response = bytearray(make_response(self.request, self.nonce))
        response[0] ^= 0x20
        with self.assertRaisesRegex(rt.RoughtimeError, "magic"):
            self.verify(bytes(response))

    def test_truncated_datagram_rejects(self):
        response = make_response(self.request, self.nonce)[:-1]
        with self.assertRaisesRegex(rt.RoughtimeError, "length"):
            self.verify(response)

    def test_excessive_radius_rejects(self):
        response = make_response(self.request, self.nonce, radius=301)
        with self.assertRaisesRegex(rt.RoughtimeError, "radius"):
            self.verify(response)

    def test_invalid_timestamp_bounds_reject(self):
        response = make_response(self.request, self.nonce, midpoint=rt.MIN_TIME_SECONDS - 10)
        with self.assertRaisesRegex(rt.RoughtimeError, "clock bounds"):
            self.verify(response)

    def test_timestamp_arithmetic_overflow_rejects(self):
        response = make_response(
            self.request, self.nonce, midpoint=0xFFFFFFFFFFFFFFFF, radius=2,
            mint=0, maxt=0xFFFFFFFFFFFFFFFF,
        )
        with self.assertRaisesRegex(rt.RoughtimeError, "clock bounds"):
            self.verify(response)

    def test_expired_measurement_is_not_fresh(self):
        response = make_response(self.request, self.nonce)
        evidence = self.verify(response, evidence_lifetime_seconds=1)
        self.assertFalse(rt.evidence_is_fresh(evidence, evidence.freshness_deadline_monotonic_ns + 1))

    def test_inconsistent_server_intervals_reject_sequence(self):
        first = make_evidence(FIXTURE_TIME, ROOT_KEY)
        second = make_evidence(FIXTURE_TIME + 100, OTHER_KEY)
        self.assertTrue(rt.intervals_are_causally_consistent((first, second)))
        conflicting = make_evidence(FIXTURE_TIME - 100, OTHER_KEY)
        self.assertFalse(rt.intervals_are_causally_consistent((first, conflicting)))

    def test_missing_secure_randomness_rejects(self):
        def unavailable(_):
            raise OSError("no CSPRNG")
        with self.assertRaisesRegex(rt.RoughtimeError, "secure randomness"):
            rt.build_request(self.server_public_key, nonce_source=unavailable)

    def test_short_random_output_rejects(self):
        with self.assertRaisesRegex(rt.RoughtimeError, "32-byte nonce"):
            rt.build_request(self.server_public_key, nonce_source=lambda _: b"short")

    def test_network_timeout_rejects_and_closes_socket(self):
        fake = FakeSocket(timeout=True)
        with self.assertRaisesRegex(rt.RoughtimeError, "timed out"):
            rt.query_server(
                "127.0.0.1", 5319, self.server_public_key,
                socket_factory=lambda *_: fake,
            )
        self.assertTrue(fake.closed)
        self.assertLessEqual(fake.timeout_value, 2.0)

    def test_unexpected_response_source_rejects_and_closes_socket(self):
        fake = FakeSocket(
            response_factory=lambda request: make_response(request, parse_request_nonce(request)),
            source=("127.0.0.2", 5319),
        )
        with self.assertRaisesRegex(rt.RoughtimeError, "source"):
            rt.query_server(
                "127.0.0.1", 5319, self.server_public_key,
                socket_factory=lambda *_: fake,
            )
        self.assertTrue(fake.closed)

    def test_controlled_udp_server_round_trip(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(2)
        port = server.getsockname()[1]
        server_error = []

        def serve_one():
            try:
                request, peer = server.recvfrom(2048)
                server.sendto(make_response(request, parse_request_nonce(request)), peer)
            except Exception as exc:  # surfaced to the test thread
                server_error.append(exc)
            finally:
                server.close()

        worker = threading.Thread(target=serve_one, daemon=True)
        worker.start()
        try:
            evidence = rt.query_server("127.0.0.1", port, self.server_public_key, timeout_seconds=1)
        finally:
            worker.join(timeout=2)
            if server.fileno() != -1:
                server.close()
        self.assertFalse(worker.is_alive())
        self.assertFalse(server_error, server_error)
        self.assertEqual(evidence.source, f"127.0.0.1:{port}")
        self.assertTrue(evidence.signature_verified)


if __name__ == "__main__":
    unittest.main()

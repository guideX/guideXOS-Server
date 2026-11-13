"""Hosted differential checks for the freestanding RFC 10049 verifier."""

from __future__ import annotations

import base64
import ctypes
import hashlib
import json
import os
import random
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
import roughtime_v1 as rt  # noqa: E402
from test_roughtime_v1 import (  # noqa: E402
    DELEGATED_KEY,
    FIXTURE_TIME,
    OTHER_KEY,
    ROOT_KEY,
    make_response,
    u32,
)


class Workspace(ctypes.Structure):
    _fields_ = [
        ("transcript", ctypes.c_uint8 * 1088),
        ("digest", ctypes.c_uint8 * 64),
        ("merkle_current", ctypes.c_uint8 * 32),
        ("merkle_next", ctypes.c_uint8 * 32),
    ]


class Evidence(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("protocol_version", ctypes.c_uint32),
        ("radius_seconds", ctypes.c_uint32),
        ("server_midpoint_unix_seconds", ctypes.c_uint64),
        ("estimated_midpoint_unix_seconds", ctypes.c_uint64),
        ("server_earliest_unix_seconds", ctypes.c_uint64),
        ("server_latest_unix_seconds", ctypes.c_uint64),
        ("earliest_unix_seconds", ctypes.c_uint64),
        ("latest_unix_seconds", ctypes.c_uint64),
        ("estimated_uncertainty_seconds", ctypes.c_uint64),
        ("sent_monotonic_ns", ctypes.c_uint64),
        ("received_monotonic_ns", ctypes.c_uint64),
        ("measured_transport_delay_ns", ctypes.c_uint64),
        ("server_key_identity", ctypes.c_uint8 * 32),
        ("request_nonce", ctypes.c_uint8 * 32),
        ("request_identity", ctypes.c_uint8 * 32),
        ("signature_verified", ctypes.c_uint8),
        ("nonce_matched", ctypes.c_uint8),
        ("merkle_proof_verified", ctypes.c_uint8),
        ("production_policy_accepted", ctypes.c_uint8),
    ]


def _raw_map(entries: list[tuple[str, bytes]]) -> bytes:
    values = bytearray()
    offsets = []
    tag_numbers = []
    for index, (tag, value) in enumerate(entries):
        if index:
            offsets.append(len(values))
        if index and len(values) % 4:
            raise ValueError("raw test fields must begin on a four-byte boundary")
        values.extend(value)
        tag_numbers.append(rt._tag_number(tag))
    header = struct.pack("<I", len(entries))
    if offsets:
        header += struct.pack("<" + "I" * len(offsets), *offsets)
    header += struct.pack("<" + "I" * len(tag_numbers), *tag_numbers)
    return header + values


def _packet(message: bytes) -> bytes:
    return rt.MAGIC + struct.pack("<I", len(message)) + message


def _field_bounds(packet: bytes, target: str) -> tuple[int, int]:
    message = rt._unwrap_packet(bytes(packet))
    count = struct.unpack_from("<I", message)[0]
    offsets_start = 4
    tags_start = 4 + 4 * (count - 1)
    values_start = 4 + 4 * (count - 1) + 4 * count
    offsets = [0]
    if count > 1:
        offsets += list(struct.unpack_from("<" + "I" * (count - 1), message, offsets_start))
    offsets.append(len(message) - values_start)
    tag_numbers = struct.unpack_from("<" + "I" * count, message, tags_start)
    for index, tag_number in enumerate(tag_numbers):
        if rt._tag_text(tag_number) == target:
            return 12 + values_start + offsets[index], offsets[index + 1] - offsets[index]
    raise KeyError(target)


def _read_independent_vector() -> tuple[dict, bytes, bytes, bytes]:
    vector = json.loads((ROOT / "tests/vectors/roughtime_roughenough_2_2_0.json").read_text())
    request = base64.b64decode(vector["request_packet_base64"])
    response = base64.b64decode(vector["response_packet_base64"])
    key = bytes.fromhex(vector["server_public_key_hex"])
    return vector, request, response, key


class PortableRoughtimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_dir = tempfile.TemporaryDirectory(prefix="gxos-roughtime-")
        build = Path(cls.build_dir.name)
        is_windows = os.name == "nt"
        extension = ".dll" if is_windows else ".so"
        library_path = build / ("gxos_roughtime_v1" + extension)

        manifest = (ROOT / "guidexos/libsodium_ed25519_sources.mk").read_text()
        sources = re.findall(r"\$\(GUIDEXOS_ED25519_ROOT\)/([^\s\\]+\.c)", manifest)
        if len(sources) != 9:
            raise RuntimeError(f"Expected 9 pinned Ed25519 C sources, found {len(sources)}")

        sodium = ROOT / "third_party/libsodium/src"
        include_dirs = [
            ROOT / "kernel/core/include",
            sodium / "include",
            sodium / "include/sodium",
            sodium / "crypto_hash/sha512",
            sodium / "crypto_hash/sha512/cp",
            sodium / "crypto_sign/ed25519/ref10",
            sodium / "crypto_verify",
            sodium / "crypto_core/ed25519",
            sodium / "crypto_core/ed25519/ref10",
        ]
        common_includes = [item for path in include_dirs for item in ("-I", str(path))]
        defines = [
            "-DCONFIGURED=1",
            "-DSODIUM_STATIC",
            "-DHAVE_TI_MODE=1",
            "-Dabort=__builtin_trap",
            "-Wno-unused-function",
            "-fno-builtin",
            "-ffreestanding",
            "-O2",
        ]
        objects = []
        gcc = os.environ.get("CC", "gcc")
        gxx = os.environ.get("CXX", "g++")
        for index, relative in enumerate(sources):
            source = sodium / relative
            obj = build / f"libsodium-{index}.o"
            subprocess.run(
                [gcc, "-x", "c", "-std=c11", *defines, *common_includes,
                 "-c", str(source), "-o", str(obj)],
                check=True,
                capture_output=True,
                text=True,
            )
            objects.append(obj)

        component_obj = build / "gxos_roughtime_v1.o"
        cxx_flags = [
            gxx,
            "-x", "c++",
            "-std=c++14",
            "-fno-exceptions",
            "-fno-rtti",
            "-fno-builtin",
            "-ffreestanding",
            "-O2",
            "-DGXOS_BARE_METAL",
            *common_includes,
            "-c", str(ROOT / "kernel/core/gxos_roughtime_v1.cpp"),
            "-o", str(component_obj),
        ]
        subprocess.run(cxx_flags, check=True, capture_output=True, text=True)
        link_args = [gxx, "-shared", "-o", str(library_path), str(component_obj),
                     *(str(obj) for obj in objects)]
        if is_windows:
            link_args.append("-Wl,--export-all-symbols")
        else:
            link_args.insert(1, "-fPIC")
        subprocess.run(link_args, check=True, capture_output=True, text=True)

        cls.library = ctypes.CDLL(str(library_path))
        cls.verify_function = cls.library.gxos_roughtime_v1_verify
        cls.verify_function.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64, ctypes.c_uint64,
            ctypes.POINTER(Workspace), ctypes.POINTER(Evidence),
        ]
        cls.verify_function.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        if os.name == "nt" and hasattr(cls, "library"):
            handle = cls.library._handle
            cls.verify_function = None
            ctypes.windll.kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
            ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(handle))
            cls.library._handle = 0
            cls.library = None
        cls.build_dir.cleanup()

    def setUp(self):
        self.sent_ns = 10_000_000_000
        self.received_ns = 10_100_000_000
        vector, self.independent_request, self.independent_response, self.independent_key = _read_independent_vector()
        self.independent_nonce = rt._parse_message(
            rt._unwrap_packet(self.independent_request)
        )["NONC"]
        self.vector = vector

    def verify_portable(self, response, request, nonce, key, workspace=None):
        response_arg = (ctypes.c_uint8 * len(response)).from_buffer_copy(response)
        request_arg = (ctypes.c_uint8 * len(request)).from_buffer_copy(request)
        nonce_arg = (ctypes.c_uint8 * len(nonce)).from_buffer_copy(nonce)
        key_arg = (ctypes.c_uint8 * len(key)).from_buffer_copy(key)
        workspace = workspace or Workspace()
        result = Evidence()
        status = self.verify_function(
            response_arg, len(response), request_arg, len(request),
            nonce_arg, key_arg, self.sent_ns, self.received_ns,
            ctypes.byref(workspace), ctypes.byref(result),
        )
        return status, result, workspace

    def assert_rejected_by_both(self, response, request, nonce, key, expected_status=None):
        with self.assertRaises(rt.RoughtimeError):
            rt.verify_response(
                response, request, nonce, key, source="fixture",
                sent_monotonic_ns=self.sent_ns, received_monotonic_ns=self.received_ns,
            )
        status, result, _ = self.verify_portable(response, request, nonce, key)
        self.assertNotEqual(status, 0)
        self.assertEqual(result.status, status)
        self.assertEqual(result.signature_verified, 0)
        self.assertEqual(result.nonce_matched, 0)
        self.assertEqual(result.merkle_proof_verified, 0)
        self.assertEqual(result.production_policy_accepted, 0)
        self.assertEqual(result.protocol_version, 0)
        self.assertEqual(result.earliest_unix_seconds, 0)
        self.assertEqual(bytes(result.server_key_identity), bytes(32))
        if expected_status is not None:
            self.assertEqual(status, expected_status)

    def test_independent_roughenough_capture_matches_python_evidence(self):
        self.assertEqual(len(self.independent_request), 1024)
        self.assertEqual(len(self.independent_response), 416)
        self.assertEqual(hashlib.sha256(self.independent_request).hexdigest(), self.vector["request_packet_sha256"])
        self.assertEqual(hashlib.sha256(self.independent_response).hexdigest(), self.vector["response_packet_sha256"])
        vector_path = ROOT / "tests/vectors/roughtime_roughenough_2_2_0.json"
        inventory = ROOT / "tests/vectors/roughtime_v1_fixtures.sha256"
        fixture_digest = next(
            line.split()[0]
            for line in inventory.read_text().splitlines()
            if line.split()[-1] == "tests/vectors/roughtime_roughenough_2_2_0.json"
        )
        self.assertEqual(hashlib.sha256(vector_path.read_bytes()).hexdigest(), fixture_digest)
        reference = rt.verify_response(
            self.independent_response, self.independent_request, self.independent_nonce,
            self.independent_key, source="Roughenough 2.2.0 static capture",
            sent_monotonic_ns=self.sent_ns, received_monotonic_ns=self.received_ns,
        )
        status, result, workspace = self.verify_portable(
            self.independent_response, self.independent_request,
            self.independent_nonce, self.independent_key,
        )
        self.assertEqual(status, 0)
        self.assertEqual(result.status, 0)
        self.assertEqual(result.protocol_version, reference.protocol_version)
        self.assertEqual(result.server_midpoint_unix_seconds, self.vector["server_midpoint_unix_seconds"])
        self.assertEqual(result.radius_seconds, self.vector["server_radius_seconds"])
        self.assertEqual(result.estimated_midpoint_unix_seconds, reference.midpoint_unix_seconds)
        self.assertEqual(result.earliest_unix_seconds, reference.earliest_unix_seconds)
        self.assertEqual(result.latest_unix_seconds, reference.latest_unix_seconds)
        self.assertEqual(result.estimated_uncertainty_seconds, reference.estimated_uncertainty_seconds)
        self.assertEqual(result.measured_transport_delay_ns, reference.measured_transport_delay_ns)
        self.assertEqual(bytes(result.server_key_identity).hex(), reference.server_key_id)
        self.assertEqual(bytes(result.request_nonce), self.independent_nonce)
        expected_leaf = hashlib.sha512(b"\x00" + self.independent_request).digest()[:32]
        self.assertEqual(bytes(result.request_identity), expected_leaf)
        self.assertEqual(result.received_monotonic_ns, self.received_ns)
        self.assertEqual(result.signature_verified, 1)
        self.assertEqual(result.nonce_matched, 1)
        self.assertEqual(result.merkle_proof_verified, 1)
        self.assertEqual(result.production_policy_accepted, 0)
        self.assertEqual(bytes(workspace), bytes(ctypes.sizeof(workspace)))

    def test_controlled_valid_response_matches_python(self):
        key = ROOT_KEY.public_key().public_bytes_raw()
        request, nonce = rt.build_request(key, nonce_source=lambda _: b"R" * 32)
        response = make_response(request, nonce)
        reference = rt.verify_response(
            response, request, nonce, key, source="fixture",
            sent_monotonic_ns=self.sent_ns, received_monotonic_ns=self.received_ns,
        )
        status, result, _ = self.verify_portable(response, request, nonce, key)
        self.assertEqual(status, 0)
        self.assertEqual(result.estimated_midpoint_unix_seconds, reference.midpoint_unix_seconds)
        self.assertEqual(result.earliest_unix_seconds, reference.earliest_unix_seconds)
        self.assertEqual(result.latest_unix_seconds, reference.latest_unix_seconds)
        self.assertEqual(result.estimated_uncertainty_seconds, reference.estimated_uncertainty_seconds)

    def test_unknown_well_formed_top_level_tag_is_ignored(self):
        fields = rt._parse_message(rt._unwrap_packet(self.independent_response))
        fields["ABCD"] = b"ext!"
        response = _packet(rt.encode_message(fields))
        reference = rt.verify_response(
            response, self.independent_request, self.independent_nonce,
            self.independent_key, source="fixture", sent_monotonic_ns=self.sent_ns,
            received_monotonic_ns=self.received_ns,
        )
        status, result, _ = self.verify_portable(
            response, self.independent_request, self.independent_nonce, self.independent_key,
        )
        self.assertEqual(status, 0)
        self.assertEqual(result.estimated_midpoint_unix_seconds, reference.midpoint_unix_seconds)

    def test_wrong_expected_key_rejects_delivered_valid_response(self):
        wrong_key = bytearray(self.independent_key)
        wrong_key[0] ^= 1
        self.assert_rejected_by_both(
            self.independent_response, self.independent_request, self.independent_nonce,
            bytes(wrong_key), expected_status=8,
        )

    def test_corrupted_response_signature_rejects(self):
        response = bytearray(self.independent_response)
        offset, _ = _field_bounds(response, "SIG")
        response[offset] ^= 1
        self.assert_rejected_by_both(
            bytes(response), self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=10,
        )

    def test_corrupted_delegation_signature_rejects(self):
        response = bytearray(self.independent_response)
        cert_offset, _ = _field_bounds(response, "CERT")
        cert = bytes(response[cert_offset:cert_offset + _field_bounds(response, "CERT")[1]])
        sig_offset, _ = _field_bounds(_packet(cert), "SIG")
        response[cert_offset + sig_offset - 12] ^= 1
        self.assert_rejected_by_both(
            bytes(response), self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=10,
        )

    def test_invalid_signed_delegation_interval_rejects(self):
        key = ROOT_KEY.public_key().public_bytes_raw()
        request, nonce = rt.build_request(key, nonce_source=lambda _: b"R" * 32)
        response = make_response(request, nonce, mint=FIXTURE_TIME + 1, maxt=FIXTURE_TIME)
        self.assert_rejected_by_both(response, request, nonce, key, expected_status=11)

    def test_nonce_mismatch_and_replay_reject(self):
        key = ROOT_KEY.public_key().public_bytes_raw()
        request, nonce = rt.build_request(key, nonce_source=lambda _: b"R" * 32)
        mismatch = make_response(request, nonce, response_nonce=b"X" * 32)
        self.assert_rejected_by_both(mismatch, request, nonce, key, expected_status=9)

        old_response = make_response(request, nonce)
        new_request, new_nonce = rt.build_request(key, nonce_source=lambda _: b"S" * 32)
        self.assert_rejected_by_both(old_response, new_request, new_nonce, key, expected_status=9)

    def test_wrong_merkle_path_rejects(self):
        key = ROOT_KEY.public_key().public_bytes_raw()
        request, nonce = rt.build_request(key, nonce_source=lambda _: b"R" * 32)
        response = make_response(request, nonce, merkle_root_override=b"M" * 32)
        self.assert_rejected_by_both(response, request, nonce, key, expected_status=12)

    def test_missing_required_field_rejects(self):
        fields = rt._parse_message(rt._unwrap_packet(self.independent_response))
        del fields["CERT"]
        response = _packet(rt.encode_message(fields))
        self.assert_rejected_by_both(
            response, self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=5,
        )

    def test_duplicate_and_unsorted_tags_reject(self):
        fields = rt._parse_message(rt._unwrap_packet(self.independent_response))
        entries = sorted(fields.items(), key=lambda item: rt._tag_number(item[0]))
        duplicate_entries = sorted(entries + [("SIG", fields["SIG"])], key=lambda item: rt._tag_number(item[0]))
        duplicate = _packet(_raw_map(duplicate_entries))
        self.assert_rejected_by_both(
            duplicate, self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=4,
        )
        unsorted_entries = entries[:]
        unsorted_entries[0], unsorted_entries[1] = unsorted_entries[1], unsorted_entries[0]
        unsorted = _packet(_raw_map(unsorted_entries))
        self.assert_rejected_by_both(
            unsorted, self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=4,
        )

    def test_invalid_tag_padding_and_offset_alignment_reject(self):
        malformed = _packet(struct.pack("<I", 1) + struct.pack("<I", 0x00420041) + b"data")
        with self.assertRaises(rt.RoughtimeError):
            rt._parse_message(rt._unwrap_packet(malformed))
        status, result, _ = self.verify_portable(
            malformed, self.independent_request, self.independent_nonce, self.independent_key,
        )
        self.assertEqual(status, 4)
        self.assertEqual(result.signature_verified, 0)

        response = bytearray(self.independent_response)
        response[12 + 4] = 1  # first value offset must be a multiple of four
        self.assert_rejected_by_both(
            bytes(response), self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=4,
        )

    def test_nonzero_request_padding_is_rejected(self):
        request = bytearray(self.independent_request)
        offset, length = _field_bounds(request, "ZZZZ")
        self.assertGreater(length, 0)
        request[offset] = 1
        self.assert_rejected_by_both(
            self.independent_response, bytes(request), self.independent_nonce,
            self.independent_key, expected_status=7,
        )

    def test_truncated_oversized_and_request_buffer_limits_reject(self):
        truncated = self.independent_response[:-1]
        self.assert_rejected_by_both(
            truncated, self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=3,
        )
        oversized_message = self.independent_response[12:] + bytes(1025 - len(self.independent_response))
        oversized = rt.MAGIC + struct.pack("<I", len(oversized_message)) + oversized_message
        self.assert_rejected_by_both(
            oversized, self.independent_request, self.independent_nonce,
            self.independent_key, expected_status=2,
        )
        oversized_request = self.independent_request + bytes(13)
        self.assert_rejected_by_both(
            self.independent_response, oversized_request, self.independent_nonce,
            self.independent_key, expected_status=2,
        )

    def test_invalid_timestamp_radius_and_unsupported_version_reject(self):
        key = ROOT_KEY.public_key().public_bytes_raw()
        request, nonce = rt.build_request(key, nonce_source=lambda _: b"R" * 32)
        invalid_time = make_response(request, nonce, midpoint=rt.MIN_TIME_SECONDS - 10)
        self.assert_rejected_by_both(invalid_time, request, nonce, key, expected_status=13)

        overflow = make_response(
            request, nonce, midpoint=0xFFFFFFFFFFFFFFFF, radius=2,
            mint=0, maxt=0xFFFFFFFFFFFFFFFF,
        )
        self.assert_rejected_by_both(overflow, request, nonce, key, expected_status=13)

        excessive_radius = make_response(request, nonce, radius=301)
        self.assert_rejected_by_both(excessive_radius, request, nonce, key, expected_status=14)

        unsupported = make_response(request, nonce, response_version=2, server_versions=(1, 2))
        self.assert_rejected_by_both(unsupported, request, nonce, key, expected_status=6)

    def test_rfc_message_sized_request_and_reusable_workspace(self):
        key = ROOT_KEY.public_key().public_bytes_raw()
        request, nonce = rt.build_request(key, nonce_source=lambda _: b"R" * 32)
        fields = rt._parse_message(rt._unwrap_packet(request))
        fields["ZZZZ"] += bytes(12)
        request = _packet(rt.encode_message(fields))
        self.assertEqual(len(request), 1036)
        response = make_response(request, nonce)
        reference = rt.verify_response(
            response, request, nonce, key, source="fixture",
            sent_monotonic_ns=self.sent_ns, received_monotonic_ns=self.received_ns,
        )

        workspace = Workspace()
        ctypes.memset(ctypes.byref(workspace), 0xA5, ctypes.sizeof(workspace))
        bad = response[:-1]
        status, rejected, workspace = self.verify_portable(bad, request, nonce, key, workspace)
        self.assertNotEqual(status, 0)
        self.assertEqual(rejected.signature_verified, 0)
        self.assertEqual(bytes(workspace), bytes(ctypes.sizeof(workspace)))

        status, result, workspace = self.verify_portable(response, request, nonce, key, workspace)
        self.assertEqual(status, 0)
        self.assertEqual(result.estimated_midpoint_unix_seconds, reference.midpoint_unix_seconds)
        self.assertEqual(result.latest_unix_seconds, reference.latest_unix_seconds)
        self.assertEqual(bytes(workspace), bytes(ctypes.sizeof(workspace)))

    def test_deterministic_malformed_packet_mutations_match_reference(self):
        rng = random.Random(0x10049)
        cases = []
        for _ in range(96):
            packet = bytearray(self.independent_response)
            offset = rng.randrange(len(packet))
            packet[offset] ^= rng.randrange(1, 256)
            cases.append(bytes(packet))
        for _ in range(96):
            length = rng.randrange(12, 1100)
            packet = bytearray(rng.randbytes(length))
            packet[:8] = rt.MAGIC
            packet[8:12] = struct.pack("<I", rng.randrange(0, 1200))
            cases.append(bytes(packet))

        for index, response in enumerate(cases):
            try:
                rt.verify_response(
                    response, self.independent_request, self.independent_nonce,
                    self.independent_key, source="mutation", sent_monotonic_ns=self.sent_ns,
                    received_monotonic_ns=self.received_ns,
                )
                python_accepted = True
            except rt.RoughtimeError:
                python_accepted = False
            status, result, _ = self.verify_portable(
                response, self.independent_request, self.independent_nonce, self.independent_key,
            )
            self.assertEqual(status == 0, python_accepted, f"classification differs for mutation {index}")
            if status != 0:
                self.assertEqual(result.signature_verified, 0)
                self.assertEqual(result.nonce_matched, 0)
                self.assertEqual(result.merkle_proof_verified, 0)


if __name__ == "__main__":
    unittest.main()

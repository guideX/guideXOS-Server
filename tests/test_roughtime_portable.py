"""Hosted differential checks for the freestanding Roughtime and agreement cores."""

from __future__ import annotations

import base64
import ctypes
import hashlib
import json
import os
import random
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

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
    parse_request_nonce,
    u32,
)

THIRD_ROOT_KEY = Ed25519PrivateKey.from_private_bytes(bytes.fromhex("44" * 32))


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


class AgreementToken(ctypes.Structure):
    _fields_ = [("opaque", ctypes.c_uint8 * 256)]


class AgreementSource(ctypes.Structure):
    _fields_ = [("server_public_key", ctypes.c_uint8 * 32),
                ("operator_identity", ctypes.c_uint32)]


class AgreementPolicy(ctypes.Structure):
    _fields_ = [
        ("policy_identity", ctypes.c_uint32),
        ("profile", ctypes.c_int),
        ("source_count", ctypes.c_uint8),
        ("quorum", ctypes.c_uint8),
        ("reserved", ctypes.c_uint16),
        ("maximum_age_seconds", ctypes.c_uint32),
        ("maximum_source_uncertainty_seconds", ctypes.c_uint32),
        ("maximum_transport_delay_ms", ctypes.c_uint32),
        ("maximum_forward_jump_seconds", ctypes.c_uint32),
        ("sources", AgreementSource * 8),
    ]


class AgreementContinuity(ctypes.Structure):
    _fields_ = [
        ("initialized", ctypes.c_uint8),
        ("failed", ctypes.c_uint8),
        ("failure_status", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8 * 5),
        ("evaluated_monotonic_ns", ctypes.c_uint64),
        ("earliest_unix_seconds", ctypes.c_uint64),
        ("latest_unix_seconds", ctypes.c_uint64),
    ]


class AgreementResult(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("policy_identity", ctypes.c_uint32),
        ("profile", ctypes.c_int),
        ("independent_source_count", ctypes.c_uint8),
        ("evidence_key_count", ctypes.c_uint8),
        ("continuity_checked", ctypes.c_uint8),
        ("production_policy_accepted", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8 * 2),
        ("earliest_unix_seconds", ctypes.c_uint64),
        ("latest_unix_seconds", ctypes.c_uint64),
        ("estimated_midpoint_unix_seconds", ctypes.c_uint64),
        ("evaluated_monotonic_ns", ctypes.c_uint64),
        ("maximum_evidence_age_seconds", ctypes.c_uint64),
        ("maximum_uncertainty_seconds", ctypes.c_uint64),
        ("operator_identities", ctypes.c_uint32 * 8),
        ("server_key_identities", (ctypes.c_uint8 * 32) * 8),
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
        acquisition_obj = build / "gxos_roughtime_acquisition_core.o"
        subprocess.run(
            [gxx, "-std=c++14", "-fno-exceptions", "-fno-rtti", "-fno-builtin",
             "-ffreestanding", "-O2", "-DGXOS_BARE_METAL", *common_includes,
             "-c", str(ROOT / "kernel/core/gxos_roughtime_acquisition_core.cpp"),
             "-o", str(acquisition_obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        agreement_obj = build / "gxos_roughtime_agreement.o"
        subprocess.run(
            [gxx, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-fno-exceptions",
             "-fno-rtti", "-fno-builtin", "-ffreestanding", "-O2", "-DGXOS_BARE_METAL",
             "-DGXOS_ROUGHTIME_AGREEMENT_TESTING",
             *common_includes, "-c", str(ROOT / "kernel/core/gxos_roughtime_agreement.cpp"),
             "-o", str(agreement_obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        # The shared harness includes the bounded acquisition and agreement
        # code used by the freestanding kernel.
        link_args = [gxx, "-shared", "-o", str(library_path), str(component_obj),
                     str(acquisition_obj), str(agreement_obj), *(str(obj) for obj in objects)]
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

        cls.build_request_function = cls.library.gxos_roughtime_v1_build_request
        cls.build_request_function.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8),
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        cls.build_request_function.restype = ctypes.c_int
        cls.build_request_random_function = cls.library.gxos_roughtime_v1_build_request_with_random
        cls.random_fill_callback_type = ctypes.CFUNCTYPE(
            ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t
        )
        cls.build_request_random_function.argtypes = [
            cls.random_fill_callback_type, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8),
            ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
        ]
        cls.build_request_random_function.restype = ctypes.c_int
        cls.dispatch_function = cls.library.gxos_roughtime_v1_dispatch_response
        cls.dispatch_function.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.c_uint32, ctypes.c_uint16, ctypes.c_uint32, ctypes.c_uint16,
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64, ctypes.c_uint64,
            ctypes.POINTER(Workspace), ctypes.POINTER(Evidence),
        ]
        cls.dispatch_function.restype = ctypes.c_int
        cls.agreement_issue_function = cls.library.gxos_roughtime_agreement_issue_evidence
        cls.agreement_issue_function.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64, ctypes.c_uint64,
            ctypes.POINTER(Workspace), ctypes.POINTER(AgreementToken),
        ]
        cls.agreement_issue_function.restype = ctypes.c_int
        cls.agreement_evaluate_function = cls.library.gxos_roughtime_agreement_evaluate
        cls.agreement_evaluate_function.argtypes = [
            ctypes.POINTER(AgreementPolicy), ctypes.POINTER(AgreementToken), ctypes.c_size_t,
            ctypes.c_uint64, ctypes.POINTER(AgreementContinuity),
            ctypes.POINTER(AgreementResult),
        ]
        cls.agreement_evaluate_function.restype = ctypes.c_int

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
        self.agreement_now_ns = 20_000_000_000

    @staticmethod
    def _u8(data):
        return (ctypes.c_uint8 * len(data)).from_buffer_copy(data)

    def issue_agreement_token(self, response, request, nonce, key, *, sent_ns=None, received_ns=None):
        sent_ns = self.sent_ns if sent_ns is None else sent_ns
        received_ns = self.received_ns if received_ns is None else received_ns
        response_arg = self._u8(response)
        request_arg = self._u8(request)
        nonce_arg = self._u8(nonce)
        key_arg = self._u8(key)
        workspace = Workspace()
        token = AgreementToken()
        status = self.agreement_issue_function(
            response_arg, len(response), request_arg, len(request), nonce_arg, key_arg,
            sent_ns, received_ns, ctypes.byref(workspace), ctypes.byref(token),
        )
        return status, token

    def signed_agreement_token(self, root_key, nonce_byte, *, midpoint=None, radius=2,
                               sent_ns=None, received_ns=None):
        if midpoint is None:
            midpoint = self.vector["server_midpoint_unix_seconds"]
        server_key = root_key.public_key().public_bytes_raw()
        nonce = bytes([nonce_byte]) * 32
        request, _ = rt.build_request(server_key, nonce_source=lambda _: nonce)
        response = make_response(
            request, nonce, root_key=root_key, delegated_key=DELEGATED_KEY,
            midpoint=midpoint, radius=radius,
        )
        status, token = self.issue_agreement_token(
            response, request, nonce, server_key, sent_ns=sent_ns, received_ns=received_ns,
        )
        self.assertEqual(status, 0, "test response must pass the production verifier path")
        return server_key, token

    def captured_agreement_token(self, *, sent_ns=None, received_ns=None):
        status, token = self.issue_agreement_token(
            self.independent_response, self.independent_request, self.independent_nonce,
            self.independent_key, sent_ns=sent_ns, received_ns=received_ns,
        )
        self.assertEqual(status, 0, "captured independent fixture must pass the production verifier path")
        return self.independent_key, token

    def agreement_policy(self, keys, operators=None, *, quorum=2, profile=1,
                         max_uncertainty=30, max_delay_ms=2000, max_age=300,
                         max_forward_jump=300):
        operators = operators or list(range(101, 101 + len(keys)))
        policy = AgreementPolicy()
        policy.policy_identity = 0x1401
        policy.profile = profile
        policy.source_count = len(keys)
        policy.quorum = quorum
        policy.maximum_age_seconds = max_age
        policy.maximum_source_uncertainty_seconds = max_uncertainty
        policy.maximum_transport_delay_ms = max_delay_ms
        policy.maximum_forward_jump_seconds = max_forward_jump
        for index, (key, operator_id) in enumerate(zip(keys, operators)):
            policy.sources[index].server_public_key[:] = key
            policy.sources[index].operator_identity = operator_id
        return policy

    def evaluate_agreement(self, policy, tokens, *, now_ns=None, continuity=None):
        now_ns = self.agreement_now_ns if now_ns is None else now_ns
        if tokens:
            token_array = (AgreementToken * len(tokens))()
            for index, token in enumerate(tokens):
                ctypes.memmove(ctypes.byref(token_array[index]), ctypes.byref(token), ctypes.sizeof(token))
            token_pointer = token_array
        else:
            token_pointer = ctypes.POINTER(AgreementToken)()
        result = AgreementResult()
        status = self.agreement_evaluate_function(
            ctypes.byref(policy), token_pointer, len(tokens), now_ns,
            ctypes.byref(continuity) if continuity is not None else None,
            ctypes.byref(result),
        )
        return status, result

    def three_agreeing_measurements(self, *, midpoint=None, radius=2,
                                    received_times=None):
        if midpoint is None:
            midpoint = self.vector["server_midpoint_unix_seconds"]
        received_times = received_times or [self.received_ns] * 3
        key1, token1 = self.captured_agreement_token(received_ns=received_times[0])
        key2, token2 = self.signed_agreement_token(
            ROOT_KEY, 0x41, midpoint=midpoint, radius=radius,
            sent_ns=received_times[1] - 100_000_000,
            received_ns=received_times[1],
        )
        key3, token3 = self.signed_agreement_token(
            OTHER_KEY, 0x42, midpoint=midpoint, radius=radius,
            sent_ns=received_times[2] - 100_000_000,
            received_ns=received_times[2],
        )
        return [key1, key2, key3], [token1, token2, token3]

    def build_acquisition_request(self, nonce, key):
        nonce_arg = (ctypes.c_uint8 * 32).from_buffer_copy(nonce)
        key_arg = (ctypes.c_uint8 * 32).from_buffer_copy(key)
        request_arg = (ctypes.c_uint8 * 1036)()
        request_length = ctypes.c_size_t()
        status = self.build_request_function(
            nonce_arg, key_arg, request_arg, len(request_arg), ctypes.byref(request_length)
        )
        self.assertEqual(status, 0)
        return bytes(request_arg[:request_length.value])

    def dispatch_acquisition(self, response, request, nonce, key, *,
                             actual_ip=0x7F000001, actual_port=5319,
                             expected_ip=0x7F000001, expected_port=5319):
        response_arg = (ctypes.c_uint8 * max(1, len(response)))()
        if response:
            response_arg[:len(response)] = response
        request_arg = (ctypes.c_uint8 * len(request)).from_buffer_copy(request)
        nonce_arg = (ctypes.c_uint8 * 32).from_buffer_copy(nonce)
        key_arg = (ctypes.c_uint8 * 32).from_buffer_copy(key)
        workspace = Workspace()
        result = Evidence()
        status = self.dispatch_function(
            response_arg, len(response), actual_ip, actual_port,
            expected_ip, expected_port, request_arg, len(request),
            nonce_arg, key_arg, self.sent_ns, self.received_ns,
            ctypes.byref(workspace), ctypes.byref(result),
        )
        return status, result

    def test_kernel_request_builder_matches_hosted_rfc_v1_request(self):
        nonce = bytes(range(32))
        request = self.build_acquisition_request(nonce, self.independent_key)
        hosted_request, hosted_nonce = rt.build_request(
            self.independent_key, nonce_source=lambda count: nonce
        )
        self.assertEqual(hosted_nonce, nonce)
        self.assertEqual(request, hosted_request)
        self.assertEqual(len(request), rt.MAX_REQUEST_PACKET_BYTES)
        fields = rt._parse_message(rt._unwrap_packet(request, max_packet_bytes=rt.MAX_REQUEST_PACKET_BYTES))
        self.assertEqual(fields["VER"], b"\x01\0\0\0")
        self.assertEqual(fields["NONC"], nonce)
        self.assertEqual(fields["TYPE"], b"\0\0\0\0")
        self.assertEqual(len(fields["ZZZZ"]), 912)
        self.assertFalse(any(fields["ZZZZ"]))

    def test_guest_vectors_are_copies_of_the_pinned_independent_fixture(self):
        header = (ROOT / "kernel/core/roughtime_guest_vectors.h").read_text()
        vector_bytes = {
            "gxos_roughtime_fixture_request": self.independent_request,
            "gxos_roughtime_fixture_response": self.independent_response,
            "gxos_roughtime_fixture_public_key": self.independent_key,
            "gxos_roughtime_fixture_nonce": self.independent_nonce,
        }
        for name, expected in vector_bytes.items():
            match = re.search(
                rf"static const uint8_t {name}\[\] = \{{(.*?)\}};",
                header,
                flags=re.DOTALL,
            )
            self.assertIsNotNone(match, name)
            actual = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
            self.assertEqual(actual, expected, name)

    def test_kernel_random_failure_produces_no_request(self):
        key_arg = (ctypes.c_uint8 * 32).from_buffer_copy(self.independent_key)
        nonce_arg = (ctypes.c_uint8 * 32)(*([0xA5] * 32))
        request_arg = (ctypes.c_uint8 * 1036)(*([0xA5] * 1036))
        request_length = ctypes.c_size_t(99)

        @self.random_fill_callback_type
        def entropy_failure(_context, _output, _length):
            return 0

        status = self.build_request_random_function(
            entropy_failure, None, key_arg, nonce_arg, request_arg,
            len(request_arg), ctypes.byref(request_length),
        )
        self.assertEqual(status, 2)
        self.assertEqual(request_length.value, 0)
        self.assertEqual(bytes(nonce_arg), bytes(32))
        self.assertEqual(bytes(request_arg), bytes(1036))

    def test_native_dispatch_covers_key_signature_nonce_source_and_bounds(self):
        nonce = b"N" * 32
        request = self.build_acquisition_request(nonce, bytes(ROOT_KEY.public_key().public_bytes_raw()))
        response = make_response(request, nonce)

        status, result = self.dispatch_acquisition(response, request, nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        self.assertEqual(status, 0)
        self.assertTrue(result.signature_verified)
        self.assertTrue(result.nonce_matched)
        self.assertTrue(result.merkle_proof_verified)
        self.assertFalse(result.production_policy_accepted)

        wrong_key = bytes(OTHER_KEY.public_key().public_bytes_raw())
        status, result = self.dispatch_acquisition(response, request, nonce, wrong_key)
        self.assertEqual(status, 9)
        self.assertEqual(result.status, 8)  # request server-key identity mismatch

        signature_offset, _ = _field_bounds(response, "SIG")
        corrupted = bytearray(response)
        corrupted[signature_offset] ^= 1
        status, result = self.dispatch_acquisition(bytes(corrupted), request, nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        self.assertEqual(status, 9)
        self.assertEqual(result.status, 10)

        wrong_nonce = b"X" * 32
        status, result = self.dispatch_acquisition(response, request, wrong_nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        self.assertEqual(status, 9)

        fresh_nonce = b"F" * 32
        fresh_request = self.build_acquisition_request(fresh_nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        status, result = self.dispatch_acquisition(response, fresh_request, fresh_nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        self.assertEqual(status, 9)  # a signed response to the old request cannot be replayed
        self.assertEqual(result.status, 9)

        status, _ = self.dispatch_acquisition(response, request, nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()), actual_ip=0x7F000002)
        self.assertEqual(status, 7)

        status, result = self.dispatch_acquisition(response[:-1], request, nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        self.assertEqual(status, 9)
        self.assertEqual(result.status, 3)

        oversized = response + bytes(1025 - len(response))
        status, _ = self.dispatch_acquisition(oversized, request, nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        self.assertEqual(status, 8)

        uncertain = make_response(request, nonce, radius=301)
        status, result = self.dispatch_acquisition(uncertain, request, nonce,
            bytes(ROOT_KEY.public_key().public_bytes_raw()))
        self.assertEqual(status, 9)
        self.assertEqual(result.status, 14)

    def test_native_request_and_dispatch_run_through_controlled_udp_fixture(self):
        nonce = bytes((index * 7) & 0xFF for index in range(32))
        key = bytes(ROOT_KEY.public_key().public_bytes_raw())
        request = self.build_acquisition_request(nonce, key)
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(2.0)
        endpoint = server.getsockname()
        fixture_errors = []

        def fixture_once():
            try:
                incoming, client_address = server.recvfrom(1472)
                actual_nonce = parse_request_nonce(incoming)
                signed_response = make_response(incoming, actual_nonce, midpoint=FIXTURE_TIME, radius=3)
                server.sendto(signed_response, client_address)
            except BaseException as error:  # surfaced in the unittest thread
                fixture_errors.append(error)

        fixture_thread = threading.Thread(target=fixture_once, daemon=True)
        fixture_thread.start()
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.settimeout(2.0)
        client_fd = client.fileno()
        server_fd = server.fileno()
        try:
            client.sendto(request, endpoint)
            try:
                response, source = client.recvfrom(1024)
            except Exception as receive_error:
                fixture_thread.join(timeout=2.0)
                if fixture_errors:
                    self.fail(f"controlled fixture failed: {fixture_errors[0]!r}")
                raise receive_error
            status, evidence = self.dispatch_acquisition(
                response, request, nonce, key,
                actual_ip=int.from_bytes(socket.inet_aton(source[0]), "big"),
                actual_port=source[1], expected_port=endpoint[1],
            )
            self.assertEqual(status, 0)
            self.assertEqual(evidence.server_midpoint_unix_seconds, FIXTURE_TIME)
            self.assertEqual(evidence.radius_seconds, 3)
            self.assertTrue(evidence.merkle_proof_verified)
        finally:
            client.close()
            server.close()
            fixture_thread.join(timeout=2.0)
        self.assertEqual(client.fileno(), -1)
        self.assertEqual(server.fileno(), -1)
        self.assertFalse(fixture_thread.is_alive())
        self.assertEqual(fixture_errors, [])

    def test_unavailable_controlled_server_times_out_without_retrying(self):
        key = bytes(ROOT_KEY.public_key().public_bytes_raw())
        nonce = b"T" * 32
        request = self.build_acquisition_request(nonce, key)
        reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reservation.bind(("127.0.0.1", 0))
        unused_port = reservation.getsockname()[1]
        reservation.close()

        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.settimeout(0.05)
        started = time.monotonic()
        try:
            client.sendto(request, ("127.0.0.1", unused_port))
            with self.assertRaises((socket.timeout, ConnectionResetError)):
                client.recvfrom(1024)
        finally:
            client.close()
        self.assertLess(time.monotonic() - started, 1.0)
        self.assertEqual(client.fileno(), -1)

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
        self.assertEqual(len(request), 1036)
        self.assertEqual(struct.unpack_from("<I", request, 8)[0], 1024)
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

    def test_agreement_accepts_two_independent_operators_with_captured_signature(self):
        keys, tokens = self.three_agreeing_measurements()
        policy = self.agreement_policy(keys, quorum=2)
        status, result = self.evaluate_agreement(policy, tokens[:2])
        self.assertEqual(status, 0)
        self.assertEqual(result.independent_source_count, 2)
        self.assertEqual(result.evidence_key_count, 2)
        self.assertEqual(result.policy_identity, policy.policy_identity)
        self.assertEqual(result.profile, 1)
        self.assertEqual(result.production_policy_accepted, 0)
        self.assertLessEqual(result.earliest_unix_seconds, result.latest_unix_seconds)
        self.assertEqual(result.maximum_evidence_age_seconds, 10)

    def test_agreement_accepts_three_agreeing_operators(self):
        keys, tokens = self.three_agreeing_measurements()
        status, result = self.evaluate_agreement(self.agreement_policy(keys), tokens)
        self.assertEqual(status, 0)
        self.assertEqual(result.independent_source_count, 3)
        self.assertEqual(result.evidence_key_count, 3)

    def test_one_valid_source_is_insufficient(self):
        keys, tokens = self.three_agreeing_measurements()
        status, _ = self.evaluate_agreement(self.agreement_policy(keys), tokens[:1])
        self.assertEqual(status, 1)

    def test_two_keys_for_one_operator_count_once(self):
        keys, tokens = self.three_agreeing_measurements()
        policy = self.agreement_policy(keys, operators=[77, 77, 88])
        status, result = self.evaluate_agreement(policy, tokens[:2])
        self.assertEqual(status, 1)
        status, result = self.evaluate_agreement(policy, tokens)
        self.assertEqual(status, 0)
        self.assertEqual(result.independent_source_count, 2)
        self.assertEqual(result.evidence_key_count, 3)

    def test_duplicate_policy_key_and_duplicate_response_reject(self):
        keys, tokens = self.three_agreeing_measurements()
        duplicate_key_policy = self.agreement_policy([keys[0], keys[0], keys[2]])
        status, _ = self.evaluate_agreement(duplicate_key_policy, tokens)
        self.assertEqual(status, 5)
        policy = self.agreement_policy(keys)
        status, _ = self.evaluate_agreement(policy, [tokens[0], tokens[0]])
        self.assertEqual(status, 8)

    def test_missing_source_metadata_and_excessive_policy_capacity_reject(self):
        keys, tokens = self.three_agreeing_measurements()
        policy = self.agreement_policy(keys)
        policy.sources[1].operator_identity = 0
        self.assertEqual(self.evaluate_agreement(policy, tokens)[0], 5)
        policy = self.agreement_policy(keys)
        policy.source_count = 9
        self.assertEqual(self.evaluate_agreement(policy, tokens)[0], 5)
        forged = AgreementToken()
        self.assertEqual(self.evaluate_agreement(self.agreement_policy(keys[:2]),
                                                  [tokens[0], forged])[0], 6)

    def test_unrecognized_source_and_unissued_caller_values_reject(self):
        keys, tokens = self.three_agreeing_measurements()
        unrecognized_key, unrecognized_token = self.signed_agreement_token(THIRD_ROOT_KEY, 0x43)
        status, _ = self.evaluate_agreement(self.agreement_policy(keys[:2]), [tokens[0], unrecognized_token])
        self.assertEqual(status, 7)

        forged = AgreementToken()
        ctypes.memset(ctypes.byref(forged), 0xA5, ctypes.sizeof(forged))
        status, _ = self.evaluate_agreement(self.agreement_policy(keys[:2]), [tokens[0], forged])
        self.assertEqual(status, 6)
        self.assertNotEqual(unrecognized_key, keys[0])
        wrong_key = ROOT_KEY.public_key().public_bytes_raw()
        wrong_status, _ = self.issue_agreement_token(
            self.independent_response, self.independent_request, self.independent_nonce,
            wrong_key,
        )
        self.assertEqual(wrong_status, 13)

    def test_production_header_has_no_raw_evidence_or_raw_response_issuer(self):
        probe = r'''
#include "gxos_roughtime_agreement.h"
void raw_values_must_not_mint_or_evaluate(
    gxos_roughtime_agreement_policy* policy,
    const gxos_roughtime_v1_evidence* raw,
    gxos_roughtime_agreement_result* result) {
    gxos_roughtime_agreement_evaluate(policy, raw, 1, 0, 0, result);
    gxos_roughtime_agreement_issue_evidence(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}
'''
        completed = subprocess.run(
            [os.environ.get("CXX", "g++"), "-std=c++14", "-fsyntax-only",
             "-I", str(ROOT / "kernel/core/include"), "-I", str(ROOT), "-x", "c++", "-"],
            input=probe, text=True, capture_output=True,
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("gxos_roughtime_agreement_issue_evidence", completed.stderr)
        self.assertIn("gxos_roughtime_agreement_evaluate", completed.stderr)

    def test_stale_expired_and_future_monotonic_evidence_reject(self):
        keys, tokens = self.three_agreeing_measurements()
        stale_key, stale_token = self.signed_agreement_token(
            ROOT_KEY, 0x51, received_ns=self.received_ns,
        )
        fresh_key, fresh_token = self.signed_agreement_token(OTHER_KEY, 0x52)
        policy = self.agreement_policy([keys[0], stale_key, fresh_key])
        status, _ = self.evaluate_agreement(
            policy, [stale_token, fresh_token],
            now_ns=self.received_ns + 300_000_000_000,
        )
        self.assertEqual(status, 0)
        status, _ = self.evaluate_agreement(policy, [stale_token, fresh_token],
                                             now_ns=self.received_ns + 301_000_000_000)
        self.assertEqual(status, 3)
        status, _ = self.evaluate_agreement(self.agreement_policy(keys), tokens,
                                             now_ns=self.received_ns - 1)
        self.assertEqual(status, 6)

    def test_per_source_uncertainty_and_network_delay_limits(self):
        key1, token1 = self.captured_agreement_token()
        key2, token2 = self.signed_agreement_token(ROOT_KEY, 0x61, radius=20)
        key3, token3 = self.signed_agreement_token(OTHER_KEY, 0x62, radius=20)
        policy = self.agreement_policy([key1, key2, key3], max_uncertainty=10)
        status, _ = self.evaluate_agreement(policy, [token2, token3])
        self.assertEqual(status, 4)

        delayed_key, delayed_token = self.signed_agreement_token(
            ROOT_KEY, 0x63, sent_ns=self.sent_ns, received_ns=self.sent_ns + 100_000_000,
        )
        other_key, other_token = self.signed_agreement_token(OTHER_KEY, 0x64)
        delay_policy = self.agreement_policy([key1, delayed_key, other_key], max_delay_ms=50)
        status, _ = self.evaluate_agreement(delay_policy, [delayed_token, other_token])
        self.assertEqual(status, 4)

    def test_conflicting_valid_source_is_not_silently_discarded(self):
        base = self.vector["server_midpoint_unix_seconds"]
        keys, tokens = self.three_agreeing_measurements(midpoint=base)
        keys[2], tokens[2] = self.signed_agreement_token(OTHER_KEY, 0x71, midpoint=base + 1000)
        status, _ = self.evaluate_agreement(self.agreement_policy(keys), tokens)
        self.assertEqual(status, 2)

    def test_multiple_incompatible_quorums_are_ambiguous(self):
        base = self.vector["server_midpoint_unix_seconds"]
        keys, tokens = self.three_agreeing_measurements(midpoint=base, radius=10)
        keys[1], tokens[1] = self.signed_agreement_token(ROOT_KEY, 0x72,
                                                         midpoint=base + 15, radius=10)
        keys[2], tokens[2] = self.signed_agreement_token(OTHER_KEY, 0x73,
                                                         midpoint=base + 30, radius=10)
        policy = self.agreement_policy(keys, max_uncertainty=30)
        status, _ = self.evaluate_agreement(policy, tokens)
        self.assertEqual(status, 9)

    def test_measurements_are_advanced_to_one_monotonic_instant(self):
        keys, tokens = self.three_agreeing_measurements(
            received_times=[self.received_ns, self.received_ns + 1_000_000_000,
                            self.received_ns + 2_000_000_000],
        )
        status, result = self.evaluate_agreement(self.agreement_policy(keys), tokens)
        self.assertEqual(status, 0)
        self.assertGreaterEqual(result.earliest_unix_seconds,
                                self.vector["server_midpoint_unix_seconds"] - 5 + 9)

    def test_boundary_timestamps_and_signed_overflow_underflow_reject(self):
        minimum = rt.MIN_TIME_SECONDS
        maximum = rt.MAX_TIME_SECONDS
        key1, token1 = self.signed_agreement_token(ROOT_KEY, 0x81,
                                                  midpoint=minimum + 2, radius=2,
                                                  sent_ns=self.received_ns,
                                                  received_ns=self.received_ns)
        key2, token2 = self.signed_agreement_token(OTHER_KEY, 0x82,
                                                  midpoint=minimum + 2, radius=2,
                                                  sent_ns=self.received_ns,
                                                  received_ns=self.received_ns)
        status, result = self.evaluate_agreement(
            self.agreement_policy([key1, key2]), [token1, token2], now_ns=self.received_ns,
        )
        self.assertEqual(status, 0)
        self.assertEqual(result.earliest_unix_seconds, minimum)

        key3, token3 = self.signed_agreement_token(ROOT_KEY, 0x83,
                                                  midpoint=maximum - 3, radius=2,
                                                  sent_ns=self.received_ns,
                                                  received_ns=self.received_ns)
        key4, token4 = self.signed_agreement_token(OTHER_KEY, 0x84,
                                                  midpoint=maximum - 3, radius=2,
                                                  sent_ns=self.received_ns,
                                                  received_ns=self.received_ns)
        status, _ = self.evaluate_agreement(
            self.agreement_policy([key3, key4]), [token3, token4], now_ns=self.received_ns,
        )
        self.assertEqual(status, 0)

        for nonce_byte, midpoint, radius in ((0x85, minimum + 1, 2),
                                              (0x86, maximum - 1, 2),
                                              (0x87, 0xFFFFFFFFFFFFFFFF, 2)):
            server_key = ROOT_KEY.public_key().public_bytes_raw()
            nonce = bytes([nonce_byte]) * 32
            request, _ = rt.build_request(server_key, nonce_source=lambda _: nonce)
            response = make_response(request, nonce, root_key=ROOT_KEY,
                                     midpoint=midpoint, radius=radius,
                                     mint=0, maxt=0xFFFFFFFFFFFFFFFF)
            status, _ = self.issue_agreement_token(response, request, nonce, server_key)
            self.assertEqual(status, 13)

    def test_zero_invalid_thresholds_and_fixture_policy_separation(self):
        keys, tokens = self.three_agreeing_measurements()
        policy = self.agreement_policy(keys, quorum=0)
        self.assertEqual(self.evaluate_agreement(policy, tokens)[0], 5)
        policy = self.agreement_policy(keys, quorum=4)
        self.assertEqual(self.evaluate_agreement(policy, tokens)[0], 5)
        policy = self.agreement_policy(keys, max_age=0)
        self.assertEqual(self.evaluate_agreement(policy, tokens)[0], 5)
        policy = self.agreement_policy(keys, max_age=301)
        self.assertEqual(self.evaluate_agreement(policy, tokens)[0], 5)

        fixture_policy = self.agreement_policy(keys, profile=1)
        status, fixture_result = self.evaluate_agreement(fixture_policy, tokens[:2])
        self.assertEqual(status, 0)
        self.assertEqual(fixture_result.profile, 1)
        self.assertEqual(fixture_result.production_policy_accepted, 0)
        configured_policy = self.agreement_policy(keys, profile=2)
        status, configured_result = self.evaluate_agreement(configured_policy, tokens[:2])
        self.assertEqual(status, 0)
        self.assertEqual(configured_result.profile, 2)
        self.assertEqual(configured_result.production_policy_accepted, 0)

    def test_continuity_detects_rollback_forward_jump_and_latches(self):
        base = self.vector["server_midpoint_unix_seconds"]
        keys, tokens = self.three_agreeing_measurements(midpoint=base)
        policy = self.agreement_policy(keys, max_forward_jump=60)
        continuity = AgreementContinuity()
        status, _ = self.evaluate_agreement(policy, tokens[:2], continuity=continuity)
        self.assertEqual(status, 0)

        rollback_tokens = [
            self.signed_agreement_token(ROOT_KEY, 0x91, midpoint=base - 100,
                                        sent_ns=20_000_000_000,
                                        received_ns=20_100_000_000)[1],
            self.signed_agreement_token(OTHER_KEY, 0x93, midpoint=base - 100,
                                        sent_ns=20_000_000_000,
                                        received_ns=20_100_000_000)[1],
        ]
        status, _ = self.evaluate_agreement(policy, rollback_tokens,
                                             now_ns=30_000_000_000,
                                             continuity=continuity)
        self.assertEqual(status, 10)
        self.assertEqual(continuity.failed, 1)
        status, _ = self.evaluate_agreement(policy, tokens[:2], now_ns=31_000_000_000,
                                             continuity=continuity)
        self.assertEqual(status, 10)

        forward_policy = self.agreement_policy(keys, max_forward_jump=60)
        forward_state = AgreementContinuity()
        self.assertEqual(self.evaluate_agreement(forward_policy, tokens[:2],
                                                  continuity=forward_state)[0], 0)
        forward_tokens = [
            self.signed_agreement_token(ROOT_KEY, 0x92, midpoint=base + 1000,
                                        sent_ns=20_000_000_000,
                                        received_ns=20_100_000_000)[1],
            self.signed_agreement_token(OTHER_KEY, 0x94, midpoint=base + 1000,
                                        sent_ns=20_000_000_000,
                                        received_ns=20_100_000_000)[1],
        ]
        self.assertEqual(self.evaluate_agreement(forward_policy, forward_tokens,
                                                  now_ns=30_000_000_000,
                                                  continuity=forward_state)[0], 11)

    def test_monotonic_backwards_and_counter_wrap_are_rejected_safely(self):
        keys, tokens = self.three_agreeing_measurements()
        policy = self.agreement_policy(keys)
        continuity = AgreementContinuity()
        self.assertEqual(self.evaluate_agreement(policy, tokens[:2], now_ns=20_000_000_000,
                                                  continuity=continuity)[0], 0)
        self.assertEqual(self.evaluate_agreement(policy, tokens[:2], now_ns=19_000_000_000,
                                                  continuity=continuity)[0], 12)

        top = 0xFFFFFFFFFFFFFFFF
        sent = top - 1_000_000
        # A response that would overflow its mandatory freshness deadline is rejected.
        server_key = ROOT_KEY.public_key().public_bytes_raw()
        nonce = bytes([0xA1]) * 32
        request, _ = rt.build_request(server_key, nonce_source=lambda _: nonce)
        response = make_response(request, nonce, root_key=ROOT_KEY,
                                 delegated_key=DELEGATED_KEY,
                                 midpoint=self.vector["server_midpoint_unix_seconds"])
        status, _ = self.issue_agreement_token(
            response, request, nonce, server_key,
            sent_ns=sent, received_ns=top - 500_000,
        )
        self.assertEqual(status, 13)
        status, _ = self.issue_agreement_token(
            self.independent_response, self.independent_request, self.independent_nonce,
            self.independent_key, sent_ns=top, received_ns=top - 1,
        )
        self.assertEqual(status, 6)

    def test_new_boot_continuity_starts_without_a_persistent_trusted_floor(self):
        old_time = self.vector["server_midpoint_unix_seconds"] - 3600
        key1, token1 = self.signed_agreement_token(ROOT_KEY, 0xA3, midpoint=old_time)
        key2, token2 = self.signed_agreement_token(OTHER_KEY, 0xA4, midpoint=old_time)
        new_boot_state = AgreementContinuity()
        status, result = self.evaluate_agreement(
            self.agreement_policy([key1, key2]), [token1, token2],
            now_ns=self.received_ns, continuity=new_boot_state,
        )
        self.assertEqual(status, 0)
        self.assertEqual(result.continuity_checked, 0)
        self.assertEqual(new_boot_state.initialized, 1)


if __name__ == "__main__":
    unittest.main()

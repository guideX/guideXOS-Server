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


class RoundSource(ctypes.Structure):
    _fields_ = [
        ("server_ipv4", ctypes.c_uint32),
        ("server_port", ctypes.c_uint16),
        ("reserved", ctypes.c_uint16),
        ("operator_identity", ctypes.c_uint32),
        ("server_public_key", ctypes.c_uint8 * 32),
    ]


class RoundConfiguration(ctypes.Structure):
    _fields_ = [
        ("policy_identity", ctypes.c_uint32),
        ("source_count", ctypes.c_uint8),
        ("quorum", ctypes.c_uint8),
        ("reserved", ctypes.c_uint16),
        ("maximum_age_seconds", ctypes.c_uint32),
        ("maximum_source_uncertainty_seconds", ctypes.c_uint32),
        ("maximum_transport_delay_ms", ctypes.c_uint32),
        ("maximum_forward_jump_seconds", ctypes.c_uint32),
        ("sources", RoundSource * 3),
    ]


class RoundSourceResult(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("server_ipv4", ctypes.c_uint32),
        ("server_port", ctypes.c_uint16),
        ("request_bytes", ctypes.c_uint16),
        ("response_bytes", ctypes.c_uint16),
        ("verified", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8 * 3),
        ("server_key_identity", ctypes.c_uint8 * 32),
    ]


class RoundResult(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("attempted_source_count", ctypes.c_uint8),
        ("verified_response_count", ctypes.c_uint8),
        ("independent_operator_count", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8),
        ("sources", RoundSourceResult * 3),
        ("evidence", AgreementToken * 3),
        ("agreement", AgreementResult),
    ]


class TimeEstimateInfo(ctypes.Structure):
    _fields_ = [
        ("state", ctypes.c_uint8),
        ("scope", ctypes.c_uint8),
        ("reason", ctypes.c_uint8),
        ("available", ctypes.c_uint8),
        ("agreement_satisfied", ctypes.c_uint8),
        ("configured_source_count", ctypes.c_uint8),
        ("accepted_source_count", ctypes.c_uint8),
        ("distinct_operator_count", ctypes.c_uint8),
        ("operator_identity_count", ctypes.c_uint8),
        ("key_identity_count", ctypes.c_uint8),
        ("production_policy_accepted", ctypes.c_uint8),
        ("system_clock_changed", ctypes.c_uint8),
        ("public_https_enabled", ctypes.c_uint8),
        ("policy_identity", ctypes.c_uint32),
        ("operator_identities", ctypes.c_uint32 * 3),
        ("server_key_identities", (ctypes.c_uint8 * 32) * 3),
        ("agreed_earliest_unix_seconds", ctypes.c_uint64),
        ("agreed_latest_unix_seconds", ctypes.c_uint64),
        ("current_earliest_unix_seconds", ctypes.c_uint64),
        ("current_latest_unix_seconds", ctypes.c_uint64),
        ("measurement_anchor_monotonic_ns", ctypes.c_uint64),
        ("last_observed_monotonic_ns", ctypes.c_uint64),
        ("maximum_age_seconds", ctypes.c_uint64),
        ("evidence_age_at_anchor_seconds", ctypes.c_uint64),
        ("maximum_uncertainty_seconds", ctypes.c_uint64),
        ("current_uncertainty_seconds", ctypes.c_uint64),
        ("remaining_freshness_seconds", ctypes.c_uint64),
    ]


class TlsTimeEstimateLease(ctypes.Structure):
    _fields_ = [
        ("result", ctypes.c_uint8),
        ("state", ctypes.c_uint8),
        ("scope", ctypes.c_uint8),
        ("rejection_reason", ctypes.c_uint8),
        ("available", ctypes.c_uint8),
        ("source_policy_identity", ctypes.c_uint32),
        ("trust_scope_identity", ctypes.c_uint32),
        ("earliest_unix_seconds", ctypes.c_uint64),
        ("latest_unix_seconds", ctypes.c_uint64),
        ("measurement_anchor_monotonic_ns", ctypes.c_uint64),
        ("observed_monotonic_ns", ctypes.c_uint64),
        ("remaining_freshness_seconds", ctypes.c_uint64),
        ("uncertainty_seconds", ctypes.c_uint64),
        ("generation", ctypes.c_uint64),
    ]


class AcquisitionResult(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("evidence", Evidence),
        ("server_ipv4", ctypes.c_uint32),
        ("server_port", ctypes.c_uint16),
        ("request_bytes", ctypes.c_uint16),
        ("response_bytes", ctypes.c_uint16),
        ("clock_evidence_contract_valid", ctypes.c_uint8),
        ("evidence_expires_monotonic_ns", ctypes.c_uint64),
        ("agreement_evidence", AgreementToken),
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


class RoughtimeUdpFixture:
    """One-request test-only signer with a separately pinned root key."""

    def __init__(self, root_key, *, mode="normal", midpoint=FIXTURE_TIME, radius=2):
        self.root_key = root_key
        self.mode = mode
        self.midpoint = midpoint
        self.radius = radius
        self.errors = []
        self.endpoint = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.endpoint.bind(("127.0.0.1", 0))
        self.endpoint.settimeout(3.0)
        self.address = self.endpoint.getsockname()
        self.rogue = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.thread = threading.Thread(target=self._serve_once, daemon=True)
        self.thread.start()

    def _serve_once(self):
        try:
            request, client = self.endpoint.recvfrom(1472)
            fields = rt._parse_message(
                rt._unwrap_packet(request, max_packet_bytes=rt.MAX_REQUEST_PACKET_BYTES)
            )
            public_key = self.root_key.public_key().public_bytes_raw()
            expected_identity = hashlib.sha512(b"\xff" + public_key).digest()[:32]
            if fields.get("SRV") != expected_identity or fields.get("TYPE") != b"\0\0\0\0":
                raise AssertionError("fixture received a request for a different pinned key")
            if self.mode == "timeout":
                return
            nonce = parse_request_nonce(request)
            response_nonce = bytes([nonce[0] ^ 1]) * 32 if self.mode == "incorrect_nonce" else nonce
            response = make_response(
                request, response_nonce, root_key=self.root_key,
                delegated_key=DELEGATED_KEY, midpoint=self.midpoint, radius=self.radius,
            )
            if self.mode == "invalid_signature":
                signature_offset, _ = _field_bounds(response, "SIG")
                damaged = bytearray(response)
                damaged[signature_offset] ^= 1
                response = bytes(damaged)
            sender = self.rogue if self.mode == "unexpected_source" else self.endpoint
            sender.sendto(response, client)
        except socket.timeout:
            self.errors.append(TimeoutError("fixture did not receive its bounded request"))
        except OSError:
            # close() may race with the bounded receive timeout during cleanup.
            pass
        except BaseException as error:
            self.errors.append(error)

    def close(self):
        self.endpoint.close()
        self.rogue.close()
        self.thread.join(timeout=3.5)
        if self.thread.is_alive():
            self.errors.append(TimeoutError("fixture thread did not terminate"))


def _unused_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.bind(("127.0.0.1", 0))
        return endpoint.getsockname()[1]


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
        round_obj = build / "gxos_roughtime_round.o"
        subprocess.run(
            [gxx, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-fno-exceptions",
             "-fno-rtti", "-fno-builtin", "-ffreestanding", "-O2", "-DGXOS_BARE_METAL",
             "-DGXOS_ROUGHTIME_AGREEMENT_TESTING", "-DGXOS_ROUGHTIME_ROUND_TESTING",
             *common_includes, "-c", str(ROOT / "kernel/core/gxos_roughtime_round.cpp"),
             "-o", str(round_obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        time_estimate_obj = build / "gxos_time_estimate.o"
        subprocess.run(
            [gxx, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-fno-exceptions",
             "-fno-rtti", "-fno-builtin", "-ffreestanding", "-O2", "-DGXOS_BARE_METAL",
             "-DGXOS_TIME_ESTIMATE_TESTING",
             *common_includes, "-c", str(ROOT / "kernel/core/gxos_time_estimate.cpp"),
             "-o", str(time_estimate_obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        tls_time_policy_obj = build / "gxos_tls_time_policy.o"
        subprocess.run(
            [gxx, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-fno-exceptions",
             "-fno-rtti", "-fno-builtin", "-ffreestanding", "-O2", "-DGXOS_BARE_METAL",
             *common_includes, "-c", str(ROOT / "kernel/core/gxos_tls_time_policy.cpp"),
             "-o", str(tls_time_policy_obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        time_estimate_bridge_obj = build / "roughtime_time_estimate_test_bridge.o"
        subprocess.run(
            [gxx, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-fno-exceptions",
             "-fno-rtti", "-fno-builtin", "-O2", "-DGXOS_TIME_ESTIMATE_TESTING",
             *common_includes,
             "-c", str(ROOT / "tests/roughtime_time_estimate_test_bridge.cpp"),
             "-o", str(time_estimate_bridge_obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        # The shared harness includes the bounded acquisition and agreement
        # code used by the freestanding kernel.
        link_args = [gxx, "-shared", "-o", str(library_path), str(component_obj),
                     str(acquisition_obj), str(agreement_obj), str(round_obj),
                     str(time_estimate_obj), str(tls_time_policy_obj),
                     str(time_estimate_bridge_obj),
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
        cls.seal_acquisition_function = cls.library.gxos_roughtime_agreement_test_seal_acquisition_evidence
        cls.seal_acquisition_function.argtypes = [
            ctypes.POINTER(Evidence), ctypes.POINTER(ctypes.c_uint8),
            ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(AgreementToken),
        ]
        cls.seal_acquisition_function.restype = ctypes.c_int
        cls.agreement_evaluate_function = cls.library.gxos_roughtime_agreement_evaluate
        cls.agreement_evaluate_function.argtypes = [
            ctypes.POINTER(AgreementPolicy), ctypes.POINTER(AgreementToken), ctypes.c_size_t,
            ctypes.c_uint64, ctypes.POINTER(AgreementContinuity),
            ctypes.POINTER(AgreementResult),
        ]
        cls.agreement_evaluate_function.restype = ctypes.c_int
        cls.round_test_provider_type = ctypes.CFUNCTYPE(
            ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(RoundSource),
            ctypes.POINTER(AcquisitionResult),
        )
        cls.round_test_function = cls.library.gxos_roughtime_round_test_run
        cls.round_test_function.argtypes = [
            ctypes.POINTER(RoundConfiguration), ctypes.c_uint64,
            ctypes.POINTER(AgreementContinuity), ctypes.POINTER(RoundResult),
            cls.round_test_provider_type, ctypes.c_void_p,
        ]
        cls.round_test_function.restype = ctypes.c_int

        cls.time_estimate_reset_function = cls.library.gxos_time_estimate_test_reset
        cls.time_estimate_reset_function.argtypes = [ctypes.c_uint32]
        cls.time_estimate_accept_function = cls.library.gxos_time_estimate_test_accept
        cls.time_estimate_accept_function.argtypes = [
            ctypes.c_uint32, ctypes.POINTER(RoundConfiguration), ctypes.POINTER(RoundResult),
            ctypes.c_uint64,
        ]
        cls.time_estimate_accept_function.restype = ctypes.c_int
        cls.time_estimate_query_function = cls.library.gxos_time_estimate_test_query
        cls.time_estimate_query_function.argtypes = [
            ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(TimeEstimateInfo),
        ]
        cls.time_estimate_query_function.restype = ctypes.c_int
        cls.time_estimate_tls_request_function = cls.library.gxos_time_estimate_test_tls_request
        cls.time_estimate_tls_request_function.argtypes = [
            ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(TlsTimeEstimateLease),
        ]
        cls.time_estimate_tls_request_function.restype = ctypes.c_int
        cls.time_estimate_tls_refresh_function = cls.library.gxos_time_estimate_test_tls_refresh
        cls.time_estimate_tls_refresh_function.argtypes = [
            ctypes.c_uint32, ctypes.POINTER(TlsTimeEstimateLease), ctypes.c_uint64,
            ctypes.POINTER(TlsTimeEstimateLease),
        ]
        cls.time_estimate_tls_refresh_function.restype = ctypes.c_int
        cls.tls_time_policy_check_function = cls.library.gxos_tls_time_policy_check_interval
        cls.tls_time_policy_check_function.argtypes = [
            ctypes.c_uint64, ctypes.c_uint64, ctypes.c_int64, ctypes.c_int64,
        ]
        cls.tls_time_policy_check_function.restype = ctypes.c_int
        cls.tls_time_policy_utc_function = cls.library.gxos_tls_time_policy_utc_to_unix
        cls.tls_time_policy_utc_function.argtypes = [
            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, ctypes.POINTER(ctypes.c_int64),
        ]
        cls.tls_time_policy_utc_function.restype = ctypes.c_int
        cls.time_estimate_invalidate_function = cls.library.gxos_time_estimate_test_invalidate
        cls.time_estimate_invalidate_function.argtypes = [ctypes.c_uint32, ctypes.c_uint8]
        cls.time_estimate_discontinuity_function = cls.library.gxos_time_estimate_test_discontinuity
        cls.time_estimate_discontinuity_function.argtypes = [ctypes.c_uint32]

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

    def run_udp_acquisition_round(self, *, modes=None, midpoints=None, radii=None,
                                  operators=None, receipt_times=None, now_ns=1_000_000_000_000,
                                  maximum_uncertainty=30, duplicate_key=False,
                                  expected_round_status=0):
        root_keys = [ROOT_KEY, OTHER_KEY, THIRD_ROOT_KEY]
        modes = modes or ["normal"] * 3
        midpoints = midpoints or [FIXTURE_TIME] * 3
        radii = radii or [2] * 3
        operators = operators or [201, 202, 203]
        fixtures = []
        ports = []
        for index, (root_key, mode, midpoint, radius) in enumerate(
                zip(root_keys, modes, midpoints, radii)):
            if mode in ("network_failure", "monotonic_discontinuity"):
                fixture = None
                port = _unused_udp_port()
            else:
                fixture = RoughtimeUdpFixture(
                    root_key, mode=mode, midpoint=midpoint, radius=radius,
                )
                port = fixture.address[1]
            fixtures.append(fixture)
            ports.append(port)

        configuration = RoundConfiguration()
        configuration.policy_identity = 0x1501
        configuration.source_count = 3
        configuration.quorum = 2
        configuration.maximum_age_seconds = 300
        configuration.maximum_source_uncertainty_seconds = maximum_uncertainty
        configuration.maximum_transport_delay_ms = 2000
        configuration.maximum_forward_jump_seconds = 300
        for index, source in enumerate(configuration.sources):
            source.server_ipv4 = 0x7F000001
            source.server_port = ports[index]
            source.operator_identity = operators[index]
            key = root_keys[index].public_key().public_bytes_raw()
            if duplicate_key and index == 1:
                key = root_keys[0].public_key().public_bytes_raw()
            source.server_public_key[:] = key

        call_index = [0]
        nonce_counter = [0]
        fixture_errors = []

        @self.random_fill_callback_type
        def random_fill(_context, output, output_length):
            seed = hashlib.sha256(
                b"NAVHTTPS15-test-only-nonce" + nonce_counter[0].to_bytes(4, "little")
            ).digest()
            nonce_counter[0] += 1
            for offset in range(output_length):
                output[offset] = seed[offset % len(seed)]
            return 1

        @self.round_test_provider_type
        def provider(_context, source_ptr, acquisition_ptr):
            index = call_index[0]
            call_index[0] += 1
            source = source_ptr.contents
            acquired = acquisition_ptr.contents
            acquired.server_ipv4 = source.server_ipv4
            acquired.server_port = source.server_port

            if modes[index] == "network_failure":
                return 3  # NETWORK_UNAVAILABLE
            if modes[index] == "monotonic_discontinuity":
                return 12  # MONOTONIC_DISCONTINUITY from the PIT adapter

            key = bytes(source.server_public_key)
            nonce = (ctypes.c_uint8 * 32)()
            request = (ctypes.c_uint8 * 1036)()
            request_length = ctypes.c_size_t()
            build_status = self.build_request_random_function(
                random_fill, None, self._u8(key), nonce, request, len(request),
                ctypes.byref(request_length),
            )
            if build_status != 0:
                return build_status

            client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            client.settimeout(0.12)
            try:
                client.sendto(bytes(request[:request_length.value]),
                              ("127.0.0.1", source.server_port))
                response, actual = client.recvfrom(1024)
            except (socket.timeout, ConnectionResetError, OSError):
                return 6  # TIMEOUT / bounded network failure
            finally:
                client.close()
            if client.fileno() != -1:
                fixture_errors.append(AssertionError("UDP client socket was not closed"))

            received_ns = (receipt_times[index] if receipt_times else now_ns - 100_000_000)
            sent_ns = received_ns - 100_000_000
            response_arg = self._u8(response)
            nonce_arg = (ctypes.c_uint8 * 32).from_buffer_copy(bytes(nonce))
            workspace = Workspace()
            evidence = Evidence()
            actual_ip = int.from_bytes(socket.inet_aton(actual[0]), "big")
            verified_status = self.dispatch_function(
                response_arg, len(response), actual_ip, actual[1],
                source.server_ipv4, source.server_port,
                request, request_length.value, nonce_arg, self._u8(key),
                sent_ns, received_ns, ctypes.byref(workspace), ctypes.byref(evidence),
            )
            if verified_status != 0:
                return verified_status

            acquired.status = 0
            acquired.evidence = evidence
            acquired.request_bytes = request_length.value
            acquired.response_bytes = len(response)
            contract_valid = ctypes.c_uint8(0)
            expires_ns = ctypes.c_uint64(0)
            sealed = self.seal_acquisition_function(
                ctypes.byref(evidence), ctypes.byref(contract_valid),
                ctypes.byref(expires_ns),
                ctypes.byref(acquired.agreement_evidence),
            )
            if sealed != 1:
                return 10  # CLOCK_EVIDENCE_REJECTED
            acquired.clock_evidence_contract_valid = contract_valid.value
            acquired.evidence_expires_monotonic_ns = expires_ns.value
            return 0

        result = RoundResult()
        try:
            status = self.round_test_function(
                ctypes.byref(configuration), now_ns, None, ctypes.byref(result), provider, None,
            )
        finally:
            for fixture in fixtures:
                if fixture is not None:
                    fixture.close()
                    fixture_errors.extend(fixture.errors)
        self.assertEqual(status, expected_round_status)
        self.assertEqual(call_index[0], 0 if duplicate_key else 3)
        self.assertEqual(fixture_errors, [])
        return configuration, result

    def test_round_acquisition_three_live_udp_signers_agree(self):
        _configuration, result = self.run_udp_acquisition_round()
        self.assertEqual(result.attempted_source_count, 3)
        self.assertEqual(result.verified_response_count, 3)
        self.assertEqual(result.independent_operator_count, 3)
        self.assertEqual(result.agreement.status, 0)
        self.assertEqual(result.agreement.profile, 2)
        self.assertEqual(result.agreement.production_policy_accepted, 0)
        self.assertLessEqual(result.agreement.earliest_unix_seconds,
                             result.agreement.latest_unix_seconds)

    def test_round_keeps_two_verified_sources_when_one_times_out(self):
        _configuration, result = self.run_udp_acquisition_round(
            modes=["normal", "normal", "timeout"],
        )
        self.assertEqual(result.attempted_source_count, 3)
        self.assertEqual(result.verified_response_count, 2)
        self.assertEqual(result.sources[2].status, 6)
        self.assertEqual(result.agreement.status, 0)

    def test_round_rejects_two_agreeing_sources_with_a_valid_outlier(self):
        _configuration, result = self.run_udp_acquisition_round(
            midpoints=[FIXTURE_TIME, FIXTURE_TIME, FIXTURE_TIME + 100],
        )
        self.assertEqual(result.verified_response_count, 3)
        self.assertEqual(result.agreement.status, 2)  # INCONSISTENT_SOURCES

    def test_round_with_one_verified_source_reports_insufficient_sources(self):
        _configuration, result = self.run_udp_acquisition_round(
            modes=["normal", "timeout", "network_failure"],
        )
        self.assertEqual(result.verified_response_count, 1)
        self.assertEqual(result.independent_operator_count, 1)
        self.assertEqual(result.agreement.status, 1)  # INSUFFICIENT_SOURCES

    def test_round_collapses_duplicate_operator_identity(self):
        _configuration, result = self.run_udp_acquisition_round(
            operators=[201, 201, 202],
        )
        self.assertEqual(result.verified_response_count, 3)
        self.assertEqual(result.independent_operator_count, 2)
        self.assertEqual(result.agreement.status, 0)
        self.assertEqual(result.agreement.independent_source_count, 2)

    def test_round_rejects_duplicate_server_key_before_network_access(self):
        _configuration, result = self.run_udp_acquisition_round(
            duplicate_key=True, expected_round_status=1,
        )
        self.assertEqual(result.status, 1)  # INVALID_CONFIGURATION
        self.assertEqual(result.attempted_source_count, 0)

    def test_round_rejects_bad_signature_without_losing_other_operators(self):
        _configuration, result = self.run_udp_acquisition_round(
            modes=["normal", "normal", "invalid_signature"],
        )
        self.assertEqual(result.verified_response_count, 2)
        self.assertEqual(result.sources[2].status, 9)
        self.assertEqual(result.agreement.status, 0)

    def test_round_rejects_incorrect_nonce_without_losing_other_operators(self):
        _configuration, result = self.run_udp_acquisition_round(
            modes=["normal", "normal", "incorrect_nonce"],
        )
        self.assertEqual(result.verified_response_count, 2)
        self.assertEqual(result.sources[2].status, 9)
        self.assertEqual(result.agreement.status, 0)

    def test_round_rejects_response_from_unexpected_udp_source(self):
        _configuration, result = self.run_udp_acquisition_round(
            modes=["normal", "normal", "unexpected_source"],
        )
        self.assertEqual(result.verified_response_count, 2)
        self.assertEqual(result.sources[2].status, 7)
        self.assertEqual(result.agreement.status, 0)

    def test_round_excludes_verified_but_excessively_uncertain_source_set(self):
        _configuration, result = self.run_udp_acquisition_round(
            radii=[40, 40, 40], maximum_uncertainty=30,
        )
        self.assertEqual(result.verified_response_count, 3)
        self.assertEqual(result.agreement.status, 4)  # EXCESSIVE_UNCERTAINTY

    def test_round_rejects_stale_evidence_using_receipt_age(self):
        now_ns = 1_000_000_000_000
        old_receipt = now_ns - 301_000_000_000
        _configuration, result = self.run_udp_acquisition_round(
            receipt_times=[old_receipt] * 3, now_ns=now_ns,
        )
        self.assertEqual(result.verified_response_count, 3)
        self.assertEqual(result.agreement.status, 3)  # STALE_EVIDENCE

    def test_round_records_reordered_receipt_times_at_one_evaluation_instant(self):
        now_ns = 1_000_000_000_000
        _configuration, result = self.run_udp_acquisition_round(
            receipt_times=[now_ns - 200_000_000, now_ns - 900_000_000,
                           now_ns - 500_000_000],
            now_ns=now_ns,
        )
        self.assertEqual(result.agreement.status, 0)
        self.assertEqual(result.agreement.evaluated_monotonic_ns, now_ns)
        self.assertEqual(result.agreement.maximum_evidence_age_seconds, 1)

    def test_round_reports_network_failure_and_uses_remaining_quorum(self):
        _configuration, result = self.run_udp_acquisition_round(
            modes=["normal", "normal", "network_failure"],
        )
        self.assertEqual(result.sources[2].status, 3)
        self.assertEqual(result.verified_response_count, 2)
        self.assertEqual(result.agreement.status, 0)

    def test_round_reports_monotonic_discontinuity_without_counting_source(self):
        _configuration, result = self.run_udp_acquisition_round(
            modes=["normal", "normal", "monotonic_discontinuity"],
        )
        self.assertEqual(result.sources[2].status, 12)
        self.assertEqual(result.verified_response_count, 2)
        self.assertEqual(result.agreement.status, 0)

    def test_round_classifies_multiple_disjoint_candidate_quorums_ambiguous(self):
        _configuration, result = self.run_udp_acquisition_round(
            midpoints=[FIXTURE_TIME + 5, FIXTURE_TIME - 1, FIXTURE_TIME + 11],
            radii=[10, 2, 2],
        )
        self.assertEqual(result.verified_response_count, 3)
        self.assertEqual(result.agreement.status, 9)  # AMBIGUOUS

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

    def test_acquisition_sealing_sets_bounded_expiry_and_clears_overflow(self):
        key = bytes(ROOT_KEY.public_key().public_bytes_raw())
        nonce = b"E" * 32
        request = self.build_acquisition_request(nonce, key)
        response = make_response(request, nonce, midpoint=FIXTURE_TIME, radius=3)
        status, evidence = self.dispatch_acquisition(response, request, nonce, key)
        self.assertEqual(status, 0)

        contract_valid = ctypes.c_uint8(0)
        expires_ns = ctypes.c_uint64(0)
        token = AgreementToken()
        sealed = self.seal_acquisition_function(
            ctypes.byref(evidence), ctypes.byref(contract_valid),
            ctypes.byref(expires_ns), ctypes.byref(token),
        )
        self.assertEqual(sealed, 1)
        self.assertEqual(contract_valid.value, 1)
        self.assertEqual(expires_ns.value, self.received_ns + 300_000_000_000)
        self.assertNotEqual(bytes(token.opaque), bytes(256))

        overflow_evidence = Evidence.from_buffer_copy(bytes(evidence))
        overflow_evidence.received_monotonic_ns = (1 << 64) - 1
        overflow_evidence.sent_monotonic_ns = overflow_evidence.received_monotonic_ns - 100_000_000
        overflow_evidence.measured_transport_delay_ns = 100_000_000
        contract_valid.value = 1
        expires_ns.value = (1 << 64) - 1
        token.opaque[:] = b"\xa5" * 256
        sealed = self.seal_acquisition_function(
            ctypes.byref(overflow_evidence), ctypes.byref(contract_valid),
            ctypes.byref(expires_ns), ctypes.byref(token),
        )
        self.assertEqual(sealed, 0)
        self.assertEqual(contract_valid.value, 0)
        self.assertEqual(expires_ns.value, 0)
        self.assertEqual(bytes(token.opaque), bytes(256))

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

    def accept_fixture_round(self, slot, configuration, result, *, now_ns=None):
        now_ns = result.agreement.evaluated_monotonic_ns if now_ns is None else now_ns
        self.time_estimate_reset_function(slot)
        return self.time_estimate_accept_function(
            slot, ctypes.byref(configuration), ctypes.byref(result), now_ns,
        )

    def query_fixture_estimate(self, slot, now_ns):
        info = TimeEstimateInfo()
        self.time_estimate_query_function(slot, now_ns, ctypes.byref(info))
        return info

    def request_tls_time_lease(self, slot, now_ns):
        lease = TlsTimeEstimateLease()
        status = self.time_estimate_tls_request_function(
            slot, now_ns, ctypes.byref(lease),
        )
        return status, lease

    def refresh_tls_time_lease(self, slot, lease, now_ns):
        refreshed = TlsTimeEstimateLease()
        status = self.time_estimate_tls_refresh_function(
            slot, ctypes.byref(lease), now_ns, ctypes.byref(refreshed),
        )
        return status, refreshed

    def valid_navhttps15_fixture_round(self, **kwargs):
        kwargs.setdefault("operators", [401, 402, 403])
        return self.run_udp_acquisition_round(**kwargs)

    def test_fixture_two_of_three_agreement_is_retained_read_only(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        self.assertEqual(result.agreement.status, 0)
        self.assertEqual(result.agreement.profile, 2)
        self.assertEqual(result.agreement.production_policy_accepted, 0)
        self.assertEqual(self.accept_fixture_round(0, configuration, result), 0)

        info = self.query_fixture_estimate(0, result.agreement.evaluated_monotonic_ns)
        self.assertEqual(info.state, 1)  # Fresh
        self.assertEqual(info.scope, 1)  # FixtureOnly
        self.assertEqual(info.available, 1)
        self.assertEqual(info.agreement_satisfied, 1)
        self.assertEqual(info.configured_source_count, 3)
        self.assertEqual(info.accepted_source_count, 3)
        self.assertEqual(info.distinct_operator_count, 3)
        self.assertEqual(info.operator_identity_count, 3)
        self.assertEqual(info.policy_identity, 0x1501)
        self.assertLessEqual(info.agreed_earliest_unix_seconds,
                             info.agreed_latest_unix_seconds)
        self.assertEqual(info.current_earliest_unix_seconds,
                         info.agreed_earliest_unix_seconds)
        self.assertEqual(info.current_latest_unix_seconds,
                         info.agreed_latest_unix_seconds)
        self.assertEqual(info.production_policy_accepted, 0)
        self.assertEqual(info.system_clock_changed, 0)
        self.assertEqual(info.public_https_enabled, 0)

    def test_fixture_time_rejects_one_source_duplicate_operator_and_unpinned_key(self):
        configuration, one_source = self.valid_navhttps15_fixture_round(
            modes=["normal", "timeout", "network_failure"],
        )
        self.assertEqual(one_source.agreement.status, 1)
        self.assertEqual(self.accept_fixture_round(1, configuration, one_source), 3)

        duplicate_configuration, duplicate_result = self.valid_navhttps15_fixture_round(
            operators=[401, 401, 403],
        )
        self.assertEqual(duplicate_result.agreement.status, 0)
        self.assertEqual(self.accept_fixture_round(2, duplicate_configuration, duplicate_result), 2)

        pinned_configuration, pinned_result = self.valid_navhttps15_fixture_round()
        pinned_configuration.sources[0].server_public_key[0] ^= 1
        self.assertEqual(self.accept_fixture_round(3, pinned_configuration, pinned_result), 2)

    def test_fixture_time_rejects_invalid_signature_rejected_and_ambiguous_agreements(self):
        invalid_configuration, invalid_result = self.valid_navhttps15_fixture_round(
            modes=["invalid_signature", "timeout", "timeout"],
        )
        self.assertNotEqual(invalid_result.agreement.status, 0)
        self.assertNotEqual(
            self.accept_fixture_round(4, invalid_configuration, invalid_result), 0,
        )

        ambiguous_configuration, ambiguous_result = self.valid_navhttps15_fixture_round(
            midpoints=[FIXTURE_TIME, FIXTURE_TIME + 15, FIXTURE_TIME + 30],
            radii=[10, 10, 10], maximum_uncertainty=30,
        )
        self.assertEqual(ambiguous_result.agreement.status, 9)
        self.assertNotEqual(
            self.accept_fixture_round(5, ambiguous_configuration, ambiguous_result), 0,
        )

    def test_fixture_time_rejects_caller_fabricated_agreement_interval(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        forged = RoundResult.from_buffer_copy(result)
        forged.agreement.earliest_unix_seconds += 1
        forged.agreement.latest_unix_seconds += 1
        forged.agreement.estimated_midpoint_unix_seconds += 1
        self.assertEqual(self.accept_fixture_round(2, configuration, forged), 1)
        rejected = self.query_fixture_estimate(
            2, result.agreement.evaluated_monotonic_ns,
        )
        self.assertEqual(rejected.available, 0)
        self.assertEqual(rejected.state, 8)  # Rejected

    def test_fixture_time_advances_interval_and_grows_uncertainty(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        anchor = result.agreement.evaluated_monotonic_ns
        self.assertEqual(self.accept_fixture_round(6, configuration, result), 0)
        initial = self.query_fixture_estimate(6, anchor)
        advanced = self.query_fixture_estimate(6, anchor + 10_000_000_000)
        self.assertEqual(advanced.state, 2)  # Advanced
        self.assertEqual(advanced.last_observed_monotonic_ns, anchor + 10_000_000_000)
        self.assertGreater(advanced.current_earliest_unix_seconds,
                           initial.current_earliest_unix_seconds)
        self.assertGreater(advanced.current_latest_unix_seconds,
                           initial.current_latest_unix_seconds)
        self.assertGreater(advanced.current_uncertainty_seconds,
                           initial.current_uncertainty_seconds)
        self.assertLessEqual(advanced.current_uncertainty_seconds, 30)
        self.assertLess(advanced.remaining_freshness_seconds,
                        initial.remaining_freshness_seconds)

    def test_fixture_time_expiry_rollback_discontinuity_and_explicit_invalidation(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        anchor = result.agreement.evaluated_monotonic_ns
        self.assertEqual(self.accept_fixture_round(7, configuration, result), 0)
        expired = self.query_fixture_estimate(7, anchor + 300_000_000_000)
        self.assertEqual(expired.state, 3)
        self.assertEqual(expired.available, 0)
        self.assertEqual(expired.remaining_freshness_seconds, 0)

        self.assertEqual(self.accept_fixture_round(8, configuration, result), 0)
        rollback = self.query_fixture_estimate(8, anchor - 1)
        self.assertEqual(rollback.state, 6)
        self.assertEqual(rollback.available, 0)

        self.assertEqual(self.accept_fixture_round(9, configuration, result), 0)
        self.time_estimate_discontinuity_function(9)
        discontinuity = self.query_fixture_estimate(9, anchor)
        self.assertEqual(discontinuity.state, 5)
        self.assertEqual(discontinuity.available, 0)

        self.assertEqual(self.accept_fixture_round(10, configuration, result), 0)
        self.time_estimate_invalidate_function(10, 17)  # ExplicitInvalidation
        invalidated = self.query_fixture_estimate(10, anchor)
        self.assertEqual(invalidated.state, 7)
        self.assertEqual(invalidated.available, 0)

    def test_fixture_time_rejects_evidence_anchored_in_the_monotonic_future(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        anchor = result.agreement.evaluated_monotonic_ns
        self.assertGreater(anchor, 0)
        self.assertEqual(self.accept_fixture_round(1, configuration, result,
                                                   now_ns=anchor - 1), 10)
        rollback = self.query_fixture_estimate(1, anchor)
        self.assertEqual(rollback.state, 6)  # RollbackSuspected
        self.assertEqual(rollback.reason, 10)  # MonotonicWentBackwards
        self.assertEqual(rollback.available, 0)
        self.assertEqual(self.time_estimate_accept_function(
            1, ctypes.byref(configuration), ctypes.byref(result), anchor,
        ), 10)

    def test_fixture_time_detects_conflict_rollback_and_excessive_forward_evidence(self):
        first_configuration, first = self.valid_navhttps15_fixture_round()
        anchor = first.agreement.evaluated_monotonic_ns

        self.assertEqual(self.accept_fixture_round(11, first_configuration, first), 0)
        next_configuration, conflicting = self.valid_navhttps15_fixture_round(
            midpoints=[FIXTURE_TIME + 100] * 3,
        )
        self.assertEqual(self.time_estimate_accept_function(
            11, ctypes.byref(next_configuration), ctypes.byref(conflicting), anchor,
        ), 9)  # ConflictingEvidence
        self.assertEqual(self.query_fixture_estimate(11, anchor).state, 4)

        self.assertEqual(self.accept_fixture_round(12, first_configuration, first), 0)
        _configuration, rollback = self.valid_navhttps15_fixture_round(
            midpoints=[FIXTURE_TIME - 100] * 3,
        )
        self.assertEqual(self.time_estimate_accept_function(
            12, ctypes.byref(first_configuration), ctypes.byref(rollback), anchor,
        ), 10)  # RollbackDetected

        self.assertEqual(self.accept_fixture_round(13, first_configuration, first), 0)
        _configuration, forward = self.valid_navhttps15_fixture_round(
            midpoints=[FIXTURE_TIME + 1000] * 3,
        )
        self.assertEqual(self.time_estimate_accept_function(
            13, ctypes.byref(first_configuration), ctypes.byref(forward), anchor,
        ), 11)  # ExcessiveForwardMovement

    def test_fixture_time_checks_policy_change_interval_bounds_and_expiry_overflow(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        anchor = result.agreement.evaluated_monotonic_ns
        self.assertEqual(self.accept_fixture_round(14, configuration, result), 0)
        changed_policy = RoundConfiguration.from_buffer_copy(configuration)
        changed_policy.policy_identity = 0x1502
        self.assertEqual(self.time_estimate_accept_function(
            14, ctypes.byref(changed_policy), ctypes.byref(result), anchor,
        ), 8)  # SourcePolicyChanged

        bad_interval = RoundResult.from_buffer_copy(result)
        bad_interval.agreement.earliest_unix_seconds = 1
        self.assertNotEqual(self.accept_fixture_round(15, configuration, bad_interval), 0)

        overflow = RoundResult.from_buffer_copy(result)
        overflow.agreement.evaluated_monotonic_ns = 0xFFFFFFFFFFFFFFFF - 1
        self.assertEqual(self.accept_fixture_round(
            15, configuration, overflow,
            now_ns=overflow.agreement.evaluated_monotonic_ns,
        ), 1)  # InvalidRound: the timestamp no longer matches issued tokens.

        self.assertEqual(self.accept_fixture_round(15, configuration, result), 0)
        end_of_counter = self.query_fixture_estimate(15, 0xFFFFFFFFFFFFFFFF)
        self.assertEqual(end_of_counter.state, 3)  # Expired, without wrapped freshness.
        self.assertEqual(end_of_counter.available, 0)
        self.assertEqual(end_of_counter.remaining_freshness_seconds, 0)

    def test_fixture_time_has_no_cross_boot_persistence_or_production_authority(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        anchor = result.agreement.evaluated_monotonic_ns
        self.assertEqual(self.accept_fixture_round(0, configuration, result), 0)
        self.time_estimate_reset_function(0)  # Simulated reboot with no secure persistent state.
        after_reboot = self.query_fixture_estimate(0, anchor + 10_000_000_000)
        self.assertEqual(after_reboot.state, 0)  # NoEvidence
        self.assertEqual(after_reboot.scope, 0)
        self.assertEqual(after_reboot.reason, 1)  # NoAuthenticatedPersistentState
        self.assertEqual(after_reboot.production_policy_accepted, 0)
        self.assertEqual(after_reboot.system_clock_changed, 0)
        self.assertEqual(after_reboot.public_https_enabled, 0)

    def test_tls_certificate_interval_requires_full_inclusive_containment(self):
        check = self.tls_time_policy_check_function
        self.assertEqual(check(100, 200, 100, 200), 0)  # Exact boundaries.
        self.assertEqual(check(101, 199, 100, 200), 0)
        self.assertEqual(check(99, 101, 100, 200), 3)  # Crosses notBefore.
        self.assertEqual(check(90, 99, 100, 200), 3)  # Entirely before.
        self.assertEqual(check(199, 201, 100, 200), 4)  # Crosses notAfter.
        self.assertEqual(check(201, 210, 100, 200), 4)  # Entirely after.
        self.assertEqual(check(201, 200, 100, 200), 1)  # Inverted estimate.
        self.assertEqual(check(100, 200, 201, 200), 2)  # Inverted certificate.
        self.assertEqual(check(0x7FFFFFFFFFFFFFFF, 0x8000000000000000,
                               0, 0x7FFFFFFFFFFFFFFF), 1)

    def test_tls_certificate_utc_conversion_validates_calendar_and_overflow(self):
        convert = self.tls_time_policy_utc_function
        unix_seconds = ctypes.c_int64(-1)
        self.assertEqual(convert(1970, 1, 1, 0, 0, 0, ctypes.byref(unix_seconds)), 1)
        self.assertEqual(unix_seconds.value, 0)
        self.assertEqual(convert(2000, 2, 29, 0, 0, 0, ctypes.byref(unix_seconds)), 1)
        self.assertEqual(unix_seconds.value, 951782400)
        self.assertEqual(convert(1900, 2, 29, 0, 0, 0, ctypes.byref(unix_seconds)), 0)
        self.assertEqual(convert(2000, 2, 30, 0, 0, 0, ctypes.byref(unix_seconds)), 0)
        self.assertEqual(convert(2000, 12, 31, 23, 59, 60, ctypes.byref(unix_seconds)), 0)
        self.assertEqual(convert(10000, 1, 1, 0, 0, 0, ctypes.byref(unix_seconds)), 0)

    def test_tls_time_consumer_requires_stored_quorum_evidence_and_scopes_lease(self):
        status, absent = self.request_tls_time_lease(0, 0)
        self.assertEqual(status, 1)  # NoEvidence, regardless of plausible RTC state.
        self.assertEqual(absent.available, 0)
        self.assertEqual(absent.trust_scope_identity, 0)

        configuration, one_source = self.valid_navhttps15_fixture_round(
            modes=["normal", "timeout", "network_failure"],
        )
        self.assertNotEqual(one_source.agreement.status, 0)
        self.assertNotEqual(self.accept_fixture_round(0, configuration, one_source), 0)
        status, rejected = self.request_tls_time_lease(
            0, one_source.agreement.evaluated_monotonic_ns,
        )
        self.assertEqual(status, 1)
        self.assertEqual(rejected.available, 0)

        configuration, result = self.valid_navhttps15_fixture_round(
            modes=["normal", "normal", "timeout"],
        )
        self.assertEqual(result.agreement.status, 0)
        self.assertEqual(result.agreement.independent_source_count, 2)
        self.assertEqual(self.accept_fixture_round(0, configuration, result), 0)
        anchor = result.agreement.evaluated_monotonic_ns
        status, lease = self.request_tls_time_lease(0, anchor)
        self.assertEqual(status, 0)
        self.assertEqual(lease.available, 1)
        self.assertEqual(lease.scope, 1)  # FixtureOnly.
        self.assertEqual(lease.source_policy_identity, 0x1501)
        self.assertEqual(lease.trust_scope_identity, 0x1701)
        self.assertEqual(lease.earliest_unix_seconds,
                         result.agreement.earliest_unix_seconds)
        self.assertEqual(lease.latest_unix_seconds,
                         result.agreement.latest_unix_seconds)
        self.assertEqual(lease.uncertainty_seconds,
                         result.agreement.maximum_uncertainty_seconds)

        forged = TlsTimeEstimateLease.from_buffer_copy(lease)
        forged.earliest_unix_seconds = 1
        forged.latest_unix_seconds = 2
        status, refreshed = self.refresh_tls_time_lease(0, forged, anchor)
        self.assertEqual(status, 0)
        self.assertEqual(refreshed.earliest_unix_seconds,
                         result.agreement.earliest_unix_seconds)
        self.assertEqual(refreshed.latest_unix_seconds,
                         result.agreement.latest_unix_seconds)

        wrong_scope = TlsTimeEstimateLease.from_buffer_copy(lease)
        wrong_scope.trust_scope_identity ^= 1
        self.assertEqual(self.refresh_tls_time_lease(0, wrong_scope, anchor)[0], 9)

    def test_tls_time_consumer_rejects_duplicate_operators_stale_time_and_rollback(self):
        configuration, duplicate = self.valid_navhttps15_fixture_round(
            operators=[401, 401, 403],
        )
        self.assertEqual(self.accept_fixture_round(0, configuration, duplicate), 2)
        status, lease = self.request_tls_time_lease(
            0, duplicate.agreement.evaluated_monotonic_ns,
        )
        self.assertEqual(status, 1)
        self.assertEqual(lease.available, 0)

        configuration, result = self.valid_navhttps15_fixture_round()
        anchor = result.agreement.evaluated_monotonic_ns
        self.assertEqual(self.accept_fixture_round(1, configuration, result), 0)
        status, lease = self.request_tls_time_lease(1, anchor)
        self.assertEqual(status, 0)
        self.assertEqual(self.request_tls_time_lease(1, anchor + 300_000_000_000)[0], 6)

        self.assertEqual(self.accept_fixture_round(2, configuration, result), 0)
        status, lease = self.request_tls_time_lease(2, anchor)
        self.assertEqual(status, 0)
        self.assertEqual(self.request_tls_time_lease(2, anchor - 1)[0], 7)
        self.assertEqual(self.refresh_tls_time_lease(2, lease, anchor)[0], 9)

    def test_tls_time_lease_invalidation_and_replacement_are_connection_scoped(self):
        configuration, result = self.valid_navhttps15_fixture_round()
        anchor = result.agreement.evaluated_monotonic_ns
        self.assertEqual(self.accept_fixture_round(3, configuration, result), 0)
        status, prior_connection = self.request_tls_time_lease(3, anchor)
        self.assertEqual(status, 0)

        # Reaccepting creates a new provenance generation. An in-flight
        # connection cannot reuse its old lease after evidence replacement.
        self.assertEqual(self.time_estimate_accept_function(
            3, ctypes.byref(configuration), ctypes.byref(result), anchor,
        ), 0)
        status, next_connection = self.request_tls_time_lease(3, anchor)
        self.assertEqual(status, 0)
        self.assertNotEqual(prior_connection.generation, next_connection.generation)
        self.assertEqual(self.refresh_tls_time_lease(3, prior_connection, anchor)[0], 9)
        self.assertEqual(self.refresh_tls_time_lease(3, next_connection, anchor)[0], 0)

        self.time_estimate_invalidate_function(3, 17)
        self.assertEqual(self.refresh_tls_time_lease(3, next_connection, anchor)[0], 9)

    def test_concurrent_tls_contexts_keep_distinct_fixture_intervals(self):
        config_a, result_a = self.valid_navhttps15_fixture_round()
        config_b, result_b = self.valid_navhttps15_fixture_round(
            midpoints=[FIXTURE_TIME + 60] * 3,
        )
        anchor_a = result_a.agreement.evaluated_monotonic_ns
        anchor_b = result_b.agreement.evaluated_monotonic_ns
        self.assertEqual(self.accept_fixture_round(4, config_a, result_a), 0)
        self.assertEqual(self.accept_fixture_round(5, config_b, result_b), 0)
        status_a, lease_a = self.request_tls_time_lease(4, anchor_a)
        status_b, lease_b = self.request_tls_time_lease(5, anchor_b)
        self.assertEqual(status_a, 0)
        self.assertEqual(status_b, 0)
        self.assertNotEqual(lease_a.earliest_unix_seconds,
                            lease_b.earliest_unix_seconds)
        self.assertEqual(self.refresh_tls_time_lease(4, lease_a, anchor_a)[0], 0)
        self.assertEqual(self.refresh_tls_time_lease(5, lease_b, anchor_b)[0], 0)


if __name__ == "__main__":
    unittest.main()

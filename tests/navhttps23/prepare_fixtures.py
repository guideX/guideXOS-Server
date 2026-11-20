#!/usr/bin/env python3
"""Capture the DigiCert-disclosed NAVHTTPS23 demo chains and derive tamper cases.

The two endpoints are disclosed in the pinned CCADB V5 root record for
DigiCert TLS ECC P384 Root G5. This tool does not fabricate certificates. It
stores the peer-provided chain exactly, verifies the root fingerprints, and
builds invalid-signature controls by changing one signature octet.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives import hashes
from cryptography.x509.oid import ExtensionOID


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "fixtures" / "navhttps23"
OPENSSL_DEFAULTS = (
    Path(r"C:\Program Files\Git\usr\bin\openssl.exe"),
    Path(r"C:\mingw64\bin\openssl.exe"),
)
G5_FINGERPRINT = "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05"
GLOBAL_G3_FINGERPRINT = "31ad6648f8104138c738f39ea4320133393e3a18cc02296ef97c2ac9ef6731d0"
VALID_HOST = "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com"
EXPIRED_HOST = "digicert-tls-ecc-p384-root-g5-expired.chain-demos.digicert.com"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pem_blocks(data: bytes) -> list[bytes]:
    return [m.group(0) + b"\n" for m in re.finditer(
        rb"-----BEGIN CERTIFICATE-----\s*.*?-----END CERTIFICATE-----",
        data,
        re.S,
    )]


def capture(openssl: Path, host: str) -> list[bytes]:
    result = subprocess.run(
        [str(openssl), "s_client", "-connect", f"{host}:443", "-servername", host,
         "-showcerts"],
        input=b"",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=45,
        check=False,
    )
    certs = pem_blocks(result.stdout)
    if not certs:
        raise SystemExit(f"No peer certificates returned by {host}: {result.stdout[-1000:]!r}")
    return certs


def cert_info(pem: bytes) -> dict[str, str]:
    cert = x509.load_pem_x509_certificate(pem)
    info = {
        "subject": cert.subject.rfc4514_string(),
        "issuer": cert.issuer.rfc4514_string(),
        "sha256": cert.fingerprint(hashes.SHA256()).hex(),
        "not_before_utc": cert.not_valid_before_utc.isoformat().replace("+00:00", "Z"),
        "not_after_utc": cert.not_valid_after_utc.isoformat().replace("+00:00", "Z"),
    }
    for oid, key in (
        (ExtensionOID.CERTIFICATE_POLICIES, "certificate_policy_oids"),
        (ExtensionOID.EXTENDED_KEY_USAGE, "extended_key_usage_oids"),
    ):
        try:
            extension = cert.extensions.get_extension_for_oid(oid)
            info[key] = [value.dotted_string if hasattr(value, "dotted_string") else value.policy_identifier.dotted_string
                         for value in extension.value]
        except x509.ExtensionNotFound:
            info[key] = []
    try:
        extension = cert.extensions.get_extension_for_oid(ExtensionOID.NAME_CONSTRAINTS)
        info["name_constraints"] = {
            "critical": extension.critical,
            "permitted_subtrees": len(extension.value.permitted_subtrees or ()),
            "excluded_subtrees": len(extension.value.excluded_subtrees or ()),
        }
    except x509.ExtensionNotFound:
        info["name_constraints"] = None
    try:
        san = cert.extensions.get_extension_for_oid(ExtensionOID.SUBJECT_ALTERNATIVE_NAME).value
        info["subject_alt_name_dns"] = san.get_values_for_type(x509.DNSName)
    except x509.ExtensionNotFound:
        info["subject_alt_name_dns"] = []
    return info


def write(path: Path, data: bytes) -> None:
    path.write_bytes(data)


def chain_bytes(*certs: bytes) -> bytes:
    return b"".join(certs)


def tamper_signature(source: Path, destination: Path) -> None:
    der = bytearray(x509.load_pem_x509_certificate(source.read_bytes()).public_bytes(
        serialization.Encoding.DER
    ))
    der[-1] ^= 1
    cert = x509.load_der_x509_certificate(bytes(der))
    write(destination, cert.public_bytes(serialization.Encoding.PEM))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--openssl", type=Path)
    parser.add_argument("--from-captured-peer-files", action="store_true",
                        help="rebuild metadata from valid-N.pem and expired-N.pem snapshots already in the fixture directory")
    parser.add_argument("--capture-date-utc", required=True,
                        help="Date of capture in YYYY-MM-DD form; part of the pinned fixture record.")
    args = parser.parse_args()
    openssl = args.openssl or next((p for p in OPENSSL_DEFAULTS if p.is_file()), None)
    if openssl is None or not openssl.is_file():
        raise SystemExit("Pass --openssl with OpenSSL 3.x s_client.")
    FIXTURES.mkdir(parents=True, exist_ok=True)

    if args.from_captured_peer_files:
        valid = [(FIXTURES / f"valid-{index}.pem").read_bytes() for index in range(5)]
        expired = [(FIXTURES / f"expired-{index}.pem").read_bytes() for index in range(3)]
    else:
        valid = capture(openssl, VALID_HOST)
        expired = capture(openssl, EXPIRED_HOST)
    valid_info = [cert_info(cert) for cert in valid]
    expired_info = [cert_info(cert) for cert in expired]
    if len(valid) < 5 or valid_info[2]["sha256"] != G5_FINGERPRINT:
        raise SystemExit("DigiCert valid demo no longer serves the expected self-signed G5 path.")
    if valid_info[4]["sha256"] != GLOBAL_G3_FINGERPRINT:
        raise SystemExit("DigiCert valid demo alternate anchor differs from pinned Global Root G3.")
    if len(expired) < 3 or expired_info[0]["not_after_utc"] >= args.capture_date_utc:
        raise SystemExit("DigiCert expired demo no longer provides an expired leaf certificate.")

    names = {
        "valid_leaf.pem": valid[0],
        "issuing_ca.pem": valid[1],
        "g5_root.pem": valid[2],
        "g5_cross_signed_by_global_g3.pem": valid[3],
        "global_root_g3.pem": valid[4],
        "expired_leaf.pem": expired[0],
        "expired_issuing_ca.pem": expired[1],
        "expired_g5_cross_signed_by_global_g3.pem": expired[2],
        "valid_g5_chain.pem": chain_bytes(valid[0], valid[1]),
        "expired_g5_chain.pem": chain_bytes(expired[0], expired[1], expired[2]),
        "alternate_global_g3_chain.pem": chain_bytes(valid[0], valid[1], valid[3], valid[4]),
        "alternate_candidate_only_chain.pem": chain_bytes(valid[0], valid[1], valid[3]),
    }
    for name, contents in names.items():
        write(FIXTURES / name, contents)
    tamper_signature(FIXTURES / "valid_leaf.pem", FIXTURES / "tampered_leaf.pem")
    tamper_signature(FIXTURES / "issuing_ca.pem", FIXTURES / "tampered_issuing_ca.pem")
    write(FIXTURES / "tampered_leaf_chain.pem", chain_bytes(
        (FIXTURES / "tampered_leaf.pem").read_bytes(),
        (FIXTURES / "issuing_ca.pem").read_bytes(),
    ))
    write(FIXTURES / "tampered_issuing_ca_chain.pem", chain_bytes(
        (FIXTURES / "valid_leaf.pem").read_bytes(),
        (FIXTURES / "tampered_issuing_ca.pem").read_bytes(),
    ))

    records = {
        path.name: {"sha256": sha256(path.read_bytes()), "bytes": path.stat().st_size}
        for path in sorted(FIXTURES.glob("*.pem"))
    }
    manifest = {
        "schema_version": "guidexos.navhttps23.digicert-chain-fixtures.v1",
        "capture_date_utc": args.capture_date_utc,
    "capture_method": "OpenSSL s_client -showcerts over TLS 1.3/1.2; peer PEM bytes pinned by SHA-256",
        "source_record": {
            "authority": "Common CA Database (Linux Foundation), V5 snapshot 2026-11-17",
            "local_snapshot": "assets/certs/mozilla/navhttps21-sources/ccadb-v5-2026-11-17.csv",
            "root_sha256": G5_FINGERPRINT,
            "valid_demo_url": f"https://{VALID_HOST}/",
            "expired_demo_url": f"https://{EXPIRED_HOST}/",
        },
        "valid_server_chain": valid_info,
        "expired_server_chain": expired_info,
        "fixtures": records,
        "limitations": [
            "Only the CA-disclosed demo paths captured on the stated date are qualified.",
            "The test site serves both the exact G5 self-signed anchor and a G5 cross-certificate under DigiCert Global Root G3.",
            "The altered-signature certificates are negative test controls and are not represented as public certificates.",
        ],
    }
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    write(FIXTURES / "manifest.json", manifest_bytes)
    print(f"NAVHTTPS23 fixture manifest SHA-256: {sha256(manifest_bytes)}")
    print(f"Captured {len(valid)} valid-demo and {len(expired)} expired-demo peer certificates.")


if __name__ == "__main__":
    main()

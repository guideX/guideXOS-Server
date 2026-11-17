#!/usr/bin/env python3
"""Create two valid paths for one server key through cross-signed intermediates."""

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def name(common_name: str) -> x509.Name:
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, common_name)])


def key():
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def cert(subject, public_key, issuer, issuer_key, serial, *, ca, san=None):
    now = datetime(2024, 1, 1, tzinfo=timezone.utc)
    later = datetime(2035, 1, 1, tzinfo=timezone.utc)
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(public_key)
        .serial_number(serial)
        .not_valid_before(now)
        .not_valid_after(later)
        .add_extension(x509.BasicConstraints(ca=ca, path_length=1 if ca else None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=not ca,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=ca,
                crl_sign=ca,
                encipher_only=None,
                decipher_only=None,
            ),
            critical=True,
        )
    )
    if san:
        builder = builder.add_extension(x509.SubjectAlternativeName([x509.DNSName(san)]), critical=False)
        builder = builder.add_extension(
            x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False
        )
    return builder.sign(issuer_key, hashes.SHA256())


def write_cert(path: Path, certificate: x509.Certificate) -> None:
    path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))


def fingerprint(certificate: x509.Certificate) -> str:
    return hashlib.sha256(certificate.public_bytes(serialization.Encoding.DER)).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)

    root_a_key, root_b_key, intermediate_key, leaf_key = key(), key(), key(), key()
    root_a_name, root_b_name = name("NAVHTTPS21 Alternate Root A"), name("NAVHTTPS21 Alternate Root B")
    root_a = cert(root_a_name, root_a_key.public_key(), root_a_name, root_a_key, 1001, ca=True)
    root_b = cert(root_b_name, root_b_key.public_key(), root_b_name, root_b_key, 1002, ca=True)
    intermediate_name = name("NAVHTTPS21 Shared Cross-Signed Issuer")
    intermediate_a = cert(
        intermediate_name, intermediate_key.public_key(), root_a_name, root_a_key, 2001, ca=True
    )
    intermediate_b = cert(
        intermediate_name, intermediate_key.public_key(), root_b_name, root_b_key, 2002, ca=True
    )
    leaf = cert(
        name("alternate.test"), leaf_key.public_key(), intermediate_name, intermediate_key,
        3001, ca=False, san="alternate.test",
    )

    write_cert(out / "alternate-root-a.pem", root_a)
    write_cert(out / "alternate-root-b.pem", root_b)
    (out / "alternate-chain-a.pem").write_bytes(
        leaf.public_bytes(serialization.Encoding.PEM) + intermediate_a.public_bytes(serialization.Encoding.PEM)
    )
    (out / "alternate-chain-b.pem").write_bytes(
        leaf.public_bytes(serialization.Encoding.PEM) + intermediate_b.public_bytes(serialization.Encoding.PEM)
    )
    spki_a = intermediate_a.public_key().public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    spki_b = intermediate_b.public_key().public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    manifest = {
        "fixture_only": True,
        "leaf_sha256": fingerprint(leaf),
        "restricted_anchor_a_sha256": fingerprint(root_a),
        "alternate_anchor_b_sha256": fingerprint(root_b),
        "cross_signed_intermediate_a_sha256": fingerprint(intermediate_a),
        "cross_signed_intermediate_b_sha256": fingerprint(intermediate_b),
        "intermediate_spki_equal": spki_a == spki_b,
        "cross_signed_intermediate_der_equal": intermediate_a.public_bytes(serialization.Encoding.DER)
        == intermediate_b.public_bytes(serialization.Encoding.DER),
    }
    (out / "alternate-anchor-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Create temporary private-CA certificates for negative verifier controls.

These keys and certificates are generated for one offline test run. They are
not copied into the repository and cannot authorize or contaminate the public
CA candidate. The pinned public chain is read separately from the fixture PEM.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import re
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID, ObjectIdentifier


NOW = dt.datetime(2026, 6, 15)
NOT_BEFORE = dt.datetime(2024, 1, 1)
NOT_AFTER = dt.datetime(2035, 1, 1)


def key() -> rsa.RSAPrivateKey:
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def name(common_name: str) -> x509.Name:
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, common_name)])


def issue_ca(
    common_name: str,
    subject_key: rsa.RSAPrivateKey,
    issuer: x509.Name,
    issuer_key: rsa.RSAPrivateKey,
    path_length: int,
    name_constraints: x509.NameConstraints | None = None,
    name_constraints_critical: bool = False,
) -> x509.Certificate:
    subject = name(common_name)
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(subject_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(NOT_BEFORE)
        .not_valid_after(NOT_AFTER)
        .add_extension(x509.BasicConstraints(ca=True, path_length=path_length), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=True,
                crl_sign=True,
                encipher_only=None,
                decipher_only=None,
            ),
            critical=True,
        )
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(subject_key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(issuer_key.public_key()), critical=False)
    )
    if name_constraints is not None:
        builder = builder.add_extension(name_constraints, critical=name_constraints_critical)
    return builder.sign(issuer_key, hashes.SHA256())


def issue_leaf(
    common_name: str,
    dns_name: str,
    subject_key: rsa.RSAPrivateKey,
    issuer: x509.Name,
    issuer_key: rsa.RSAPrivateKey,
    eku: ExtendedKeyUsageOID,
    certificate_policy: bool = False,
    unknown_critical_extension: bool = False,
) -> x509.Certificate:
    builder = (
        x509.CertificateBuilder()
        .subject_name(name(common_name))
        .issuer_name(issuer)
        .public_key(subject_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(NOT_BEFORE)
        .not_valid_after(NOT_AFTER)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=True,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=False,
                crl_sign=False,
                encipher_only=None,
                decipher_only=None,
            ),
            critical=True,
        )
        .add_extension(x509.ExtendedKeyUsage([eku]), critical=False)
        .add_extension(x509.SubjectAlternativeName([x509.DNSName(dns_name)]), critical=False)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(subject_key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(issuer_key.public_key()), critical=False)
    )
    if certificate_policy:
        builder = builder.add_extension(
            x509.CertificatePolicies(
                [x509.PolicyInformation(ObjectIdentifier("1.3.6.1.4.1.55555.18.1"), None)]
            ),
            critical=False,
        )
    if unknown_critical_extension:
        builder = builder.add_extension(
            x509.UnrecognizedExtension(ObjectIdentifier("1.3.6.1.4.1.55555.18.2"), b"\x05\x00"),
            critical=True,
        )
    return builder.sign(issuer_key, hashes.SHA256())


def pem_certificate(certificate: x509.Certificate) -> bytes:
    return certificate.public_bytes(serialization.Encoding.PEM)


def write_chain(path: Path, leaf: x509.Certificate, intermediate: x509.Certificate) -> None:
    path.write_bytes(pem_certificate(leaf) + pem_certificate(intermediate))


def write_test_ca_hierarchy(out_dir: Path, prefix: str, path_length: int,
                            name_constraints_critical: bool | None = None) -> tuple[Path, Path, rsa.RSAPrivateKey, x509.Name, rsa.RSAPrivateKey]:
    root_key = key()
    root_name = name(f"NAVHTTPS18 {prefix} Root")
    root = issue_ca(prefix + " Root", root_key, root_name, root_key, path_length)
    intermediate_key = key()
    constraints = None
    if name_constraints_critical is not None:
        constraints = x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".allowed.example")],
            excluded_subtrees=None,
        )
    intermediate = issue_ca(
        prefix + " Intermediate",
        intermediate_key,
        root.subject,
        root_key,
        path_length=0,
        name_constraints=constraints,
        name_constraints_critical=bool(name_constraints_critical),
    )
    root_path = out_dir / f"{prefix.lower()}-root.pem"
    intermediate_path = out_dir / f"{prefix.lower()}-intermediate.pem"
    root_path.write_bytes(pem_certificate(root))
    intermediate_path.write_bytes(pem_certificate(intermediate))
    return root_path, intermediate_path, intermediate_key, intermediate.subject, root_key


def make_fixture_chains(out_dir: Path) -> None:
    main_root, main_intermediate, main_issuer_key, main_issuer, _ = write_test_ca_hierarchy(
        out_dir, "Verifier", 1
    )
    del main_root, main_intermediate

    server_key = key()
    server_leaf = issue_leaf(
        "www.policy.example",
        "www.policy.example",
        server_key,
        main_issuer,
        main_issuer_key,
        ExtendedKeyUsageOID.SERVER_AUTH,
    )
    policy_leaf = issue_leaf(
        "www.policy.example",
        "www.policy.example",
        server_key,
        main_issuer,
        main_issuer_key,
        ExtendedKeyUsageOID.SERVER_AUTH,
        certificate_policy=True,
    )
    client_leaf = issue_leaf(
        "www.policy.example",
        "www.policy.example",
        server_key,
        main_issuer,
        main_issuer_key,
        ExtendedKeyUsageOID.CLIENT_AUTH,
    )
    unknown_critical_leaf = issue_leaf(
        "www.policy.example",
        "www.policy.example",
        server_key,
        main_issuer,
        main_issuer_key,
        ExtendedKeyUsageOID.SERVER_AUTH,
        unknown_critical_extension=True,
    )
    main_intermediate_cert = x509.load_pem_x509_certificate(
        (out_dir / "verifier-intermediate.pem").read_bytes()
    )
    write_chain(out_dir / "server-auth-chain.pem", server_leaf, main_intermediate_cert)
    write_chain(out_dir / "server-auth-policy-chain.pem", policy_leaf, main_intermediate_cert)
    write_chain(out_dir / "client-auth-chain.pem", client_leaf, main_intermediate_cert)
    write_chain(
        out_dir / "unknown-critical-chain.pem", unknown_critical_leaf, main_intermediate_cert
    )

    restricted_root, restricted_intermediate, restricted_issuer_key, restricted_issuer, _ = write_test_ca_hierarchy(
        out_dir, "NoncriticalNC", 1, name_constraints_critical=False
    )
    restricted_leaf = issue_leaf(
        "www.outside.example",
        "www.outside.example",
        key(),
        restricted_issuer,
        restricted_issuer_key,
        ExtendedKeyUsageOID.SERVER_AUTH,
    )
    restricted_intermediate_cert = x509.load_pem_x509_certificate(restricted_intermediate.read_bytes())
    write_chain(out_dir / "noncritical-nc-chain.pem", restricted_leaf, restricted_intermediate_cert)

    critical_root, critical_intermediate, critical_issuer_key, critical_issuer, _ = write_test_ca_hierarchy(
        out_dir, "CriticalNC", 1, name_constraints_critical=True
    )
    critical_leaf = issue_leaf(
        "www.outside.example",
        "www.outside.example",
        key(),
        critical_issuer,
        critical_issuer_key,
        ExtendedKeyUsageOID.SERVER_AUTH,
    )
    critical_intermediate_cert = x509.load_pem_x509_certificate(critical_intermediate.read_bytes())
    write_chain(out_dir / "critical-nc-chain.pem", critical_leaf, critical_intermediate_cert)

    pathlen_root, pathlen_intermediate, pathlen_issuer_key, pathlen_issuer, _ = write_test_ca_hierarchy(
        out_dir, "PathLenZero", 0
    )
    pathlen_leaf = issue_leaf(
        "www.pathlen.example",
        "www.pathlen.example",
        key(),
        pathlen_issuer,
        pathlen_issuer_key,
        ExtendedKeyUsageOID.SERVER_AUTH,
    )
    pathlen_intermediate_cert = x509.load_pem_x509_certificate(pathlen_intermediate.read_bytes())
    write_chain(out_dir / "pathlen-zero-chain.pem", pathlen_leaf, pathlen_intermediate_cert)

    (out_dir / "noncritical-nc-root.pem").write_bytes(restricted_root.read_bytes())
    (out_dir / "critical-nc-root.pem").write_bytes(critical_root.read_bytes())
    (out_dir / "pathlen-zero-root.pem").write_bytes(pathlen_root.read_bytes())


def make_corrupted_public_chain(public_chain_path: Path, output_path: Path) -> None:
    text = public_chain_path.read_text(encoding="ascii")
    certificates = re.findall(
        r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", text, re.DOTALL
    )
    if len(certificates) < 2:
        raise ValueError("the public chain fixture must contain its leaf and intermediate")
    lines = certificates[1].splitlines()
    body = "".join(line for line in lines if not line.startswith("-----"))
    der = bytearray(base64.b64decode(body))
    der[-1] ^= 0x01
    encoded = base64.b64encode(der).decode("ascii")
    lines_out = ["-----BEGIN CERTIFICATE-----"]
    lines_out.extend(encoded[index : index + 64] for index in range(0, len(encoded), 64))
    lines_out.append("-----END CERTIFICATE-----")
    certificates[1] = "\n".join(lines_out)
    output_path.write_text("\n\n".join(certificates) + "\n", encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--public-chain", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    make_fixture_chains(args.output_dir)
    make_corrupted_public_chain(
        args.public_chain, args.output_dir / "tampered-public-chain.pem"
    )


if __name__ == "__main__":
    main()

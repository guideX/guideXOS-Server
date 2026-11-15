#!/usr/bin/env python3
"""Generate controlled TLS chains for NAVHTTPS19 guest qualification."""

from __future__ import annotations

import argparse
import datetime as dt
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


NOT_BEFORE = dt.datetime(2024, 1, 1)
NOT_AFTER = dt.datetime(2035, 1, 1)


def new_key() -> rsa.RSAPrivateKey:
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def common_name(value: str) -> x509.Name:
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, value)])


def write_key(path: Path, key: rsa.RSAPrivateKey) -> None:
    path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ))


def issue_ca(
    subject: x509.Name,
    subject_key: rsa.RSAPrivateKey,
    issuer: x509.Name,
    issuer_key: rsa.RSAPrivateKey,
    path_length: int,
    constraints: x509.NameConstraints | None = None,
) -> x509.Certificate:
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
    if constraints is not None:
        builder = builder.add_extension(constraints, critical=False)
    return builder.sign(issuer_key, hashes.SHA256())


def issue_server(
    host: str,
    issuer: x509.Name,
    issuer_key: rsa.RSAPrivateKey,
    uri_san: str | None = None,
) -> tuple[x509.Certificate, rsa.RSAPrivateKey]:
    key = new_key()
    sans: list[x509.GeneralName] = [x509.DNSName(host)]
    if uri_san is not None:
        sans.append(x509.UniformResourceIdentifier(uri_san))
    cert = (
        x509.CertificateBuilder()
        .subject_name(common_name(host))
        .issuer_name(issuer)
        .public_key(key.public_key())
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
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .add_extension(x509.SubjectAlternativeName(sans), critical=False)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(issuer_key.public_key()), critical=False)
        .sign(issuer_key, hashes.SHA256())
    )
    return cert, key


def write_profile(
    output: Path,
    name: str,
    root_cert: x509.Certificate,
    root_name: x509.Name,
    root_key: rsa.RSAPrivateKey,
    host: str,
    constraints: x509.NameConstraints,
    uri_san: str | None = None,
) -> None:
    intermediate_key = new_key()
    intermediate_name = common_name(f"NAVHTTPS19 {name} Intermediate")
    intermediate = issue_ca(
        intermediate_name,
        intermediate_key,
        root_name,
        root_key,
        path_length=0,
        constraints=constraints,
    )
    leaf, leaf_key = issue_server(host, intermediate_name, intermediate_key, uri_san)
    (output / f"{name}-root.pem").write_bytes(root_cert.public_bytes(serialization.Encoding.PEM))
    (output / f"{name}-server-chain.pem").write_bytes(
        leaf.public_bytes(serialization.Encoding.PEM) + intermediate.public_bytes(serialization.Encoding.PEM)
    )
    write_key(output / f"{name}-server.key", leaf_key)


def generate(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    root_key = new_key()
    root_name = common_name("NAVHTTPS19 Controlled Guest Root")
    root_cert = issue_ca(root_name, root_key, root_name, root_key, path_length=2)

    write_profile(
        output,
        "allowed",
        root_cert,
        root_name,
        root_key,
        "prod.guidexos.test",
        x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".guidexos.test")],
            excluded_subtrees=None,
        ),
    )
    write_profile(
        output,
        "navhttps18-prohibited",
        root_cert,
        root_name,
        root_key,
        "www.outside.example",
        x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".allowed.example")],
            excluded_subtrees=None,
        ),
    )
    write_profile(
        output,
        "unsupported-uri",
        root_cert,
        root_name,
        root_key,
        "www.outside.example",
        x509.NameConstraints(
            permitted_subtrees=[x509.UniformResourceIdentifier(".example")],
            excluded_subtrees=None,
        ),
        uri_san="https://www.outside.example/navigator",
    )

    print("NAVHTTPS19_GUEST_FIXTURES: PASS profiles=3 constraint_ca=intermediate")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generate(args.output_dir)


if __name__ == "__main__":
    main()

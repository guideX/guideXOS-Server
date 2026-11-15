#!/usr/bin/env python3
"""Generate a bounded, offline Mbed TLS Name Constraints test corpus."""

from __future__ import annotations

import argparse
import datetime as dt
import ipaddress
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID, ObjectIdentifier


NOT_BEFORE = dt.datetime(2024, 1, 1)
NOT_AFTER = dt.datetime(2035, 1, 1)
NC_OID = ObjectIdentifier("2.5.29.30")


def key() -> rsa.RSAPrivateKey:
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def cn(value: str) -> x509.Name:
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, value)])


def pem(cert: x509.Certificate) -> bytes:
    return cert.public_bytes(serialization.Encoding.PEM)


def der_tlv(tag: int, value: bytes) -> bytes:
    length = len(value)
    if length < 128:
        encoded_length = bytes([length])
    else:
        octets = length.to_bytes((length.bit_length() + 7) // 8, "big")
        encoded_length = bytes([0x80 | len(octets)]) + octets
    return bytes([tag]) + encoded_length + value


def raw_ip_constraint(address: str, mask: bytes) -> bytes:
    packed = ipaddress.ip_address(address).packed
    general_name = der_tlv(0x87, packed + mask)
    subtree = der_tlv(0x30, general_name)
    subtrees = der_tlv(0x30, subtree)
    return der_tlv(0x30, der_tlv(0xA0, subtrees))


def raw_malformed_constraint() -> bytes:
    # NameConstraints with a truncated GeneralSubtree base.
    return bytes.fromhex("30 03 A0 01 00")


def issue_ca(
    subject: x509.Name,
    subject_key: rsa.RSAPrivateKey,
    issuer: x509.Name,
    issuer_key: rsa.RSAPrivateKey,
    path_length: int,
    constraints: x509.NameConstraints | None = None,
    constraints_critical: bool = False,
    raw_constraints: bytes | None = None,
    subject_alt_names: list[x509.GeneralName] | None = None,
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
        builder = builder.add_extension(constraints, critical=constraints_critical)
    if raw_constraints is not None:
        builder = builder.add_extension(x509.UnrecognizedExtension(NC_OID, raw_constraints),
                                        critical=constraints_critical)
    if subject_alt_names:
        builder = builder.add_extension(x509.SubjectAlternativeName(subject_alt_names), critical=False)
    return builder.sign(issuer_key, hashes.SHA256())


def issue_leaf(
    subject_key: rsa.RSAPrivateKey,
    issuer: x509.Name,
    issuer_key: rsa.RSAPrivateKey,
    common_name: str,
    san: list[x509.GeneralName] | None,
    subject: x509.Name | None = None,
) -> x509.Certificate:
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject if subject is not None else cn(common_name))
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
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(subject_key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(issuer_key.public_key()), critical=False)
    )
    if san is not None:
        builder = builder.add_extension(x509.SubjectAlternativeName(san), critical=False)
    return builder.sign(issuer_key, hashes.SHA256())


class Corpus:
    def __init__(self, output: Path):
        self.output = output
        self.rows: list[str] = []
        self.root_key = key()
        self.root_name = cn("NAVHTTPS19 Unconstrained Test Root")
        self.root = issue_ca(self.root_name, self.root_key, self.root_name,
                             self.root_key, path_length=3)
        (output / "unconstrained-root.pem").write_bytes(pem(self.root))
        self.leaf_key = key()

    def profile(
        self,
        label: str,
        permitted: list[x509.GeneralName] | None = None,
        excluded: list[x509.GeneralName] | None = None,
        critical: bool = False,
        raw_constraints: bytes | None = None,
        ca_sans: list[x509.GeneralName] | None = None,
        root_constraints: x509.NameConstraints | None = None,
        root_critical: bool = False,
        self_issued: bool = False,
    ) -> tuple[x509.Name, rsa.RSAPrivateKey, x509.Certificate, str]:
        root_key = self.root_key
        root_name = self.root_name
        root_path = "unconstrained-root.pem"
        if root_constraints is not None:
            root_key = key()
            root_name = cn("NAVHTTPS19 Constrained Test Root")
            root = issue_ca(root_name, root_key, root_name, root_key,
                            path_length=3, constraints=root_constraints,
                            constraints_critical=root_critical)
            root_path = f"{label}-root.pem"
            (self.output / root_path).write_bytes(pem(root))

        intermediate_key = key()
        name = root_name if self_issued else cn(f"NAVHTTPS19 {label} Intermediate")
        constraints = None
        if permitted is not None or excluded is not None:
            constraints = x509.NameConstraints(
                permitted_subtrees=permitted,
                excluded_subtrees=excluded,
            )
        intermediate = issue_ca(
            name,
            intermediate_key,
            root_name,
            root_key,
            path_length=0,
            constraints=constraints,
            constraints_critical=critical,
            raw_constraints=raw_constraints,
            subject_alt_names=ca_sans,
        )
        (self.output / f"{label}-intermediate.pem").write_bytes(pem(intermediate))
        return name, intermediate_key, intermediate, root_path

    def cross_signed_cases(self) -> None:
        root_a_key = key()
        root_a_name = cn("NAVHTTPS19 Cross-sign Root A")
        root_a = issue_ca(root_a_name, root_a_key, root_a_name, root_a_key, path_length=3)
        (self.output / "cross-sign-root-a.pem").write_bytes(pem(root_a))

        root_b_key = key()
        root_b_name = cn("NAVHTTPS19 Cross-sign Root B")
        root_b_constraints = x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".allowed.example")],
            excluded_subtrees=None,
        )
        root_b = issue_ca(root_b_name, root_b_key, root_b_name, root_b_key,
                          path_length=3, constraints=root_b_constraints)
        root_b_path = "cross-sign-root-b.pem"
        (self.output / root_b_path).write_bytes(pem(root_b))

        intermediate_key = key()
        intermediate_name = cn("NAVHTTPS19 Cross-signed Intermediate")
        cross_a = issue_ca(intermediate_name, intermediate_key, root_a_name,
                           root_a_key, path_length=0)
        cross_b = issue_ca(intermediate_name, intermediate_key, root_b_name,
                           root_b_key, path_length=0)
        (self.output / "cross-signed-a.pem").write_bytes(pem(cross_a))
        (self.output / "cross-signed-b.pem").write_bytes(pem(cross_b))

        for label, host, expectation in (
            ("cross-signed-path-allowed", "www.allowed.example", "A"),
            ("cross-signed-selected-root-constraint", "www.outside.example", "C"),
        ):
            leaf = issue_leaf(
                self.leaf_key, intermediate_name, intermediate_key, host,
                [x509.DNSName(host)],
            )
            chain_path = f"{label}-chain.pem"
            # The trusted root B can validate only the B cross-sign. A is an
            # alternate, untrusted cross-sign of the same intermediate key.
            (self.output / chain_path).write_bytes(pem(leaf) + pem(cross_b) + pem(cross_a))
            self.rows.append(f"{label}|{chain_path}|{root_b_path}|{host}|{expectation}")

    def case(
        self,
        label: str,
        profile: tuple[x509.Name, rsa.RSAPrivateKey, x509.Certificate, str],
        expectation: str,
        hostname: str = "-",
        sans: list[x509.GeneralName] | None = None,
        subject: x509.Name | None = None,
        include_san: bool = True,
    ) -> None:
        issuer_name, issuer_key, intermediate, root_path = profile
        cert_subject = subject if subject is not None else cn(hostname if hostname != "-" else label)
        leaf = issue_leaf(
            self.leaf_key,
            issuer_name,
            issuer_key,
            hostname if hostname != "-" else label,
            sans if include_san else None,
            subject=cert_subject,
        )
        chain_path = f"{label}-chain.pem"
        (self.output / chain_path).write_bytes(pem(leaf) + pem(intermediate))
        self.rows.append(f"{label}|{chain_path}|{root_path}|{hostname}|{expectation}")

    def write_manifest(self) -> None:
        (self.output / "cases.tsv").write_text("\n".join(self.rows) + "\n", encoding="ascii")


def generate(output: Path) -> None:
    corpus = Corpus(output)

    no_nc = corpus.profile("plain")
    corpus.case("plain-no-name-constraints", no_nc, "A", "www.example.com",
                [x509.DNSName("www.example.com")])

    dns = corpus.profile("dns-permitted", permitted=[x509.DNSName(".allowed.example")])
    corpus.case("dns-permitted-subdomain", dns, "A", "www.allowed.example",
                [x509.DNSName("www.allowed.example")])
    corpus.case("dns-case-insensitive", dns, "A", "WWW.ALLOWED.EXAMPLE",
                [x509.DNSName("WWW.ALLOWED.EXAMPLE")])
    corpus.case("dns-leading-dot-excludes-apex", dns, "C", "allowed.example",
                [x509.DNSName("allowed.example")])
    corpus.case("dns-outside-permitted-subtree", dns, "C", "www.outside.example",
                [x509.DNSName("www.outside.example")])
    corpus.case("dns-cn-fallback-permitted", dns, "A", "www.allowed.example",
                None, include_san=False)
    corpus.case("dns-cn-fallback-prohibited", dns, "C", "www.outside.example",
                None, include_san=False)
    corpus.case("dns-critical-permitted", corpus.profile(
        "dns-critical", permitted=[x509.DNSName(".allowed.example")], critical=True),
        "A", "www.allowed.example", [x509.DNSName("www.allowed.example")])

    exact_domain = corpus.profile("dns-label-boundary", permitted=[x509.DNSName("example.com")])
    corpus.case("dns-permitted-base-and-subtree", exact_domain, "A", "api.example.com",
                [x509.DNSName("api.example.com")])
    corpus.case("dns-label-boundary-confusion", exact_domain, "C", "fakeexample.com",
                [x509.DNSName("fakeexample.com")])

    excluded = corpus.profile("dns-excluded", excluded=[x509.DNSName("blocked.example")])
    corpus.case("dns-excluded-subtree", excluded, "C", "www.blocked.example",
                [x509.DNSName("www.blocked.example")])
    corpus.case("dns-excluded-label-boundary-neighbor", excluded, "A", "notblocked.example",
                [x509.DNSName("notblocked.example")])

    overlapping = corpus.profile(
        "dns-permitted-and-excluded",
        permitted=[x509.DNSName(".example.com")],
        excluded=[x509.DNSName(".blocked.example.com")],
    )
    corpus.case("dns-excluded-wins-over-permitted", overlapping, "C",
                "api.blocked.example.com", [x509.DNSName("api.blocked.example.com")])

    wildcard_excluded = corpus.profile(
        "dns-wildcard-excluded", excluded=[x509.DNSName("bad.allowed.example")]
    )
    corpus.case("dns-wildcard-overlaps-excluded", wildcard_excluded, "C",
                "ok.allowed.example", [x509.DNSName("*.allowed.example")])
    wildcard_permitted = corpus.profile(
        "dns-wildcard-permitted", permitted=[x509.DNSName(".allowed.example")]
    )
    corpus.case("dns-wildcard-entirely-permitted", wildcard_permitted, "A",
                "ok.allowed.example", [x509.DNSName("*.allowed.example")])
    narrow_permitted = corpus.profile(
        "dns-wildcard-not-entirely-permitted",
        permitted=[x509.DNSName(".secure.allowed.example")],
    )
    corpus.case("dns-wildcard-not-entirely-permitted", narrow_permitted, "C",
                "x.example.com", [x509.DNSName("*.example.com")])

    ipv4_permitted = corpus.profile(
        "ipv4-permitted", permitted=[x509.IPAddress(ipaddress.ip_network("198.51.100.0/24"))]
    )
    corpus.case("ipv4-permitted", ipv4_permitted, "A", "-",
                [x509.IPAddress(ipaddress.ip_address("198.51.100.42"))])
    ipv4_excluded = corpus.profile(
        "ipv4-excluded", excluded=[x509.IPAddress(ipaddress.ip_network("198.51.100.42/32"))]
    )
    corpus.case("ipv4-excluded", ipv4_excluded, "C", "-",
                [x509.IPAddress(ipaddress.ip_address("198.51.100.42"))])
    ipv6_permitted = corpus.profile(
        "ipv6-permitted", permitted=[x509.IPAddress(ipaddress.ip_network("2001:db8:1::/48"))]
    )
    corpus.case("ipv6-permitted", ipv6_permitted, "A", "-",
                [x509.IPAddress(ipaddress.ip_address("2001:db8:1::42"))])
    ipv6_excluded = corpus.profile(
        "ipv6-excluded", excluded=[x509.IPAddress(ipaddress.ip_network("2001:db8:1::/48"))]
    )
    corpus.case("ipv6-excluded", ipv6_excluded, "C", "-",
                [x509.IPAddress(ipaddress.ip_address("2001:db8:1::42"))])
    malformed_mask = raw_ip_constraint("192.0.2.0", bytes.fromhex("ff00ff00"))
    corpus.case("ip-malformed-mask-parse-rejected",
                corpus.profile("ip-malformed-mask", raw_constraints=malformed_mask),
                "P", "-", [x509.IPAddress(ipaddress.ip_address("192.0.2.1"))])

    email = corpus.profile("email-permitted", permitted=[x509.RFC822Name(".example.com")])
    corpus.case("email-permitted-subdomain", email, "A", "www.example.com",
                [x509.DNSName("www.example.com"), x509.RFC822Name("user@sub.example.com")])
    corpus.case("email-outside-permitted", email, "C", "www.example.com",
                [x509.DNSName("www.example.com"), x509.RFC822Name("user@other.net")])
    email_excluded = corpus.profile("email-excluded", excluded=[x509.RFC822Name("blocked.example.com")])
    corpus.case("email-excluded-mailbox-host", email_excluded, "C", "www.example.com",
                [x509.DNSName("www.example.com"), x509.RFC822Name("user@blocked.example.com")])

    org = x509.NameAttribute(NameOID.ORGANIZATION_NAME, "GuideX Test Org")
    directory = corpus.profile(
        "directory-permitted",
        permitted=[x509.DirectoryName(x509.Name([org]))],
    )
    corpus.case(
        "directory-subject-permitted", directory, "A", "www.example.com",
        [x509.DNSName("www.example.com")],
        subject=x509.Name([org, x509.NameAttribute(NameOID.COMMON_NAME, "www.example.com")]),
    )
    corpus.case(
        "directory-subject-outside-permitted", directory, "C", "www.example.com",
        [x509.DNSName("www.example.com")],
        subject=x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Other Org"),
                           x509.NameAttribute(NameOID.COMMON_NAME, "www.example.com")]),
    )
    directory_excluded = corpus.profile(
        "directory-excluded", excluded=[x509.DirectoryName(x509.Name([org]))]
    )
    corpus.case(
        "directory-subject-excluded", directory_excluded, "C", "www.example.com",
        [x509.DNSName("www.example.com")],
        subject=x509.Name([org, x509.NameAttribute(NameOID.COMMON_NAME, "www.example.com")]),
    )

    uri_profile = corpus.profile(
        "unsupported-uri-constrained", permitted=[x509.UniformResourceIdentifier(".example.com")]
    )
    corpus.case("unsupported-uri-name-fails-closed", uri_profile, "C", "www.example.com",
                [x509.DNSName("www.example.com"),
                 x509.UniformResourceIdentifier("https://api.example.com/resource")])
    corpus.case("unsupported-uri-form-not-present", uri_profile, "A", "www.example.com",
                [x509.DNSName("www.example.com")])

    malformed = corpus.profile("malformed-asn1", raw_constraints=raw_malformed_constraint())
    corpus.case("malformed-name-constraints-asn1-parse-rejected", malformed, "P",
                "www.example.com", [x509.DNSName("www.example.com")])

    critical_violation = corpus.profile(
        "critical-violation", permitted=[x509.DNSName(".allowed.example")], critical=True
    )
    corpus.case("critical-prohibited-name-rejected", critical_violation, "C",
                "www.outside.example", [x509.DNSName("www.outside.example")])

    root_nc = corpus.profile(
        "root-constraint",
        root_constraints=x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".allowed.example")],
            excluded_subtrees=None,
        ),
    )
    corpus.case("trust-anchor-constraint-applies", root_nc, "C",
                "www.outside.example", [x509.DNSName("www.outside.example")])

    root_nc_ca_violation = corpus.profile(
        "root-constraint-intermediate-violation",
        ca_sans=[x509.DNSName("www.outside.example")],
        root_constraints=x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".allowed.example")],
            excluded_subtrees=None,
        ),
    )
    corpus.case("trust-anchor-constraint-intermediate-violation", root_nc_ca_violation, "C",
                "www.allowed.example", [x509.DNSName("www.allowed.example")])

    nested = corpus.profile(
        "nested-constraints",
        permitted=[x509.DNSName(".secure.example.com")],
        root_constraints=x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".example.com")],
            excluded_subtrees=None,
        ),
    )
    corpus.case("nested-intermediate-constraint-violation", nested, "C",
                "www.example.com", [x509.DNSName("www.example.com")])

    multiple_permitted = corpus.profile(
        "multiple-permitted",
        permitted=[x509.DNSName(".allowed.example"), x509.DNSName(".other.example")],
    )
    corpus.case("multiple-permitted-subtrees-or", multiple_permitted, "A",
                "www.other.example", [x509.DNSName("www.other.example")])

    self_issued = corpus.profile(
        "self-issued-intermediate",
        permitted=[x509.DNSName(".allowed.example")],
        self_issued=True,
    )
    corpus.case("self-issued-intermediate-permitted", self_issued, "A",
                "www.allowed.example", [x509.DNSName("www.allowed.example")])
    corpus.case("self-issued-intermediate-constraint-violation", self_issued, "C",
                "www.outside.example", [x509.DNSName("www.outside.example")])

    corpus.cross_signed_cases()

    corpus.write_manifest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    generate(args.output_dir)
    print(f"Generated {len((args.output_dir / 'cases.tsv').read_text().splitlines())} Name Constraints cases.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Deterministic, deny-by-default Mozilla/NSS trust metadata importer."""

from __future__ import annotations

import argparse
import base64
import binascii
import datetime as dt
import hashlib
import json
import re
import sys
import warnings
from pathlib import Path
from typing import Any

from cryptography import x509
from cryptography.utils import CryptographyDeprecationWarning


SCHEMA_VERSION = "guidexos.navigator.mozilla-trust-manifest.v1"
SOURCE_LOCK_VERSION = "guidexos.mozilla-trust-source-lock.v1"
OVERLAY_VERSION = "guidexos.mozilla-root-restrictions.v1"
NSS_SOURCE_REVISION = "8c60e757c30b1b239bae46532b4f6d4ebb1e1cad"
SUPPORTED_NSS_TRUST = {
    "CKT_NSS_TRUSTED_DELEGATOR",
    "CKT_NSS_NOT_TRUSTED",
    "CKT_NSS_MUST_VERIFY_TRUST",
    "CKT_NSS_TRUST_UNKNOWN",
}
SUPPORTED_CERT_ATTRIBUTES = {
    "CKA_CLASS", "CKA_TOKEN", "CKA_PRIVATE", "CKA_MODIFIABLE", "CKA_LABEL",
    "CKA_CERTIFICATE_TYPE", "CKA_SUBJECT", "CKA_ID", "CKA_ISSUER",
    "CKA_SERIAL_NUMBER", "CKA_VALUE", "CKA_CERT_SHA1_HASH", "CKA_CERT_MD5_HASH",
    "CKA_NSS_MOZILLA_CA_POLICY", "CKA_NSS_SERVER_DISTRUST_AFTER",
    "CKA_NSS_EMAIL_DISTRUST_AFTER",
}
SUPPORTED_TRUST_ATTRIBUTES = {
    "CKA_CLASS", "CKA_TOKEN", "CKA_PRIVATE", "CKA_MODIFIABLE", "CKA_LABEL",
    "CKA_CERT_SHA1_HASH", "CKA_CERT_MD5_HASH", "CKA_ISSUER", "CKA_SERIAL_NUMBER",
    "CKA_TRUST_SERVER_AUTH", "CKA_TRUST_EMAIL_PROTECTION", "CKA_TRUST_CODE_SIGNING",
    "CKA_TRUST_STEP_UP_APPROVED", "CKA_NSS_SERVER_DISTRUST_AFTER",
    "CKA_NSS_EMAIL_DISTRUST_AFTER",
}
PEM_CERTIFICATE_RE = re.compile(
    rb"-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----", re.DOTALL
)


class ImportErrorDetail(ValueError):
    """Input did not satisfy the pinned, unambiguous source contract."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _decode_octets(lines: list[str], source: str) -> bytes:
    joined = " ".join(lines)
    tokens = re.findall(r"\\([0-7]{3})", joined)
    residue = re.sub(r"\\[0-7]{3}|\s+", "", joined)
    if residue:
        raise ImportErrorDetail(f"unsupported octal encoding in {source}")
    return bytes(int(token, 8) for token in tokens)


def _parse_object(block: str, block_number: int) -> dict[str, dict[str, Any]]:
    lines = block.splitlines()
    attrs: dict[str, dict[str, Any]] = {}
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line or line.startswith("#"):
            continue
        match = re.match(r"^(CKA_[A-Z0-9_]+)\s+(\S+)(?:\s+(.*))?$", line)
        if not match:
            continue
        name, kind, value = match.group(1), match.group(2), (match.group(3) or "").strip()
        if name in attrs:
            raise ImportErrorDetail(f"duplicate {name} in NSS object {block_number}")
        if kind == "MULTILINE_OCTAL":
            octal_lines: list[str] = []
            while i < len(lines) and lines[i].strip() != "END":
                octal_lines.append(lines[i].strip())
                i += 1
            if i >= len(lines):
                raise ImportErrorDetail(f"unterminated {name} in NSS object {block_number}")
            i += 1
            parsed_value: Any = _decode_octets(octal_lines, f"{name} object {block_number}")
        elif kind == "UTF8":
            parsed_value = value
            if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
                parsed_value = value[1:-1].replace('\\"', '"').replace('\\\\', '\\')
        else:
            parsed_value = value
        attrs[name] = {"type": kind, "value": parsed_value}
    return attrs


def _attr(obj: dict[str, dict[str, Any]], name: str, default: Any = None) -> Any:
    entry = obj.get(name)
    return default if entry is None else entry["value"]


def _expected_octet_hash(obj: dict[str, dict[str, Any]], name: str, digest: bytes, identity: str) -> None:
    stored = _attr(obj, name)
    if stored is not None and stored != digest:
        raise ImportErrorDetail(f"{name} mismatch for NSS certificate {identity}")


def parse_nss_certdata(data: bytes) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    """Return certs and trust records keyed by lowercase SHA-1 identity."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ImportErrorDetail("NSS certdata is not UTF-8") from exc
    objects: list[dict[str, dict[str, Any]]] = []
    for number, block in enumerate(re.split(r"\n\s*\n", text), start=1):
        if "CKA_CLASS CK_OBJECT_CLASS" in block:
            objects.append(_parse_object(block, number))

    certs_by_sha1: dict[str, dict[str, Any]] = {}
    trust_by_sha1: dict[str, dict[str, Any]] = {}
    classes_seen: set[str] = set()
    for obj in objects:
        object_class = _attr(obj, "CKA_CLASS")
        classes_seen.add(object_class)
        if object_class == "CKO_CERTIFICATE":
            der = _attr(obj, "CKA_VALUE")
            if not isinstance(der, bytes) or not der:
                raise ImportErrorDetail("NSS certificate lacks a DER CKA_VALUE")
            sha1_bytes = hashlib.sha1(der).digest()
            sha1_hex = sha1_bytes.hex()
            identity = sha256(der)
            _expected_octet_hash(obj, "CKA_CERT_SHA1_HASH", sha1_bytes, identity)
            md5_value = hashlib.md5(der).digest()
            _expected_octet_hash(obj, "CKA_CERT_MD5_HASH", md5_value, identity)
            if sha1_hex in certs_by_sha1:
                prior = certs_by_sha1[sha1_hex]
                if prior["der"] != der:
                    raise ImportErrorDetail("SHA-1 identity collision in NSS certificate objects")
                raise ImportErrorDetail(f"duplicate NSS certificate object {identity}")
            certs_by_sha1[sha1_hex] = {"attributes": obj, "der": der, "sha1": sha1_hex, "sha256": identity}
        elif object_class == "CKO_NSS_TRUST":
            sha1_value = _attr(obj, "CKA_CERT_SHA1_HASH")
            if not isinstance(sha1_value, bytes) or len(sha1_value) != 20:
                raise ImportErrorDetail("NSS trust object lacks a valid certificate SHA-1 identity")
            sha1_hex = sha1_value.hex()
            if sha1_hex in trust_by_sha1:
                old = trust_by_sha1[sha1_hex]
                if old != obj:
                    raise ImportErrorDetail(f"contradictory duplicate NSS trust records for {sha1_hex}")
                raise ImportErrorDetail(f"duplicate NSS trust record for {sha1_hex}")
            trust_by_sha1[sha1_hex] = obj
        elif object_class != "CKO_NSS_BUILTIN_ROOT_LIST":
            raise ImportErrorDetail(f"unsupported NSS object class {object_class!r}")

    if not certs_by_sha1 or not trust_by_sha1:
        raise ImportErrorDetail("NSS source contains no certificate/trust pair records")
    if any(sha1 not in certs_by_sha1 for sha1 in trust_by_sha1):
        missing = sorted(sha1 for sha1 in trust_by_sha1 if sha1 not in certs_by_sha1)
        raise ImportErrorDetail(f"orphan NSS trust records: {','.join(missing[:3])}")
    for sha1, trust_obj in trust_by_sha1.items():
        cert_obj = certs_by_sha1[sha1]
        cert_der = cert_obj["der"]
        _expected_octet_hash(trust_obj, "CKA_CERT_MD5_HASH", hashlib.md5(cert_der).digest(), cert_obj["sha256"])
        if _attr(trust_obj, "CKA_CERT_SHA1_HASH") != hashlib.sha1(cert_der).digest():
            raise ImportErrorDetail(f"NSS trust record does not match exact certificate identity {cert_obj['sha256']}")
    return certs_by_sha1, trust_by_sha1


def parse_pem_bundle(data: bytes) -> list[bytes]:
    if not data or len(data) > 512 * 1024:
        raise ImportErrorDetail("candidate PEM is empty or exceeds the 512 KiB cap")
    begin_count = data.count(b"-----BEGIN CERTIFICATE-----")
    end_count = data.count(b"-----END CERTIFICATE-----")
    matches = list(PEM_CERTIFICATE_RE.finditer(data))
    if begin_count == 0 or begin_count != end_count or begin_count != len(matches):
        raise ImportErrorDetail("candidate PEM has mismatched or unsupported certificate blocks")
    residual = PEM_CERTIFICATE_RE.sub(b"", data)
    try:
        residual_text = residual.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ImportErrorDetail("candidate PEM contains non-text data outside certificate blocks") from exc
    if any(not char.isprintable() and char not in "\r\n\t" for char in residual_text):
        raise ImportErrorDetail("candidate PEM contains control data outside certificate blocks")
    ders: list[bytes] = []
    seen: set[str] = set()
    for match in matches:
        encoded = re.sub(rb"\s+", b"", match.group(1))
        try:
            der = base64.b64decode(encoded, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise ImportErrorDetail("candidate PEM contains invalid base64") from exc
        fingerprint = sha256(der)
        if not der or fingerprint in seen:
            raise ImportErrorDetail("candidate PEM contains empty or duplicate certificates")
        seen.add(fingerprint)
        ders.append(der)
    return ders


def _distrust_after(value: Any) -> str | None:
    if value in (None, "CK_FALSE"):
        return None
    if value == "CK_TRUE":
        raise ImportErrorDetail("distrust-after boolean is true but has no timestamp")
    if not isinstance(value, bytes):
        raise ImportErrorDetail("unsupported NSS distrust-after encoding")
    try:
        text = value.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ImportErrorDetail("NSS distrust-after timestamp is not ASCII") from exc
    formats = ("%Y%m%d%H%M%SZ", "%y%m%d%H%M%SZ")
    for fmt in formats:
        try:
            parsed = dt.datetime.strptime(text, fmt).replace(tzinfo=dt.timezone.utc)
            return parsed.strftime("%Y-%m-%dT%H:%M:%SZ")
        except ValueError:
            pass
    raise ImportErrorDetail(f"unsupported NSS distrust-after timestamp {text!r}")


def issued_after_distrust_after(certificate_not_before: dt.datetime, distrust_after: dt.datetime) -> bool:
    """NSS distrust-after rejects certificates whose notBefore is strictly later."""
    if certificate_not_before.tzinfo is None or distrust_after.tzinfo is None:
        raise ValueError("distrust-after comparison requires timezone-aware timestamps")
    return certificate_not_before > distrust_after


def _pem_certificate(der: bytes) -> bytes:
    encoded = base64.b64encode(der).decode("ascii")
    lines = "\n".join(encoded[i : i + 64] for i in range(0, len(encoded), 64))
    return f"-----BEGIN CERTIFICATE-----\n{lines}\n-----END CERTIFICATE-----\n".encode("ascii")


def evaluate_root_eligibility(
    root: dict[str, Any],
    overlay: dict[str, Any],
    fixture_fingerprints: set[str] | None = None,
) -> list[str]:
    """Return stable rejection codes; an empty list means technical eligibility."""
    reasons = list(root.get("source_rejection_reasons", []))
    fingerprint = root["certificate_sha256"]
    if fingerprint in (fixture_fingerprints or set()):
        reasons.append("fixture-root-contamination")
    if not root.get("identity_verified"):
        reasons.append("certificate-identity-not-verified")
    if root.get("mozilla_ca_policy") is not True:
        reasons.append("mozilla-ca-policy-membership-not-affirmed")
    purpose = root.get("server_auth_trust_record")
    if purpose == "CKT_NSS_NOT_TRUSTED":
        reasons.append("explicitly-distrusted-for-server-auth")
    elif purpose != "CKT_NSS_TRUSTED_DELEGATOR":
        reasons.append("tls-server-auth-not-explicitly-permitted")
    if root.get("unknown_nss_fields"):
        reasons.append("unsupported-nss-metadata")
    if root.get("path_validation_status") != "verified-by-mbedtls-4.1.0":
        reasons.append("certificate-path-validation-unverified")
    if root.get("server_distrust_after") and not root.get("server_distrust_after_enforced"):
        reasons.append("server-distrust-after-not-enforced")

    if overlay.get("schema_version") != OVERLAY_VERSION:
        reasons.append("unsupported-external-policy-overlay-version")
        return sorted(set(reasons))
    if overlay.get("complete_for_source_roots") is not True:
        reasons.append("external-policy-snapshot-incomplete")
    policy = overlay.get("records", {}).get(fingerprint)
    if not isinstance(policy, dict):
        reasons.append("root-specific-external-restrictions-missing")
        return sorted(set(reasons))

    if policy.get("trust_purpose") != "tls-server-auth":
        reasons.append("incorrect-server-auth-purpose")
    if policy.get("root_restrictions_status") != "complete":
        reasons.append("root-specific-restrictions-incomplete")
    if policy.get("distrust") not in ("none", "not-distrusted"):
        reasons.append("explicit-external-distrust")
    if policy.get("domain_restrictions_status") not in ("none", "enforced"):
        reasons.append("unsupported-domain-restrictions")
    if policy.get("domain_restrictions_status") == "enforced" and policy.get("domain_enforcement") not in (
        "guidexos-trust-profile", "mbedtls-name-constraints"
    ):
        reasons.append("domain-restrictions-not-enforceable")
    if policy.get("name_constraints_status") not in ("none", "compatible"):
        reasons.append("incompatible-name-constraints")
    if policy.get("name_constraints_status") == "compatible" and policy.get("name_constraints_enforcement") != "mbedtls-4.1.0-navhttps19":
        reasons.append("name-constraints-enforcement-unverified")
    certificate_policy = policy.get("certificate_policy_status")
    if certificate_policy not in ("not-required", "enforced"):
        reasons.append("unsupported-certificate-policy-requirement")
    if certificate_policy == "enforced" and policy.get("certificate_policy_enforcement") != "selected-policy-tree":
        reasons.append("certificate-policy-requirement-not-enforced")
    if policy.get("program_policy_status") != "satisfied":
        reasons.append("root-program-policy-requirements-unverified")
    return sorted(set(reasons))


def _hash_attr(obj: dict[str, dict[str, Any]], name: str) -> str | None:
    value = _attr(obj, name)
    return value.hex() if isinstance(value, bytes) else None


def _root_record(
    der: bytes,
    certs_by_sha1: dict[str, dict[str, Any]],
    trust_by_sha1: dict[str, dict[str, Any]],
    source_revision: str,
) -> dict[str, Any]:
    fingerprint = sha256(der)
    sha1_hex = hashlib.sha1(der).hexdigest()
    nss_cert = certs_by_sha1.get(sha1_hex)
    nss_trust = trust_by_sha1.get(sha1_hex)
    reasons: list[str] = []
    identity_verified = False
    mozilla_ca_policy: bool | None = None
    server_auth_trust: str | None = None
    email_trust: str | None = None
    code_signing_trust: str | None = None
    step_up_approved: str | None = None
    server_distrust_after: str | None = None
    email_distrust_after: str | None = None
    unknown_fields: list[str] = []
    nss_label: str | None = None
    if nss_cert is None:
        reasons.append("nss-certificate-record-missing")
    else:
        identity_verified = nss_cert["der"] == der
        if not identity_verified:
            reasons.append("nss-certificate-der-mismatch")
        attrs = nss_cert["attributes"]
        mozilla_ca_policy = _attr(attrs, "CKA_NSS_MOZILLA_CA_POLICY") == "CK_TRUE"
        unknown_fields.extend(sorted(set(attrs) - SUPPORTED_CERT_ATTRIBUTES))
        nss_label = _attr(attrs, "CKA_LABEL")
        server_distrust_attr = attrs.get("CKA_NSS_SERVER_DISTRUST_AFTER")
        email_distrust_attr = attrs.get("CKA_NSS_EMAIL_DISTRUST_AFTER")
        if server_distrust_attr is None:
            reasons.append("server-distrust-after-metadata-missing")
        if email_distrust_attr is None:
            reasons.append("email-distrust-after-metadata-missing")
        try:
            server_distrust_after = _distrust_after(None if server_distrust_attr is None else server_distrust_attr["value"])
            email_distrust_after = _distrust_after(None if email_distrust_attr is None else email_distrust_attr["value"])
        except ImportErrorDetail:
            reasons.append("unsupported-distrust-after-metadata")
    if nss_trust is None:
        reasons.append("nss-trust-record-missing")
    else:
        server_auth_trust = _attr(nss_trust, "CKA_TRUST_SERVER_AUTH")
        email_trust = _attr(nss_trust, "CKA_TRUST_EMAIL_PROTECTION")
        code_signing_trust = _attr(nss_trust, "CKA_TRUST_CODE_SIGNING")
        step_up_approved = _attr(nss_trust, "CKA_TRUST_STEP_UP_APPROVED")
        if server_auth_trust not in SUPPORTED_NSS_TRUST:
            reasons.append("unsupported-server-auth-trust-value")
        unknown_fields.extend(sorted(set(nss_trust) - SUPPORTED_TRUST_ATTRIBUTES))
        if server_auth_trust == "CKT_NSS_NOT_TRUSTED":
            reasons.append("explicitly-distrusted-for-server-auth")
        elif server_auth_trust != "CKT_NSS_TRUSTED_DELEGATOR":
            reasons.append("tls-server-auth-not-explicitly-permitted")
    if unknown_fields:
        reasons.append("unsupported-nss-metadata")
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", CryptographyDeprecationWarning)
            cert = x509.load_der_x509_certificate(der)
        subject = cert.subject.rfc4514_string()
        issuer = cert.issuer.rfc4514_string()
    except Exception as exc:
        raise ImportErrorDetail(f"candidate certificate {fingerprint} cannot be decoded") from exc
    return {
        "certificate_sha256": fingerprint,
        "certificate_sha1": sha1_hex,
        "subject": subject,
        "issuer": issuer,
        "nss_label": nss_label,
        "source_revision": source_revision,
        "identity_verified": identity_verified,
        "nss_cert_sha1": None if nss_cert is None else nss_cert["sha1"],
        "nss_cert_embedded_sha1": None if nss_cert is None else _hash_attr(nss_cert["attributes"], "CKA_CERT_SHA1_HASH"),
        "nss_trust_cert_sha1": None if nss_trust is None else _hash_attr(nss_trust, "CKA_CERT_SHA1_HASH"),
        "mozilla_ca_policy": mozilla_ca_policy,
        "tls_server_auth_purpose": "tls-server-auth" if server_auth_trust == "CKT_NSS_TRUSTED_DELEGATOR" else "not-granted",
        "server_auth_trust_record": server_auth_trust or "missing",
        "email_protection_trust_record": email_trust,
        "code_signing_trust_record": code_signing_trust,
        "step_up_approved": step_up_approved,
        "server_distrust_after": server_distrust_after,
        "server_distrust_after_enforced": False,
        "email_distrust_after": email_distrust_after if nss_trust is not None else None,
        "name_constraints_enforcement": "Mbed TLS 4.1.0 guideXOS NAVHTTPS19 for DNS/IP/email/directoryName; unsupported applicable forms fail closed",
        "path_validation_status": "unverified-by-mbedtls-per-root",
        "unknown_nss_fields": sorted(set(unknown_fields)),
        "source_rejection_reasons": sorted(set(reasons)),
    }


def build_report(
    certdata: bytes,
    candidate_bundle: bytes,
    overlay: dict[str, Any],
    fixture_fingerprints: set[str] | None = None,
    source_revision: str = NSS_SOURCE_REVISION,
) -> dict[str, Any]:
    certs_by_sha1, trust_by_sha1 = parse_nss_certdata(certdata)
    roots = [_root_record(der, certs_by_sha1, trust_by_sha1, source_revision) for der in parse_pem_bundle(candidate_bundle)]
    for root in roots:
        policy_record = overlay.get("records", {}).get(root["certificate_sha256"])
        if not isinstance(policy_record, dict):
            policy_record = None
        root["external_policy_record"] = policy_record
        root["external_policy_source_revision"] = overlay.get("source_revision")
        root["external_policy_source_sha256"] = overlay.get("source_sha256")
        root["external_restrictions"] = (
            "complete"
            if overlay.get("complete_for_source_roots") is True
            and policy_record is not None
            and policy_record.get("root_restrictions_status") == "complete"
            else "incomplete" if policy_record is not None else "missing"
        )
        root["domain_restrictions_status"] = "missing" if policy_record is None else policy_record.get("domain_restrictions_status", "missing")
        root["name_constraints_status"] = "missing" if policy_record is None else policy_record.get("name_constraints_status", "missing")
        root["certificate_policy_status"] = "missing" if policy_record is None else policy_record.get("certificate_policy_status", "missing")
        root["program_policy_status"] = "missing" if policy_record is None else policy_record.get("program_policy_status", "missing")
        root["certificate_policy_requirements"] = root["certificate_policy_status"]
        root["eligibility_rejection_reasons"] = evaluate_root_eligibility(root, overlay, fixture_fingerprints)
        root["technical_eligibility"] = "eligible" if not root["eligibility_rejection_reasons"] else "ineligible"
        root["candidate_included"] = False
        root["production_approved"] = False
        root["inclusion_rationale"] = (
            "Not included; one or more trust, source-policy, restriction, or verifier requirements is missing or unsupported."
            if root["eligibility_rejection_reasons"]
            else "Eligible for restricted candidate generation only; production approval remains false."
        )
    roots.sort(key=lambda record: record["certificate_sha256"])
    tls_trust_records = [_attr(trust, "CKA_TRUST_SERVER_AUTH") for trust in trust_by_sha1.values()]
    tls_count = tls_trust_records.count("CKT_NSS_TRUSTED_DELEGATOR")
    eligible = [root for root in roots if root["technical_eligibility"] == "eligible"]
    return {
        "schema_version": SCHEMA_VERSION,
        "source_lock_schema_version": SOURCE_LOCK_VERSION,
        "metadata_integrity": "source-files-verified-by-sha256",
        "external_policy_complete": overlay.get("complete_for_source_roots") is True,
        "nss_certificate_object_count": len(certs_by_sha1),
        "nss_trust_object_count": len(trust_by_sha1),
        "nss_tls_server_auth_trust_record_count": tls_count,
        "nss_tls_server_auth_explicit_distrust_record_count": tls_trust_records.count("CKT_NSS_NOT_TRUSTED"),
        "nss_tls_server_auth_must_verify_trust_record_count": tls_trust_records.count("CKT_NSS_MUST_VERIFY_TRUST"),
        "nss_tls_server_auth_unsupported_trust_record_count": sum(1 for value in tls_trust_records if value not in SUPPORTED_NSS_TRUST),
        "input_candidate_bundle_sha256": sha256(candidate_bundle),
        "input_candidate_root_count": len(roots),
        "technically_eligible_root_count": len(eligible),
        "candidate_included_root_count": len(eligible),
        "production_approved_root_count": 0,
        "public_https_enabled": False,
        "candidate_bundle_sha256": None,
        "candidate_bundle_generated": False,
        "roots": roots,
    }


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ImportErrorDetail(f"could not read JSON input {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ImportErrorDetail(f"JSON input must be an object: {path}")
    return value


def verify_pinned_inputs(repo_root: Path, source_lock: dict[str, Any], overlay: dict[str, Any]) -> tuple[bytes, bytes, bytes]:
    if source_lock.get("schema_version") != SOURCE_LOCK_VERSION:
        raise ImportErrorDetail("unsupported source-lock schema")
    sources = source_lock.get("sources", {})
    nss = sources.get("nss_certdata", {})
    policy = sources.get("mozilla_root_store_policy", {})
    wiki = sources.get("mozilla_additional_trust_changes", {})
    curl = sources.get("curl_ca_extract_comparison", {})
    overlay_lock = source_lock.get("external_policy_overlay", {})
    overlay_path = overlay_lock.get("local_path")
    overlay_hash = overlay_lock.get("sha256")
    if not isinstance(overlay_path, str) or not isinstance(overlay_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", overlay_hash):
        raise ImportErrorDetail("external-policy overlay integrity pin is incomplete")
    overlay_data = (repo_root / overlay_path).read_bytes()
    if sha256(overlay_data) != overlay_hash:
        raise ImportErrorDetail("pinned external-policy overlay hash mismatch")
    if json.loads(overlay_data.decode("utf-8")) != overlay:
        raise ImportErrorDetail("external-policy overlay object differs from its pinned file")
    for name, source in (("nss_certdata", nss), ("mozilla_root_store_policy", policy), ("mozilla_additional_trust_changes", wiki)):
        relative = source.get("local_path")
        expected = source.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise ImportErrorDetail(f"source lock entry {name} is incomplete")
        raw = (repo_root / relative).read_bytes()
        if sha256(raw) != expected:
            raise ImportErrorDetail(f"pinned source hash mismatch for {name}")
        if name == "nss_certdata":
            nss_data = raw
        elif name == "mozilla_root_store_policy":
            policy_data = raw
        else:
            wiki_data = raw
    candidate = (repo_root / curl["local_path"]).read_bytes()
    if sha256(candidate) != curl.get("sha256"):
        raise ImportErrorDetail("pinned curl comparison bundle hash mismatch")
    converter_path = curl.get("converter_local_path")
    converter_hash = curl.get("converter_sha256")
    if not isinstance(converter_path, str) or not isinstance(converter_hash, str):
        raise ImportErrorDetail("curl conversion source is not pinned")
    converter_data = (repo_root / converter_path).read_bytes()
    if sha256(converter_data) != converter_hash:
        raise ImportErrorDetail("pinned curl conversion script hash mismatch")
    if overlay.get("schema_version") != OVERLAY_VERSION:
        raise ImportErrorDetail("unsupported external-policy overlay schema")
    if overlay.get("source_sha256") != wiki.get("sha256"):
        raise ImportErrorDetail("external-policy overlay does not bind to its pinned source")
    if policy.get("policy_version") != "3.1" or policy.get("effective_date") != "2026-07-01":
        raise ImportErrorDetail("root-store policy version/date differs from pinned expectation")
    return nss_data, candidate, wiki_data


def collect_fixture_fingerprints(repo_root: Path, source_lock: dict[str, Any]) -> set[str]:
    fingerprints: set[str] = set()
    fixture_files = source_lock.get("fixture_root_files")
    if not isinstance(fixture_files, list) or not fixture_files:
        raise ImportErrorDetail("source lock has no fixture-root inventory")
    for entry in fixture_files:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise ImportErrorDetail("malformed fixture-root inventory entry")
        fixture_path = repo_root / entry["path"]
        data = fixture_path.read_bytes()
        if sha256(data) != entry.get("sha256"):
            raise ImportErrorDetail(f"fixture-root inventory hash mismatch: {entry['path']}")
        for der in parse_pem_bundle(data):
            fingerprints.add(sha256(der))
    return fingerprints


def deterministic_json(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=False) + "\n").encode("utf-8")


def run_import(repo_root: Path, output_path: Path, candidate_pem_path: Path | None = None) -> dict[str, Any]:
    source_dir = repo_root / "assets/certs/mozilla/navhttps20-sources"
    source_lock_path = source_dir / "source-lock.json"
    source_lock_bytes = source_lock_path.read_bytes()
    source_lock = json.loads(source_lock_bytes.decode("utf-8"))
    overlay = load_json(source_dir / "external-policy-overlay.json")
    certdata, candidate, _wiki_data = verify_pinned_inputs(repo_root, source_lock, overlay)
    fixture_fingerprints = collect_fixture_fingerprints(repo_root, source_lock)
    nss_revision = source_lock["sources"]["nss_certdata"].get("revision")
    if nss_revision != NSS_SOURCE_REVISION:
        raise ImportErrorDetail("NSS source revision differs from the importer implementation pin")
    report = build_report(certdata, candidate, overlay, fixture_fingerprints, nss_revision)
    report["source_snapshot_date"] = source_lock["snapshot_date"]
    report["source_lock_sha256"] = sha256(source_lock_bytes)
    report["external_policy_overlay_sha256"] = source_lock["external_policy_overlay"]["sha256"]
    report["source_revisions"] = {
        key: {
            "revision": value.get("revision"),
            "sha256": value.get("sha256"),
            "date": value.get("revision_date_utc", value.get("retrieved_date_utc", value.get("source_date_utc"))),
        }
        for key, value in source_lock["sources"].items()
    }
    report["fixture_root_contamination"] = (
        "detected" if any("fixture-root-contamination" in root["eligibility_rejection_reasons"] for root in report["roots"])
        else "none-detected"
    )
    eligible_roots = [root for root in report["roots"] if root["technical_eligibility"] == "eligible"]
    if eligible_roots:
        if candidate_pem_path is None:
            raise ImportErrorDetail("eligible roots exist; --candidate-pem-output is required")
        by_fingerprint: dict[str, bytes] = {}
        for der in parse_pem_bundle(candidate):
            by_fingerprint[sha256(der)] = der
        pem = b"".join(_pem_certificate(by_fingerprint[root["certificate_sha256"]]) for root in eligible_roots)
        candidate_pem_path.parent.mkdir(parents=True, exist_ok=True)
        candidate_pem_path.write_bytes(pem)
        report["candidate_bundle_generated"] = True
        report["candidate_bundle_sha256"] = sha256(pem)
        report["candidate_bundle_root_count"] = len(eligible_roots)
        for root in eligible_roots:
            root["candidate_included"] = True
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(deterministic_json(report))
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True, help="deterministic trust metadata accounting JSON")
    parser.add_argument("--candidate-pem-output", type=Path, help="required if at least one root is technically eligible")
    args = parser.parse_args(argv)
    try:
        import cryptography

        if cryptography.__version__ != "50.0.2":
            raise ImportErrorDetail("cryptography==50.0.2 is required; install requirements-navhttps20.txt")
        report = run_import(args.repo_root.resolve(), args.output.resolve(), None if args.candidate_pem_output is None else args.candidate_pem_output.resolve())
    except (ImportErrorDetail, OSError, KeyError, TypeError) as exc:
        print(f"NAVHTTPS20_IMPORT_ERROR: {exc}", file=sys.stderr)
        return 2
    print(
        "NAVHTTPS20_IMPORT_RESULT: "
        f"input={report['input_candidate_root_count']} eligible={report['technically_eligible_root_count']} "
        f"candidate={report['candidate_included_root_count']} production_approved=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

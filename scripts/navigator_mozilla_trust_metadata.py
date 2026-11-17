#!/usr/bin/env python3
"""Deterministic, deny-by-default Mozilla/NSS trust metadata importer."""

from __future__ import annotations

import argparse
import base64
import binascii
import csv
import datetime as dt
import hashlib
import io
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
SOURCE_LOCK_V2_VERSION = "guidexos.mozilla-trust-source-lock.v2"
OVERLAY_VERSION = "guidexos.mozilla-root-restrictions.v1"
OVERLAY_V2_VERSION = "guidexos.mozilla-root-restrictions.v2"
DISTRUST_AFTER_POLICY_VERSION = "guidexos.mozilla-server-distrust-after.v2"
NSS_SOURCE_REVISION = "8c60e757c30b1b239bae46532b4f6d4ebb1e1cad"
NAVHTTPS21_DISTRUST_AFTER = {
    "2530cc8e98321502bad96f9b1fba1b099e2d299e0f4548bb914f363bc0d4531f": "2604-01-05T23:59:59Z",
    "40f6af0346a99aa1cd1d555a4e9cce62c7f9634603ee406615833dc8c8d00367": "2611-01-06T23:59:59Z",
    "552f7bdcf1a7af9e6ce672017f4f12abf77240c78e761ac203d1d9d20ac89988": "2609-01-04T23:59:59Z",
    "57de0583efd2b26e0361da99da9df4648def7ee8441c3b728afa9bcde0f9b26a": "2608-06-07T01:00:00Z",
}
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


def _valid_sha256(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _valid_iso_date(value: Any) -> bool:
    if not isinstance(value, str) or re.fullmatch(r"\d{4}-\d{2}-\d{2}", value) is None:
        return False
    try:
        return dt.date.fromisoformat(value).isoformat() == value
    except ValueError:
        return False


def _valid_utc_timestamp(value: Any) -> bool:
    if not isinstance(value, str) or re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", value) is None:
        return False
    try:
        parsed = dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        return False
    return parsed.strftime("%Y-%m-%dT%H:%M:%SZ") == value


def _validate_cutoff_record(value: Any, field_name: str) -> None:
    if not isinstance(value, dict) or value.get("status") not in ("none", "date", "unknown"):
        raise ImportErrorDetail(f"{field_name} must use the none/date/unknown state model")
    status = value["status"]
    timestamp = value.get("utc")
    if status == "date":
        if not _valid_utc_timestamp(timestamp):
            raise ImportErrorDetail(f"{field_name} date must be a valid UTC second-resolution timestamp")
    elif timestamp is not None:
        raise ImportErrorDetail(f"{field_name} has a timestamp that contradicts status {status!r}")


def validate_restrictions_overlay_v2(overlay: dict[str, Any]) -> None:
    """Validate the structured, provenance-bound NAVHTTPS21 restriction overlay."""
    if overlay.get("schema_version") != OVERLAY_V2_VERSION:
        raise ImportErrorDetail("unsupported NAVHTTPS21 external-policy overlay version")
    if not isinstance(overlay.get("source_revision"), str) or not overlay["source_revision"]:
        raise ImportErrorDetail("NAVHTTPS21 external-policy overlay lacks source_revision")
    if not _valid_iso_date(overlay.get("effective_date")):
        raise ImportErrorDetail("NAVHTTPS21 external-policy overlay has an invalid effective_date")
    if overlay.get("coverage_status") not in ("complete", "incomplete", "unknown"):
        raise ImportErrorDetail("NAVHTTPS21 external-policy overlay has an invalid coverage_status")

    sources = overlay.get("sources")
    if not isinstance(sources, list) or not sources:
        raise ImportErrorDetail("NAVHTTPS21 external-policy overlay must identify its source catalog")
    source_ids: set[str] = set()
    for source in sources:
        if not isinstance(source, dict):
            raise ImportErrorDetail("NAVHTTPS21 restriction source entry must be an object")
        source_id = source.get("source_id")
        required = ("owner", "url", "revision", "license", "coverage")
        if not isinstance(source_id, str) or not source_id or source_id in source_ids:
            raise ImportErrorDetail("NAVHTTPS21 restriction source_id is missing or duplicated")
        if any(not isinstance(source.get(name), str) or not source[name] for name in required):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction source {source_id!r} lacks provenance fields")
        if not source["url"].startswith("https://") or not _valid_sha256(source.get("sha256")):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction source {source_id!r} has an invalid URL or SHA-256")
        source_ids.add(source_id)

    records = overlay.get("records")
    if not isinstance(records, dict):
        raise ImportErrorDetail("NAVHTTPS21 restriction records must be an object keyed by exact SHA-256")
    for fingerprint, record in records.items():
        if not _valid_sha256(fingerprint) or not isinstance(record, dict):
            raise ImportErrorDetail("NAVHTTPS21 restriction record has an invalid identity or value type")
        if record.get("certificate_sha256") != fingerprint:
            raise ImportErrorDetail("NAVHTTPS21 restriction record identity differs from its key")
        if not _valid_iso_date(record.get("effective_date")):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid effective date")
        refs = record.get("source_ids")
        if not isinstance(refs, list) or not refs or any(ref not in source_ids for ref in refs):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an unresolved source reference")
        if record.get("trust_purpose") != "tls-server-auth":
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an unsupported trust purpose")
        if record.get("root_restrictions_status") not in ("complete", "incomplete", "unknown"):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid completeness state")
        if record.get("explicit_distrust") not in ("distrusted", "not-distrusted", "unknown"):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid distrust state")
        _validate_cutoff_record(record.get("server_distrust_after"), "server_distrust_after")
        _validate_cutoff_record(record.get("email_distrust_after"), "email_distrust_after")

        domains = record.get("domain_restrictions")
        if not isinstance(domains, dict) or domains.get("status") not in ("none", "restricted", "unknown", "unsupported"):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid domain state")
        domain_values = domains.get("domains")
        if not isinstance(domain_values, list) or any(not isinstance(domain, str) or not domain for domain in domain_values):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has malformed domain values")
        if domains["status"] == "none" and domain_values:
            raise ImportErrorDetail("domain status none contradicts a nonempty domain restriction list")
        if domains["status"] == "restricted" and not domain_values:
            raise ImportErrorDetail("restricted domain state requires at least one domain")

        name_constraints = record.get("name_constraints")
        if not isinstance(name_constraints, dict) or name_constraints.get("status") not in (
            "none", "compatible", "unsupported", "unknown"
        ):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid Name Constraints state")
        policy = record.get("certificate_policy")
        if not isinstance(policy, dict) or policy.get("status") not in (
            "not-required", "required", "unsupported", "unknown"
        ):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid certificate-policy state")
        policy_oids = policy.get("required_oids")
        if not isinstance(policy_oids, list) or any(
            not isinstance(oid, str) or re.fullmatch(r"[0-2](?:\.(?:0|[1-9][0-9]*))+", oid) is None
            for oid in policy_oids
        ):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has malformed policy OIDs")
        if policy["status"] == "not-required" and policy_oids:
            raise ImportErrorDetail("certificate-policy status not-required contradicts required OIDs")
        if policy["status"] == "required" and not policy_oids:
            raise ImportErrorDetail("required certificate-policy state needs at least one OID")
        issuance_evidence = record.get("issuance_evidence")
        if not isinstance(issuance_evidence, dict) or issuance_evidence.get("status") not in (
            "not-required", "required", "unsupported", "unknown"
        ):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid issuance-evidence state")
        evidence_types = issuance_evidence.get("types")
        if not isinstance(evidence_types, list) or any(not isinstance(value, str) or not value for value in evidence_types):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has malformed issuance-evidence types")
        if issuance_evidence["status"] == "not-required" and evidence_types:
            raise ImportErrorDetail("issuance-evidence status not-required contradicts evidence types")
        if issuance_evidence["status"] == "required" and not evidence_types:
            raise ImportErrorDetail("required issuance-evidence state needs at least one evidence type")
        if record.get("program_policy_status") not in ("satisfied", "unsatisfied", "unknown"):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has an invalid program-policy state")
        supported = record.get("supported_enforcement_mechanisms")
        if not isinstance(supported, list) or any(not isinstance(item, str) or not item for item in supported):
            raise ImportErrorDetail(f"NAVHTTPS21 restriction record {fingerprint} has malformed enforcement mechanisms")


def parse_ccadb_v5(data: bytes) -> dict[str, dict[str, Any]]:
    """Parse only identity/status metadata from a pinned CCADB V5 CSV snapshot."""
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise ImportErrorDetail("CCADB V5 snapshot is not UTF-8") from exc
    reader = csv.DictReader(io.StringIO(text, newline=""))
    required_columns = (
        "Certificate Name", "Certificate Record Type", "Mozilla Status", "Status of Root Cert",
        "SHA-256 Fingerprint", "Trust Bits for Root Cert", "EV OIDs for Root Cert",
        "Technically Constrained", "Derived Trust Bits",
    )
    required = set(required_columns)
    if not reader.fieldnames or not required.issubset(reader.fieldnames):
        raise ImportErrorDetail("CCADB V5 snapshot lacks required fingerprint/status columns")
    records: dict[str, list[dict[str, str]]] = {}
    for row in reader:
        raw_fingerprint = row.get("SHA-256 Fingerprint") or ""
        fingerprint = raw_fingerprint.replace(":", "").strip().lower()
        if not _valid_sha256(fingerprint):
            if raw_fingerprint.strip():
                raise ImportErrorDetail("CCADB V5 contains a malformed SHA-256 fingerprint")
            continue
        record = {key: (row.get(key) or "").strip() for key in required_columns}
        record["certificate_sha256"] = fingerprint
        bucket = records.setdefault(fingerprint, [])
        if record not in bucket:
            bucket.append(record)
    if not records:
        raise ImportErrorDetail("CCADB V5 snapshot contains no fingerprinted certificate records")
    return {
        fingerprint: {
            "records": sorted(
                bucket,
                key=lambda record: (
                    record["Certificate Record Type"], record["Certificate Name"], record["Status of Root Cert"],
                    record["Mozilla Status"], record["Trust Bits for Root Cert"], record["EV OIDs for Root Cert"],
                ),
            )
        }
        for fingerprint, bucket in records.items()
    }


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
    if root.get("server_distrust_after") and not root.get("server_distrust_after_notbefore_enforced"):
        reasons.append("server-distrust-after-notbefore-not-enforced")
    if root.get("server_distrust_after") and not root.get("server_distrust_after_sct_enforced"):
        reasons.append("server-distrust-after-sct-timestamp-not-enforced")
    if root.get("server_distrust_after") and not root.get("server_distrust_after_enforced"):
        reasons.append("server-distrust-after-not-enforced")

    overlay_version = overlay.get("schema_version")
    if overlay_version == OVERLAY_V2_VERSION:
        try:
            validate_restrictions_overlay_v2(overlay)
        except ImportErrorDetail:
            reasons.append("invalid-external-policy-overlay")
            return sorted(set(reasons))
        if overlay.get("coverage_status") != "complete":
            reasons.append("external-policy-snapshot-incomplete")
        policy = overlay.get("records", {}).get(fingerprint)
        if not isinstance(policy, dict):
            reasons.append("root-specific-external-restrictions-missing")
            return sorted(set(reasons))
        if policy.get("root_restrictions_status") != "complete":
            reasons.append("root-specific-restrictions-incomplete")
        if policy.get("trust_purpose") != "tls-server-auth":
            reasons.append("incorrect-server-auth-purpose")
        if policy.get("explicit_distrust") == "distrusted":
            reasons.append("explicit-external-distrust")
        elif policy.get("explicit_distrust") != "not-distrusted":
            reasons.append("external-distrust-status-unknown")

        server_cutoff = policy["server_distrust_after"]["status"]
        if server_cutoff == "unknown":
            reasons.append("server-distrust-after-status-unknown")
        elif server_cutoff == "date" and not root.get("server_distrust_after_notbefore_enforced"):
            reasons.append("server-distrust-after-notbefore-not-enforced")
        if server_cutoff == "date" and not root.get("server_distrust_after_sct_enforced"):
            reasons.append("server-distrust-after-sct-timestamp-not-enforced")
        if server_cutoff == "date" and not root.get("server_distrust_after_enforced"):
            reasons.append("server-distrust-after-not-enforced")

        domains = policy["domain_restrictions"]
        if domains["status"] == "unknown":
            reasons.append("domain-restriction-status-unknown")
        elif domains["status"] == "unsupported":
            reasons.append("unsupported-domain-restrictions")
        elif domains["status"] == "restricted":
            if "guidexos-trust-profile" not in policy["supported_enforcement_mechanisms"]:
                reasons.append("domain-restrictions-not-enforceable")

        name_constraints = policy["name_constraints"]
        if name_constraints["status"] == "unknown":
            reasons.append("name-constraints-status-unknown")
        elif name_constraints["status"] == "unsupported":
            reasons.append("incompatible-name-constraints")
        elif name_constraints["status"] == "compatible" and "mbedtls-4.1.0-navhttps19" not in policy["supported_enforcement_mechanisms"]:
            reasons.append("name-constraints-enforcement-unverified")

        certificate_policy = policy["certificate_policy"]
        if certificate_policy["status"] in ("unknown", "unsupported"):
            reasons.append("unsupported-certificate-policy-requirement")
        elif certificate_policy["status"] == "required":
            # Mbed TLS 4.1.0 parses policy OIDs but does not implement RFC 5280
            # policy-tree processing. Overlay text cannot assert an enforcer.
            reasons.append("certificate-policy-requirement-not-enforced")
        issuance_evidence = policy["issuance_evidence"]
        if issuance_evidence["status"] in ("unknown", "unsupported"):
            reasons.append("issuance-evidence-requirement-unknown-or-unsupported")
        elif issuance_evidence["status"] == "required":
            # Navigator does not validate CT SCTs; a source overlay cannot
            # promote a claimed mechanism into runtime enforcement.
            reasons.append("issuance-evidence-not-enforceable")
        if policy.get("program_policy_status") != "satisfied":
            reasons.append("root-program-policy-requirements-unverified")
        return sorted(set(reasons))

    if overlay_version != OVERLAY_VERSION:
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
    enforced_distrust_after: dict[str, str] | None = None,
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
    server_distrust_after_status = "unknown"
    email_distrust_after_status = "unknown"
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
            server_distrust_after_status = (
                "unknown" if server_distrust_attr is None else "none" if server_distrust_after is None else "date"
            )
            email_distrust_after_status = (
                "unknown" if email_distrust_attr is None else "none" if email_distrust_after is None else "date"
            )
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
    enforced_cutoff = (enforced_distrust_after or {}).get(fingerprint)
    notbefore_enforcement_verified = (
        server_distrust_after_status == "date"
        and enforced_cutoff == server_distrust_after
        and NAVHTTPS21_DISTRUST_AFTER.get(fingerprint) == server_distrust_after
    )
    # Mozilla's TLS distrust-after semantics also reject SCT timestamps after
    # the cutoff. Navigator has no SCT verifier, so full enforcement is false.
    sct_enforcement_verified = False
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
        "server_distrust_after_status": server_distrust_after_status,
        "server_distrust_after_notbefore_enforced": notbefore_enforcement_verified,
        "server_distrust_after_sct_enforced": sct_enforcement_verified,
        "server_distrust_after_enforced": notbefore_enforcement_verified and sct_enforcement_verified,
        "email_distrust_after": email_distrust_after if nss_trust is not None else None,
        "email_distrust_after_status": email_distrust_after_status,
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
    ccadb_records: dict[str, dict[str, Any]] | None = None,
    enforced_distrust_after: dict[str, str] | None = None,
    source_lock_schema_version: str = SOURCE_LOCK_VERSION,
) -> dict[str, Any]:
    certs_by_sha1, trust_by_sha1 = parse_nss_certdata(certdata)
    roots = [
        _root_record(der, certs_by_sha1, trust_by_sha1, source_revision, enforced_distrust_after)
        for der in parse_pem_bundle(candidate_bundle)
    ]
    for root in roots:
        policy_record = overlay.get("records", {}).get(root["certificate_sha256"])
        if not isinstance(policy_record, dict):
            policy_record = None
        root["external_policy_record"] = policy_record
        root["external_policy_source_revision"] = overlay.get("source_revision")
        root["external_policy_source_sha256"] = overlay.get("source_sha256")
        if overlay.get("schema_version") == OVERLAY_V2_VERSION:
            root["external_policy_sources"] = overlay.get("sources", [])
        if overlay.get("schema_version") == OVERLAY_V2_VERSION:
            root["external_restrictions"] = (
                "complete" if policy_record is not None and policy_record.get("root_restrictions_status") == "complete"
                else "incomplete" if policy_record is not None
                else "unknown"
            )
            root["domain_restrictions_status"] = (
                "unknown" if policy_record is None else policy_record["domain_restrictions"]["status"]
            )
            root["name_constraints_status"] = (
                "unknown" if policy_record is None else policy_record["name_constraints"]["status"]
            )
            root["certificate_policy_status"] = (
                "unknown" if policy_record is None else policy_record["certificate_policy"]["status"]
            )
            root["program_policy_status"] = (
                "unknown" if policy_record is None else policy_record["program_policy_status"]
            )
        else:
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
        ccadb_snapshot_record = (ccadb_records or {}).get(root["certificate_sha256"])
        ccadb_rows = [] if ccadb_snapshot_record is None else ccadb_snapshot_record.get("records", [])
        ccadb_root_rows = [row for row in ccadb_rows if row.get("Certificate Record Type") == "Root Certificate"]
        if len(ccadb_root_rows) == 1:
            root["ccadb_record_status"] = "matched-exact-root-fingerprint"
            root["ccadb_record"] = ccadb_root_rows[0]
        elif len(ccadb_root_rows) > 1:
            root["ccadb_record_status"] = "ambiguous-multiple-root-records"
            root["ccadb_record"] = {"records": ccadb_root_rows}
        elif ccadb_rows:
            root["ccadb_record_status"] = "matched-non-root-record-only"
            root["ccadb_record"] = {"records": ccadb_rows}
        else:
            root["ccadb_record_status"] = "unknown"
            root["ccadb_record"] = None
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
        "source_lock_schema_version": source_lock_schema_version,
        "metadata_integrity": "source-files-verified-by-sha256",
        "external_policy_complete": (
            overlay.get("complete_for_source_roots") is True
            or overlay.get("coverage_status") == "complete"
        ),
        "external_policy_coverage_status": overlay.get("coverage_status", "complete" if overlay.get("complete_for_source_roots") is True else "incomplete"),
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
        "nss_trust_value_interpretation": {
            "CKT_NSS_TRUSTED_DELEGATOR": "explicitly trusted for this specific NSS purpose; not sufficient alone for root eligibility",
            "CKT_NSS_MUST_VERIFY_TRUST": "not the generic trusted-delegator value; does not grant server-auth root eligibility",
            "eligibility_requires_exact_der_and_complete_restriction_coverage": True,
        },
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


def _verified_navhttps21_inputs(
    repo_root: Path,
    source_lock_path: Path,
) -> tuple[dict[str, Any], bytes, bytes, dict[str, Any], dict[str, dict[str, Any]], dict[str, str], bytes]:
    lock_bytes = source_lock_path.read_bytes()
    lock = json.loads(lock_bytes.decode("utf-8"))
    if not isinstance(lock, dict) or lock.get("schema_version") != SOURCE_LOCK_V2_VERSION:
        raise ImportErrorDetail("unsupported NAVHTTPS21 source-lock schema")
    if not _valid_iso_date(lock.get("snapshot_date")):
        raise ImportErrorDetail("NAVHTTPS21 source lock has an invalid snapshot date")

    baseline = lock.get("baseline_source_lock")
    if not isinstance(baseline, dict) or not isinstance(baseline.get("local_path"), str) or not _valid_sha256(baseline.get("sha256")):
        raise ImportErrorDetail("NAVHTTPS21 source lock lacks a valid NAVHTTPS20 baseline pin")
    baseline_path = repo_root / baseline["local_path"]
    baseline_bytes = baseline_path.read_bytes()
    if sha256(baseline_bytes) != baseline["sha256"]:
        raise ImportErrorDetail("NAVHTTPS20 baseline source-lock hash mismatch")
    baseline_lock = json.loads(baseline_bytes.decode("utf-8"))
    nav20_overlay = load_json(repo_root / "assets/certs/mozilla/navhttps20-sources/external-policy-overlay.json")
    certdata, candidate, _wiki_data = verify_pinned_inputs(repo_root, baseline_lock, nav20_overlay)

    ccadb = lock.get("ccadb_v5_all_certificate_records")
    if not isinstance(ccadb, dict) or not isinstance(ccadb.get("local_path"), str) or not _valid_sha256(ccadb.get("sha256")):
        raise ImportErrorDetail("NAVHTTPS21 source lock lacks a valid CCADB V5 snapshot pin")
    ccadb_bytes = (repo_root / ccadb["local_path"]).read_bytes()
    if sha256(ccadb_bytes) != ccadb["sha256"]:
        raise ImportErrorDetail("pinned CCADB V5 snapshot hash mismatch")
    if ccadb.get("revision") != f"downloaded-snapshot-{lock['snapshot_date']}" or ccadb.get("retrieved_date_utc") != lock["snapshot_date"]:
        raise ImportErrorDetail("CCADB V5 snapshot revision/date differs from its source lock")
    ccadb_records = parse_ccadb_v5(ccadb_bytes)

    overlay_lock = lock.get("root_restrictions_overlay")
    if not isinstance(overlay_lock, dict) or not isinstance(overlay_lock.get("local_path"), str) or not _valid_sha256(overlay_lock.get("sha256")):
        raise ImportErrorDetail("NAVHTTPS21 root-restrictions overlay pin is incomplete")
    overlay_bytes = (repo_root / overlay_lock["local_path"]).read_bytes()
    if sha256(overlay_bytes) != overlay_lock["sha256"]:
        raise ImportErrorDetail("pinned NAVHTTPS21 root-restrictions overlay hash mismatch")
    overlay = json.loads(overlay_bytes.decode("utf-8"))
    validate_restrictions_overlay_v2(overlay)
    if overlay.get("effective_date") != lock["snapshot_date"]:
        raise ImportErrorDetail("NAVHTTPS21 overlay effective date differs from the pinned snapshot")

    base_sources = baseline_lock["sources"]
    source_by_id = {source["source_id"]: source for source in overlay["sources"]}
    expected_source_bindings = {
        "mozilla-additional-trust-changes": base_sources["mozilla_additional_trust_changes"]["sha256"],
        "mozilla-root-store-policy": base_sources["mozilla_root_store_policy"]["sha256"],
        "ccadb-v5": ccadb["sha256"],
    }
    for source_id, expected_hash in expected_source_bindings.items():
        source = source_by_id.get(source_id)
        if source is None or source.get("sha256") != expected_hash:
            raise ImportErrorDetail(f"NAVHTTPS21 restriction overlay source binding mismatch: {source_id}")

    enforcement_lock = lock.get("distrust_after_enforcement_policy")
    if not isinstance(enforcement_lock, dict) or not isinstance(enforcement_lock.get("local_path"), str) or not _valid_sha256(enforcement_lock.get("sha256")):
        raise ImportErrorDetail("NAVHTTPS21 distrust-after enforcement policy pin is incomplete")
    enforcement_bytes = (repo_root / enforcement_lock["local_path"]).read_bytes()
    if sha256(enforcement_bytes) != enforcement_lock["sha256"]:
        raise ImportErrorDetail("pinned NAVHTTPS21 distrust-after enforcement policy hash mismatch")
    enforcement = json.loads(enforcement_bytes.decode("utf-8"))
    if not isinstance(enforcement, dict) or enforcement.get("schema_version") != DISTRUST_AFTER_POLICY_VERSION:
        raise ImportErrorDetail("unsupported NAVHTTPS21 distrust-after policy schema")
    if enforcement.get("source_revision") != base_sources["nss_certdata"].get("revision") or enforcement.get("source_sha256") != base_sources["nss_certdata"].get("sha256"):
        raise ImportErrorDetail("NAVHTTPS21 distrust-after policy does not bind to pinned NSS certdata")
    if enforcement.get("trust_purpose") != "tls-server-auth" or enforcement.get("supported_enforcement_mechanism") != enforcement_lock.get("mechanism"):
        raise ImportErrorDetail("NAVHTTPS21 distrust-after policy purpose/mechanism is inconsistent")
    if enforcement.get("tls_sct_timestamp_check") != "unsupported" or enforcement.get("full_tls_enforcement_status") != "partial-notbefore-only":
        raise ImportErrorDetail("NAVHTTPS21 distrust-after policy overstates TLS SCT enforcement")
    policy_records = enforcement.get("records")
    if not isinstance(policy_records, list):
        raise ImportErrorDetail("NAVHTTPS21 distrust-after records must be an array")
    enforced: dict[str, str] = {}
    for record in policy_records:
        if not isinstance(record, dict):
            raise ImportErrorDetail("NAVHTTPS21 distrust-after record must be an object")
        fingerprint = record.get("certificate_sha256")
        cutoff = record.get("cutoff_utc")
        if not _valid_sha256(fingerprint) or not _valid_utc_timestamp(cutoff):
            raise ImportErrorDetail("NAVHTTPS21 distrust-after record has malformed identity or cutoff")
        if record.get("tls_server_auth_trusted") is not True or record.get("enforcement_status") != "notbefore-only":
            raise ImportErrorDetail("NAVHTTPS21 distrust-after record has an unsupported enforcement status")
        if fingerprint in enforced:
            raise ImportErrorDetail("duplicate NAVHTTPS21 distrust-after fingerprint")
        enforced[fingerprint] = cutoff
    if enforced != NAVHTTPS21_DISTRUST_AFTER:
        raise ImportErrorDetail("NAVHTTPS21 compiled and pinned distrust-after tables differ")

    report = build_report(
        certdata,
        candidate,
        overlay,
        collect_fixture_fingerprints(repo_root, baseline_lock),
        base_sources["nss_certdata"]["revision"],
        ccadb_records,
        enforced,
        SOURCE_LOCK_V2_VERSION,
    )
    # Compare every active cutoff from the 118-root comparison bundle with the
    # pinned runtime table. A cutoff missing from code cannot silently become an
    # unrestricted candidate.
    observed = {
        root["certificate_sha256"]: root["server_distrust_after"]
        for root in report["roots"]
        if root["server_distrust_after_status"] == "date"
    }
    if observed != enforced:
        raise ImportErrorDetail("NSS distrust-after records differ from the compiled NAVHTTPS21 enforcement table")

    report["source_snapshot_date"] = lock["snapshot_date"]
    report["source_lock_sha256"] = sha256(lock_bytes)
    report["baseline_source_lock_sha256"] = baseline["sha256"]
    report["external_policy_overlay_sha256"] = overlay_lock["sha256"]
    report["distrust_after_enforcement_policy_sha256"] = enforcement_lock["sha256"]
    report["ccadb_snapshot_sha256"] = ccadb["sha256"]
    report["ccadb_snapshot_record_count"] = len(ccadb_records)
    report["source_revisions"] = {
        key: {
            "revision": value.get("revision"),
            "sha256": value.get("sha256"),
            "date": value.get("revision_date_utc", value.get("retrieved_date_utc", value.get("source_date_utc"))),
        }
        for key, value in base_sources.items()
    }
    report["source_revisions"]["ccadb_v5_all_certificate_records"] = {
        "revision": ccadb["revision"],
        "sha256": ccadb["sha256"],
        "date": ccadb["retrieved_date_utc"],
        "license": ccadb["license"],
    }
    report["restriction_coverage_note"] = (
        "CCADB V5 is used for exact fingerprint and Mozilla root-status cross-checks. Its aggregate trust bits and blank fields do not establish absence of Mozilla restrictions."
    )
    return lock, candidate, report, ccadb_records, enforced, overlay_bytes, enforcement_bytes


def run_import_navhttps21(
    repo_root: Path,
    output_path: Path,
    source_lock_path: Path,
    candidate_pem_path: Path | None = None,
) -> dict[str, Any]:
    _lock, candidate, report, _ccadb, _enforced, _overlay, _enforcement = _verified_navhttps21_inputs(
        repo_root, source_lock_path
    )
    eligible_roots = [root for root in report["roots"] if root["technical_eligibility"] == "eligible"]
    if eligible_roots:
        if candidate_pem_path is None:
            raise ImportErrorDetail("eligible roots exist; --candidate-pem-output is required")
        by_fingerprint = {sha256(der): der for der in parse_pem_bundle(candidate)}
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


def run_import(
    repo_root: Path,
    output_path: Path,
    candidate_pem_path: Path | None = None,
    source_lock_path: Path | None = None,
) -> dict[str, Any]:
    if source_lock_path is not None:
        if not source_lock_path.is_absolute():
            source_lock_path = repo_root / source_lock_path
        if json.loads(source_lock_path.read_text(encoding="utf-8")).get("schema_version") == SOURCE_LOCK_V2_VERSION:
            return run_import_navhttps21(repo_root, output_path, source_lock_path, candidate_pem_path)
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
    parser.add_argument("--source-lock", type=Path, help="optional pinned source lock; NAVHTTPS21 v2 extends the accepted NAVHTTPS20 inputs")
    args = parser.parse_args(argv)
    try:
        import cryptography

        if cryptography.__version__ != "50.0.2":
            raise ImportErrorDetail("cryptography==50.0.2 is required; install requirements-navhttps20.txt")
        report = run_import(
            args.repo_root.resolve(), args.output.resolve(),
            None if args.candidate_pem_output is None else args.candidate_pem_output.resolve(),
            None if args.source_lock is None else args.source_lock.resolve(),
        )
    except (ImportErrorDetail, OSError, KeyError, TypeError) as exc:
        print(f"NAVHTTPS20_IMPORT_ERROR: {exc}", file=sys.stderr)
        return 2
    result_label = (
        "NAVHTTPS21_IMPORT_RESULT"
        if report.get("source_lock_schema_version") == SOURCE_LOCK_V2_VERSION
        else "NAVHTTPS20_IMPORT_RESULT"
    )
    print(
        f"{result_label}: input={report['input_candidate_root_count']} "
        f"eligible={report['technically_eligible_root_count']} "
        f"candidate={report['candidate_included_root_count']} production_approved=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

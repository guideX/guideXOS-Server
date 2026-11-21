#!/usr/bin/env python3
"""Build the immutable, one-root NAVHTTPS24 guest qualification manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
from typing import Any

from cryptography import x509
from cryptography.hazmat.primitives.serialization import Encoding


ROOT = Path(__file__).resolve().parents[1]
FINGERPRINT = "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05"
OUTPUT = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps24-qualification.json"
SOURCE_MANIFEST_SHA256 = "8fb3889404f5c632f67f51ef517ff025d8e49b2e9f0625ccd0fe2fa160be6790"
SIDECAR_SHA256 = "e54283023e73f1b7fd02d3b3f81be2ecf4d3810322e315b4a66b43c2794660e1"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def canonical_json(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def validate_manifest_artifacts(manifest: dict[str, Any], repo_root: Path = ROOT) -> None:
    """Fail closed if the one-root scope, pinned metadata, or artifacts drift."""
    candidate = manifest.get("candidate", {})
    restrictions = manifest.get("restriction_metadata", {})
    activation = manifest.get("activation_gates", {})
    if manifest.get("schema_version") != "guidexos.navhttps24.g5-disabled-candidate-qualification.v1":
        raise ValueError("unsupported NAVHTTPS24 candidate manifest schema")
    if candidate.get("root_sha256") != FINGERPRINT or candidate.get("bundle_root_count") != 1:
        raise ValueError("NAVHTTPS24 candidate identity or root count mismatch")
    if candidate.get("trust_purpose") != "tls-server-auth" or candidate.get("candidate_included") is not True:
        raise ValueError("NAVHTTPS24 candidate purpose or inclusion metadata mismatch")
    if candidate.get("candidate_enabled") is not False or candidate.get("production_approved") is not False or candidate.get("public_https_enabled") is not False:
        raise ValueError("NAVHTTPS24 candidate activation gates must remain false")
    if restrictions.get("source_manifest_sha256") != SOURCE_MANIFEST_SHA256 or restrictions.get("selected_root_fingerprints") != [FINGERPRINT]:
        raise ValueError("NAVHTTPS23 selected-root manifest binding mismatch")
    if restrictions.get("candidate_sidecar_sha256") != SIDECAR_SHA256:
        raise ValueError("NAVHTTPS23 candidate sidecar binding mismatch")
    if restrictions.get("other_public_roots_included") != 0 or restrictions.get("other_public_roots_ineligible") != 117:
        raise ValueError("NAVHTTPS24 root scope must contain only G5")
    if restrictions.get("coverage_scope") != "selected-root-set" or restrictions.get("selected_root_attributes_complete") != 11:
        raise ValueError("NAVHTTPS23 restriction metadata is incomplete or out of scope")
    if (
        restrictions.get("external_restrictions") != "complete"
        or restrictions.get("domain_restrictions") != "none-identified-for-selected-root"
        or restrictions.get("name_constraints") != "none-on-root; path enforcement retained"
        or restrictions.get("ordinary_certificate_policy") != "not-required"
        or restrictions.get("ev_supported") is not False
        or restrictions.get("ev_claimed") is not False
        or restrictions.get("full_rfc5280_policy_tree") != "unsupported-and-disabled"
        or restrictions.get("distrust_after_policy") != "NAVHTTPS21 enforcement retained; no G5 cutoff identified"
    ):
        raise ValueError("NAVHTTPS23 policy and restriction scope must remain exact and unsupported features disabled")
    profile = manifest.get("runtime_profile", {})
    if profile.get("profile_class") != "candidate" or profile.get("activation_scope") != "disabled-offline-qualification-only":
        raise ValueError("NAVHTTPS24 candidate profile scope mismatch")
    if profile.get("clock_assurance") != "authenticated-fresh-rollback-protected-required" or profile.get("offline_time_scope") != "certificate-validity-test-only; not clock evidence or authorization":
        raise ValueError("NAVHTTPS24 time-assurance scope mismatch")
    if activation.get("release_owner_authorized") is not False or activation.get("fixture_time_promoted") is not False:
        raise ValueError("NAVHTTPS24 must not imply release or time authorization")
    bundle = repo_root / candidate.get("bundle_path", "")
    if not bundle.is_file() or sha256(bundle) != candidate.get("bundle_sha256"):
        raise ValueError("NAVHTTPS24 candidate bundle integrity mismatch")
    pem = bundle.read_bytes()
    if len(re.findall(rb"-----BEGIN CERTIFICATE-----", pem)) != 1 or len(re.findall(rb"-----END CERTIFICATE-----", pem)) != 1:
        raise ValueError("NAVHTTPS24 candidate bundle contains an additional or incomplete certificate")
    try:
        der = x509.load_pem_x509_certificate(pem).public_bytes(Encoding.DER)
    except Exception as exc:
        raise ValueError("NAVHTTPS24 candidate bundle does not contain one valid certificate") from exc
    if hashlib.sha256(der).hexdigest() != FINGERPRINT:
        raise ValueError("NAVHTTPS24 candidate certificate fingerprint mismatch")
    pinned_sources = {
        "source_manifest_sha256": "assets/certs/candidates/mozilla-nss-2026-11-19.navhttps23-trust-manifest.json",
        "candidate_sidecar_sha256": "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.metadata.json",
        "source_lock_sha256": "assets/certs/mozilla/navhttps23-sources/source-lock.json",
        "restriction_overlay_sha256": "assets/certs/mozilla/navhttps23-sources/root-restrictions-overlay.json",
    }
    for field, relative_path in pinned_sources.items():
        path = repo_root / relative_path
        if not path.is_file() or sha256(path) != restrictions.get(field):
            raise ValueError(f"NAVHTTPS23 source evidence integrity mismatch: {relative_path}")
    capture = manifest.get("public_chain_capture", {})
    capture_manifest = repo_root / capture.get("capture_manifest_path", "")
    if not capture_manifest.is_file() or sha256(capture_manifest) != capture.get("capture_manifest_sha256"):
        raise ValueError("NAVHTTPS24 public-chain capture manifest integrity mismatch")
    for certificate in capture.get("presented_certificates", []):
        path = repo_root / certificate.get("path", "")
        if not path.is_file() or path.stat().st_size != certificate.get("bytes") or sha256(path) != certificate.get("sha256"):
            raise ValueError(f"NAVHTTPS24 captured DER integrity mismatch: {certificate.get('path')}")
    for name, row in capture.get("chain_files", {}).items():
        path = repo_root / "tests/fixtures/navhttps24" / name
        if not path.is_file() or path.stat().st_size != row.get("bytes") or sha256(path) != row.get("sha256"):
            raise ValueError(f"NAVHTTPS24 captured chain integrity mismatch: {path}")
    for fixture in manifest.get("qualification_fixtures", {}).values():
        path = repo_root / fixture.get("path", "")
        if not path.is_file() or path.stat().st_size != fixture.get("bytes") or sha256(path) != fixture.get("sha256"):
            raise ValueError(f"NAVHTTPS24 fixture integrity mismatch: {fixture.get('path')}")


def build_manifest() -> dict[str, Any]:
    sidecar_path = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.metadata.json"
    source_manifest_path = ROOT / "assets/certs/candidates/mozilla-nss-2026-11-19.navhttps23-trust-manifest.json"
    overlay_path = ROOT / "assets/certs/mozilla/navhttps23-sources/root-restrictions-overlay.json"
    source_lock_path = ROOT / "assets/certs/mozilla/navhttps23-sources/source-lock.json"
    bundle_path = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.pem"
    capture_path = ROOT / "tests/fixtures/navhttps24/capture-manifest.json"
    capture = load_json(capture_path)
    sidecar = load_json(sidecar_path)
    source_manifest = load_json(source_manifest_path)
    overlay = load_json(overlay_path)

    certificates = []
    for der_path in sorted((ROOT / "tests/fixtures/navhttps24").glob("presented-*.der")):
        der = der_path.read_bytes()
        certificate = x509.load_der_x509_certificate(der)
        certificates.append({
            "path": der_path.relative_to(ROOT).as_posix(),
            "bytes": len(der),
            "sha256": hashlib.sha256(der).hexdigest(),
            "subject": certificate.subject.rfc4514_string(),
            "issuer": certificate.issuer.rfc4514_string(),
        })

    root_record = next(
        row for row in source_manifest["roots"] if row["certificate_sha256"] == FINGERPRINT
    )
    restriction_record = overlay["records"][FINGERPRINT]
    if len(certificates) != 5 or certificates[2]["sha256"] != FINGERPRINT:
        raise SystemExit("Live NAVHTTPS24 capture does not contain the exact selected G5 DER at index 2.")
    if sidecar["candidate"]["sha256"] != FINGERPRINT:
        raise SystemExit("NAVHTTPS23 sidecar does not bind the exact G5 fingerprint.")
    if len(source_manifest["selected_root_fingerprints"]) != 1 or source_manifest["selected_root_fingerprints"][0] != FINGERPRINT:
        raise SystemExit("NAVHTTPS23 source manifest does not select exactly the G5 root.")
    if source_manifest["candidate_included_root_count"] != 1 or root_record["candidate_included"] is not True:
        raise SystemExit("NAVHTTPS23 candidate manifest no longer describes exactly one included root.")
    if root_record["production_approved"] is not False or source_manifest["production_approved_root_count"] != 0:
        raise SystemExit("NAVHTTPS23 candidate is no longer marked unapproved.")
    if sidecar["activation"]["candidate_enabled"] or sidecar["activation"]["production_approved"] or sidecar["activation"]["public_https_enabled"]:
        raise SystemExit("NAVHTTPS23 activation gates must remain closed.")
    if restriction_record["root_restrictions_status"] != "complete" or any(
        entry["status"] != "complete" for entry in restriction_record["attribute_coverage"].values()
    ):
        raise SystemExit("NAVHTTPS23 selected-root restriction coverage is incomplete.")
    if capture["hostname"] != "digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com" or capture["presented_certificate_count"] != 5:
        raise SystemExit("NAVHTTPS24 live capture endpoint or chain cardinality changed.")
    if capture["certificates"][2]["sha256"] != FINGERPRINT or capture["certificates"][0]["subject_alt_name_dns"] != [capture["hostname"]]:
        raise SystemExit("NAVHTTPS24 live capture leaf or exact presented G5 root identity changed.")

    fixture_paths = {
        "expired_leaf_chain": "tests/fixtures/navhttps23/expired_g5_chain.pem",
        "synthetic_invalid_leaf_signature_chain": "tests/fixtures/navhttps23/tampered_leaf_chain.pem",
        "synthetic_invalid_intermediate_signature_chain": "tests/fixtures/navhttps23/tampered_issuing_ca_chain.pem",
        "valid_exact_g5_path": "tests/fixtures/navhttps24/g5-self-signed-path.pem",
        "valid_global_g3_cross_signed_path": "tests/fixtures/navhttps24/global-g3-cross-signed-path.pem",
        "presented_peer_chain": "tests/fixtures/navhttps24/presented-all-chain.pem",
        "wrong_anchor_global_g3_root_der": "tests/fixtures/navhttps24/presented-4.der",
    }
    fixtures = {
        name: {"path": path, "bytes": (ROOT / path).stat().st_size, "sha256": sha256(ROOT / path)}
        for name, path in fixture_paths.items()
    }
    if sha256(bundle_path) != sidecar["artifacts"]["candidate_bundle_sha256"]:
        raise SystemExit("NAVHTTPS23 candidate bundle bytes disagree with the accepted sidecar.")

    result = {
        "schema_version": "guidexos.navhttps24.g5-disabled-candidate-qualification.v1",
        "candidate": {
            "name": "DigiCert TLS ECC P384 Root G5",
            "root_sha256": FINGERPRINT,
            "bundle_path": bundle_path.relative_to(ROOT).as_posix(),
            "bundle_sha256": sha256(bundle_path),
            "bundle_root_count": 1,
            "trust_purpose": "tls-server-auth",
            "candidate_included": True,
            "candidate_enabled": False,
            "production_approved": False,
            "public_https_enabled": False,
        },
        "runtime_profile": {
            "trust_profile_schema_version": "guidexos.navigator.trust-profile.v1",
            "bundle_manifest_schema_version": "guidexos.navigator.ca-bundle-manifest.v0.1",
            "profile_class": "candidate",
            "profile_id": "navigator-candidate-navhttps24-digicert-g5",
            "activation_scope": "disabled-offline-qualification-only",
            "clock_assurance": "authenticated-fresh-rollback-protected-required",
            "offline_evaluation_time_unix": 1795089600,
            "offline_evaluation_time_utc": "2026-11-19T12:00:00Z",
            "offline_time_scope": "certificate-validity-test-only; not clock evidence or authorization",
            "revocation_policy": "owner-decision-pending-no-crl-ocsp-or-stapled-status-enforced",
        },
        "restriction_metadata": {
            "source_manifest_schema_version": source_manifest["schema_version"],
            "source_manifest_sha256": sha256(source_manifest_path),
            "candidate_sidecar_schema_version": sidecar["schema_version"],
            "candidate_sidecar_sha256": sha256(sidecar_path),
            "source_lock_sha256": sha256(source_lock_path),
            "restriction_overlay_schema_version": overlay["schema_version"],
            "restriction_overlay_sha256": sha256(overlay_path),
            "coverage_scope": "selected-root-set",
            "selected_root_fingerprints": [FINGERPRINT],
            "selected_root_attribute_count": 11,
            "selected_root_attributes_complete": 11,
            "other_public_roots_included": 0,
            "other_public_roots_ineligible": 117,
            "external_restrictions": "complete",
            "domain_restrictions": "none-identified-for-selected-root",
            "name_constraints": "none-on-root; path enforcement retained",
            "ordinary_certificate_policy": "not-required",
            "ev_supported": False,
            "ev_claimed": False,
            "full_rfc5280_policy_tree": "unsupported-and-disabled",
            "distrust_after_policy": "NAVHTTPS21 enforcement retained; no G5 cutoff identified",
            "certificate_path_verifier": "Mbed TLS 4.1.0; mbedtls_x509_crt_verify_with_profile",
        },
        "public_chain_capture": {
            "capture_manifest_path": capture_path.relative_to(ROOT).as_posix(),
            "capture_manifest_sha256": sha256(capture_path),
            "endpoint": capture["endpoint"],
            "retrieval_context": capture["retrieval_context"],
            "tls_version": capture["tls_version"],
            "cipher_suite": capture["cipher_suite"],
            "presented_certificate_count": capture["presented_certificate_count"],
            "presented_certificates": certificates,
            "chain_files": capture["chain_files"],
            "path_interpretation": {
                "exact_candidate_path": "leaf -> DigiCert G5 TLS ECC SHA384 2021 CA1 -> exact self-signed DigiCert TLS ECC P384 Root G5",
                "alternate_path": "leaf -> DigiCert G5 TLS ECC SHA384 2021 CA1 -> G5 cross-certificate -> DigiCert Global Root G3",
                "alternate_path_is_candidate_trust": False,
            },
        },
        "qualification_fixtures": fixtures,
        "activation_gates": {
            "release_owner_authorized": False,
            "candidate_approved_for_production": False,
            "public_https_enabled": False,
            "general_public_browsing": False,
            "fixture_time_promoted": False,
        },
    }
    validate_manifest_artifacts(result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    result = canonical_json(build_manifest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print(f"NAVHTTPS24_MANIFEST_SHA256={hashlib.sha256(result).hexdigest()}")
    print(f"NAVHTTPS24_MANIFEST_PATH={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

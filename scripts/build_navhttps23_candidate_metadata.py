#!/usr/bin/env python3
"""Create the deterministic, disabled NAVHTTPS23 candidate metadata sidecar."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from cryptography import x509


ROOT = Path(__file__).resolve().parents[1]
FINGERPRINT = "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05"
MANIFEST_PATH = ROOT / "assets/certs/candidates/mozilla-nss-2026-11-19.navhttps23-trust-manifest.json"
SOURCE_LOCK_PATH = ROOT / "assets/certs/mozilla/navhttps23-sources/source-lock.json"
OVERLAY_PATH = ROOT / "assets/certs/mozilla/navhttps23-sources/root-restrictions-overlay.json"
CANDIDATE_PATH = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.pem"
FIXTURE_MANIFEST_PATH = ROOT / "tests/fixtures/navhttps23/manifest.json"
QUALIFICATION_PATH = ROOT / "tests/fixtures/navhttps23/mbedtls-qualification.json"
OUTPUT_PATH = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.metadata.json"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha(path: Path) -> str:
    return sha256(path.read_bytes())


def canonical_json(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=False) + "\n").encode("utf-8")


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def pem_der_certificates(data: bytes) -> list[bytes]:
    import re
    from base64 import b64decode

    blocks = re.findall(
        rb"-----BEGIN CERTIFICATE-----\s*(.*?)\s*-----END CERTIFICATE-----",
        data,
        re.DOTALL,
    )
    if not blocks:
        raise SystemExit("Candidate PEM bundle contains no certificates")
    ders = []
    for block in blocks:
        ders.append(b64decode(re.sub(rb"\s+", b"", block), validate=True))
    return ders


def main() -> None:
    manifest = load_json(MANIFEST_PATH)
    source_lock = load_json(SOURCE_LOCK_PATH)
    overlay = load_json(OVERLAY_PATH)
    fixture_manifest = load_json(FIXTURE_MANIFEST_PATH)
    qualification = load_json(QUALIFICATION_PATH)
    root = next((record for record in manifest["roots"] if record.get("certificate_sha256") == FINGERPRINT), None)
    if root is None or root.get("technical_eligibility") != "eligible" or not root.get("candidate_included"):
        raise SystemExit("Selected root is not present as an eligible candidate in the importer manifest")
    if (
        manifest.get("input_candidate_root_count") != 118
        or manifest.get("technically_eligible_root_count") != 1
        or manifest.get("candidate_included_root_count") != 1
        or manifest.get("production_approved_root_count") != 0
        or manifest.get("public_https_enabled") is not False
        or manifest.get("external_policy_coverage_scope") != "selected-root-set"
        or manifest.get("full_inventory_external_policy_complete") is not False
    ):
        raise SystemExit("Trust manifest violates the selected-root or disabled-state contract")
    candidate_ders = pem_der_certificates(CANDIDATE_PATH.read_bytes())
    candidate_hashes = [sha256(der) for der in candidate_ders]
    if candidate_hashes != [FINGERPRINT]:
        raise SystemExit(f"Candidate PEM does not contain only the selected root: {candidate_hashes}")
    candidate_certificate = x509.load_der_x509_certificate(candidate_ders[0])
    if (
        fixture_manifest.get("source_record", {}).get("root_sha256") != FINGERPRINT
        or qualification.get("candidate_root_sha256") != FINGERPRINT
        or qualification.get("result") != "offline-qualified-for-scoped-ordinary-tls-path-tests"
        or qualification.get("passed_case_count") != 11
        or qualification.get("fixture_manifest_sha256") != file_sha(FIXTURE_MANIFEST_PATH)
    ):
        raise SystemExit("Fixture and verifier evidence do not bind to the selected root")
    if overlay.get("selected_root_fingerprints") != [FINGERPRINT] or overlay.get("coverage_status") != "complete":
        raise SystemExit("Selected-root restriction overlay does not establish exact-root coverage")

    metadata: dict[str, Any] = {
        "schema_version": "guidexos.navhttps23.disabled-public-root-candidate.v1",
        "assessment_date_utc": "2026-11-19",
        "candidate": {
            "name": "DigiCert TLS ECC P384 Root G5",
            "subject": candidate_certificate.subject.rfc4514_string(),
            "sha256": FINGERPRINT,
            "der_bytes": len(candidate_ders[0]),
            "trust_purpose": "tls-server-auth",
            "domain_restrictions": "none-identified-for-selected-root",
            "name_constraints": "no-root-extension-or-mozilla-imposed-root-constraint",
            "certificate_policy": {
                "ordinary_tls_status": "not-required",
                "required_oids": [],
                "ev_oid": "2.23.140.1.1",
                "ev_supported": False,
                "ev_claimed": False,
            },
            "explicit_distrust": "not-distrusted",
            "server_distrust_after": {"status": "none", "utc": None},
            "email_distrust_after": {"status": "none", "utc": None},
            "eligibility": "technically-eligible-for-scoped-ordinary-tls-candidate",
        },
        "source_coverage": {
            "status": "complete-for-selected-root-set",
            "selected_root_fingerprints": [FINGERPRINT],
            "selected_root_attributes_complete": 11,
            "selected_root_attribute_count": 11,
            "full_source_root_inventory_complete": False,
            "other_roots_covered": 0,
            "other_roots_unknown": 117,
        },
        "offline_qualification": {
            "status": "passed",
            "verifier": qualification["verifier"],
            "mbedtls_source_revision": qualification["mbedtls_source_revision"],
            "expected_case_count": qualification["expected_case_count"],
            "passed_case_count": qualification["passed_case_count"],
            "fixture_manifest_sha256": file_sha(FIXTURE_MANIFEST_PATH),
            "qualification_report_sha256": file_sha(QUALIFICATION_PATH),
        },
        "artifacts": {
            "trust_manifest_path": str(MANIFEST_PATH.relative_to(ROOT)).replace("\\", "/"),
            "trust_manifest_sha256": file_sha(MANIFEST_PATH),
            "candidate_bundle_path": str(CANDIDATE_PATH.relative_to(ROOT)).replace("\\", "/"),
            "candidate_bundle_sha256": file_sha(CANDIDATE_PATH),
            "source_lock_sha256": file_sha(SOURCE_LOCK_PATH),
            "restriction_overlay_sha256": file_sha(OVERLAY_PATH),
        },
        "qualification_states": {
            "source_coverage_complete_for_selected_root": True,
            "technically_eligible": True,
            "offline_qualified": True,
            "guest_distrust_after_qualified": False,
            "production_approved": False,
        },
        "activation": {
            "candidate_enabled": False,
            "production_approved": False,
            "public_https_enabled": False,
            "candidate_mode": "disabled-offline-only",
            "fixture_roots_included": False,
        },
        "independent_gates": [
            "NAVHTTPS21 distrust-after guest scenario remains pending",
            "production trusted-time provisioning and cross-boot rollback assurance remain incomplete",
            "production certificate revocation policy and enforcement remain unresolved; no soft-fail decision is made",
            "current Mozilla program/audit status must be refreshed before any production approval",
            "release-owner approval remains required",
        ],
    }
    metadata["sidecar_sha256"] = sha256(canonical_json(metadata))
    OUTPUT_PATH.write_bytes(canonical_json(metadata))
    print(f"NAVHTTPS23 candidate metadata SHA-256: {file_sha(OUTPUT_PATH)}")
    print(f"NAVHTTPS23 candidate PEM SHA-256: {file_sha(CANDIDATE_PATH)}")
    print("Candidate state: disabled, offline-only, production approval false, public HTTPS false")


if __name__ == "__main__":
    main()

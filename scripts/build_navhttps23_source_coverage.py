#!/usr/bin/env python3
"""Build the immutable, candidate-scoped NAVHTTPS23 source coverage pins."""

from __future__ import annotations

import csv
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.x509.oid import ExtensionOID


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "assets/certs/mozilla/navhttps23-sources"
NAV21_DIR = ROOT / "assets/certs/mozilla/navhttps21-sources"
FINGERPRINT = "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05"
ASSESSMENT_DATE = "2026-11-19"
NSS_REVISION = "8c60e757c30b1b239bae46532b4f6d4ebb1e1cad"
MOZILLA_REVISION = "28b8992494cac70d7abafa3ed120411de6d44dc0"
POLICY_REVISION = "bb613bb30b8d85706bac7ecdd877d19b50efe4e7"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha(path: Path) -> str:
    return sha256(path.read_bytes())


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=False) + "\n", encoding="utf-8")


def source_pin(
    source_id: str,
    local_path: str,
    url: str,
    owner: str,
    revision: str,
    license_name: str,
    coverage: str,
    *,
    retrieved_date: str = ASSESSMENT_DATE,
    bind_overlay: bool = True,
) -> dict[str, Any]:
    path = ROOT / local_path
    pin = {
        "source_id": source_id,
        "local_path": local_path,
        "url": url,
        "owner": owner,
        "revision": revision,
        "retrieved_date_utc": retrieved_date,
        "sha256": file_sha(path),
        "license": license_name,
        "coverage": coverage,
        "bind_overlay": bind_overlay,
    }
    return pin


def overlay_source(pin: dict[str, Any]) -> dict[str, Any]:
    return {
        "source_id": pin["source_id"],
        "owner": pin["owner"],
        "url": pin["url"],
        "revision": pin["revision"],
        "sha256": pin["sha256"],
        "license": pin["license"],
        "coverage": pin["coverage"],
    }


def ccadb_row(path: Path, field: str, fingerprint: str) -> dict[str, str]:
    with path.open(encoding="utf-8-sig", newline="") as stream:
        matches = [
            row for row in csv.DictReader(stream)
            if (row.get(field) or "").replace(":", "").strip().lower() == fingerprint
        ]
    if len(matches) != 1:
        raise SystemExit(f"Expected one CCADB row for {fingerprint}, found {len(matches)}")
    return matches[0]


def main() -> None:
    sys.path.insert(0, str(ROOT / "scripts"))
    import navigator_mozilla_trust_metadata as importer

    SOURCE_DIR.mkdir(parents=True, exist_ok=True)
    previous_overlay_path = NAV21_DIR / "root-restrictions-overlay.json"
    previous_lock_path = NAV21_DIR / "source-lock.json"
    previous_overlay = load_json(previous_overlay_path)
    previous_lock = load_json(previous_lock_path)

    fixture_manifest_path = ROOT / "tests/fixtures/navhttps23/manifest.json"
    qualification_path = ROOT / "tests/fixtures/navhttps23/mbedtls-qualification.json"
    fixture_manifest = load_json(fixture_manifest_path)
    qualification = load_json(qualification_path)
    candidate_root_path = ROOT / "tests/fixtures/navhttps23/g5_root.pem"
    root_der = importer.parse_pem_bundle(candidate_root_path.read_bytes())[0]
    root_fingerprint = sha256(root_der)
    if root_fingerprint != FINGERPRINT:
        raise SystemExit(f"Fixture root identity differs from selected root: {root_fingerprint}")
    root_certificate = x509.load_der_x509_certificate(root_der)
    root_subject = root_certificate.subject.rfc4514_string()
    if root_subject != r"CN=DigiCert TLS ECC P384 Root G5,O=DigiCert\, Inc.,C=US":
        raise SystemExit(f"Unexpected root subject: {root_subject}")
    try:
        root_certificate.extensions.get_extension_for_oid(ExtensionOID.NAME_CONSTRAINTS)
    except x509.ExtensionNotFound:
        root_has_name_constraints_extension = False
    else:
        root_has_name_constraints_extension = True
    if root_has_name_constraints_extension:
        raise SystemExit("Selected root unexpectedly contains a Name Constraints extension")

    ccadb_v5 = ROOT / "assets/certs/mozilla/navhttps21-sources/ccadb-v5-2026-11-17.csv"
    ccadb_root = ccadb_row(ccadb_v5, "SHA-256 Fingerprint", FINGERPRINT)
    community_csv = SOURCE_DIR / "ccadb-tls-server-auth-community-2026-11-19.csv"
    community_root = ccadb_row(community_csv, "SHA-256 Fingerprint", FINGERPRINT)
    if (
        ccadb_root.get("Certificate Name") != "DigiCert TLS ECC P384 Root G5"
        or ccadb_root.get("Certificate Record Type") != "Root Certificate"
        or ccadb_root.get("Mozilla Status") != "Included"
        or community_root.get("Mozilla Constrained") != "false"
        or community_root.get("Certificate Name") != "DigiCert TLS ECC P384 Root G5"
    ):
        raise SystemExit("The pinned CCADB record no longer supports the selected root facts")
    community_certificate = x509.load_pem_x509_certificate(
        community_root["X.509 Certificate (PEM)"].encode("ascii")
    )
    if community_certificate.fingerprint(hashes.SHA256()).hex() != FINGERPRINT:
        raise SystemExit("CCADB community report's embedded certificate differs from the selected root")

    nss_code = (SOURCE_DIR / "nss-genname-8c60e757.c").read_text(encoding="utf-8")
    table_match = re.search(
        r"static const SECItem builtInNameConstraints\[\]\[2\] = \{(.*?)\};",
        nss_code,
        re.DOTALL,
    )
    if table_match is None:
        raise SystemExit("Pinned NSS source no longer exposes the built-in constraint table")
    built_in_entries = re.findall(r"NAME_CONSTRAINTS_ENTRY\(([^)]+)\)", table_match.group(1))
    if built_in_entries != ["ANSSI", "TUBITAK1"]:
        raise SystemExit(f"Unexpected NSS imposed-name-constraint table: {built_in_entries}")
    if "DigiCert TLS ECC P384 Root G5" in nss_code:
        raise SystemExit("Selected root appears by name in NSS built-in name constraints")

    gecko_issuer_code = (SOURCE_DIR / "mozilla-NSSCertDBTrustDomain-28b89924.cpp").read_text(encoding="utf-8")
    cert_verifier_code = (SOURCE_DIR / "mozilla-CertVerifier-28b89924.cpp").read_text(encoding="utf-8")
    ev_code = (SOURCE_DIR / "mozilla-ExtendedValidation-28b89924.cpp").read_text(encoding="utf-8")
    policy_text = (SOURCE_DIR / "mozilla-root-store-policy-v3.1.md").read_text(encoding="utf-8")
    bug2031732 = load_json(ROOT / "assets/certs/mozilla/navhttps22-sources/bug-2031732.json")
    bug2031732_text = json.dumps(bug2031732, ensure_ascii=False)
    if "CERT_GetImposedNameConstraints" not in gecko_issuer_code:
        raise SystemExit("Pinned Gecko issuer path no longer applies NSS imposed name constraints")
    if "CertPolicyId::anyPolicy" not in cert_verifier_code or "GetKnownEVPolicies" not in cert_verifier_code:
        raise SystemExit("Pinned Gecko source does not distinguish EV from ordinary TLS path validation")
    if "CN=DigiCert TLS ECC P384 Root G5" not in ev_code or "2.23.140.1.1" not in ev_code:
        raise SystemExit("Pinned PSM source does not contain selected-root EV handling evidence")
    if "Version 3.1" not in policy_text or "Effective July 1, 2026" not in policy_text:
        raise SystemExit("Pinned Mozilla Root Store Policy is not v3.1")
    if FINGERPRINT.upper() not in bug2031732_text or "DigiCert TLS ECC P384 Root G5" not in bug2031732_text:
        raise SystemExit("Pinned DigiCert transition plan lacks the selected root record")

    new_pins = [
        source_pin(
            "mozilla-nss-genname-8c60e757",
            "assets/certs/mozilla/navhttps23-sources/nss-genname-8c60e757.c",
            f"https://github.com/nss-dev/nss/blob/{NSS_REVISION}/lib/certdb/genname.c",
            "Mozilla NSS",
            NSS_REVISION,
            "MPL-2.0",
            "Exact exhaustive source table for NSS imposed root Name Constraints and lookup behavior.",
            retrieved_date="2026-11-19",
        ),
        source_pin(
            "mozilla-gecko-nss-certdb-trust-domain-28b89924",
            "assets/certs/mozilla/navhttps23-sources/mozilla-NSSCertDBTrustDomain-28b89924.cpp",
            f"https://hg.mozilla.org/mozilla-central/file/{MOZILLA_REVISION}/security/certverifier/NSSCertDBTrustDomain.cpp",
            "Mozilla Firefox / NSSCertDBTrustDomain",
            MOZILLA_REVISION,
            "MPL-2.0",
            "Confirms Gecko calls the exact NSS imposed-name-constraints API for issuer certificates.",
            retrieved_date=ASSESSMENT_DATE,
        ),
        source_pin(
            "mozilla-gecko-cert-verifier-28b89924",
            "assets/certs/mozilla/navhttps23-sources/mozilla-CertVerifier-28b89924.cpp",
            f"https://hg.mozilla.org/mozilla-central/file/{MOZILLA_REVISION}/security/certverifier/CertVerifier.cpp",
            "Mozilla Firefox certificate verifier",
            MOZILLA_REVISION,
            "MPL-2.0",
            "Shows EV policy-path handling is separate from ordinary TLS anyPolicy validation.",
            retrieved_date=ASSESSMENT_DATE,
        ),
        source_pin(
            "mozilla-gecko-extended-validation-28b89924",
            "assets/certs/mozilla/navhttps23-sources/mozilla-ExtendedValidation-28b89924.cpp",
            f"https://hg.mozilla.org/mozilla-central/file/{MOZILLA_REVISION}/security/certverifier/ExtendedValidation.cpp",
            "Mozilla Firefox EV policy authorization source",
            MOZILLA_REVISION,
            "MPL-2.0",
            "Pins Mozilla's separate G5 EV policy authorization record; NAVHTTPS23 candidate scope excludes EV.",
            retrieved_date=ASSESSMENT_DATE,
        ),
        source_pin(
            "mozilla-gecko-extended-validation-header-28b89924",
            "assets/certs/mozilla/navhttps23-sources/mozilla-ExtendedValidation-28b89924.h",
            f"https://hg.mozilla.org/mozilla-central/file/{MOZILLA_REVISION}/security/certverifier/ExtendedValidation.h",
            "Mozilla Firefox EV policy authorization source",
            MOZILLA_REVISION,
            "MPL-2.0",
            "Pins the EV policy lookup interface used by the separate EV validation branch.",
            retrieved_date=ASSESSMENT_DATE,
        ),
        source_pin(
            "mozilla-root-store-policy-v3.1",
            "assets/certs/mozilla/navhttps23-sources/mozilla-root-store-policy-v3.1.md",
            f"https://github.com/mozilla/pkipolicy/blob/{POLICY_REVISION}/rootstore/policy.md",
            "Mozilla Root Program",
            POLICY_REVISION,
            "CC-BY-SA-3.0-or-later",
            "Current Mozilla root inclusion, CA operational, audit, ordinary TLS and conditional EV requirements.",
            retrieved_date=ASSESSMENT_DATE,
        ),
        source_pin(
            "ccadb-tls-server-auth-community-2026-11-19",
            "assets/certs/mozilla/navhttps23-sources/ccadb-tls-server-auth-community-2026-11-19.csv",
            "https://ccadb.my.salesforce-sites.com/ccadb/Report?Name=TLSServerAuthenticationCSV",
            "Common CA Database (Linux Foundation)",
            "downloaded-snapshot-2026-11-19",
            "CDLA-2.0-Permissive; CCADB attribution required",
            "Exact root identity and Mozilla Constrained boolean in the TLS server-auth community report.",
            retrieved_date=ASSESSMENT_DATE,
        ),
        source_pin(
            "ccadb-resources-2026-11-19",
            "assets/certs/mozilla/navhttps23-sources/ccadb-resources-2026-11-19.html",
            "https://www.ccadb.org/resources",
            "Common CA Database (Linux Foundation)",
            "retrieved-snapshot-2026-11-19",
            "CDLA-2.0-Permissive; CCADB attribution required",
            "Defines Mozilla Constrained as a boolean indicating whether constraint information exists; does not publish detail here.",
            retrieved_date=ASSESSMENT_DATE,
        ),
        source_pin(
            "digicert-navhttps23-demo-fixtures",
            "tests/fixtures/navhttps23/manifest.json",
            "https://digicert-tls-ecc-p384-root-g5.chain-demos.digicert.com/",
            "DigiCert public TLS demo endpoints listed in CCADB V5",
            "OpenSSL s_client peer capture 2026-11-19",
            "Publicly served certificate data; preserved for test and identity verification",
            "Pins actual peer-served G5 valid and expired demo chains, cross-signs, and fixture byte hashes.",
            retrieved_date=ASSESSMENT_DATE,
        ),
    ]

    qualification_pin = source_pin(
        "navhttps23-mbedtls-qualification",
        "tests/fixtures/navhttps23/mbedtls-qualification.json",
        "",
        "guideXOS offline qualification evidence",
        "Mbed TLS 4.1.0 commit 0fe989b6b514192783c469039edd325fd0989806",
        "guideXOS repository-local generated test evidence",
        "Generated Mbed TLS candidate path test results; the pinned report is evidence, not an authority source.",
        retrieved_date=ASSESSMENT_DATE,
        bind_overlay=False,
    )
    qualification_pin.pop("url")
    qualification_pin.pop("owner")
    qualification_pin.pop("license")
    qualification_pin.pop("coverage")
    new_pins.append(qualification_pin)

    new_test_inputs = [
        ("mbedtls-x509-verifier-source", "third_party/mbedtls/library/x509_crt.c"),
        ("guidexos-mbedtls-config", "guidexos/mbedtls_config.h"),
        ("guidexos-crypto-config", "guidexos/crypto_config.h"),
        ("navhttps23-verifier-harness", "tests/navhttps23/navhttps23_mbedtls_verifier.c"),
        ("navhttps23-cmake", "tests/navhttps23/CMakeLists.txt"),
        ("navhttps23-offline-runner", "scripts/test-navhttps23-digicert-offline.ps1"),
        ("navhttps23-fixture-preparer", "tests/navhttps23/prepare_fixtures.py"),
    ]
    test_inputs = [
        {"artifact_id": artifact_id, "local_path": local_path, "sha256": file_sha(ROOT / local_path)}
        for artifact_id, local_path in new_test_inputs
    ]
    if qualification.get("test_input_sha256") != {entry["artifact_id"]: entry["sha256"] for entry in test_inputs}:
        raise SystemExit("Pinned qualification report does not bind to the current Mbed TLS test inputs")
    if qualification.get("mbedtls_source_revision") != "0fe989b6b514192783c469039edd325fd0989806":
        raise SystemExit("Pinned qualification report uses an unexpected Mbed TLS source revision")
    if qualification.get("fixture_manifest_sha256") != file_sha(fixture_manifest_path):
        raise SystemExit("Pinned qualification report does not bind to the current fixture manifest")

    previous_sources = previous_overlay["sources"]
    source_ids = {source["source_id"] for source in previous_sources}
    if any(pin["source_id"] in source_ids for pin in new_pins):
        raise SystemExit("NAVHTTPS23 source IDs collide with the accepted NAVHTTPS21 overlay")
    all_overlay_sources = previous_sources + [overlay_source(pin) for pin in new_pins if pin.get("bind_overlay", True)]
    new_source_by_id = {pin["source_id"]: pin for pin in new_pins}

    ccadb_provenance = new_source_by_id["ccadb-tls-server-auth-community-2026-11-19"]
    ccadb_resources = new_source_by_id["ccadb-resources-2026-11-19"]
    nss_name_constraints = new_source_by_id["mozilla-nss-genname-8c60e757"]
    gecko_trust_domain = new_source_by_id["mozilla-gecko-nss-certdb-trust-domain-28b89924"]
    gecko_verifier = new_source_by_id["mozilla-gecko-cert-verifier-28b89924"]
    gecko_ev = new_source_by_id["mozilla-gecko-extended-validation-28b89924"]
    policy_source = new_source_by_id["mozilla-root-store-policy-v3.1"]
    fixture_source = new_source_by_id["digicert-navhttps23-demo-fixtures"]
    source_map = {source["source_id"]: source for source in all_overlay_sources}

    root_source_ids = [
        "mozilla-nss-certdata",
        "ccadb-v5",
        "mozilla-bugzilla-1764392",
        "mozilla-bugzilla-1764397",
        "mozilla-bugzilla-2031732",
        "mozilla-additional-trust-changes",
        "mozilla-root-store-policy",
        policy_source["source_id"],
        ccadb_provenance["source_id"],
        ccadb_resources["source_id"],
        nss_name_constraints["source_id"],
        gecko_trust_domain["source_id"],
        gecko_verifier["source_id"],
        gecko_ev["source_id"],
        fixture_source["source_id"],
    ]
    if any(source_id not in source_map for source_id in root_source_ids):
        raise SystemExit("NAVHTTPS23 overlay references a missing pinned source")

    identity_sources = ["mozilla-nss-certdata", "ccadb-v5", "mozilla-bugzilla-1764392", ccadb_provenance["source_id"]]
    inclusion_sources = ["mozilla-nss-certdata", "ccadb-v5", "mozilla-bugzilla-1764392", "mozilla-bugzilla-2031732"]
    purpose_sources = ["mozilla-nss-certdata", "ccadb-v5", "mozilla-bugzilla-1764392"]
    distrust_sources = ["mozilla-nss-certdata", "mozilla-bugzilla-2031732"]
    domain_sources = [
        "mozilla-additional-trust-changes",
        ccadb_provenance["source_id"],
        ccadb_resources["source_id"],
        nss_name_constraints["source_id"],
        gecko_trust_domain["source_id"],
        "mozilla-bugzilla-1764392",
        policy_source["source_id"],
    ]
    name_constraint_sources = [
        "mozilla-nss-certdata",
        "ccadb-v5",
        ccadb_provenance["source_id"],
        ccadb_resources["source_id"],
        nss_name_constraints["source_id"],
        gecko_trust_domain["source_id"],
    ]
    policy_sources = [
        "ccadb-v5",
        "mozilla-bugzilla-1764397",
        policy_source["source_id"],
        gecko_verifier["source_id"],
        gecko_ev["source_id"],
    ]
    issuance_sources = ["mozilla-nss-certdata", "mozilla-additional-trust-changes", policy_source["source_id"]]
    program_sources = [
        "mozilla-nss-certdata",
        "ccadb-v5",
        ccadb_provenance["source_id"],
        ccadb_resources["source_id"],
        policy_source["source_id"],
        "mozilla-bugzilla-1764392",
        "mozilla-bugzilla-2031732",
    ]
    attribute_coverage = {
        "certificate_identity": {"status": "complete", "source_ids": identity_sources},
        "root_inclusion": {"status": "complete", "source_ids": inclusion_sources},
        "trust_purpose": {"status": "complete", "source_ids": purpose_sources},
        "explicit_distrust": {"status": "complete", "source_ids": distrust_sources},
        "server_distrust_after": {"status": "complete", "source_ids": distrust_sources},
        "email_distrust_after": {"status": "complete", "source_ids": distrust_sources},
        "domain_restrictions": {"status": "complete", "source_ids": domain_sources},
        "name_constraints": {"status": "complete", "source_ids": name_constraint_sources},
        "certificate_policy": {"status": "complete", "source_ids": policy_sources},
        "issuance_evidence": {"status": "complete", "source_ids": issuance_sources},
        "program_policy": {"status": "complete", "source_ids": program_sources},
    }

    root_record = {
        "certificate_sha256": FINGERPRINT,
        "certificate_subject": root_subject,
        "effective_date": ASSESSMENT_DATE,
        "qualification_scope": "ordinary TLS server authentication only; EV presentation and EV policy-tree processing are excluded",
        "source_ids": root_source_ids,
        "root_restrictions_status": "complete",
        "trust_purpose": "tls-server-auth",
        "explicit_distrust": "not-distrusted",
        "server_distrust_after": {"status": "none", "utc": None},
        "email_distrust_after": {"status": "none", "utc": None},
        "domain_restrictions": {
            "status": "none",
            "domains": [],
            "assessment": "No selected-root-specific Mozilla domain restriction is present in CCADB and no enforced NSS/Gecko imposed-name-constraints entry matches this exact root. The CA Additional Trust Changes page is best-efforts and was not treated as exhaustive by itself.",
            "ccadb_mozilla_constrained": community_root["Mozilla Constrained"],
            "ccadb_scope": "boolean: whether constraint information exists for this root in that root-program record; not a detailed domain list",
            "nss_imposed_constraint_table_entries": built_in_entries,
            "nss_exact_subject_match": False,
            "root_certificate_has_name_constraints_extension": root_has_name_constraints_extension,
        },
        "name_constraints": {
            "status": "none",
            "certificate_extension": "absent-on-selected-root",
            "mozilla_enforcement": "NSS CERT_GetImposedNameConstraints table checked at pinned NSS revision; Gecko invokes it when finding issuers",
            "path_enforcement": "Mbed TLS 4.1.0 NAVHTTPS19 covers DNS/IP/email/directoryName and fails closed for unsupported applicable constrained forms",
        },
        "certificate_policy": {
            "status": "not-required",
            "required_oids": [],
            "ordinary_tls_basis": "Mozilla Root Store Policy 3.1 uses ordinary serverAuth verification and treats EV as a separate conditional policy validation path; its inclusion application asks for the EV OID if the CA issues EV certificates, not as an ordinary TLS path policy requirement.",
            "root_ev_oid": "2.23.140.1.1",
            "mozilla_ev_mapping_exists": True,
            "candidate_ev_processing": "unsupported-and-disabled; no EV indicator or qualified EV claim is made",
            "policy_tree_limitation": "Mbed TLS 4.1.0 does not implement full RFC 5280 policy-tree processing; ordinary TLS scope does not require that behavior.",
        },
        "issuance_evidence": {
            "status": "not-required",
            "types": [],
            "assessment": "No root-specific issuance evidence or certificate-transparency requirement was found for this ordinary TLS server-auth candidate; the ordinary Mozilla path accepts anyPolicy while EV is handled separately.",
        },
        "program_policy_status": "satisfied",
        "program_policy_assessment": {
            "status_basis": "Current Mozilla inclusion and TLS trust purpose are confirmed in pinned NSS certdata and current CCADB records; the submitted transition plan is a cross-check only.",
            "policy_revision": POLICY_REVISION,
            "policy_effective_date": "2026-07-01",
            "scope_limit": "This status means the selected root is currently included and no root-specific Mozilla program restriction contradicts ordinary TLS eligibility. It is not an independent audit of DigiCert's ongoing CA operations.",
            "ccadb_audit_metadata": {
                "standard_statement_date": ccadb_root.get("Standard Audit Statement Date"),
                "standard_period_start": ccadb_root.get("Standard Audit Period Start Date"),
                "standard_period_end": ccadb_root.get("Standard Audit Period End Date"),
                "netsec_statement_date": ccadb_root.get("NetSec Audit Statement Date"),
                "tls_br_statement_date": ccadb_root.get("TLS BR Audit Statement Date"),
                "tls_evg_statement_date": ccadb_root.get("TLS EVG Audit Statement Date"),
                "cp_cps_effective_date": ccadb_root.get("CP/CPS Effective Date"),
            },
            "production_recheck_required": True,
        },
        "supported_enforcement_mechanisms": [
            "mbedtls-4.1.0-navhttps19",
            "navigator-https-trust-profile-v2",
        ],
        "attribute_coverage": attribute_coverage,
        "candidate_path_validation": {
            "status": "verified-by-mbedtls-4.1.0",
            "qualification_report_sha256": file_sha(qualification_path),
            "fixture_manifest_sha256": file_sha(fixture_manifest_path),
            "passed_case_count": qualification["passed_case_count"],
        },
    }

    overlay = {
        "schema_version": "guidexos.mozilla-root-restrictions.v2",
        "source_revision": "navhttps23-selected-root-assessment-2026-11-19",
        "effective_date": ASSESSMENT_DATE,
        "coverage_status": "complete",
        "coverage_scope": "selected-root-set",
        "selected_root_fingerprints": [FINGERPRINT],
        "sources": all_overlay_sources,
        "coverage_notes": [
            "Coverage is complete only for the exact selected DigiCert TLS ECC P384 Root G5 fingerprint; it does not cover the other source roots.",
            "CCADB's Mozilla Constrained field is a boolean and is interpreted according to CCADB's pinned field documentation; it is not treated as a detailed restriction registry.",
            "The Mozilla Additional Trust Changes page explicitly provides best-efforts coverage, so its silence is not used alone to establish absence.",
            "The exact NSS imposed-name-constraints table and Gecko issuer hook were inspected at pinned revisions; only ANSSI and TUBITAK1 are present in the table, and neither matches the selected root.",
            "Current Mozilla inclusion and root-specific program state are established; the independent ongoing CA audit and operational compliance process remains under Mozilla program oversight and is not re-audited by guideXOS.",
            "EV policy authorization is explicitly excluded from this ordinary TLS candidate; unsupported policy-tree semantics cannot yield an EV result.",
            "OneCRL/intermediate revocation, runtime guest qualification, trusted-time provisioning, production revocation decisions, rollback assurance, and release-owner approval remain independent gates.",
        ],
        "records": {FINGERPRINT: root_record},
    }
    importer.validate_restrictions_overlay_v2(overlay)
    overlay_bytes = (json.dumps(overlay, ensure_ascii=False, indent=2, sort_keys=False) + "\n").encode("utf-8")
    overlay_path = SOURCE_DIR / "root-restrictions-overlay.json"
    overlay_path.write_bytes(overlay_bytes)

    lock = dict(previous_lock)
    lock["external_policy_effective_date"] = ASSESSMENT_DATE
    lock["root_restrictions_overlay"] = {
        "local_path": "assets/certs/mozilla/navhttps23-sources/root-restrictions-overlay.json",
        "sha256": sha256(overlay_bytes),
        "schema_version": overlay["schema_version"],
        "coverage_status": overlay["coverage_status"],
        "coverage_scope": overlay["coverage_scope"],
    }
    lock["navhttps23_pinned_sources"] = new_pins
    lock["navhttps23_test_inputs"] = test_inputs
    lock["navhttps23_assessment"] = {
        "assessment_date_utc": ASSESSMENT_DATE,
        "candidate_root_sha256": FINGERPRINT,
        "scope": "one selected root; ordinary TLS server authentication; disabled candidate only",
        "fixture_manifest_sha256": file_sha(fixture_manifest_path),
        "qualification_report_sha256": file_sha(qualification_path),
        "qualification_case_count": qualification["passed_case_count"],
        "builder_path": "scripts/build_navhttps23_source_coverage.py",
        "builder_sha256": file_sha(Path(__file__).resolve()),
    }
    lock_path = SOURCE_DIR / "source-lock.json"
    write_json(lock_path, lock)
    print(f"NAVHTTPS23 overlay SHA-256: {sha256(overlay_bytes)}")
    print(f"NAVHTTPS23 source-lock SHA-256: {file_sha(lock_path)}")
    print(f"Selected root: {FINGERPRINT}; source scope=selected-root-set")


if __name__ == "__main__":
    main()

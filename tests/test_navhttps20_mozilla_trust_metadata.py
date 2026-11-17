import copy
import datetime as dt
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import navigator_mozilla_trust_metadata as importer  # noqa: E402


SOURCE_DIR = ROOT / "assets/certs/mozilla/navhttps20-sources"
CERTDATA = (SOURCE_DIR / "certdata.txt").read_bytes()
CANDIDATE = (ROOT / "assets/certs/candidates/mozilla-cacert-2026-11-05.pem").read_bytes()
INCOMPLETE_OVERLAY = json.loads((SOURCE_DIR / "external-policy-overlay.json").read_text(encoding="utf-8"))


def eligible_root(**changes):
    root = {
        "certificate_sha256": "a" * 64,
        "identity_verified": True,
        "mozilla_ca_policy": True,
        "server_auth_trust_record": "CKT_NSS_TRUSTED_DELEGATOR",
        "server_distrust_after": None,
        "server_distrust_after_enforced": True,
        "path_validation_status": "verified-by-mbedtls-4.1.0",
        "unknown_nss_fields": [],
        "source_rejection_reasons": [],
    }
    root.update(changes)
    return root


def complete_overlay(fingerprint="a" * 64, **changes):
    record = {
        "trust_purpose": "tls-server-auth",
        "root_restrictions_status": "complete",
        "distrust": "none",
        "domain_restrictions_status": "none",
        "name_constraints_status": "none",
        "certificate_policy_status": "not-required",
        "program_policy_status": "satisfied",
    }
    record.update(changes)
    return {
        "schema_version": importer.OVERLAY_VERSION,
        "complete_for_source_roots": True,
        "records": {fingerprint: record},
    }


class MozillaTrustMetadataTests(unittest.TestCase):
    def test_pinned_snapshot_keeps_all_118_roots_excluded(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        self.assertEqual(report["nss_certificate_object_count"], 167)
        self.assertEqual(report["nss_trust_object_count"], 167)
        self.assertEqual(report["input_candidate_root_count"], 118)
        self.assertEqual(report["nss_tls_server_auth_trust_record_count"], 113)
        self.assertEqual(report["nss_tls_server_auth_explicit_distrust_record_count"], 0)
        self.assertEqual(report["nss_tls_server_auth_must_verify_trust_record_count"], 54)
        self.assertEqual(report["nss_tls_server_auth_unsupported_trust_record_count"], 0)
        self.assertEqual(report["technically_eligible_root_count"], 0)
        self.assertEqual(report["candidate_included_root_count"], 0)
        self.assertEqual(report["production_approved_root_count"], 0)
        self.assertFalse(report["public_https_enabled"])

    def test_report_has_fingerprint_keyed_source_provenance(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        self.assertEqual(len({root["certificate_sha256"] for root in report["roots"]}), 118)
        self.assertTrue(all(re.fullmatch(r"[0-9a-f]{64}", root["certificate_sha256"]) for root in report["roots"]))
        self.assertTrue(all(root["source_revision"] == "8c60e757c30b1b239bae46532b4f6d4ebb1e1cad" for root in report["roots"]))

    def test_two_retired_bundle_certificates_are_not_joined_to_current_nss(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        absent = [root for root in report["roots"] if "nss-certificate-record-missing" in root["source_rejection_reasons"]]
        self.assertEqual(len(absent), 2)
        self.assertTrue(all(not root["identity_verified"] for root in absent))

    def test_every_current_source_identity_is_exact_der_verified(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        verified = [root for root in report["roots"] if root["identity_verified"]]
        self.assertEqual(len(verified), 116)
        self.assertTrue(all(root["nss_cert_sha1"] == root["certificate_sha1"] for root in verified))
        self.assertTrue(all(root["nss_trust_cert_sha1"] == root["certificate_sha1"] for root in verified))

    def test_distrust_after_dates_are_preserved(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        dated = [root for root in report["roots"] if root["server_distrust_after"]]
        self.assertEqual(len(dated), 4)
        self.assertTrue(all(root["server_distrust_after"].endswith("Z") for root in dated))
        self.assertTrue(all("server-distrust-after-not-enforced" in root["eligibility_rejection_reasons"] for root in dated))

    def test_latest_nss_snapshot_restores_digicert_g4_server_auth_with_distrust_after(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        digicert = next(root for root in report["roots"] if root["nss_label"] == "DigiCert Trusted Root G4")
        self.assertEqual(digicert["server_auth_trust_record"], "CKT_NSS_TRUSTED_DELEGATOR")
        self.assertEqual(digicert["server_distrust_after"], "2609-01-04T23:59:59Z")
        self.assertIn("server-distrust-after-not-enforced", digicert["eligibility_rejection_reasons"])

    def test_other_certificate_purposes_are_preserved_separately(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        root = next(root for root in report["roots"] if root["nss_label"] == "DigiCert Trusted Root G4")
        self.assertEqual(root["tls_server_auth_purpose"], "tls-server-auth")
        self.assertIsNotNone(root["email_protection_trust_record"])
        self.assertIsNotNone(root["code_signing_trust_record"])
        self.assertIsNotNone(root["step_up_approved"])

    def test_unrecognized_root_is_rejected(self):
        reasons = importer.evaluate_root_eligibility(eligible_root(identity_verified=False), complete_overlay())
        self.assertIn("certificate-identity-not-verified", reasons)

    def test_untrusted_root_is_rejected(self):
        root = eligible_root(server_auth_trust_record="CKT_NSS_MUST_VERIFY_TRUST")
        reasons = importer.evaluate_root_eligibility(root, complete_overlay())
        self.assertIn("tls-server-auth-not-explicitly-permitted", reasons)

    def test_explicit_server_distrust_is_rejected(self):
        root = eligible_root(server_auth_trust_record="CKT_NSS_NOT_TRUSTED")
        reasons = importer.evaluate_root_eligibility(root, complete_overlay())
        self.assertIn("explicitly-distrusted-for-server-auth", reasons)

    def test_distrust_after_boundary_is_inclusive(self):
        cutoff = dt.datetime(2026, 11, 1, tzinfo=dt.timezone.utc)
        self.assertFalse(importer.issued_after_distrust_after(cutoff, cutoff))

    def test_cert_issued_after_distrust_after_is_rejected(self):
        cutoff = dt.datetime(2026, 11, 1, tzinfo=dt.timezone.utc)
        issued = dt.datetime(2026, 11, 1, 0, 0, 1, tzinfo=dt.timezone.utc)
        self.assertTrue(importer.issued_after_distrust_after(issued, cutoff))

    def test_naive_distrust_timestamp_is_rejected(self):
        with self.assertRaises(ValueError):
            importer.issued_after_distrust_after(dt.datetime(2026, 1, 1), dt.datetime(2026, 1, 1, tzinfo=dt.timezone.utc))

    def test_missing_metadata_is_ineligible(self):
        reasons = importer.evaluate_root_eligibility(eligible_root(), INCOMPLETE_OVERLAY)
        self.assertIn("root-specific-external-restrictions-missing", reasons)
        self.assertIn("external-policy-snapshot-incomplete", reasons)

    def test_manifest_carries_missing_external_policy_state_per_root(self):
        report = importer.build_report(CERTDATA, CANDIDATE, INCOMPLETE_OVERLAY)
        self.assertTrue(all(root["external_policy_record"] is None for root in report["roots"]))
        self.assertTrue(all(root["external_restrictions"] == "missing" for root in report["roots"]))
        self.assertTrue(all(root["domain_restrictions_status"] == "missing" for root in report["roots"]))
        self.assertTrue(all(root["certificate_policy_status"] == "missing" for root in report["roots"]))

    def test_unknown_metadata_version_is_rejected(self):
        overlay = complete_overlay()
        overlay["schema_version"] = "guidexos.mozilla-root-restrictions.v99"
        self.assertIn("unsupported-external-policy-overlay-version", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_incomplete_root_restriction_assessment_is_rejected(self):
        overlay = complete_overlay(root_restrictions_status="partial")
        self.assertIn("root-specific-restrictions-incomplete", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_wrong_server_auth_purpose_is_rejected(self):
        overlay = complete_overlay(trust_purpose="tls-client-auth")
        self.assertIn("incorrect-server-auth-purpose", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_explicit_external_distrust_is_rejected(self):
        overlay = complete_overlay(distrust="distrusted")
        self.assertIn("explicit-external-distrust", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_unknown_domain_restrictions_are_rejected(self):
        overlay = complete_overlay(domain_restrictions_status="unknown")
        self.assertIn("unsupported-domain-restrictions", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_domain_restrictions_without_an_enforcer_are_rejected(self):
        overlay = complete_overlay(domain_restrictions_status="enforced", domain_enforcement="ignored")
        self.assertIn("domain-restrictions-not-enforceable", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_incompatible_name_constraints_are_rejected(self):
        overlay = complete_overlay(name_constraints_status="incompatible")
        self.assertIn("incompatible-name-constraints", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_unverified_name_constraints_enforcement_is_rejected(self):
        overlay = complete_overlay(name_constraints_status="compatible", name_constraints_enforcement="other")
        self.assertIn("name-constraints-enforcement-unverified", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_unsupported_certificate_policy_is_rejected(self):
        overlay = complete_overlay(certificate_policy_status="unknown")
        self.assertIn("unsupported-certificate-policy-requirement", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_required_certificate_policy_without_selected_tree_is_rejected(self):
        overlay = complete_overlay(certificate_policy_status="enforced", certificate_policy_enforcement="not-enforced")
        self.assertIn("certificate-policy-requirement-not-enforced", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_supported_required_certificate_policy_can_pass(self):
        overlay = complete_overlay(certificate_policy_status="enforced", certificate_policy_enforcement="selected-policy-tree")
        self.assertEqual(importer.evaluate_root_eligibility(eligible_root(), overlay), [])

    def test_unverified_certificate_path_is_rejected(self):
        root = eligible_root(path_validation_status="unverified")
        self.assertIn("certificate-path-validation-unverified", importer.evaluate_root_eligibility(root, complete_overlay()))

    def test_fixture_root_contamination_is_rejected(self):
        root = eligible_root()
        self.assertIn(root["certificate_sha256"], {root["certificate_sha256"]})
        self.assertIn("fixture-root-contamination", importer.evaluate_root_eligibility(root, complete_overlay(), {root["certificate_sha256"]}))

    def test_unrecognized_nss_field_is_rejected(self):
        root = eligible_root(unknown_nss_fields=["CKA_NSS_NEW_POLICY"])
        self.assertIn("unsupported-nss-metadata", importer.evaluate_root_eligibility(root, complete_overlay()))

    def test_missing_mozilla_membership_is_rejected(self):
        root = eligible_root(mozilla_ca_policy=None)
        self.assertIn("mozilla-ca-policy-membership-not-affirmed", importer.evaluate_root_eligibility(root, complete_overlay()))

    def test_nss_metadata_hash_tamper_is_rejected(self):
        lock = importer.load_json(SOURCE_DIR / "source-lock.json")
        lock = copy.deepcopy(lock)
        lock["sources"]["nss_certdata"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(importer.ImportErrorDetail, "pinned source hash mismatch"):
            importer.verify_pinned_inputs(ROOT, lock, INCOMPLETE_OVERLAY)

    def test_external_policy_overlay_tamper_is_rejected(self):
        overlay = copy.deepcopy(INCOMPLETE_OVERLAY)
        overlay["complete_for_source_roots"] = True
        lock = importer.load_json(SOURCE_DIR / "source-lock.json")
        with self.assertRaisesRegex(importer.ImportErrorDetail, "external-policy overlay"):
            importer.verify_pinned_inputs(ROOT, lock, overlay)

    def test_contradictory_trust_records_are_rejected(self):
        trust_start = CERTDATA.rfind(b"CKA_CLASS CK_OBJECT_CLASS CKO_NSS_TRUST")
        trust_block = CERTDATA[trust_start:]
        trust_block = trust_block.split(b"\n\n", 1)[0]
        contradicted = trust_block.replace(
            b"CKA_TRUST_SERVER_AUTH CK_TRUST CKT_NSS_MUST_VERIFY_TRUST",
            b"CKA_TRUST_SERVER_AUTH CK_TRUST CKT_NSS_TRUSTED_DELEGATOR",
            1,
        )
        if contradicted == trust_block:
            contradicted = trust_block.replace(
                b"CKA_TRUST_SERVER_AUTH CK_TRUST CKT_NSS_TRUSTED_DELEGATOR",
                b"CKA_TRUST_SERVER_AUTH CK_TRUST CKT_NSS_MUST_VERIFY_TRUST",
                1,
            )
        with self.assertRaisesRegex(importer.ImportErrorDetail, "duplicate NSS trust record"):
            importer.parse_nss_certdata(CERTDATA + b"\n\n" + contradicted + b"\n")

    def test_modified_certificate_der_is_rejected(self):
        modified = bytearray(CANDIDATE)
        marker = b"-----BEGIN CERTIFICATE-----"
        index = modified.index(marker) + len(marker)
        while modified[index] in b"\r\n ":
            index += 1
        modified[index] = ord("A") if modified[index] != ord("A") else ord("B")
        with self.assertRaises(importer.ImportErrorDetail):
            importer.build_report(CERTDATA, bytes(modified), INCOMPLETE_OVERLAY)

    def test_repeated_import_generation_is_byte_identical_and_emits_no_bundle(self):
        with tempfile.TemporaryDirectory(prefix="navhttps20-determinism-") as temporary:
            first = Path(temporary) / "first.json"
            second = Path(temporary) / "second.json"
            candidate = Path(temporary) / "must-not-exist.pem"
            importer.run_import(ROOT, first)
            importer.run_import(ROOT, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            result = json.loads(first.read_text(encoding="utf-8"))
            self.assertEqual(result["candidate_included_root_count"], 0)
            self.assertFalse(candidate.exists())


if __name__ == "__main__":
    unittest.main()

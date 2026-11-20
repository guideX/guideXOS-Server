import copy
import datetime as dt
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import navigator_mozilla_trust_metadata as importer  # noqa: E402


SOURCE = ROOT / "assets/certs/mozilla/navhttps21-sources"
INCOMPLETE = json.loads((SOURCE / "root-restrictions-overlay.json").read_text(encoding="utf-8"))


def eligible_root(**changes):
    root = {
        "certificate_sha256": "a" * 64,
        "identity_verified": True,
        "mozilla_ca_policy": True,
        "server_auth_trust_record": "CKT_NSS_TRUSTED_DELEGATOR",
        "server_distrust_after": None,
        "server_distrust_after_status": "none",
        "server_distrust_after_notbefore_enforced": False,
        "server_distrust_after_sct_enforced": False,
        "server_distrust_after_enforced": False,
        "path_validation_status": "verified-by-mbedtls-4.1.0",
        "unknown_nss_fields": [],
        "source_rejection_reasons": [],
    }
    root.update(changes)
    return root


def complete_overlay(**record_changes):
    overlay = copy.deepcopy(INCOMPLETE)
    overlay["coverage_status"] = "complete"
    fingerprint = "a" * 64
    record = {
        "certificate_sha256": fingerprint,
        "effective_date": "2026-11-17",
        "source_ids": [source["source_id"] for source in overlay["sources"]],
        "trust_purpose": "tls-server-auth",
        "root_restrictions_status": "complete",
        "explicit_distrust": "not-distrusted",
        "server_distrust_after": {"status": "none", "utc": None},
        "email_distrust_after": {"status": "none", "utc": None},
        "domain_restrictions": {"status": "none", "domains": []},
        "name_constraints": {"status": "none"},
        "certificate_policy": {"status": "not-required", "required_oids": []},
        "issuance_evidence": {"status": "not-required", "types": []},
        "program_policy_status": "satisfied",
        "supported_enforcement_mechanisms": [],
        "attribute_coverage": {
            attribute: {"status": "complete", "source_ids": [source["source_id"] for source in overlay["sources"]]}
            for attribute in importer.RESTRICTION_COVERAGE_FIELDS
        },
    }
    record.update(record_changes)
    overlay["records"] = {fingerprint: record}
    return overlay


class MozillaTrustRestrictionTests(unittest.TestCase):
    def test_cutoff_accepts_leaf_issued_before_boundary(self):
        cutoff = dt.datetime(2604, 1, 5, 23, 59, 59, tzinfo=dt.timezone.utc)
        issued = cutoff - dt.timedelta(seconds=1)
        self.assertFalse(importer.issued_after_distrust_after(issued, cutoff))

    def test_cutoff_accepts_leaf_issued_exactly_at_boundary(self):
        cutoff = dt.datetime(2604, 1, 5, 23, 59, 59, tzinfo=dt.timezone.utc)
        self.assertFalse(importer.issued_after_distrust_after(cutoff, cutoff))

    def test_cutoff_rejects_leaf_issued_after_boundary(self):
        cutoff = dt.datetime(2604, 1, 5, 23, 59, 59, tzinfo=dt.timezone.utc)
        issued = cutoff + dt.timedelta(seconds=1)
        self.assertTrue(importer.issued_after_distrust_after(issued, cutoff))

    def test_pinned_nss_utc_time_uses_two_digit_year(self):
        self.assertEqual(importer._distrust_after(b"261116235959Z"), "2026-11-16T23:59:59Z")

    def test_pinned_nss_generalized_time_preserves_extended_years(self):
        self.assertEqual(importer._distrust_after(b"26040105235959Z"), "2604-01-05T23:59:59Z")

    def test_pinned_nss_cutoff_values_match_certdata_comments(self):
        self.assertEqual(
            importer.NAVHTTPS21_DISTRUST_AFTER,
            {
                "2530cc8e98321502bad96f9b1fba1b099e2d299e0f4548bb914f363bc0d4531f": "2026-04-15T23:59:59Z",
                "40f6af0346a99aa1cd1d555a4e9cce62c7f9634603ee406615833dc8c8d00367": "2026-11-16T23:59:59Z",
                "552f7bdcf1a7af9e6ce672017f4f12abf77240c78e761ac203d1d9d20ac89988": "2026-09-14T23:59:59Z",
                "57de0583efd2b26e0361da99da9df4648def7ee8441c3b728afa9bcde0f9b26a": "2026-08-06T07:01:00Z",
            },
        )

    def test_missing_overlay_record_is_unknown_and_excluded(self):
        reasons = importer.evaluate_root_eligibility(eligible_root(), INCOMPLETE)
        self.assertIn("root-specific-external-restrictions-missing", reasons)
        self.assertIn("external-policy-snapshot-incomplete", reasons)

    def test_unknown_cutoff_state_is_excluded(self):
        overlay = complete_overlay(server_distrust_after={"status": "unknown", "utc": None})
        self.assertIn("server-distrust-after-status-unknown", importer.evaluate_root_eligibility(eligible_root(), overlay))

    def test_malformed_cutoff_string_is_rejected(self):
        overlay = complete_overlay(server_distrust_after={"status": "date", "utc": "2604-01-05"})
        with self.assertRaisesRegex(importer.ImportErrorDetail, "valid UTC"):
            importer.validate_restrictions_overlay_v2(overlay)

    def test_invalid_utc_offset_is_rejected(self):
        overlay = complete_overlay(server_distrust_after={"status": "date", "utc": "2604-01-05T23:59:59+00:00"})
        with self.assertRaisesRegex(importer.ImportErrorDetail, "valid UTC"):
            importer.validate_restrictions_overlay_v2(overlay)

    def test_invalid_calendar_and_timestamp_overflow_are_rejected(self):
        for timestamp in ("2604-02-30T00:00:00Z", "10000-01-01T00:00:00Z", "9999-12-31T23:59:60Z"):
            with self.subTest(timestamp=timestamp):
                overlay = complete_overlay(server_distrust_after={"status": "date", "utc": timestamp})
                with self.assertRaises(importer.ImportErrorDetail):
                    importer.validate_restrictions_overlay_v2(overlay)

    def test_email_cutoff_does_not_become_tls_cutoff(self):
        overlay = complete_overlay(email_distrust_after={"status": "date", "utc": "2030-01-01T00:00:00Z"})
        self.assertEqual(importer.evaluate_root_eligibility(eligible_root(), overlay), [])

    def test_root_with_affirmatively_no_cutoff_is_not_rejected(self):
        self.assertEqual(importer.evaluate_root_eligibility(eligible_root(), complete_overlay()), [])

    def test_cross_signed_der_identity_does_not_inherit_root_cutoff(self):
        cross_signed_variant = "b" * 64
        self.assertIsNone(importer.NAVHTTPS21_DISTRUST_AFTER.get(cross_signed_variant))

    def test_alternate_anchor_does_not_inherit_unrelated_root_cutoff(self):
        alternate_anchor = "c" * 64
        self.assertIsNone(importer.NAVHTTPS21_DISTRUST_AFTER.get(alternate_anchor))
        self.assertEqual(importer.evaluate_root_eligibility(eligible_root(), complete_overlay()), [])

    def test_cutoff_policy_table_is_bound_to_four_exact_nss_roots(self):
        policy = json.loads((SOURCE / "distrust-after-enforcement.json").read_text(encoding="utf-8"))
        pinned = {record["certificate_sha256"]: record["cutoff_utc"] for record in policy["records"]}
        self.assertEqual(pinned, importer.NAVHTTPS21_DISTRUST_AFTER)
        self.assertEqual(policy["full_tls_enforcement_status"], "partial-notbefore-only")
        self.assertEqual(policy["tls_sct_timestamp_check"], "unsupported")
        self.assertEqual(
            policy["sct_semantics_source"],
            [
                "https://bugzilla.mozilla.org/show_bug.cgi?id=2018158",
                "https://bugzilla.mozilla.org/show_bug.cgi?id=2089115",
            ],
        )
        self.assertIn("2018979 is an eMudhra transition-tracking bug", policy["sct_semantics_context"])

    def test_notbefore_enforcement_does_not_claim_full_distrust_after(self):
        root = eligible_root(
            server_distrust_after="2604-01-05T23:59:59Z",
            server_distrust_after_notbefore_enforced=True,
            server_distrust_after_sct_enforced=False,
            server_distrust_after_enforced=False,
        )
        reasons = importer.evaluate_root_eligibility(root, complete_overlay())
        self.assertIn("server-distrust-after-sct-timestamp-not-enforced", reasons)

    def test_pinned_mozilla_ca_status_does_not_claim_restriction_completeness(self):
        report = json.loads((ROOT / "assets/certs/candidates/mozilla-nss-2026-11-13.navhttps21-trust-manifest.json").read_text(encoding="utf-8"))
        root = next(item for item in report["roots"] if item["nss_label"] == "Telia EC TLS Root CA v3")
        self.assertEqual(root["ccadb_record"]["Mozilla Status"], "Included")
        self.assertEqual(root["external_restrictions"], "unknown")
        self.assertEqual(root["technical_eligibility"], "ineligible")

    def test_accounting_manifest_counts_overlapping_exclusion_reasons_and_unknowns(self):
        _, _, report, _, _, _, _ = importer._verified_navhttps21_inputs(
            ROOT, SOURCE / "source-lock.json"
        )
        reasons = report["eligibility_rejection_reason_counts"]
        self.assertEqual(reasons["certificate-path-validation-unverified"], 118)
        self.assertEqual(reasons["external-policy-snapshot-incomplete"], 118)
        self.assertEqual(reasons["root-specific-external-restrictions-missing"], 117)
        self.assertEqual(reasons["root-specific-restrictions-incomplete"], 1)
        self.assertEqual(reasons["root-domain-restrictions-coverage-unknown"], 1)
        self.assertEqual(reasons["root-program-policy-coverage-unknown"], 1)
        self.assertEqual(reasons["mozilla-root-removed-from-trust-store"], 2)
        self.assertEqual(reasons["nss-server-auth-purpose-not-explicitly-trusted"], 3)
        self.assertNotIn("tls-server-auth-not-explicitly-permitted", reasons)
        self.assertEqual(reasons["nss-trust-record-missing"], 2)
        self.assertNotIn("unsupported-certificate-policy-requirement", reasons)
        self.assertEqual(reasons["server-distrust-after-sct-timestamp-not-enforced"], 4)
        self.assertEqual(
            report["restriction_attribute_status_counts"]["domain_restrictions_status"],
            {"unknown": 118},
        )
        self.assertEqual(
            report["restriction_attribute_status_counts"]["certificate_policy_status"],
            {"not-required": 1, "unknown": 117},
        )
        self.assertEqual(
            report["restriction_attribute_coverage_counts"]["root_inclusion"],
            {"complete": 1, "unknown": 117},
        )
        digicert = next(
            root for root in report["roots"]
            if root["certificate_sha256"] == "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05"
        )
        self.assertEqual(digicert["root_certificate_extensions"]["name_constraints"]["status"], "absent")
        self.assertEqual(digicert["root_certificate_extensions"]["certificate_policies"]["status"], "absent")
        self.assertEqual(digicert["domain_restrictions_status"], "unknown")
        self.assertEqual(digicert["certificate_policy_status"], "not-required")
        self.assertEqual(
            digicert["external_policy_record"]["qualification_scope"],
            "ordinary TLS server authentication; Extended Validation presentation and policy processing are excluded",
        )
        self.assertEqual(digicert["technical_eligibility"], "ineligible")

    def test_accounting_separates_removed_roots_from_missing_nss_trust(self):
        _, _, report, _, _, _, _ = importer._verified_navhttps21_inputs(
            ROOT, SOURCE / "source-lock.json"
        )
        removed = [
            root for root in report["roots"]
            if "mozilla-root-removed-from-trust-store" in root["eligibility_rejection_reasons"]
        ]
        self.assertEqual(len(removed), 2)
        self.assertTrue(all(root["ccadb_record"]["Mozilla Status"] == "Removed" for root in removed))
        self.assertTrue(all("nss-trust-record-missing" in root["eligibility_rejection_reasons"] for root in removed))
        explicit_non_grants = [
            root for root in report["roots"]
            if "nss-server-auth-purpose-not-explicitly-trusted" in root["eligibility_rejection_reasons"]
        ]
        self.assertEqual(len(explicit_non_grants), 3)
        self.assertTrue(all(root["server_auth_trust_record"] == "CKT_NSS_MUST_VERIFY_TRUST" for root in explicit_non_grants))

    def test_complete_root_coverage_cannot_contain_unknown_attribute_coverage(self):
        overlay = complete_overlay()
        record = overlay["records"]["a" * 64]
        record["attribute_coverage"]["domain_restrictions"]["status"] = "unknown"
        with self.assertRaisesRegex(importer.ImportErrorDetail, "claims complete coverage"):
            importer.validate_restrictions_overlay_v2(overlay)

    def test_tampered_ccadb_snapshot_hash_is_rejected(self):
        lock = importer.load_json(SOURCE / "source-lock.json")
        lock["ccadb_v5_all_certificate_records"]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory(prefix="navhttps21-lock-tamper-") as directory:
            temp_lock = Path(directory) / "source-lock.json"
            temp_lock.write_bytes(importer.deterministic_json(lock))
            with self.assertRaisesRegex(importer.ImportErrorDetail, "CCADB V5 snapshot hash mismatch"):
                importer._verified_navhttps21_inputs(ROOT, temp_lock)

    def test_contradictory_no_domain_rule_override_is_rejected(self):
        overlay = complete_overlay(domain_restrictions={"status": "none", "domains": ["example.test"]})
        with self.assertRaisesRegex(importer.ImportErrorDetail, "contradicts"):
            importer.validate_restrictions_overlay_v2(overlay)

    def test_required_ct_evidence_without_enforcer_excludes_backdated_leaf(self):
        overlay = complete_overlay(issuance_evidence={"status": "required", "types": ["certificate-transparency-sct"]})
        reasons = importer.evaluate_root_eligibility(eligible_root(), overlay)
        self.assertIn("issuance-evidence-not-enforceable", reasons)
        cutoff = dt.datetime(2604, 1, 5, 23, 59, 59, tzinfo=dt.timezone.utc)
        backdated_not_before = cutoff - dt.timedelta(days=1)
        self.assertFalse(importer.issued_after_distrust_after(backdated_not_before, cutoff))

    def test_overlay_cannot_claim_ct_enforcement_to_allow_backdated_certificate(self):
        overlay = complete_overlay(
            issuance_evidence={"status": "required", "types": ["certificate-transparency-sct"]},
            supported_enforcement_mechanisms=["certificate-transparency-sct-verification"],
        )
        reasons = importer.evaluate_root_eligibility(eligible_root(), overlay)
        self.assertIn("issuance-evidence-not-enforceable", reasons)

    def test_required_certificate_policy_without_policy_tree_is_excluded(self):
        overlay = complete_overlay(
            certificate_policy={"status": "required", "required_oids": ["1.3.6.1.4.1.55555.21.1"]}
        )
        reasons = importer.evaluate_root_eligibility(eligible_root(), overlay)
        self.assertIn("certificate-policy-requirement-not-enforced", reasons)

    def test_overlay_cannot_claim_certificate_policy_tree_enforcement(self):
        overlay = complete_overlay(
            certificate_policy={"status": "required", "required_oids": ["1.3.6.1.4.1.55555.21.1"]},
            supported_enforcement_mechanisms=["selected-policy-tree"],
        )
        reasons = importer.evaluate_root_eligibility(eligible_root(), overlay)
        self.assertIn("certificate-policy-requirement-not-enforced", reasons)

    def test_unknown_certificate_policy_requirement_is_not_silently_absent(self):
        overlay = complete_overlay(certificate_policy={"status": "unknown", "required_oids": []})
        reasons = importer.evaluate_root_eligibility(eligible_root(), overlay)
        self.assertIn("unsupported-certificate-policy-requirement", reasons)

    def test_policy_oid_and_trust_anchor_identity_are_not_inferred_from_ccadb_ev_metadata(self):
        manifest = json.loads((ROOT / "assets/certs/candidates/mozilla-nss-2026-11-13.navhttps21-trust-manifest.json").read_text(encoding="utf-8"))
        root = next(item for item in manifest["roots"] if item["nss_label"] == "DigiCert TLS ECC P384 Root G5")
        self.assertEqual(root["ccadb_record"]["Mozilla Status"], "Included")
        self.assertEqual(root["external_restrictions"], "unknown")
        self.assertEqual(root["certificate_policy_status"], "unknown")
        self.assertEqual(root["technical_eligibility"], "ineligible")

    def test_ccadb_marks_unmatched_buypass_roots_removed(self):
        manifest = json.loads((ROOT / "assets/certs/candidates/mozilla-nss-2026-11-13.navhttps21-trust-manifest.json").read_text(encoding="utf-8"))
        buypass = [root for root in manifest["roots"] if root.get("nss_label") is None and root["subject"].startswith("CN=Buypass")]
        self.assertEqual(len(buypass), 2)
        self.assertTrue(all("nss-certificate-record-missing" in root["source_rejection_reasons"] for root in buypass))
        self.assertTrue(all(root["ccadb_record"]["Mozilla Status"] == "Removed" for root in buypass))


if __name__ == "__main__":
    unittest.main()

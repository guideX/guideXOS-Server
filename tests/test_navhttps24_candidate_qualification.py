from __future__ import annotations

import copy
import hashlib
import json
import sys
import unittest
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives.serialization import Encoding

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_navhttps24_candidate_manifest as navhttps24  # noqa: E402


MANIFEST_PATH = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps24-qualification.json"
CAPTURE_PATH = ROOT / "tests/fixtures/navhttps24/capture-manifest.json"


class NavHttps24CandidateQualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.capture = json.loads(CAPTURE_PATH.read_text(encoding="utf-8"))

    def test_manifest_is_deterministic_and_all_artifact_pins_match(self):
        generated = navhttps24.canonical_json(navhttps24.build_manifest())
        self.assertEqual(generated, MANIFEST_PATH.read_bytes())
        navhttps24.validate_manifest_artifacts(self.manifest)
        self.assertEqual(hashlib.sha256(CAPTURE_PATH.read_bytes()).hexdigest(),
                         self.manifest["public_chain_capture"]["capture_manifest_sha256"])

    def test_candidate_identity_is_exact_der_and_single_root(self):
        bundle = ROOT / self.manifest["candidate"]["bundle_path"]
        cert = x509.load_pem_x509_certificate(bundle.read_bytes())
        self.assertEqual(hashlib.sha256(cert.public_bytes(Encoding.DER)).hexdigest(), navhttps24.FINGERPRINT)
        self.assertEqual(self.manifest["candidate"]["bundle_root_count"], 1)
        self.assertEqual(self.manifest["restriction_metadata"]["selected_root_fingerprints"], [navhttps24.FINGERPRINT])
        self.assertEqual(self.manifest["restriction_metadata"]["other_public_roots_included"], 0)
        self.assertEqual(self.manifest["restriction_metadata"]["other_public_roots_ineligible"], 117)

    def test_presented_chain_contains_distinct_self_signed_and_cross_signed_identities(self):
        certs = self.capture["certificates"]
        self.assertEqual(self.capture["presented_certificate_count"], 5)
        self.assertEqual(certs[2]["sha256"], navhttps24.FINGERPRINT)
        self.assertEqual(certs[2]["subject"], certs[3]["subject"])
        self.assertNotEqual(certs[2]["sha256"], certs[3]["sha256"])
        self.assertEqual(certs[3]["issuer"], certs[4]["subject"])
        self.assertEqual(certs[0]["subject_alt_name_dns"], [self.capture["hostname"]])
        self.assertEqual(certs[0]["not_before_utc"], "2026-11-14T00:00:00Z")
        self.assertEqual(certs[0]["not_after_utc"], "2026-12-13T23:59:59Z")
        self.assertFalse(certs[0]["name_constraints_present"])
        self.assertEqual(certs[0]["signature_hash_algorithm"], "sha384")
        self.assertTrue(certs[0]["ocsp_urls"])
        self.assertTrue(certs[0]["crl_distribution_urls"])

    def test_profile_and_restriction_scope_remain_diagnostic_only(self):
        self.assertEqual(self.manifest["runtime_profile"]["profile_class"], "candidate")
        self.assertEqual(self.manifest["runtime_profile"]["activation_scope"], "disabled-offline-qualification-only")
        self.assertEqual(self.manifest["runtime_profile"]["clock_assurance"], "authenticated-fresh-rollback-protected-required")
        self.assertFalse(self.manifest["restriction_metadata"]["ev_supported"])
        self.assertFalse(self.manifest["restriction_metadata"]["ev_claimed"])
        self.assertEqual(self.manifest["restriction_metadata"]["ordinary_certificate_policy"], "not-required")
        self.assertEqual(self.manifest["restriction_metadata"]["full_rfc5280_policy_tree"], "unsupported-and-disabled")
        self.assertEqual(self.manifest["restriction_metadata"]["name_constraints"], "none-on-root; path enforcement retained")
        self.assertEqual(self.manifest["restriction_metadata"]["distrust_after_policy"], "NAVHTTPS21 enforcement retained; no G5 cutoff identified")
        self.assertFalse(self.manifest["activation_gates"]["release_owner_authorized"])
        self.assertFalse(self.manifest["activation_gates"]["public_https_enabled"])

    def test_candidate_identity_root_count_manifest_scope_and_approval_tampering_fail_closed(self):
        mutations = [
            ("root fingerprint", lambda m: m["candidate"].__setitem__("root_sha256", "0" * 64)),
            ("root count", lambda m: m["candidate"].__setitem__("bundle_root_count", 2)),
            ("manifest scope", lambda m: m["restriction_metadata"].__setitem__("coverage_scope", "full-inventory")),
            ("additional root", lambda m: m["restriction_metadata"].__setitem__("other_public_roots_included", 1)),
            ("candidate enabled", lambda m: m["candidate"].__setitem__("candidate_enabled", True)),
            ("public HTTPS enabled", lambda m: m["candidate"].__setitem__("public_https_enabled", True)),
            ("release approval", lambda m: m["activation_gates"].__setitem__("release_owner_authorized", True)),
            ("time scope", lambda m: m["runtime_profile"].__setitem__("offline_time_scope", "production")),
            ("missing restriction metadata", lambda m: m["restriction_metadata"].pop("external_restrictions")),
            ("unknown certificate policy", lambda m: m["restriction_metadata"].__setitem__("ordinary_certificate_policy", "required-but-unsupported")),
            ("EV processing enabled", lambda m: m["restriction_metadata"].__setitem__("ev_supported", True)),
            ("RFC 5280 policy tree enabled", lambda m: m["restriction_metadata"].__setitem__("full_rfc5280_policy_tree", "supported")),
            ("name constraints omitted", lambda m: m["restriction_metadata"].__setitem__("name_constraints", "ignored")),
            ("candidate production approval", lambda m: m["candidate"].__setitem__("production_approved", True)),
            ("unsupported manifest schema", lambda m: m.__setitem__("schema_version", "unsupported")),
        ]
        for label, mutate in mutations:
            with self.subTest(label=label):
                changed = copy.deepcopy(self.manifest)
                mutate(changed)
                with self.assertRaises(ValueError):
                    navhttps24.validate_manifest_artifacts(changed)

    def test_modified_bundle_and_capture_artifacts_fail_integrity(self):
        changed = copy.deepcopy(self.manifest)
        changed["candidate"]["bundle_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "bundle integrity"):
            navhttps24.validate_manifest_artifacts(changed)
        changed = copy.deepcopy(self.manifest)
        first = next(iter(changed["qualification_fixtures"].values()))
        first["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "fixture integrity"):
            navhttps24.validate_manifest_artifacts(changed)


if __name__ == "__main__":
    unittest.main()

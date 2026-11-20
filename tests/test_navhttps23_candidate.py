from __future__ import annotations

import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

from cryptography import x509
from cryptography.x509.oid import NameOID


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import navigator_mozilla_trust_metadata as importer  # noqa: E402


FINGERPRINT = "018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05"
SOURCE_LOCK_PATH = ROOT / "assets/certs/mozilla/navhttps23-sources/source-lock.json"
OVERLAY_PATH = ROOT / "assets/certs/mozilla/navhttps23-sources/root-restrictions-overlay.json"
MANIFEST_PATH = ROOT / "assets/certs/candidates/mozilla-nss-2026-11-19.navhttps23-trust-manifest.json"
CANDIDATE_PATH = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.pem"
SIDECAR_PATH = ROOT / "assets/certs/candidates/digicert-tls-ecc-p384-root-g5-navhttps23.metadata.json"
FIXTURE_MANIFEST_PATH = ROOT / "tests/fixtures/navhttps23/manifest.json"
QUALIFICATION_PATH = ROOT / "tests/fixtures/navhttps23/mbedtls-qualification.json"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: dict) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=False) + "\n").encode("utf-8")


def write_temp_lock(directory: Path, lock: dict) -> Path:
    path = directory / "source-lock.json"
    path.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    return path


class NavHttps23CandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock = load_json(SOURCE_LOCK_PATH)
        cls.overlay = load_json(OVERLAY_PATH)
        cls.manifest = load_json(MANIFEST_PATH)
        cls.sidecar = load_json(SIDECAR_PATH)
        cls.qualification = load_json(QUALIFICATION_PATH)
        cls.selected_root = next(
            root for root in cls.manifest["roots"] if root["certificate_sha256"] == FINGERPRINT
        )

    def test_selected_identity_is_exact_der_and_only_candidate(self):
        self.assertEqual(self.selected_root["certificate_sha256"], FINGERPRINT)
        pem = CANDIDATE_PATH.read_bytes()
        self.assertEqual(len(importer.parse_pem_bundle(pem)), 1)
        der = importer.parse_pem_bundle(pem)[0]
        self.assertEqual(digest(der), FINGERPRINT)
        certificate = x509.load_der_x509_certificate(der)
        self.assertEqual(certificate.subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0].value,
                         "DigiCert TLS ECC P384 Root G5")

    def test_selected_root_has_complete_scoped_source_coverage(self):
        coverage = self.selected_root["restriction_coverage_by_attribute"]
        self.assertEqual(set(coverage), set(importer.RESTRICTION_COVERAGE_FIELDS))
        self.assertTrue(all(entry["status"] == "complete" for entry in coverage.values()))
        self.assertTrue(self.manifest["candidate_source_coverage_complete"])
        self.assertTrue(self.manifest["external_policy_complete"])
        self.assertEqual(self.manifest["external_policy_coverage_scope"], "selected-root-set")
        self.assertFalse(self.manifest["full_inventory_external_policy_complete"])
        self.assertEqual(self.manifest["selected_root_fingerprints"], [FINGERPRINT])

    def test_other_roots_remain_unknown_and_excluded(self):
        roots = self.manifest["roots"]
        self.assertEqual(len(roots), 118)
        other_roots = [root for root in roots if root["certificate_sha256"] != FINGERPRINT]
        self.assertEqual(len(other_roots), 117)
        self.assertTrue(all(root["technical_eligibility"] == "ineligible" for root in other_roots))
        self.assertTrue(all(root["candidate_included"] is False for root in other_roots))
        self.assertTrue(all(root["domain_restrictions_status"] == "unknown" for root in other_roots))

    def test_domain_policy_and_ordinary_certificate_policy_are_scoped(self):
        self.assertEqual(self.selected_root["domain_restrictions_status"], "none")
        self.assertEqual(self.selected_root["name_constraints_status"], "none")
        self.assertEqual(self.selected_root["certificate_policy_status"], "not-required")
        record = self.overlay["records"][FINGERPRINT]
        self.assertEqual(record["certificate_policy"]["required_oids"], [])
        self.assertTrue(record["certificate_policy"]["mozilla_ev_mapping_exists"])
        self.assertIn("unsupported-and-disabled", record["certificate_policy"]["candidate_ev_processing"])
        self.assertEqual(record["domain_restrictions"]["ccadb_mozilla_constrained"], "false")
        self.assertFalse(record["domain_restrictions"]["nss_exact_subject_match"])

    def test_offline_path_evidence_is_hash_bound_to_fixtures_and_mbedtls(self):
        report = self.selected_root["candidate_path_validation"]
        self.assertEqual(report["status"], "verified-by-mbedtls-4.1.0")
        self.assertEqual(report["passed_case_count"], 11)
        self.assertEqual(report["fixture_manifest_sha256"], digest(FIXTURE_MANIFEST_PATH.read_bytes()))
        self.assertEqual(report["qualification_report_sha256"], digest(QUALIFICATION_PATH.read_bytes()))
        self.assertEqual(self.qualification["mbedtls_source_revision"], "0fe989b6b514192783c469039edd325fd0989806")
        self.assertEqual(self.qualification["passed_case_count"], self.qualification["expected_case_count"])

    def test_candidate_remains_disabled_and_not_production_approved(self):
        activation = self.sidecar["activation"]
        self.assertFalse(activation["candidate_enabled"])
        self.assertFalse(activation["production_approved"])
        self.assertFalse(activation["public_https_enabled"])
        self.assertFalse(activation["fixture_roots_included"])
        self.assertEqual(self.manifest["production_approved_root_count"], 0)
        self.assertFalse(self.manifest["public_https_enabled"])
        self.assertFalse(self.sidecar["qualification_states"]["guest_distrust_after_qualified"])

    def test_candidate_metadata_sidecar_self_hash_is_valid(self):
        sidecar = copy.deepcopy(self.sidecar)
        expected = sidecar.pop("sidecar_sha256")
        self.assertEqual(expected, digest(canonical_json(sidecar)))
        self.assertEqual(self.sidecar["artifacts"]["trust_manifest_sha256"], digest(MANIFEST_PATH.read_bytes()))
        self.assertEqual(self.sidecar["artifacts"]["candidate_bundle_sha256"], digest(CANDIDATE_PATH.read_bytes()))

    def test_importer_regenerates_identical_candidate_artifacts(self):
        with tempfile.TemporaryDirectory(prefix="navhttps23-determinism-") as directory:
            temp_dir = Path(directory)
            output = temp_dir / "trust-manifest.json"
            candidate = temp_dir / "candidate.pem"
            regenerated = importer.run_import(ROOT, output, candidate, SOURCE_LOCK_PATH)
            self.assertEqual(output.read_bytes(), MANIFEST_PATH.read_bytes())
            self.assertEqual(candidate.read_bytes(), CANDIDATE_PATH.read_bytes())
            self.assertEqual(regenerated["candidate_included_root_count"], 1)

    def test_tampered_authority_source_pin_fails_closed(self):
        lock = copy.deepcopy(self.lock)
        entry = next(source for source in lock["navhttps23_pinned_sources"]
                     if source["source_id"] == "mozilla-nss-genname-8c60e757")
        entry["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory(prefix="navhttps23-source-tamper-") as directory:
            lock_path = write_temp_lock(Path(directory), lock)
            with self.assertRaisesRegex(importer.ImportErrorDetail, "pinned NAVHTTPS23 source hash mismatch"):
                importer._verified_navhttps21_inputs(ROOT, lock_path)

    def test_tampered_test_input_pin_fails_closed(self):
        lock = copy.deepcopy(self.lock)
        entry = next(source for source in lock["navhttps23_test_inputs"]
                     if source["artifact_id"] == "guidexos-mbedtls-config")
        entry["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory(prefix="navhttps23-test-input-tamper-") as directory:
            lock_path = write_temp_lock(Path(directory), lock)
            with self.assertRaisesRegex(importer.ImportErrorDetail, "pinned NAVHTTPS23 test input hash mismatch"):
                importer._verified_navhttps21_inputs(ROOT, lock_path)

    def test_unsupported_qualification_report_fails_closed(self):
        lock = copy.deepcopy(self.lock)
        entry = next(source for source in lock["navhttps23_pinned_sources"]
                     if source["source_id"] == "navhttps23-mbedtls-qualification")
        invalid_report = copy.deepcopy(self.qualification)
        invalid_report["schema_version"] = "unsupported"
        invalid_report_bytes = canonical_json(invalid_report)
        with tempfile.TemporaryDirectory(prefix="navhttps23-invalid-report-") as directory:
            report_path = Path(directory) / "qualification.json"
            report_path.write_bytes(invalid_report_bytes)
            entry["local_path"] = str(report_path)
            entry["sha256"] = digest(invalid_report_bytes)
            # The assessment pin is independently bound to the accepted report.
            lock_path = write_temp_lock(Path(directory), lock)
            with self.assertRaisesRegex(importer.ImportErrorDetail, "assessment does not bind"):
                importer._verified_navhttps21_inputs(ROOT, lock_path)


if __name__ == "__main__":
    unittest.main()

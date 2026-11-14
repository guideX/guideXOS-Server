#!/usr/bin/env python3
"""Deterministic R26 evidence and packaging rehearsal; never launches QEMU."""

from __future__ import annotations

import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path


ANALYZER_PATH = Path(__file__).with_name("analyze-c188r26-first-failure.py")
SPEC = importlib.util.spec_from_file_location("c188r26_first_failure", ANALYZER_PATH)
assert SPEC is not None and SPEC.loader is not None
R26 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(R26)
R25 = R26._R25


class FirstFailureRehearsal(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.candidate = self.root / "candidate"
        self.candidate.mkdir()
        elf = bytearray(0x300)
        elf[:4] = b"\x7fELF"
        elf[4:6] = b"\x02\x01"
        struct.pack_into("<Q", elf, 32, 64)
        struct.pack_into("<HH", elf, 54, 56, 1)
        # One executable/writable load segment includes a zero-filled record tail.
        struct.pack_into("<IIQQQQQQ", elf, 64, 1, 7, 0x100, 0x1000, 0, 0x100, 0x200, 0x1000)
        self.elf_path = self.candidate / "managed.elf"
        self.elf_path.write_bytes(elf)
        self.map_path = self.candidate / "managed.map"
        self.map_path.write_text(
            "0000000000000000 failAtInitialization 0000000000001018 f runtime-pack/platform.obj\n"
            "0000000000000000 guideXosNativeAotR25CaptureCallerReturnAddress 0000000000001030 f runtime-pack/platform.obj\n"
            "0000000000000000 guideXosFailFastImpl 0000000000001038 f runtime-pack/platform.obj\n"
            "0000000000000000 guideXosNativeAotR25FailFast 0000000000001040 f runtime-pack/platform.obj\n"
            "0000000000000000 unrelatedFunction 0000000000001060 f other.obj\n"
            "0000000000000000 g_guideXosR25FailFastOriginRecord 0000000000001100 .bss\n",
            encoding="utf-8",
        )
        # A real direct rel32 call whose return PC is 0x1025 and target is 0x1030.
        call_offset = 0x100 + 0x1020 - 0x1000
        elf[call_offset] = 0xE8
        struct.pack_into("<i", elf, call_offset + 1, 0x1030 - 0x1025)
        self.elf_path.write_bytes(elf)
        runtime = self.candidate / "runtime-pack"
        runtime.mkdir()
        (runtime / "runtime-pack.manifest.json").write_text(
            json.dumps({"c188r25FailFastOriginCapture": True, "identity": "r26-test"}), encoding="utf-8"
        )
        self.addresses_path = self.candidate / "r24-runtime-addresses.json"
        self.addresses_path.write_text(json.dumps({
            "initializeModules": "1070", "rhpRegisterOsModule": "1080",
            "createTypeManagers": "1090", "initializeModulesCallReturn": "10A0",
            "rhpRegisterCallReturn": "1088", "createTypeManagersCall": "1078",
            "osModuleHandle": "100000000",
        }), encoding="utf-8")
        self.manifest = {
            "managedElf": str(self.elf_path), "managedMap": str(self.map_path),
            "managedElfSha256": R26.hashlib.sha256(self.elf_path.read_bytes()).hexdigest().upper(),
            "managedMapSha256": R26.hashlib.sha256(self.map_path.read_bytes()).hexdigest().upper(),
            "runtimePackManifest": str(runtime / "runtime-pack.manifest.json"),
            "r24RuntimeAddresses": str(self.addresses_path),
        }
        (self.candidate / "candidate-manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")
        self.valid_words = [R25.MAGIC, R25.SCHEMA, R25.RECORD_BYTES, 9, 7,
                            0x1025, 0x1038, 2, R25.EXPECTED_RECORD_FLAGS, R25.PUBLISHED]

    def tearDown(self) -> None:
        self.temp.cleanup()

    def snapshot(self, words: list[int] | None = None, rip: int = 0x103A) -> dict[str, object]:
        actual = self.valid_words if words is None else words
        valid, _ = R25.validate_words(actual)
        return {
            "schema": "GUIDEXOS_C188R25_FAILFAST_ORIGIN_SNAPSHOT_V1",
            "trigger": "fatal-marker", "recordAddress": "0x1100",
            "recordWords": [f"0x{word:016X}" for word in actual], "recordValid": valid,
            "registersText": f"RIP={rip:016X} RSP=0000000000002000",
        }

    def run_analyze(self, snapshot: dict[str, object] | None, serial: str = "",
                    run: dict[str, object] | None = None, runner_stderr: str = "") -> dict[str, object]:
        return R26.analyze(self.candidate, snapshot, serial, run, runner_stderr)

    def test_01_valid_first_failure_record_and_source_reason_are_separate(self) -> None:
        result = self.run_analyze(self.snapshot(), "[C188R25-FAILFAST-CAPTURED]\n")
        self.assertEqual(result["fastFailCategory"], "FAST_FAIL_FATAL_APP_EXIT")
        self.assertEqual(result["originalSourceReason"]["value"], 9)
        self.assertEqual(result["result"], "FIRST_FAILURE_CALLER_ATTRIBUTED")

    def test_02_no_failure_and_initializer_return(self) -> None:
        run = {"outcome": "INITIALIZEMODULES_RETURN_VERIFIED", "qacReleaseVerified": True,
               "nativeExecutionAttempted": True, "milestones": {"M7": True}}
        serial = "[C102-STARTUP] stage=C188B005\n[C102-STARTUP] stage=C188B002\n"
        result = self.run_analyze(None, serial, run)
        self.assertEqual(result["result"], "INITIALIZEMODULES_RETURN_VERIFIED")
        self.assertTrue(result["verifiedMilestones"]["M7"])

    def test_03_valid_record_with_unknown_caller(self) -> None:
        words = self.valid_words.copy(); words[5] = 0x1065
        result = self.run_analyze(self.snapshot(words))
        self.assertEqual(result["record"]["state"], "valid")
        self.assertEqual(result["result"], "FIRST_FAILURE_EVIDENCE_INCOMPLETE")
        self.assertEqual(result["sourceAttributionConfidence"], "none")

    def test_04_invalid_caller_address_is_rejected(self) -> None:
        words = self.valid_words.copy(); words[5] = 0
        result = self.run_analyze(self.snapshot(words))
        self.assertFalse(result["record"]["valid"])
        self.assertEqual(result["record"]["classification"], "caller_capture_address_missing")

    def test_05_known_caller_maps_to_candidate_function_and_object(self) -> None:
        result = self.run_analyze(self.snapshot())
        caller = result["capturedCaller"]
        self.assertEqual(caller["sourceFunction"], "failAtInitialization")
        self.assertEqual(caller["sourceObject"], "runtime-pack/platform.obj")
        self.assertIn("source line", result["sourceAttributionConfidence"])

    def test_06_absent_record_is_not_invented_from_a_timeout(self) -> None:
        run = {"outcome": "FAILFAST_EVIDENCE_BLOCKED", "nativeExecutionAttempted": True,
               "qacReleaseVerified": True}
        run["qacReservationId"] = None
        result = self.run_analyze(None, "", run, "Guest evidence root already exists")
        self.assertEqual(result["record"]["state"], "absent")
        self.assertEqual(result["record"]["classification"], "capture_not_reached_guest_not_launched")
        self.assertEqual(result["guestLaunchStatus"], "not_launched_runner_rejected_evidence_root")

    def test_07_partial_or_corrupt_record_fails_publication_validation(self) -> None:
        words = self.valid_words.copy(); words[9] = 0
        result = self.run_analyze(self.snapshot(words))
        self.assertFalse(result["record"]["valid"])
        self.assertEqual(result["record"]["classification"], "record_not_published")

    def test_08_record_cpu_and_serial_contradiction_is_incomplete(self) -> None:
        result = self.run_analyze(self.snapshot(rip=0x1065), "[C188R25-FAILFAST-CAPTURED]\n")
        self.assertTrue(result["contradictions"])
        self.assertEqual(result["result"], "FIRST_FAILURE_EVIDENCE_INCOMPLETE")

    def test_09_stage_one_failure_stays_before_initialize_modules(self) -> None:
        words = self.valid_words.copy(); words[7] = 1
        result = self.run_analyze(self.snapshot(words), "[C102-STARTUP] stage=C188B006\n")
        self.assertEqual(result["record"]["initializationStage"], "native_module_preparation")
        self.assertEqual(result["lastIndependentlyVerifiedExecutionMilestone"],
                         "NativeAOT initialization / code-manager preparation reached")

    def test_10_stage_two_does_not_claim_initializer_entry(self) -> None:
        result = self.run_analyze(self.snapshot(), "[C102-STARTUP] stage=C188B007\n")
        self.assertEqual(result["record"]["initializationStage"], "before_or_inside_InitializeModules")
        self.assertEqual(result["lastIndependentlyVerifiedExecutionMilestone"],
                         "immediately before generated InitializeModules call")
        self.assertFalse(result["verifiedMilestones"]["M3"])

    def test_11_stage_three_and_managed_entry_prove_failure_after_registration(self) -> None:
        words = self.valid_words.copy(); words[7] = 3
        serial = "[C102-STARTUP] stage=C188B005\n[C102-STARTUP] stage=C188B002\n"
        result = self.run_analyze(self.snapshot(words), serial)
        self.assertEqual(result["lastIndependentlyVerifiedExecutionMilestone"], "ManagedMain entry reached")
        self.assertTrue(result["verifiedMilestones"]["M5"])

    def test_12_guest_timeout_without_valid_record_stays_incomplete(self) -> None:
        run = {"outcome": "FAILFAST_EVIDENCE_BLOCKED", "nativeExecutionAttempted": True,
               "qacReleaseVerified": True}
        result = self.run_analyze(self.snapshot([0] * 10), "", run)
        self.assertEqual(result["result"], "FIRST_FAILURE_EVIDENCE_INCOMPLETE")
        self.assertEqual(result["record"]["classification"], "guest_timeout_without_valid_first_failure_record")

    def test_13_post_qemu_package_hashes_are_complete_without_a_guest(self) -> None:
        run_root = self.root / "post-qemu-fixture"
        run_root.mkdir()
        (run_root / "serial.log").write_bytes(b"serial evidence")
        (run_root / "snapshot.json").write_bytes(b"snapshot evidence")
        package = R26.build_post_qemu_hash_manifest(
            run_root, self.candidate, ["serial.log", "snapshot.json", "missing.log"], 0, True
        )
        self.assertEqual(package["schema"], "GUIDEXOS_C188R25_POST_QEMU_ARTIFACT_HASHES_V1")
        self.assertEqual(package["qemuExitCode"], 0)
        self.assertTrue(package["qacReleaseVerified"])
        self.assertEqual([entry["path"] for entry in package["files"]], ["serial.log", "snapshot.json"])
        self.assertTrue(all(len(entry["sha256"]) == 64 for entry in package["files"]))
        package_path = run_root / "r25-final-artifact-hashes.json"
        self.assertFalse(package_path.exists())
        package_path.write_text(json.dumps(package, sort_keys=True), encoding="utf-8")
        reloaded = json.loads(package_path.read_text(encoding="utf-8"))
        self.assertEqual(reloaded, package)
        self.assertEqual(len(R26.hashlib.sha256(package_path.read_bytes()).hexdigest()), 64)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FirstFailureRehearsal)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print(f"R26_FIRST_FAILURE_REHEARSAL={result.testsRun} RESULT={'PASS' if result.wasSuccessful() else 'FAIL'}")
    raise SystemExit(not result.wasSuccessful())

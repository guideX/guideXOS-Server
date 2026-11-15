#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_gc_state_guard.h"
PLATFORM = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp"
ORIGIN_HEADER = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_failfast_origin.h"
ANALYZER_PATH = Path(__file__).with_name("analyze-c188r25-failfast-origin.py")
SPEC = importlib.util.spec_from_file_location("r25_failfast_analyzer", ANALYZER_PATH)
assert SPEC is not None and SPEC.loader is not None
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


def r28_words(
    subcondition: int,
    read_status: int,
    thread_result: int,
    heap_result: int,
    expected_thread: int = 0x101,
    observed_thread: int = 0x101,
    expected_heap: int = 0x303,
    observed_heap: int = 0x303,
) -> list[int]:
    return [
        ANALYZER.MAGIC, ANALYZER.R28_SCHEMA, ANALYZER.R28_RECORD_BYTES,
        8, 7, 0x1000F3A32, 0x1000F2F70, 1, 7,
        subcondition, read_status & ((1 << 64) - 1),
        thread_result, heap_result,
        expected_thread, observed_thread, expected_heap, observed_heap,
        ANALYZER.PUBLISHED,
    ]


class R28RecordTests(unittest.TestCase):
    def test_original_r27_ten_word_record_remains_valid(self) -> None:
        words = [ANALYZER.MAGIC, 1, 80, 8, 7, 0x1000F3A32,
                 0x1000F2F70, 1, 7, ANALYZER.PUBLISHED]
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "first_failure_record_valid"))

    def test_gc_state_read_failure_is_unavailable_and_stops_predicates(self) -> None:
        words = r28_words(1, -1, 0, 0, 0x101, 0, 0x303, 0)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_STATE_READ_FAILED"))
        valid, classification, evidence = ANALYZER.validate_r28_guard_words(words)
        self.assertTrue(valid)
        self.assertEqual(classification, "GC_STATE_READ_FAILED")
        self.assertEqual(evidence["gcStateReadStatus"], -1)
        self.assertEqual(evidence["threadIdentityResult"], 0)
        self.assertEqual(evidence["heapIdentityResult"], 0)

    def test_thread_identity_mismatch_is_first_when_both_ids_mismatch(self) -> None:
        words = r28_words(2, 0, 2, 2, 0x101, 0x202, 0x303, 0x404)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_THREAD_IDENTITY_MISMATCH"))
        self.assertEqual(ANALYZER.validate_r28_guard_words(words)[2]["heapIdentityResult"], 2)

    def test_heap_identity_mismatch_is_reported_independently(self) -> None:
        words = r28_words(3, 0, 1, 2, 0x101, 0x101, 0x303, 0x404)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_HEAP_IDENTITY_MISMATCH"))

    def test_matching_thread_and_heap_values_are_reported(self) -> None:
        words = r28_words(0, 0, 1, 1)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "guard_predicates_all_match"))

    def test_non_guard_first_failure_keeps_a_valid_empty_discriminator(self) -> None:
        words = r28_words(0, 0, 0, 0, 0, 0, 0, 0)
        words[3] = 6
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "guard_evidence_not_applicable"))

    def test_reason_eight_with_empty_discriminator_is_not_promoted_to_a_pass(self) -> None:
        words = r28_words(0, 0, 0, 0, 0, 0, 0, 0)
        valid, classification = ANALYZER.validate_words(words)
        self.assertTrue(valid)  # The published record is structurally intact.
        self.assertEqual(classification, "GC_STATE_FAILURE_NOT_DISCRIMINATED")
        _valid, _classification, evidence = ANALYZER.validate_r28_guard_words(words)
        self.assertEqual(evidence["firstFailedSubcondition"], "UNRECORDED")
        self.assertEqual(evidence["discriminatorStatus"],
                         "reason_8_failure_without_guard_details")

    def test_truncated_and_unpublished_r28_records_are_rejected(self) -> None:
        words = r28_words(1, -1, 0, 0, 0x101, 0, 0x303, 0)
        self.assertEqual(ANALYZER.validate_words(words[:-1])[1],
                         "record_word_count_invalid")
        words[17] = 0
        self.assertEqual(ANALYZER.validate_words(words)[1], "record_not_published")

    def test_failed_read_cannot_claim_identity_mismatch(self) -> None:
        words = r28_words(1, -1, 2, 2, 0x101, 0x202, 0x303, 0x404)
        self.assertEqual(ANALYZER.validate_words(words)[1],
                         "gc_state_read_failure_record_inconsistent")

    def test_predicate_bits_must_match_raw_identity_values(self) -> None:
        words = r28_words(2, 0, 2, 1, 0x101, 0x101, 0x303, 0x303)
        self.assertEqual(ANALYZER.validate_words(words)[1],
                         "identity_predicate_values_conflict")

    def _make_candidate(self, r28: bool) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        temporary = tempfile.TemporaryDirectory()
        candidate = Path(temporary.name)
        elf = bytearray(0x300)
        elf[:4] = b"\x7fELF"
        elf[4:6] = b"\x02\x01"
        struct.pack_into("<Q", elf, 32, 64)
        struct.pack_into("<HH", elf, 54, 56, 1)
        struct.pack_into("<IIQQQQQQ", elf, 64, 1, 7, 0x100, 0x1000, 0,
                         0x100, 0x200, 0x1000)
        caller_pc = 0x1025
        call_offset = 0x100 + caller_pc - 0x1000 - 5
        elf[call_offset] = 0xE8
        struct.pack_into("<i", elf, call_offset + 1, 0x1000 - caller_pc)
        elf_path = candidate / "managed.elf"
        map_path = candidate / "managed.map"
        symbols = [
            "0000000000000000 guideXosNativeAotR25CaptureCallerReturnAddress 0000000000001000 f runtime-pack/platform.obj",
            "0000000000000000 guideXosFailFastImpl 0000000000001008 f runtime-pack/platform.obj",
            "0000000000000000 failAtInitialization 0000000000001018 f runtime-pack/platform.obj",
            "0000000000000000 g_guideXosR25FailFastOriginRecord 0000000000001100 .bss",
        ]
        if r28:
            symbols = [line.replace("failAtInitialization", "RhpNewArray") for line in symbols]
            # The R28 helper is a direct rel32 call from the RhpNewArray test
            # body at 0x1018. Its synthetic executable body begins at 0x1030.
            symbols += [
                "0000000000000000 guideXosNativeAotR28FailFastGcStateGuard 0000000000001030 f runtime-pack/platform.obj",
                "0000000000000000 guideXosStockRhpNewArray 0000000000001040 f runtime/allocfast.obj",
                "0000000000000000 guidexos_nativeaot_gc_read_state 0000000000001050 f runtime/gcprobe.obj",
            ]
            call_at = 0x100 + 0x1018 - 0x1000
            elf[call_at] = 0xE8
            struct.pack_into("<i", elf, call_at + 1, 0x1030 - (0x1018 + 5))
        elf_path.write_bytes(elf)
        map_path.write_text("\n".join(symbols) + "\n", encoding="utf-8")
        runtime_dir = candidate / "runtime-pack"
        runtime_dir.mkdir()
        runtime_manifest = runtime_dir / "runtime-pack.manifest.json"
        runtime_manifest.write_text(json.dumps({
            "c188r25FailFastOriginCapture": True,
            "identity": "test-r28" if r28 else "test-r25",
            "c188r28GcStateGuardDiscriminator": r28,
            "c188r28GcStateGuardDiscriminatorMarker": (
                "GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR" if r28 else None
            ),
            "failFastOriginRecordSchema": 2 if r28 else 1,
            "failFastOriginRecordBytes": 144 if r28 else 80,
            "renamedRuntimeSymbols": {"RhpNewArray": "guideXosStockRhpNewArray" if r28 else None},
        }), encoding="utf-8")
        (candidate / "candidate-manifest.json").write_text(json.dumps({
            "managedMap": str(map_path), "managedElf": str(elf_path),
            "runtimePackManifest": str(runtime_manifest), "managedElfSha256": "test",
        }), encoding="utf-8")
        return temporary, candidate

    def test_candidate_link_qualifies_r28_record_and_capture_entry(self) -> None:
        temporary, candidate = self._make_candidate(r28=True)
        try:
            result = ANALYZER.verify_candidate(candidate)
            self.assertEqual(result["recordBytes"], 144)
            self.assertEqual(result["recordSchema"], 2)
            self.assertTrue(result["recordSectionWritable"])
        finally:
            temporary.cleanup()

    def test_candidate_link_preserves_r25_eighty_byte_schema(self) -> None:
        temporary, candidate = self._make_candidate(r28=False)
        try:
            result = ANALYZER.verify_candidate(candidate)
            self.assertEqual(result["recordBytes"], 80)
            self.assertEqual(result["recordSchema"], 1)
        finally:
            temporary.cleanup()


class ProductionSourceTests(unittest.TestCase):
    def test_post_allocation_guard_uses_shared_classifier_and_preserves_failfast(self) -> None:
        source = PLATFORM.read_text(encoding="utf-8")
        self.assertIn("guideXosR28ClassifyGcStateGuard", source)
        self.assertIn("guideXosFailFastGcStateGuard(guardEvidence)", source)
        allocation_check = source[source.index("if (result == nullptr)", source.index("void* result = guideXosStockRhpNewArray(eeType, length);")):]
        self.assertIn("guideXosFailFast(6u);", allocation_check[:900])
        self.assertIn("guideXosNativeAotR25FailFast", source)
        self.assertIn("guideXosNativeAotR28FailFastGcStateGuard", source)

    def test_record_publication_remains_first_writer_and_publication_last(self) -> None:
        source = PLATFORM.read_text(encoding="utf-8")
        header = ORIGIN_HEADER.read_text(encoding="utf-8")
        self.assertGreaterEqual(source.count("_InterlockedCompareExchange(&g_guideXosR25FailFastClaim, 1, 0) == 0"), 2)
        self.assertIn("record.guardFailureSubcondition = evidence.firstFailedSubcondition;", source)
        self.assertRegex(source, r"record\.observedGcHeap = evidence\.observedGcHeap;[\s\S]*record\.published")
        self.assertIn("kGuideXosR28GuardGcStateReadFailed = 1u", header)
        self.assertIn("kGuideXosR28GuardThreadIdentityMismatch = 2u", header)
        self.assertIn("kGuideXosR28GuardHeapIdentityMismatch = 3u", header)
        self.assertIn("sizeof(GuideXosR25FailFastOriginRecord) ==\n        kGuideXosR25FailFastOriginRecordBytes", header)

    def test_runtime_and_runner_manifests_carry_r28_profile_and_dynamic_size(self) -> None:
        build = (ROOT / "tools/dotnet/runtime-pack/build-runtime-pack.ps1").read_text(encoding="utf-8")
        runner = (ROOT / "scripts/dotnet/run-c188-qac-guest.ps1").read_text(encoding="utf-8")
        self.assertIn("C188R28GcStateGuardDiscriminator", build)
        self.assertIn("c188r28GcStateGuardDiscriminatorMarker", build)
        self.assertIn("x/{0}gx", runner)
        self.assertIn("$words[$words.Count - 1]", runner)


def find_vcvars() -> Path:
    candidates = [
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"),
    ]
    return next((path for path in candidates if path.is_file()), Path())


class ProductionHeaderAbiTests(unittest.TestCase):
    def test_shared_classifier_compiles_and_runs_with_windows_amd64_msvc(self) -> None:
        vcvars = find_vcvars()
        if not vcvars.is_file():
            self.skipTest("Visual Studio vcvars64.bat is unavailable")
        with tempfile.TemporaryDirectory(prefix="guidexos-r28-gc-guard-") as temp:
            temp_path = Path(temp)
            cpp = temp_path / "gc_state_guard_test.cpp"
            exe = temp_path / "gc_state_guard_test.exe"
            batch = temp_path / "build-and-run.cmd"
            cpp.write_text(
                r'''#include <cstdio>
#include "guidexos_nativeaot_gc_state_guard.h"
int main() {
    unsigned checks = 0;
    auto read = guideXosR28ClassifyGcStateGuard(-1, 0x101, 0x202, 0x303, 0x404);
    if (read.firstFailedSubcondition != kGuideXosR28GuardGcStateReadFailed ||
        read.threadIdentityResult != kGuideXosR28PredicateNotEvaluated ||
        read.heapIdentityResult != kGuideXosR28PredicateNotEvaluated ||
        read.observedCurrentThread != 0 || read.observedGcHeap != 0) return 11;
    ++checks;
    auto thread = guideXosR28ClassifyGcStateGuard(0, 0x101, 0x202, 0x303, 0x303);
    if (thread.firstFailedSubcondition != kGuideXosR28GuardThreadIdentityMismatch ||
        thread.threadIdentityResult != kGuideXosR28PredicateMismatched ||
        thread.heapIdentityResult != kGuideXosR28PredicateMatched) return 12;
    ++checks;
    auto heap = guideXosR28ClassifyGcStateGuard(0, 0x101, 0x101, 0x303, 0x404);
    if (heap.firstFailedSubcondition != kGuideXosR28GuardHeapIdentityMismatch ||
        heap.threadIdentityResult != kGuideXosR28PredicateMatched ||
        heap.heapIdentityResult != kGuideXosR28PredicateMismatched) return 13;
    ++checks;
    auto both = guideXosR28ClassifyGcStateGuard(0, 0x101, 0x202, 0x303, 0x404);
    if (both.firstFailedSubcondition != kGuideXosR28GuardThreadIdentityMismatch ||
        both.threadIdentityResult != kGuideXosR28PredicateMismatched ||
        both.heapIdentityResult != kGuideXosR28PredicateMismatched) return 14;
    ++checks;
    auto valid = guideXosR28ClassifyGcStateGuard(0, 0x101, 0x101, 0x303, 0x303);
    if (valid.firstFailedSubcondition != kGuideXosR28GuardSubconditionNone ||
        valid.threadIdentityResult != kGuideXosR28PredicateMatched ||
        valid.heapIdentityResult != kGuideXosR28PredicateMatched) return 15;
    ++checks;
    std::printf("R28_CPP_CLASSIFIER_ASSERTIONS=%u RESULT=PASS\n", checks);
    return 0;
}
''', encoding="utf-8")
            lines = [
                "@echo off",
                f'call "{vcvars}" >nul',
                "if errorlevel 1 exit /b %errorlevel%",
                f'cl.exe /nologo /W4 /std:c++17 /EHsc /DGUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR /I"{HEADER.parent}" /Fo:"{temp_path / "gc_state_guard_test.obj"}" /Fe:"{exe}" "{cpp}"',
                "if errorlevel 1 exit /b %errorlevel%",
                f'"{exe}"',
                "exit /b %errorlevel%",
            ]
            batch.write_text("\r\n".join(lines) + "\r\n", encoding="ascii")
            result = subprocess.run(["cmd.exe", "/d", "/c", str(batch)],
                                    text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(result.stdout, r"R28_CPP_CLASSIFIER_ASSERTIONS=5 RESULT=PASS")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(__import__(__name__))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print(f"R28_GC_STATE_GUARD_TESTS={result.testsRun} RESULT={'PASS' if result.wasSuccessful() else 'FAIL'}")
    raise SystemExit(not result.wasSuccessful())

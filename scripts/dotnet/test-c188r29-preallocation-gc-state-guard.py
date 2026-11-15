#!/usr/bin/env python3
"""Hosted R29 pre-allocation GC-state guard and schema qualification."""

from __future__ import annotations

import importlib.util
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANALYZER_PATH = Path(__file__).with_name("analyze-c188r25-failfast-origin.py")
SPEC = importlib.util.spec_from_file_location("r25_failfast_analyzer", ANALYZER_PATH)
assert SPEC is not None and SPEC.loader is not None
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)
PLATFORM = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp"
PROBE = ROOT / "tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_gc_allocation_probe.cpp"
HEADER = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_r29_preallocation_guard.h"
ORIGIN = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_failfast_origin.h"


def r29_words(
    subcondition: int,
    status: int,
    mask: int,
    threadstore: int = 0,
    allocation_context: int = 0,
    heap_initialized: int = 0,
    safe_read: int = 0,
    observed_thread: int = 0,
    observed_heap: int = 0,
    location: int = 1,
) -> list[int]:
    return [
        ANALYZER.MAGIC, ANALYZER.R29_SCHEMA, ANALYZER.R29_RECORD_BYTES,
        8, 7, 0x1000F3A25, 0x1000F2F70, 2, 7,
        subcondition, status & ((1 << 64) - 1), 0, 0, 0, observed_thread,
        0, observed_heap, location, mask, threadstore, allocation_context,
        heap_initialized, safe_read, ANALYZER.PUBLISHED,
    ]


class R29RecordTests(unittest.TestCase):
    def test_threadstore_unavailable_is_not_identity_mismatch(self) -> None:
        words = r29_words(1, -1, ANALYZER.R29_EVAL_THREADSTORE, threadstore=2)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "THREADSTORE_CURRENT_THREAD_UNAVAILABLE"))
        self.assertEqual(words[11:17], [0, 0, 0, 0, 0, 0])

    def test_allocation_context_unavailable_stops_heap_checks(self) -> None:
        words = r29_words(2, -1, 3, threadstore=1, allocation_context=2,
                          observed_thread=0x101)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "THREAD_ALLOCATION_CONTEXT_UNAVAILABLE"))
        self.assertEqual(words[21:23], [0, 0])
        self.assertEqual(ANALYZER.validate_words(words)[1],
                         "THREAD_ALLOCATION_CONTEXT_UNAVAILABLE")

    def test_heap_uninitialized_stops_pointer_and_state_reads(self) -> None:
        words = r29_words(3, -1, 7, threadstore=1, allocation_context=1,
                          heap_initialized=2, observed_thread=0x101)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_HEAP_NOT_INITIALIZED"))
        self.assertEqual(words[21:23], [2, 0])

    def test_null_heap_pointer_is_not_reported_as_uninitialized_heap(self) -> None:
        mask = (ANALYZER.R29_EVAL_THREADSTORE | ANALYZER.R29_EVAL_ALLOCATION_CONTEXT |
                ANALYZER.R29_EVAL_GC_HEAP_INITIALIZED | ANALYZER.R29_EVAL_GC_HEAP_POINTER)
        words = r29_words(4, -1, mask, 1, 1, 1, 0, 0x101, 0)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_HEAP_POINTER_UNAVAILABLE"))

    def test_access_violation_during_read_is_distinct_and_partial(self) -> None:
        words = r29_words(8, -2, ANALYZER.R29_EVAL_THREADSTORE,
                          threadstore=1, safe_read=2, observed_thread=0x101)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_STATE_SAFE_READ_FAILED"))
        self.assertEqual(words[20:22], [0, 0])

    def test_access_violation_before_first_prerequisite_is_recordable(self) -> None:
        words = r29_words(8, -2, 0, safe_read=2)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_STATE_SAFE_READ_FAILED"))

    def test_valid_state_does_not_call_failure_helper_or_require_identity_match(self) -> None:
        guard = HEADER.read_text(encoding="utf-8")
        platform = PLATFORM.read_text(encoding="utf-8")
        self.assertIn("evidence.firstFailedSubcondition = kGuideXosR29GuardSubconditionNone;", guard)
        self.assertIn("if (r29GuardEvidence.firstFailedSubcondition !=", platform)
        self.assertIn("kGuideXosR29GuardSubconditionNone)", platform)
        self.assertNotIn("expectedCurrentThread", guard)
        self.assertNotIn("expectedGcHeap", guard)

    def test_incomplete_read_cannot_be_reported_as_valid(self) -> None:
        words = r29_words(9, 0, 15, 1, 1, 1, 0,
                          observed_thread=0x101, observed_heap=0x303)
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_STATE_READ_INCOMPLETE"))

    def test_postallocation_identity_failure_is_not_preallocation_failure(self) -> None:
        words = r29_words(0, 0, 0, location=2)
        words[9:17] = [2, 0, 2, 1, 0x101, 0x202, 0x303, 0x303]
        self.assertEqual(ANALYZER.validate_words(words),
                         (True, "GC_THREAD_IDENTITY_MISMATCH"))
        words[17] = 1
        self.assertEqual(ANALYZER.validate_words(words)[1],
                         "preallocation_record_claims_identity_comparison")

    def test_invalid_state_cannot_claim_success_or_allocator_entry(self) -> None:
        words = r29_words(0, -1, ANALYZER.R29_EVAL_THREADSTORE,
                          threadstore=2)
        words[9] = 0
        valid, classification = ANALYZER.validate_words(words)
        self.assertFalse(valid)
        self.assertNotEqual(classification, "guard_predicates_all_match")

    def test_unpublished_and_truncated_schema_three_records_fail(self) -> None:
        words = r29_words(1, -1, 1, threadstore=2)
        self.assertEqual(ANALYZER.validate_words(words[:-1])[1],
                         "record_word_count_invalid")
        words[-1] = 0
        self.assertEqual(ANALYZER.validate_words(words)[1], "record_not_published")

    def _make_candidate(self) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        temporary = tempfile.TemporaryDirectory()
        candidate = Path(temporary.name)
        elf = bytearray(0x400)
        elf[:4] = b"\x7fELF"
        elf[4:6] = b"\x02\x01"
        struct.pack_into("<Q", elf, 32, 64)
        struct.pack_into("<HH", elf, 54, 56, 1)
        struct.pack_into("<IIQQQQQQ", elf, 64, 1, 7, 0x100, 0x1000, 0,
                         0x200, 0x1000, 0x1000)
        symbols = [
            ("guideXosNativeAotR25CaptureCallerReturnAddress", 0x1000, "f runtime-pack/platform.obj"),
            ("guideXosFailFastImpl", 0x1008, "f runtime-pack/platform.obj"),
            ("RhpNewArray", 0x1018, "f runtime-pack/platform.obj"),
            ("guidexos_nativeaot_gc_read_state_r29", 0x1040, "f runtime-pack/gcprobe.obj"),
            ("guideXosNativeAotR29FailFastPreallocationGcState", 0x1050, "f runtime-pack/platform.obj"),
            ("guideXosStockRhpNewArray", 0x1060, "f runtime/allocfast.obj"),
            ("guidexos_nativeaot_gc_read_state", 0x1070, "f runtime-pack/gcprobe.obj"),
            ("guideXosNativeAotR28FailFastGcStateGuard", 0x1080, "f runtime-pack/platform.obj"),
        ]
        call_targets = {0x1020: 0x1000, 0x1026: 0x1040, 0x102B: 0x1050,
                        0x1030: 0x1060, 0x1035: 0x1080}
        for va, target in call_targets.items():
            offset = 0x100 + va - 0x1000
            elf[offset] = 0xE8
            struct.pack_into("<i", elf, offset + 1, target - (va + 5))
        elf_path = candidate / "managed.elf"
        elf_path.write_bytes(elf)
        map_path = candidate / "managed.map"
        map_lines = [f"0000000000000000 {name} {address:016X} {suffix}"
                     for name, address, suffix in symbols]
        map_lines.append("0000000000000000 g_guideXosR25FailFastOriginRecord 0000000000001100 .bss")
        map_path.write_text("\n".join(map_lines) + "\n", encoding="utf-8")
        runtime_dir = candidate / "runtime-pack"
        runtime_dir.mkdir()
        runtime_path = runtime_dir / "runtime-pack.manifest.json"
        runtime_path.write_text(json.dumps({
            "c188r25FailFastOriginCapture": True,
            "identity": "test-r29",
            "c188r28GcStateGuardDiscriminator": True,
            "c188r28GcStateGuardDiscriminatorMarker": "GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR",
            "c188r29PreallocationGcStateGuard": True,
            "c188r29PreallocationGcStateGuardMarker": "GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD",
            "failFastOriginRecordSchema": 3,
            "failFastOriginRecordBytes": 192,
            "renamedRuntimeSymbols": {"RhpNewArray": "guideXosStockRhpNewArray"},
        }), encoding="utf-8")
        (candidate / "candidate-manifest.json").write_text(json.dumps({
            "managedMap": str(map_path), "managedElf": str(elf_path),
            "runtimePackManifest": str(runtime_path), "managedElfSha256": "test",
        }), encoding="utf-8")
        return temporary, candidate

    def test_linked_record_and_guard_are_ordered_before_stock_allocator(self) -> None:
        temporary, candidate = self._make_candidate()
        try:
            result = ANALYZER.verify_candidate(candidate)
            self.assertEqual(result["recordBytes"], 192)
            self.assertEqual(result["recordSchema"], 3)
            self.assertLess(result["preallocationReaderCallOffset"],
                            result["preallocationFailureCallOffset"])
            self.assertLess(result["preallocationFailureCallOffset"],
                            result["stockAllocatorCallOffset"])
        finally:
            temporary.cleanup()


class SourceContractTests(unittest.TestCase):
    def test_probe_keeps_prerequisite_order_and_only_catches_access_violations(self) -> None:
        source = PROBE.read_text(encoding="utf-8")
        probe_start = source.index('extern "C" int32_t guidexos_nativeaot_gc_read_state_r29(')
        probe_end = source.index('\n#endif\n\nextern "C" int32_t guidexos_nativeaot_gc_describe_object', probe_start)
        probe = source[probe_start:probe_end]
        positions = [probe.index(token) for token in (
            "ThreadStore::GetCurrentThreadIfAvailable()", "thread->GetAllocContext()",
            "GCHeapUtilities::IsGCHeapInitialized()", "GCHeapUtilities::GetGCHeap()",
            "context->alloc_ptr", "heap->GetGcCount()",
        )]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("GetExceptionCode() == 0xC0000005u", probe)
        self.assertIn("kGuideXosR29SafeReadFailed", probe)
        self.assertNotIn("EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER", probe)
        self.assertNotRegex(probe, r"\b(?:new|malloc|calloc|realloc)\s*\(")
        self.assertNotRegex(probe, r"\b(?:Collect|GarbageCollect|EnterCriticalSection|AcquireLock)\s*\(")
        self.assertNotRegex(probe, r"thread->\w+\s*=")

    def test_preallocation_diagnostic_fails_before_stock_and_postallocation_check_is_retained(self) -> None:
        source = PLATFORM.read_text(encoding="utf-8")
        wrapper = source[source.index('extern "C" __declspec(noinline) void* __cdecl RhpNewArray('):]
        reader = wrapper.index("guidexos_nativeaot_gc_read_state_r29(")
        failure = wrapper.index("guideXosFailFastR29PreallocationGcState(r29GuardEvidence)")
        stock = wrapper.index("guideXosStockRhpNewArray(eeType, length)")
        self.assertLess(reader, failure)
        self.assertLess(failure, stock)
        self.assertIn("guideXosFailFastGcStateGuard(guardEvidence)", wrapper)
        self.assertIn("record.r29GuardLocation = kGuideXosR29GuardLocationPreallocation", source)
        self.assertIn("record.r29GuardLocation = kGuideXosR29GuardLocationPostallocation", source)
        self.assertIn("_InterlockedExchange64(", source)
        self.assertRegex(source, r"record\.r29SafeReadResult = evidence\.safeReadResult;[\s\S]*?record\.published")
        failure_helper = source[source.index("guideXosNativeAotR29FailFastPreallocationGcState("):]
        self.assertNotRegex(failure_helper, r"\b(?:Collect|GarbageCollect|EnterCriticalSection|AcquireLock)\s*\(")
        self.assertNotRegex(source[source.index("guideXosNativeAotR29FailFastPreallocationGcState("):],
                              r"\b(?:new|malloc|calloc|realloc)\s*\(")

    def test_schema_three_is_fixed_width_and_does_not_add_runtime_identity_predicates(self) -> None:
        header = ORIGIN.read_text(encoding="utf-8")
        self.assertIn("kGuideXosR25FailFastOriginRecordBytes = 192u", header)
        self.assertIn("kGuideXosR29GuardStateReadIncomplete = 9u", header)
        guard = HEADER.read_text(encoding="utf-8")
        self.assertIn("kGuideXosR29GuardSafeReadFailed", guard)
        self.assertNotIn("PAL", guard)
        self.assertNotIn("ExpectedCurrentThread", guard)


def find_vcvars() -> Path:
    candidates = [
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"),
    ]
    return next((path for path in candidates if path.is_file()), Path())


class WindowsAmd64ClassifierTests(unittest.TestCase):
    def test_shared_classifier_compiles_and_runs_with_msvc_x64(self) -> None:
        vcvars = find_vcvars()
        if not vcvars.is_file():
            self.skipTest("Visual Studio vcvars64.bat is unavailable")
        with tempfile.TemporaryDirectory(prefix="guidexos-r29-gc-guard-") as temp:
            temp_path = Path(temp)
            cpp = temp_path / "r29_guard_test.cpp"
            exe = temp_path / "r29_guard_test.exe"
            batch = temp_path / "build-and-run.cmd"
            cpp.write_text(r'''#include <cstdio>
#include <cstddef>
#include "guidexos_nativeaot_r29_preallocation_guard.h"
#include "guidexos_nativeaot_gc_state_guard.h"
static_assert(sizeof(void*) == 8, "Windows AMD64 ABI required");
static_assert(sizeof(GuideXosR25FailFastOriginRecord) == 192, "R29 record size");
static_assert(offsetof(GuideXosR25FailFastOriginRecord, r29GuardLocation) == 136, "R29 fields follow R28");
static_assert(offsetof(GuideXosR25FailFastOriginRecord, published) == 184, "publication remains last");
int main() {
    unsigned checks = 0;
    GuideXosR29GcStateReadEvidence read = {};
    read.prerequisiteEvaluationMask = kGuideXosR29EvalThreadStore;
    read.threadStoreResult = kGuideXosR29CheckUnavailable;
    auto noThread = guideXosR29ClassifyPreallocationGcState(-1, read, 0, 0);
    if (noThread.firstFailedSubcondition != kGuideXosR29GuardThreadStoreUnavailable ||
        noThread.allocationContextResult != kGuideXosR29CheckNotEvaluated) return 11;
    ++checks;
    read = {}; read.prerequisiteEvaluationMask = 3;
    read.threadStoreResult = 1; read.allocationContextResult = 2;
    read.observedCurrentThread = 0x101;
    auto noContext = guideXosR29ClassifyPreallocationGcState(-1, read, 0x101, 0);
    if (noContext.firstFailedSubcondition != kGuideXosR29GuardAllocationContextUnavailable ||
        noContext.gcHeapInitializationResult != 0) return 12;
    ++checks;
    read = {}; read.prerequisiteEvaluationMask = 7;
    read.threadStoreResult = 1; read.allocationContextResult = 1;
    read.gcHeapInitializationResult = 2; read.observedCurrentThread = 0x101;
    auto noHeap = guideXosR29ClassifyPreallocationGcState(-1, read, 0x101, 0);
    if (noHeap.firstFailedSubcondition != kGuideXosR29GuardGcHeapUninitialized) return 13;
    ++checks;
    read = {}; read.safeReadResult = kGuideXosR29SafeReadFailed;
    auto unsafeRead = guideXosR29ClassifyPreallocationGcState(-2, read, 0, 0);
    if (unsafeRead.firstFailedSubcondition != kGuideXosR29GuardSafeReadFailed) return 14;
    ++checks;
    read = {}; read.prerequisiteEvaluationMask = 15;
    read.threadStoreResult = 1; read.allocationContextResult = 1;
    read.gcHeapInitializationResult = 1; read.safeReadResult = 0;
    auto incomplete = guideXosR29ClassifyPreallocationGcState(0, read, 0x101, 0x303);
    if (incomplete.firstFailedSubcondition != kGuideXosR29GuardStateReadIncomplete) return 15;
    ++checks;
    read.safeReadResult = kGuideXosR29SafeReadCompleted;
    read.prerequisiteEvaluationMask |= kGuideXosR29EvalStateOutputs;
    auto valid = guideXosR29ClassifyPreallocationGcState(0, read, 0x101, 0x303);
    if (valid.firstFailedSubcondition != kGuideXosR29GuardSubconditionNone) return 16;
    ++checks;
    auto nullThreadOutput = guideXosR29ClassifyPreallocationGcState(0, read, 0, 0x303);
    if (nullThreadOutput.firstFailedSubcondition != kGuideXosR29GuardCurrentThreadOutputNull) return 17;
    ++checks;
    auto nullHeapOutput = guideXosR29ClassifyPreallocationGcState(0, read, 0x101, 0);
    if (nullHeapOutput.firstFailedSubcondition != kGuideXosR29GuardGcHeapOutputNull) return 18;
    ++checks;
    auto postallocation = guideXosR28ClassifyGcStateGuard(0, 0x101, 0x101, 0x303, 0x303);
    if (postallocation.firstFailedSubcondition != kGuideXosR28GuardSubconditionNone) return 19;
    ++checks;
    std::printf("R29_CPP_CLASSIFIER_ASSERTIONS=%u RESULT=PASS\n", checks);
    return 0;
}
''', encoding="utf-8")
            batch.write_text("\r\n".join([
                "@echo off", f'call "{vcvars}" >nul',
                "if errorlevel 1 exit /b %errorlevel%",
                f'cl.exe /nologo /W4 /std:c++14 /EHsc /DGUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD /DGUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR /I"{HEADER.parent}" /Fo:"{temp_path / "r29_guard_test.obj"}" /Fe:"{exe}" "{cpp}"',
                "if errorlevel 1 exit /b %errorlevel%", f'"{exe}"', "exit /b %errorlevel%", "",
            ]), encoding="utf-8")
            result = subprocess.run(["cmd.exe", "/d", "/c", str(batch)],
                                    capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("R29_CPP_CLASSIFIER_ASSERTIONS=9 RESULT=PASS", result.stdout)


if __name__ == "__main__":
    outcome = unittest.main(verbosity=2, exit=False)
    if outcome.result.wasSuccessful():
        print(f"R29_PREALLOCATION_GC_STATE_TESTS={outcome.result.testsRun} RESULT=PASS")
    raise SystemExit(0 if outcome.result.wasSuccessful() else 1)

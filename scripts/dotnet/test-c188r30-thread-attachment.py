#!/usr/bin/env python3
"""Hosted R30 NativeAOT reverse-entry and thread-attachment qualification."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLATFORM = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp"
FAILFAST = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_failfast_origin.h"
R29_GUARD = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_r29_preallocation_guard.h"
READINESS_HEADER = ROOT / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_readiness.h"
R30_RUNNER = ROOT / "scripts/dotnet/run-c188r30-thread-attachment.ps1"
R30_BUILDER = ROOT / "scripts/dotnet/build-c188r30-thread-attachment-candidate.ps1"
PINNED_RUNTIME = "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
RUNTIME_ROOT = ROOT / "out/dotnet/gc-feasibility-baseline/nativeaot-runtime/src/coreclr/nativeaot/Runtime"
LINKED_CANDIDATE: Path | None = None


def linked_call_sites(elf: Path, base: int, length: int) -> dict[int, int]:
    """Read a small file-backed ELF virtual range and return direct call targets."""
    data = elf.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("candidate must be a little-endian ELF64 image")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    file_offset = None
    for index in range(phnum):
        entry = struct.unpack_from("<IIQQQQQQ", data, phoff + index * phentsize)
        p_type, p_offset, p_vaddr, p_filesz = entry[0], entry[2], entry[3], entry[5]
        if p_type == 1 and p_vaddr <= base and base + length <= p_vaddr + p_filesz:
            file_offset = p_offset + base - p_vaddr
            break
    if file_offset is None:
        raise ValueError(f"virtual range 0x{base:X}+0x{length:X} is not file-backed")
    code = data[file_offset:file_offset + length]
    if len(code) != length:
        raise ValueError("candidate ELF ended inside requested code range")
    targets: dict[int, int] = {}
    for offset, opcode in enumerate(code[:-4]):
        if opcode == 0xE8:
            displacement = struct.unpack_from("<i", code, offset + 1)[0]
            targets[base + offset] = base + offset + 5 + displacement
    return targets


def require_call(calls: dict[int, int], target: int, description: str) -> int:
    matches = [address for address, call_target in calls.items() if call_target == target]
    if len(matches) != 1:
        raise ValueError(f"expected one {description} call, found {len(matches)}")
    return matches[0]


def map_symbol(map_text: str, name: str) -> int:
    pattern = re.compile(
        rf"(?m)^\s*[0-9A-Fa-f]{{4}}:[0-9A-Fa-f]{{8}}\s+{re.escape(name)}\s+"
        rf"(?P<address>[0-9A-Fa-f]{{16}})(?:\s|$)"
    )
    matches = pattern.findall(map_text)
    if len(matches) != 1:
        raise ValueError(f"expected one linked map symbol for {name}, found {len(matches)}")
    return int(matches[0], 16)


def verify_linked_candidate(candidate: Path) -> dict[str, int]:
    manifest_path = candidate / "candidate-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    elf = Path(manifest["managedElf"])
    map_path = Path(manifest["managedMap"])
    map_text = map_path.read_text(encoding="utf-8-sig", errors="replace")
    addresses = {
        name: map_symbol(map_text, name)
        for name in (
            "InitializeModules", "RhpReversePInvoke", "RhpReversePInvokeReturn",
            "RhpNewArray", "guideXosStockRhpReversePInvoke",
            "guideXosStockRhpReversePInvokeReturn",
        )
    }

    initializer_calls = linked_call_sites(elf, addresses["InitializeModules"], 0x100)
    reverse_entry_call = require_call(
        initializer_calls, addresses["RhpReversePInvoke"],
        "generated InitializeModules reverse-P/Invoke entry",
    )
    array_call = require_call(
        initializer_calls, addresses["RhpNewArray"],
        "generated InitializeModules first-array allocation",
    )
    # The reverse entry must occur before the first array request.
    if reverse_entry_call >= array_call:
        raise ValueError("generated array request precedes its reverse-P/Invoke entry")

    require_call(
        linked_call_sites(elf, addresses["RhpReversePInvoke"], 0x200),
        addresses["guideXosStockRhpReversePInvoke"],
        "stock NativeAOT reverse-entry attachment",
    )
    require_call(
        linked_call_sites(elf, addresses["RhpReversePInvokeReturn"], 0x100),
        addresses["guideXosStockRhpReversePInvokeReturn"],
        "stock NativeAOT reverse-return transition",
    )
    return addresses


class ReverseEntrySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = PLATFORM.read_text(encoding="utf-8")
        cls.entry_start = cls.source.index(
            'extern "C" __declspec(noinline) void __cdecl RhpReversePInvoke(void* frame)'
        )
        cls.return_start = cls.source.index(
            'extern "C" __declspec(noinline) void __cdecl RhpReversePInvokeReturn(void* frame)'
        )
        cls.entry = cls.source[cls.entry_start:cls.return_start]
        cls.return_body = cls.source[cls.return_start:]

    def test_runtime_pin_is_recorded(self) -> None:
        lock = json.loads((ROOT / "tools/dotnet/runtime-pack/runtime-pack.lock.json").read_text())
        self.assertEqual(lock["ilCompiler"]["commit"], PINNED_RUNTIME)
        self.assertEqual(lock["runtimePack"]["commit"], PINNED_RUNTIME)

    def test_r29_null_accessor_is_distinct_from_raw_tls_absence(self) -> None:
        # Pinned threadstore.inl returns &tls_CurrentThread, then tests
        # IsInitialized(); the R29 null result means the TLS Thread is uninitialized.
        threadstore_inline = (RUNTIME_ROOT / "threadstore.inl").read_text(encoding="utf-8")
        raw_tls_thread = {"address": 0x1000, "state_flags": 0}
        initialized_thread = raw_tls_thread["address"] if raw_tls_thread["state_flags"] != 0 else 0
        self.assertNotEqual(raw_tls_thread["address"], 0)
        self.assertEqual(initialized_thread, 0)
        self.assertRegex(
            threadstore_inline,
            r"(?s)RawGetCurrentThread\(\).*?return\s*\(Thread\s*\*\)\s*&tls_CurrentThread",
        )
        self.assertRegex(
            threadstore_inline,
            r"(?s)GetCurrentThreadIfAvailable\(\).*?RawGetCurrentThread\(\).*?IsInitialized\(\).*?return NULL",
        )
        self.assertIn("kGuideXosR29GuardThreadStoreUnavailable",
                      R29_GUARD.read_text(encoding="utf-8"))

    def test_pinned_attachment_publishes_flags_after_pal_and_cooperative_mode_is_frame_based(self) -> None:
        threadstore = (RUNTIME_ROOT / "threadstore.cpp").read_text(encoding="utf-8")
        thread_header = (RUNTIME_ROOT / "thread.h").read_text(encoding="utf-8")
        thread_inline = (RUNTIME_ROOT / "thread.inl").read_text(encoding="utf-8")
        attach = threadstore[threadstore.index("void ThreadStore::AttachCurrentThread(bool") :]
        self.assertRegex(
            attach,
            r"(?s)RawGetCurrentThread\(\).*?IsInitialized\(\).*?return;.*?PalAttachThread\(pAttachingThread\).*?Construct\(\).*?TSF_Attached.*?m_ThreadList\.PushHead",
        )
        self.assertIn("TSF_Attached            = 0x00000001", thread_header)
        self.assertRegex(
            thread_inline,
            r"(?s)IsCurrentThreadInCooperativeMode\(\).*?return\s*\(m_pTransitionFrame\s*==\s*NULL\)",
        )

    def test_real_gc_reverse_entry_delegates_to_runtime_attachment(self) -> None:
        branch = self.entry.index("#elif defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)")
        branch_end = self.entry.index("#endif", branch)
        real_gc_branch = self.entry[branch:branch_end]
        self.assertIn("guideXosStockRhpReversePInvoke(frame);", real_gc_branch)
        self.assertNotIn("transitionFrame[0]", real_gc_branch)
        self.assertNotIn("transitionFrame[1]", real_gc_branch)
        self.assertIn("Generated UnmanagedCallersOnly entry points", real_gc_branch)

    def test_readiness_profile_verifies_stock_attachment_and_publication(self) -> None:
        readiness = self.entry.index(
            "#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS)"
        )
        real_gc = self.entry.index(
            "#elif defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)", readiness
        )
        readiness_branch = self.entry[readiness:real_gc]
        self.assertIn("guideXosStockRhpReversePInvoke(frame);", readiness_branch)
        self.assertIn("ThreadStore::GetCurrentThreadIfAvailable()", readiness_branch)
        self.assertIn("currentStore->LockThreadStore();", readiness_branch)
        self.assertIn("GX_THREADSTORE_EVENT_THREADSTORE_PUBLICATION", readiness_branch)
        self.assertIn("GX_THREADSTORE_EVENT_TRANSITION_ATTACH_INCOMPLETE", readiness_branch)
        self.assertIn("GX_THREADSTORE_EVENT_TRANSITION_ATTACH_STATE", readiness_branch)
        self.assertIn("GX_THREADSTORE_EVENT_TRANSITION_ATTACH_FLS", readiness_branch)
        self.assertIn("GuideXosThreadStorePushTelemetryFrame(", readiness_branch)

    def test_real_gc_reverse_return_pairs_runtime_return_helper(self) -> None:
        branch = self.return_body.index("#elif defined(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION)")
        fallback = self.return_body.index("#else", branch)
        real_gc_branch = self.return_body[branch:fallback]
        self.assertIn("guideXosStockRhpReversePInvokeReturn(frame);", real_gc_branch)
        self.assertNotIn("kRuntimeCellTransitionFrameOffset", real_gc_branch)

    def test_fixed_heap_legacy_tls_bridge_remains_separate(self) -> None:
        manual_frame = self.entry.index("unsigned char* cell = runtimeCell(block);")
        legacy = self.entry[manual_frame:]
        self.assertIn("transitionFrame[0]", legacy)
        self.assertIn("transitionFrame[1]", legacy)
        prefix = re.sub(r"\\\s*\r?\n", " ", self.entry[:manual_frame])
        self.assertRegex(
            prefix,
            r"#if\s+!defined\(GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS\)\s*&&\s*"
            r"!defined\(GUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION\)",
        )
        self.assertIn("kRuntimeCellTransitionFrameOffset", self.return_body)

    def test_nested_module_setup_cannot_reset_a_later_failfast_stage(self) -> None:
        start = self.source.index("void initializeNativeAotModules(")
        end = self.source.index("#endif\n\n#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)", start)
        body = self.source[start:end]
        self.assertRegex(
            body,
            r"if\s*\(!g_guideXosNativeAotModulesInitialized\)\s*\{\s*"
            r"g_guideXosR25InitializationStage\s*=\s*"
            r"kGuideXosR25FailFastStageNativeModulePreparation;",
        )
        self.assertIn("if (g_guideXosNativeAotModulesInitialized)", body)

    def test_r29_preallocation_guard_and_stock_allocator_safety_remain(self) -> None:
        start = self.source.index('extern "C" __declspec(noinline) void* __cdecl RhpNewArray(')
        body = self.source[start:]
        reader = body.index("guidexos_nativeaot_gc_read_state_r29(")
        failure = body.index("guideXosFailFastR29PreallocationGcState(r29GuardEvidence)")
        stock = body.index("guideXosStockRhpNewArray(eeType, length)")
        self.assertLess(reader, failure)
        self.assertLess(failure, stock)
        self.assertIn("kGuideXosR29GuardSubconditionNone", body)

    def test_array_boundary_markers_bracket_the_real_gc_guard_and_stock_call(self) -> None:
        start = self.source.index('extern "C" __declspec(noinline) void* __cdecl RhpNewArray(')
        end = self.source.index("#endif", self.source.index("return result;", start))
        body = self.source[start:end]
        valid = body.index("GX_THREADSTORE_EVENT_RHP_ARRAY_GC_STATE_VALID")
        stock_enter = body.index("GX_THREADSTORE_EVENT_RHP_ARRAY_STOCK_ENTER")
        stock_call = body.index("guideXosStockRhpNewArray(eeType, length)")
        stock_return = body.index("GX_THREADSTORE_EVENT_RHP_ARRAY_STOCK_RETURN")
        self.assertLess(valid, stock_enter)
        self.assertLess(stock_enter, stock_call)
        self.assertLess(stock_call, stock_return)
        for event in (
            "GX_THREADSTORE_EVENT_RHP_ARRAY_GC_STATE_VALID = 48u",
            "GX_THREADSTORE_EVENT_RHP_ARRAY_STOCK_ENTER = 49u",
            "GX_THREADSTORE_EVENT_RHP_ARRAY_STOCK_RETURN = 50u",
        ):
            self.assertIn(event, READINESS_HEADER.read_text(encoding="utf-8"))

    def test_runner_gates_exactly_one_native_invocation_and_reads_attachment_evidence(self) -> None:
        runner = R30_RUNNER.read_text(encoding="utf-8")
        self.assertEqual(runner.count("& $pwsh -NoProfile -File $runner"), 1)
        self.assertLess(runner.index("if (-not $clear)"), runner.index("$runner = Join-Path"))
        self.assertIn("runnerInvocationCount=1", runner)
        self.assertIn("automaticRetry=$false", runner)
        self.assertIn("transition_attach_state", runner)
        self.assertIn("transition_attach_fls", runner)
        self.assertIn("rhp_array_gc_state_valid", runner)
        self.assertIn("rhp_array_stock_enter", runner)
        self.assertIn("rhp_array_stock_return", runner)
        self.assertIn("THREAD_ATTACHMENT_GUEST_DEFERRED", runner)

    def test_candidate_build_selects_the_proven_stock_attachment_profile(self) -> None:
        builder = R30_BUILDER.read_text(encoding="utf-8")
        self.assertIn("-C188R12R4R5R4ThreadStoreReadiness", builder)
        self.assertIn("c188r12r4r5r4ThreadStoreReadiness", builder)
        self.assertIn("c188r12r4r5r2r3InitializeModulesAttribution", builder)
        self.assertIn("sourceInputs=$sourceInputs", builder)


class LinkedCandidateTests(unittest.TestCase):
    def test_generated_initializer_and_stock_transition_are_linked_in_order(self) -> None:
        if LINKED_CANDIDATE is None:
            self.skipTest("candidate root not supplied")
        assert LINKED_CANDIDATE is not None
        manifest = json.loads(
            (LINKED_CANDIDATE / "runtime-pack/runtime-pack.manifest.json").read_text(
                encoding="utf-8-sig"
            )
        )
        self.assertTrue(manifest["c188r12r4r5r4ThreadStoreReadiness"])
        self.assertTrue(manifest["c188r12r4r5r2r3InitializeModulesAttribution"])
        self.assertEqual(
            manifest["c188r12r4r5r4ThreadStoreReadinessSourceCommit"], PINNED_RUNTIME
        )
        verify_linked_candidate(LINKED_CANDIDATE)

    def test_negative_case_rejects_a_tls_only_candidate(self) -> None:
        calls = {0x2000: 0x3000}
        with self.assertRaisesRegex(ValueError, "stock NativeAOT attachment"):
            require_call(calls, 0x4000, "stock NativeAOT attachment")


def main() -> int:
    global LINKED_CANDIDATE
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-root", type=Path)
    args, remaining = parser.parse_known_args()
    if args.candidate_root is not None:
        LINKED_CANDIDATE = args.candidate_root.resolve()
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.wasSuccessful():
        print(f"R30_THREAD_ATTACHMENT_TESTS={result.testsRun} RESULT=PASS")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())

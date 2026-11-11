#!/usr/bin/env python3
"""Hosted offline regressions for the R16 NativeAOT unwind analyzer."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--capture-json", type=Path, required=True)
    parser.add_argument("--objdump")
    args = parser.parse_args()

    analyzer_path = Path(__file__).with_name("analyze-c188r16-native-unwind.py")
    spec = importlib.util.spec_from_file_location("c188r16_native_unwind", analyzer_path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load R16 analyzer module")
    analyzer = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = analyzer
    spec.loader.exec_module(analyzer)

    assertions = 0

    def check(condition: bool, label: str) -> None:
        nonlocal assertions
        assertions += 1
        if not condition:
            raise AssertionError(label)

    candidate_root = args.candidate_root.resolve()
    capture_path = args.capture_json.resolve()
    image_path = candidate_root / "managed" / "artifacts" / "HostLogProof.exe"
    map_path = candidate_root / "managed" / "artifacts" / "HostLogProof.map"
    capture = analyzer.read_json(capture_path)
    _, manifest_hash, register_value, register_hash, serial_value, serial_hash, _ = analyzer.metadata_fields(capture_path)
    registers_path = analyzer.path_from_metadata(register_value, capture_path.parent)
    serial_path = analyzer.path_from_metadata(serial_value, capture_path.parent)
    registers = analyzer.parse_registers(registers_path)
    image = analyzer.PeImage(image_path)
    map_base, symbols = analyzer.load_map(map_path)
    calls, _ = analyzer.disassemble(image_path, analyzer.find_objdump(args.objdump))

    analysis = analyzer.analyze(argparse.Namespace(
        candidate_root=candidate_root,
        capture_json=capture_path,
        stack_json=None,
        objdump=args.objdump,
        max_frames=16,
        output=None,
    ))
    check(analysis["candidateManifestSha256"] == manifest_hash, "preserved R15 candidate manifest is hash-bound")
    check(analysis["candidatePeSha256"] == analyzer.sha256(image_path), "preserved R15 exact PE hash is verified")
    check(analysis["candidateMapSha256"] == analyzer.sha256(map_path), "preserved R15 exact map hash is verified")
    check(map_base == image.image_base == 0x100000000, "candidate map and fixed PE image base agree")
    check(image.reloc_rva == 0 and image.reloc_size == 0, "candidate relocation directory confirms fixed preferred base")
    check(registers["RIP"] == 0x100057ECC and registers["RSP"] == 0x52B5200, "R15 CPU#0 RIP and RSP parse at exact addresses")
    check("R8" in registers and "R9" in registers,
          "QEMU extended-register fields parse when the register dump spaces the equals sign")
    check(analysis["executionContext"]["cr0"] == "0x0000000080010033" and
          analysis["executionContext"]["cr4"] == "0x0000000000000668" and
          analysis["executionContext"]["rflags"] == "0x0000000000000202", "short QEMU control-register fields parse without truncation")
    check(analysis["ripSymbol"] == "?NextInternal@StackFrameIterator@@AEAAXXZ" and
          analysis["codeManagerUnwindReturnedTrue"] and
          analysis["precedingCall"]["instruction"].lower().find("0x28") >= 0 and
          analysis["sampledInstruction"].lower().startswith("test"),
          "exact candidate symbol, code-manager vtable call, and post-call return value resolve")
    check(analysis["callerResolution"] == "unresolved-stack-memory-not-captured" and
          analysis["stackWalkClassification"] == "F_CALLER_UNRESOLVED", "R15 registers-only capture is not assigned an invented caller")
    runtime = image.runtime_function(registers["RIP"] - image.image_base)
    check(runtime is not None and runtime.begin == 0x57E28 and runtime.unwind_rva == 0xF3284,
          "R15 RIP resolves to exact candidate .pdata and chained unwind record")

    valid_bounds, _ = analyzer.validate_stack_window(0x100000, 0x102000, 0x101FF0, 16, 16, 0x101FF0)
    invalid_bounds, bounds_error = analyzer.validate_stack_window(0x100000, 0x102000, 0x102000, 16, 16, 0x102000)
    check(valid_bounds, "reported stack bounds accept an in-range exact RSP window")
    check(not invalid_bounds and bounds_error == "invalid-reported-bounds-or-rsp-outside-stack",
          "reported stack bounds reject RSP at the exclusive high address")
    try:
        analyzer.metadata_fields(capture_path.parent / "missing-r16-metadata.json")
        missing_metadata_rejected = False
    except analyzer.EvidenceError:
        missing_metadata_rejected = True
    check(missing_metadata_rejected, "capture metadata without candidate/register/serial identities is rejected")

    valid_return, next_symbol, next_call = analyzer.validate_return_address(image, symbols, calls, registers["RIP"])
    invalid_return, _, _ = analyzer.validate_return_address(image, symbols, calls, 0x41414141)
    check(valid_return and next_call is not None and next_call[1].startswith("call") and next_symbol is not None,
          "valid candidate call return address resolves to an instruction boundary")
    check(not invalid_return, "invalid synthetic return address is rejected")

    with tempfile.TemporaryDirectory(prefix="c188r16-unwind-") as temp_dir:
        incomplete_capture = Path(temp_dir) / "incomplete-stack-capture.json"
        incomplete_capture.write_text(json.dumps({
            "schema": "C188R16_GUEST_STACK_TIMEOUT_CAPTURE_V1",
            "status": "incomplete",
            "candidateManifestSha256": manifest_hash,
            "registersSha256": register_hash,
            "serialSha256": serial_hash,
        }), encoding="utf-8")
        incomplete_analysis = analyzer.analyze(argparse.Namespace(
            candidate_root=candidate_root,
            capture_json=capture_path,
            stack_json=incomplete_capture,
            objdump=args.objdump,
            max_frames=16,
            output=None,
        ))
        check(incomplete_analysis["callerResolution"] == "unresolved-stack-capture-incomplete" and
              incomplete_analysis["stackCaptureStatus"] == "incomplete",
              "offline analysis retains register evidence when the bounded stack sidecar is incomplete")

        stack_path = Path(temp_dir) / "synthetic-stack.bin"
        raw = bytearray(8192)
        stack_start = registers["RSP"]
        iterator = stack_start + 0x140

        def put_u64(offset: int, value: int) -> None:
            raw[offset:offset + 8] = value.to_bytes(8, "little")

        # Build a test-only iterator view at the source-verified offsets from
        # StackFrameIterator.h and regdisplay.h; this is not R15 guest evidence.
        put_u64(iterator - stack_start + 0x18, 0x100057EC0)
        put_u64(iterator - stack_start + 0x150, image.image_base)
        put_u64(iterator - stack_start + 0x98, stack_start)
        put_u64(iterator - stack_start + 0xA0, registers["RIP"])
        put_u64(iterator - stack_start + 0x228, iterator + 0x228)
        r15_context = dict(registers)
        r15_context["RBX"] = iterator
        stack_path.write_bytes(raw)
        memory = analyzer.StackMemory(stack_path, stack_start)
        # The exact machine-code call from StackFrameIterator::Next to NextInternal.
        nextinternal_return = next(
            end for end, (address, assembly) in calls.items()
            if "0x100057e20" in assembly.lower() and 0x100057000 < address < 0x100058000
        )
        post_unwind_registers = dict(registers)
        function_begin, after_unwind_rsp = analyzer.unwind_one(
            image, registers["RIP"] - image.image_base, post_unwind_registers, memory
        )
        check(function_begin == 0x57E28 and after_unwind_rsp == stack_start + 0x58,
              "synthetic stack reconstruction follows the candidate's chained AMD64 unwind metadata")
        put_u64(after_unwind_rsp - stack_start, nextinternal_return)
        stack_path.write_bytes(raw)
        memory = analyzer.StackMemory(stack_path, stack_start)
        reconstructed_return = memory.u64(after_unwind_rsp)
        valid_synthetic, caller_symbol, caller_call = analyzer.validate_return_address(
            image, symbols, calls, reconstructed_return
        )
        check(valid_synthetic and caller_call is not None and caller_symbol is not None and "Next" in caller_symbol,
              "synthetic return slot reconstructs the exact candidate Next-to-NextInternal caller")

        put_u64(after_unwind_rsp - stack_start, 0x41414141)
        stack_path.write_bytes(raw)
        bad_memory = analyzer.StackMemory(stack_path, stack_start)
        invalid_synthetic, _, _ = analyzer.validate_return_address(
            image, symbols, calls, bad_memory.u64(after_unwind_rsp)
        )
        check(not invalid_synthetic, "synthetic invalid unwind return slot cannot pass candidate call-site validation")

        short_path = Path(temp_dir) / "short-stack.bin"
        short_path.write_bytes(bytes(8))
        short_memory = analyzer.StackMemory(short_path, stack_start)
        try:
            analyzer.unwind_one(image, registers["RIP"] - image.image_base, dict(registers), short_memory)
            missing_memory_rejected = False
        except analyzer.EvidenceError:
            missing_memory_rejected = True
        check(missing_memory_rejected, "short stack window is reported as insufficient instead of fabricating a frame")

        decoded = analyzer.decode_iterator_context(r15_context, image, symbols, memory, 0x52B2000, 0x52B6000)
        check(decoded["status"] == "decoded-from-bounded-stack" and
              decoded["previousTransitionFrame"] == f"0x{iterator + 0x228:016X}",
              "bounded iterator context reads source-verified fields without decoding opaque MethodInfo")
        r14_context = dict(registers)
        r14_context["RBX"] = 0x1004AA000
        r14_context["R14"] = iterator
        decoded_r14 = analyzer.decode_iterator_context(r14_context, image, symbols, memory, 0x52B2000, 0x52B6000)
        check(decoded_r14["status"] == "decoded-from-bounded-stack" and
              decoded_r14["iteratorPointerRegister"] == "R14",
              "iterator context can use the validated R14 pointer when RBX is occupied by another value")

    gc_class, _, gc_support = analyzer.classify_stack_walk([
        "?NextInternal@StackFrameIterator@@AEAAXXZ", "?Next@StackFrameIterator@@QEAAXXZ",
        "Thread::GcScanRootsWorker",
    ])
    eh_class, _, _ = analyzer.classify_stack_walk(["StackFrameIterator::InternalInitForEH"])
    diag_class, _, _ = analyzer.classify_stack_walk(["StackFrameIterator::InternalInitForStackTrace"])
    unresolved_class, _, _ = analyzer.classify_stack_walk(["StackFrameIterator::NextInternal"])
    iterator_only_class, _, _ = analyzer.classify_stack_walk([
        "StackFrameIterator::NextInternal", "StackFrameIterator::Next",
    ])
    other_class, _, other_support = analyzer.classify_stack_walk([
        "StackFrameIterator::NextInternal", "Thread::OtherRuntimeWalkCaller",
    ])
    check(gc_class == "A_GC_ROOT_ENUMERATION" and "Thread::GcScanRootsWorker" in gc_support,
          "GC classification requires a recovered GC root-walk caller")
    check(eh_class == "B_EXCEPTION_DISPATCH_OR_UNWIND", "EH classification follows a recovered EH initializer")
    check(diag_class == "C_STACK_TRACE_OR_DIAGNOSTIC", "diagnostic classification follows a recovered stack-trace initializer")
    check(unresolved_class == "F_CALLER_UNRESOLVED", "iterator-only evidence remains caller-unresolved")
    check(iterator_only_class == "F_CALLER_UNRESOLVED", "StackFrameIterator wrapper frames alone do not prove the walk purpose")
    check(other_class == "E_OTHER_RUNTIME_OPERATION" and other_support == ["Thread::OtherRuntimeWalkCaller"],
          "other-operation classification requires a recovered caller outside StackFrameIterator")
    lookup_state = analyzer.lookup_loop_state(
        "?LookupUnwindInfoForMethod@@YAHIPEAUT_RUNTIME_FUNCTION@@HH@Z",
        "jg 0x10005cfd0",
        {"R9": 0x35A, "R10": 0x32B, "RAX": 0x2F, "R11": 0x1E956, "RFL": 0x202},
    )
    check(lookup_state is not None and lookup_state["status"] == "bounded-runtime-function-binary-search" and
          lookup_state["conditionalBranchTaken"] and lookup_state["maxRemainingBinarySearchUpdates"] == 3 and
          not lookup_state["temporalProgressProven"],
          "lookup-loop bounds distinguish a structurally finite search from temporal progress evidence")

    print(json.dumps({
        "schema": "C188R16_NATIVE_UNWIND_HOSTED_TESTS_V1",
        "status": "passed",
        "assertions": assertions,
        "candidateManifestSha256": manifest_hash,
        "registersSha256": register_hash,
        "serialSha256": serial_hash,
        "syntheticFramesAreGuestEvidence": False,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

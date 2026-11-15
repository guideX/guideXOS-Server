#!/usr/bin/env python3
"""Bounded parser for legacy R25 and discriminating R28 first-failure records."""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

MAGIC = 0x4738523235464154
SCHEMA = 1
RECORD_BYTES = 80
R28_SCHEMA = 2
R28_RECORD_BYTES = 144
PUBLISHED = 0x5055424C49534844
FAST_FAIL_FATAL_APP_EXIT = 7
MAX_INITIALIZATION_STAGE = 3
EXPECTED_RECORD_FLAGS = 0x7
R28_SUBCONDITIONS = {
    0: "NONE",
    1: "GC_STATE_READ_FAILED",
    2: "GC_THREAD_IDENTITY_MISMATCH",
    3: "GC_HEAP_IDENTITY_MISMATCH",
}
R28_PREDICATES = {0, 1, 2}


def signed64(value: int) -> int:
    return value - (1 << 64) if value & (1 << 63) else value


def validate_r28_guard_words(
    words: list[int], original_reason: int | None = None
) -> tuple[bool, str, dict[str, object]]:
    if len(words) != R28_RECORD_BYTES // 8:
        return False, "record_word_count_invalid", {}
    if original_reason is None:
        original_reason = words[3]
    subcondition = words[9]
    read_status = signed64(words[10])
    thread_result, heap_result = words[11], words[12]
    expected_thread, observed_thread = words[13], words[14]
    expected_heap, observed_heap = words[15], words[16]
    evidence: dict[str, object] = {
        "firstFailedSubcondition": R28_SUBCONDITIONS.get(subcondition, "UNKNOWN"),
        "gcStateReadStatus": read_status,
        "threadIdentityResult": thread_result,
        "heapIdentityResult": heap_result,
        "expectedCurrentThread": f"0x{expected_thread:016X}",
        "observedCurrentThread": f"0x{observed_thread:016X}",
        "expectedGcHeap": f"0x{expected_heap:016X}",
        "observedGcHeap": f"0x{observed_heap:016X}",
    }
    if subcondition not in R28_SUBCONDITIONS:
        return False, "guard_subcondition_invalid", evidence
    if thread_result not in R28_PREDICATES or heap_result not in R28_PREDICATES:
        return False, "guard_predicate_result_invalid", evidence
    if (subcondition == 0 and read_status == 0 and thread_result == 0 and
        heap_result == 0 and expected_thread == 0 and observed_thread == 0 and
        expected_heap == 0 and observed_heap == 0):
        if original_reason == 8:
            evidence["firstFailedSubcondition"] = "UNRECORDED"
            evidence["discriminatorStatus"] = "reason_8_failure_without_guard_details"
            return True, "GC_STATE_FAILURE_NOT_DISCRIMINATED", evidence
        evidence["discriminatorStatus"] = "not_applicable_to_original_reason"
        return True, "guard_evidence_not_applicable", evidence
    if read_status != 0:
        valid = (subcondition == 1 and thread_result == 0 and heap_result == 0
                 and observed_thread == 0 and observed_heap == 0)
        return valid, (R28_SUBCONDITIONS[subcondition] if valid else
                       "gc_state_read_failure_record_inconsistent"), evidence
    if thread_result == 0 or heap_result == 0:
        return False, "successful_read_missing_identity_predicate", evidence
    thread_mismatch = expected_thread != observed_thread
    heap_mismatch = expected_heap != observed_heap
    if thread_result != (2 if thread_mismatch else 1) or heap_result != (2 if heap_mismatch else 1):
        return False, "identity_predicate_values_conflict", evidence
    expected_subcondition = 2 if thread_mismatch else 3 if heap_mismatch else 0
    if subcondition != expected_subcondition:
        return False, "first_failed_subcondition_conflicts_with_predicates", evidence
    if subcondition == 0:
        return True, "guard_predicates_all_match", evidence
    return True, R28_SUBCONDITIONS[subcondition], evidence


def parse_hmp_words(text: str) -> list[int]:
    words: list[int] = []
    for line in text.splitlines():
        match = re.match(r"\s*(?:0x)?[0-9a-fA-F]{1,16}:\s*(.*)$", line)
        if match:
            words.extend(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{16})\b", match.group(1)))
    return words


def validate_words(words: list[int]) -> tuple[bool, str]:
    if len(words) == R28_RECORD_BYTES // 8 and words[1] == R28_SCHEMA:
        if words[0] != MAGIC:
            return False, "record_magic_invalid"
        if words[2] != R28_RECORD_BYTES:
            return False, "record_size_invalid"
        if words[4] != FAST_FAIL_FATAL_APP_EXIT:
            return False, "fast_fail_code_unexpected"
        if words[5] == 0:
            return False, "caller_capture_address_missing"
        if words[6] == 0:
            return False, "failfast_implementation_address_missing"
        if words[7] > MAX_INITIALIZATION_STAGE:
            return False, "initialization_stage_invalid"
        if words[8] != EXPECTED_RECORD_FLAGS:
            return False, "record_flags_invalid"
        if words[17] != PUBLISHED:
            return False, "record_not_published"
        valid, classification, _evidence = validate_r28_guard_words(words, words[3])
        return valid, classification
    if len(words) != RECORD_BYTES // 8:
        return False, "record_word_count_invalid"
    if words[0] != MAGIC:
        return False, "record_magic_invalid"
    if words[1] != SCHEMA:
        return False, "record_schema_invalid"
    if words[2] != RECORD_BYTES:
        return False, "record_size_invalid"
    if words[4] != FAST_FAIL_FATAL_APP_EXIT:
        return False, "fast_fail_code_unexpected"
    if words[5] == 0:
        return False, "caller_capture_address_missing"
    if words[6] == 0:
        return False, "failfast_implementation_address_missing"
    if words[7] > MAX_INITIALIZATION_STAGE:
        return False, "initialization_stage_invalid"
    if words[8] != EXPECTED_RECORD_FLAGS:
        return False, "record_flags_invalid"
    if words[9] != PUBLISHED:
        return False, "record_not_published"
    return True, "first_failure_record_valid"


def classify_r24_registers(registers: str, failfast_entry: int, terminal_loop: int) -> dict[str, object]:
    values = {name.upper(): int(value, 16) for name, value in re.findall(
        r"\b(RIP|RCX)\s*=\s*([0-9a-fA-F]{8,16})", registers
    )}
    rip, reason = values.get("RIP"), values.get("RCX")
    is_reason7 = rip is not None and failfast_entry <= rip < terminal_loop + 2 and reason == 7
    return {
        "rip": rip,
        "rcx": reason,
        "classification": "shared_fastfail_reason_7_terminal_path" if is_reason7 else "unattributed_cpu_state",
        "terminal_loop_is_original_site": False,
    }


def read_load_segments(elf: bytes) -> list[tuple[int, int, int, int, int]]:
    if elf[:4] != b"\x7fELF" or elf[4] != 2 or elf[5] != 1:
        raise ValueError("expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", elf, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 54)
    segments = []
    for i in range(phnum):
        kind, flags, offset, vaddr, _paddr, filesz, memsz, _align = struct.unpack_from(
            "<IIQQQQQQ", elf, phoff + i * phentsize
        )
        if kind == 1:
            segments.append((vaddr, offset, filesz, memsz, flags))
    return segments


def read_va(elf: bytes, segments: list[tuple[int, int, int, int, int]], address: int, size: int) -> bytes:
    for vaddr, offset, filesz, _memsz, _flags in segments:
        if vaddr <= address and address + size <= vaddr + filesz:
            return elf[offset + address - vaddr : offset + address - vaddr + size]
    raise ValueError(f"virtual address is not file-backed: 0x{address:X}")


def map_symbol_address(map_text: str, symbol: str) -> int:
    match = re.search(rf"(?m)^\s*\S+\s+{re.escape(symbol)}\s+([0-9A-Fa-f]{{16}})\b", map_text)
    if not match:
        raise ValueError(f"map omits {symbol}")
    return int(match.group(1), 16)


def map_function_address(map_text: str, symbol_fragment: str) -> int:
    match = re.search(
        rf"(?m)^\s*\S+\s+(\S*{re.escape(symbol_fragment)}\S*)\s+([0-9A-Fa-f]{{16}})\s+f\s+\S+",
        map_text,
    )
    if not match:
        raise ValueError(f"link map omits function {symbol_fragment}")
    return int(match.group(2), 16)


def has_direct_call_to_function(
    elf: bytes,
    segments: list[tuple[int, int, int, int, int]],
    map_text: str,
    source_fragment: str,
    target_fragment: str,
) -> bool:
    source = map_function_address(map_text, source_fragment)
    target = map_function_address(map_text, target_fragment)
    function_addresses: list[int] = []
    for line in map_text.splitlines():
        match = re.match(r"\s*\S+\s+\S+\s+([0-9A-Fa-f]{16})\s+f\s+", line)
        if match:
            address = int(match.group(1), 16)
            if address > source:
                function_addresses.append(address)
    end = min(function_addresses, default=source + 0x4000)
    size = min(end - source, 0x4000)
    if size < 5:
        return False
    try:
        code = read_va(elf, segments, source, size)
    except ValueError:
        return False
    for offset in range(size - 4):
        if code[offset] == 0xE8:
            displacement = struct.unpack_from("<i", code, offset + 1)[0]
            if source + offset + 5 + displacement == target:
                return True
    return False


def resolve_caller(candidate: Path, caller_pc: int) -> dict[str, object]:
    manifest = json.loads((candidate / "candidate-manifest.json").read_text(encoding="utf-8-sig"))
    map_text = Path(manifest["managedMap"]).read_text(encoding="utf-8", errors="replace")
    elf = Path(manifest["managedElf"]).read_bytes()
    segments = read_load_segments(elf)
    if not any(start <= caller_pc < start + memsz and flags & 1
               for start, _off, _filesz, memsz, flags in segments):
        return {"resolved": False, "reason": "caller_pc_outside_executable_load_segment", "callerPc": f"0x{caller_pc:016X}"}
    try:
        helper_address = map_function_address(map_text, "guideXosNativeAotR25CaptureCallerReturnAddress")
    except ValueError:
        return {"resolved": False, "reason": "capture_helper_missing_from_link_map", "callerPc": f"0x{caller_pc:016X}"}
    try:
        previous_call = read_va(elf, segments, caller_pc - 5, 5)
    except ValueError:
        return {"resolved": False, "reason": "caller_pc_has_no_backed_call_instruction", "callerPc": f"0x{caller_pc:016X}"}
    if previous_call[0] != 0xE8:
        return {"resolved": False, "reason": "capture_return_pc_not_after_direct_call", "callerPc": f"0x{caller_pc:016X}"}
    displacement = struct.unpack_from("<i", previous_call, 1)[0]
    target = caller_pc + displacement
    if target != helper_address:
        return {"resolved": False, "reason": "preceding_call_does_not_target_capture_helper", "callerPc": f"0x{caller_pc:016X}", "callTarget": f"0x{target:016X}"}
    symbols: list[tuple[int, str, str]] = []
    for line in map_text.splitlines():
        match = re.match(r"\s*\S+\s+(\S+)\s+([0-9A-Fa-f]{16})\s+f\s+(.+?)\s*$", line)
        if match:
            address = int(match.group(2), 16)
            if address <= caller_pc:
                symbols.append((address, match.group(1), match.group(3).strip()))
    owner = max(symbols, default=(0, "", ""), key=lambda item: item[0])
    return {
        "resolved": bool(owner[1]),
        "reason": "capture_helper_return_pc_verified",
        "callerPc": f"0x{caller_pc:016X}",
        "captureCallTarget": f"0x{helper_address:016X}",
        "enclosingSymbol": owner[1],
        "objectProvenance": owner[2],
        "failureContractProven": False,
    }


def verify_candidate(candidate: Path) -> dict[str, object]:
    manifest = json.loads((candidate / "candidate-manifest.json").read_text(encoding="utf-8-sig"))
    runtime_manifest = json.loads((candidate / "runtime-pack" / "runtime-pack.manifest.json").read_text(encoding="utf-8-sig"))
    map_text = Path(manifest["managedMap"]).read_text(encoding="utf-8", errors="replace")
    elf_path = Path(manifest["managedElf"])
    elf = elf_path.read_bytes()
    record = map_symbol_address(map_text, "g_guideXosR25FailFastOriginRecord")
    capture_helper = map_function_address(map_text, "guideXosNativeAotR25CaptureCallerReturnAddress")
    failfast_implementation = map_function_address(map_text, "guideXosFailFastImpl")
    segments = read_load_segments(elf)
    r28_enabled = runtime_manifest.get("c188r28GcStateGuardDiscriminator") is True
    expected_schema = R28_SCHEMA if r28_enabled else SCHEMA
    expected_bytes = R28_RECORD_BYTES if r28_enabled else RECORD_BYTES
    actual_schema = runtime_manifest.get("failFastOriginRecordSchema", SCHEMA)
    actual_bytes = runtime_manifest.get("failFastOriginRecordBytes", RECORD_BYTES)
    writable = any(start <= record and record + expected_bytes <= start + memsz and flags & 2
                   for start, _offset, _filesz, memsz, flags in segments)
    helper_executable = any(start <= capture_helper < start + memsz and flags & 1
                             for start, _offset, _filesz, memsz, flags in segments)
    failfast_executable = any(start <= failfast_implementation < start + memsz and flags & 1
                              for start, _offset, _filesz, memsz, flags in segments)
    if (not runtime_manifest.get("c188r25FailFastOriginCapture") or
        actual_schema != expected_schema or
        actual_bytes != expected_bytes or
        (r28_enabled and runtime_manifest.get("c188r28GcStateGuardDiscriminatorMarker") !=
            "GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR") or
        not writable or not helper_executable or not failfast_executable):
        raise ValueError("R25 record/functions are not enabled in the linked writable/executable ELF segments")
    r28_symbols: dict[str, str] = {}
    if r28_enabled:
        symbols = runtime_manifest.get("renamedRuntimeSymbols", {})
        if symbols.get("RhpNewArray") != "guideXosStockRhpNewArray":
            raise ValueError("R28 candidate does not link the pinned stock array allocator as its delegate")
        wrapper_address = map_function_address(map_text, "RhpNewArray")
        stock_array_address = map_function_address(map_text, "guideXosStockRhpNewArray")
        gc_state_address = map_function_address(map_text, "guidexos_nativeaot_gc_read_state")
        r28_helper_address = map_function_address(map_text, "guideXosNativeAotR28FailFastGcStateGuard")
        if not has_direct_call_to_function(
            elf, segments, map_text, "RhpNewArray", "guideXosNativeAotR28FailFastGcStateGuard"
        ):
            raise ValueError("R28 linked RhpNewArray does not directly call the discriminator fail-fast path")
        if not all(any(start <= address < start + memsz and flags & 1
                       for start, _offset, _filesz, memsz, flags in segments)
                   for address in (wrapper_address, stock_array_address, gc_state_address, r28_helper_address)):
            raise ValueError("R28 array wrapper, stock allocator, state reader, or capture helper is not executable")
        r28_symbols = {
            "rhpNewArrayWrapperAddress": f"0x{wrapper_address:016X}",
            "stockArrayAllocatorAddress": f"0x{stock_array_address:016X}",
            "gcStateReaderAddress": f"0x{gc_state_address:016X}",
            "guardFailureHelperAddress": f"0x{r28_helper_address:016X}",
        }
    return {
        "result": "PASS",
        "recordAddress": f"0x{record:016X}",
        "recordBytes": expected_bytes,
        "recordSchema": expected_schema,
        "recordSectionWritable": writable,
        "captureHelperAddress": f"0x{capture_helper:016X}",
        "failFastImplementationAddress": f"0x{failfast_implementation:016X}",
        "runtimeProfile": runtime_manifest.get("identity"),
        "managedElfSha256": manifest.get("managedElfSha256"),
        **r28_symbols,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-root", type=Path)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--registers", type=Path)
    parser.add_argument("--failfast-entry", type=lambda s: int(s, 0), default=0x1000F2F70)
    parser.add_argument("--terminal-loop", type=lambda s: int(s, 0), default=0x1000F2F77)
    args = parser.parse_args()
    result: dict[str, object] = {"result": "PASS"}
    if args.candidate_root:
        result["candidate"] = verify_candidate(args.candidate_root.resolve())
    if args.snapshot:
        snapshot = json.loads(args.snapshot.read_text(encoding="utf-8-sig"))
        words = [int(word, 16) for word in snapshot.get("recordWords", [])]
        valid, classification = validate_words(words)
        result["recordValid"] = valid
        result["recordClassification"] = classification
        result["recordWords"] = [f"0x{word:016X}" for word in words]
        expected_record = None
        if args.candidate_root:
            expected_record = int(str(result["candidate"]["recordAddress"]), 16)
            observed_record = snapshot.get("recordAddress")
            try:
                observed_record_address = int(observed_record, 16) if isinstance(observed_record, str) else None
            except ValueError:
                observed_record_address = None
            if observed_record_address != expected_record:
                valid = False
                classification = "snapshot_record_address_mismatch"
                result["recordValid"] = False
                result["recordClassification"] = classification
        if args.candidate_root and valid and words[6] != int(str(result["candidate"]["failFastImplementationAddress"]), 16):
            result["recordValid"] = False
            result["recordClassification"] = "failfast_implementation_address_mismatch"
        if len(words) in (RECORD_BYTES // 8, R28_RECORD_BYTES // 8):
            result["originalReason"] = f"0x{words[3]:016X}"
            result["fastFailCode"] = words[4]
            result["fastFailCodeName"] = "FAST_FAIL_FATAL_APP_EXIT" if words[4] == FAST_FAIL_FATAL_APP_EXIT else "unknown"
        if len(words) == R28_RECORD_BYTES // 8 and words[1] == R28_SCHEMA:
            _valid, _classification, guard_evidence = validate_r28_guard_words(words, words[3])
            result["gcStateGuard"] = guard_evidence
        if args.candidate_root and result["recordValid"]:
            result["caller"] = resolve_caller(args.candidate_root.resolve(), words[5])
    if args.registers:
        registers = args.registers.read_text(encoding="utf-8", errors="replace")
        result["r24Snapshot"] = classify_r24_registers(registers, args.failfast_entry, args.terminal_loop)
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

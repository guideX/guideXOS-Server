#!/usr/bin/env python3
"""Bounded parser for R25's ten-word first-failure record and R24 snapshot."""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

MAGIC = 0x4738523235464154
SCHEMA = 1
RECORD_BYTES = 80
PUBLISHED = 0x5055424C49534844
FAST_FAIL_FATAL_APP_EXIT = 7
MAX_INITIALIZATION_STAGE = 3
EXPECTED_RECORD_FLAGS = 0x7


def parse_hmp_words(text: str) -> list[int]:
    words: list[int] = []
    for line in text.splitlines():
        match = re.match(r"\s*(?:0x)?[0-9a-fA-F]{1,16}:\s*(.*)$", line)
        if match:
            words.extend(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{16})\b", match.group(1)))
    return words


def validate_words(words: list[int]) -> tuple[bool, str]:
    if len(words) != 10:
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
    writable = any(start <= record and record + RECORD_BYTES <= start + memsz and flags & 2
                   for start, _offset, _filesz, memsz, flags in segments)
    helper_executable = any(start <= capture_helper < start + memsz and flags & 1
                             for start, _offset, _filesz, memsz, flags in segments)
    failfast_executable = any(start <= failfast_implementation < start + memsz and flags & 1
                              for start, _offset, _filesz, memsz, flags in segments)
    if not runtime_manifest.get("c188r25FailFastOriginCapture") or not writable or not helper_executable or not failfast_executable:
        raise ValueError("R25 record/functions are not enabled in the linked writable/executable ELF segments")
    return {
        "result": "PASS",
        "recordAddress": f"0x{record:016X}",
        "recordBytes": RECORD_BYTES,
        "recordSectionWritable": writable,
        "captureHelperAddress": f"0x{capture_helper:016X}",
        "failFastImplementationAddress": f"0x{failfast_implementation:016X}",
        "runtimeProfile": runtime_manifest.get("identity"),
        "managedElfSha256": manifest.get("managedElfSha256"),
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
        if len(words) == RECORD_BYTES // 8:
            result["originalReason"] = f"0x{words[3]:016X}"
            result["fastFailCode"] = words[4]
            result["fastFailCodeName"] = "FAST_FAIL_FATAL_APP_EXIT" if words[4] == FAST_FAIL_FATAL_APP_EXIT else "unknown"
        if args.candidate_root and result["recordValid"]:
            result["caller"] = resolve_caller(args.candidate_root.resolve(), words[5])
    if args.registers:
        registers = args.registers.read_text(encoding="utf-8", errors="replace")
        result["r24Snapshot"] = classify_r24_registers(registers, args.failfast_entry, args.terminal_loop)
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

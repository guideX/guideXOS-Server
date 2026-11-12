#!/usr/bin/env python3
"""Verify a preserved NativeAOT timeout snapshot and unwind its bounded x64 stack."""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class EvidenceError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise EvidenceError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise EvidenceError(f"expected a JSON object: {path}")
    return value


def path_from_metadata(value: str, base: Path) -> Path:
    path = Path(value)
    return path if path.is_absolute() else (base / path).resolve()


def parse_registers(path: Path) -> dict[str, int]:
    try:
        text = path.read_text(encoding="ascii", errors="replace")
    except OSError as exc:
        raise EvidenceError(f"cannot read guest registers {path}: {exc}") from exc
    cpu = re.search(r"(?ms)^CPU#0\s*\r?\n(?P<body>.*?)(?=^CPU#\d+\s*$|\Z)", text)
    if not cpu:
        raise EvidenceError("CPU#0 register block is missing")
    registers = {
        name.upper(): int(value, 16)
        for name, value in re.findall(r"\b([A-Z][A-Z0-9]*)\s*=\s*([0-9A-Fa-f]{1,16})\b", cpu.group("body"))
    }
    for name in ("RIP", "RSP", "RBP", "RAX"):
        if name not in registers:
            raise EvidenceError(f"CPU#0 {name} is missing")
    return registers


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    characteristics: int

    @property
    def executable(self) -> bool:
        return bool(self.characteristics & 0x20000000)


@dataclass(frozen=True)
class RuntimeFunction:
    begin: int
    end: int
    unwind_rva: int


class PeImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 0x40 or self.data[:2] != b"MZ":
            raise EvidenceError(f"not a PE image: {path}")
        pe_offset = self.u32(0x3C)
        if self.data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise EvidenceError(f"invalid PE signature: {path}")
        coff = pe_offset + 4
        machine = self.u16(coff)
        section_count = self.u16(coff + 2)
        optional_size = self.u16(coff + 16)
        if machine != 0x8664:
            raise EvidenceError(f"expected AMD64 PE machine 0x8664, got 0x{machine:04X}")
        optional = coff + 20
        if self.u16(optional) != 0x20B:
            raise EvidenceError("expected PE32+ optional header")
        self.image_base = self.u64(optional + 24)
        self.size_of_headers = self.u32(optional + 60)
        directory_count = self.u32(optional + 108)
        directory_base = optional + 112
        if directory_count <= 5:
            raise EvidenceError("PE data directories omit the exception or relocation table")
        self.exception_rva, self.exception_size = struct.unpack_from("<II", self.data, directory_base + 3 * 8)
        self.reloc_rva, self.reloc_size = struct.unpack_from("<II", self.data, directory_base + 5 * 8)
        section_table = optional + optional_size
        self.sections: list[Section] = []
        for index in range(section_count):
            offset = section_table + index * 40
            name = self.data[offset : offset + 8].split(b"\0", 1)[0].decode("ascii", errors="replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", self.data, offset + 8)
            characteristics = self.u32(offset + 36)
            self.sections.append(Section(name, virtual_address, virtual_size, raw_offset, raw_size, characteristics))
        if not self.exception_rva or not self.exception_size or self.exception_size % 12:
            raise EvidenceError("PE exception directory is missing or malformed")
        self.runtime_functions = []
        table_offset = self.rva_to_offset(self.exception_rva)
        for offset in range(table_offset, table_offset + self.exception_size, 12):
            begin, end, unwind = struct.unpack_from("<III", self.data, offset)
            if begin < end:
                self.runtime_functions.append(RuntimeFunction(begin, end, unwind))
        self.runtime_functions.sort(key=lambda item: item.begin)
        self.function_starts = [item.begin for item in self.runtime_functions]

    def u16(self, offset: int) -> int:
        return struct.unpack_from("<H", self.data, offset)[0]

    def u32(self, offset: int) -> int:
        return struct.unpack_from("<I", self.data, offset)[0]

    def u64(self, offset: int) -> int:
        return struct.unpack_from("<Q", self.data, offset)[0]

    def rva_to_offset(self, rva: int) -> int:
        if rva < self.size_of_headers:
            return rva
        for section in self.sections:
            extent = max(section.virtual_size, section.raw_size)
            if section.virtual_address <= rva < section.virtual_address + extent:
                delta = rva - section.virtual_address
                if delta >= section.raw_size:
                    raise EvidenceError(f"RVA 0x{rva:X} has no file-backed data")
                return section.raw_offset + delta
        raise EvidenceError(f"RVA 0x{rva:X} is outside PE sections")

    def section_for_rva(self, rva: int) -> Section | None:
        for section in self.sections:
            if section.virtual_address <= rva < section.virtual_address + max(section.virtual_size, section.raw_size):
                return section
        return None

    def runtime_function(self, rva: int) -> RuntimeFunction | None:
        index = bisect.bisect_right(self.function_starts, rva) - 1
        if index >= 0:
            item = self.runtime_functions[index]
            if item.begin <= rva < item.end:
                return item
        return None

    def unwind_info(self, rva: int) -> tuple[int, int, int, int, list[tuple[int, int, int, int]], RuntimeFunction | None]:
        offset = self.rva_to_offset(rva)
        version_flags, prolog_size, code_count, frame = struct.unpack_from("<BBBB", self.data, offset)
        version = version_flags & 7
        flags = version_flags >> 3
        if version not in (1, 2):
            raise EvidenceError(f"unsupported x64 unwind-info version {version}")
        codes: list[tuple[int, int, int, int]] = []
        slot = 0
        while slot < code_count:
            code_offset, op_info = struct.unpack_from("<BB", self.data, offset + 4 + slot * 2)
            operation, info = op_info & 0xF, op_info >> 4
            slot += 1
            operand = 0
            if operation == 1:  # UWOP_ALLOC_LARGE
                if info == 0:
                    operand = self.u16(offset + 4 + slot * 2) * 8
                    slot += 1
                elif info == 1:
                    operand = self.u32(offset + 4 + slot * 2)
                    slot += 2
                else:
                    raise EvidenceError("invalid UWOP_ALLOC_LARGE operand")
            elif operation in (4, 8):  # SAVE_NONVOL / SAVE_XMM128
                operand = self.u16(offset + 4 + slot * 2) * (8 if operation == 4 else 16)
                slot += 1
            elif operation in (5, 9):  # SAVE_NONVOL_FAR / SAVE_XMM128_FAR
                operand = self.u32(offset + 4 + slot * 2)
                slot += 2
            elif operation == 2:  # UWOP_ALLOC_SMALL
                operand = info * 8 + 8
            codes.append((code_offset, operation, info, operand))
        chain: RuntimeFunction | None = None
        if flags & 0x4:  # UNW_FLAG_CHAININFO
            chain_offset = offset + 4 + ((code_count + 1) & ~1) * 2
            begin, end, unwind = struct.unpack_from("<III", self.data, chain_offset)
            chain = RuntimeFunction(begin, end, unwind)
        return version, flags, prolog_size, frame, codes, chain

    def executable_rva(self, rva: int) -> bool:
        section = self.section_for_rva(rva)
        return bool(section and section.executable)


REGISTER_BY_UNWIND_NUMBER = {
    0: "RAX", 1: "RCX", 2: "RDX", 3: "RBX", 4: "RSP", 5: "RBP", 6: "RSI", 7: "RDI",
    8: "R8", 9: "R9", 10: "R10", 11: "R11", 12: "R12", 13: "R13", 14: "R14", 15: "R15",
}


class StackMemory:
    def __init__(self, path: Path, start: int):
        self.path = path
        self.start = start
        self.data = path.read_bytes()
        self.end = start + len(self.data)

    def u64(self, address: int) -> int:
        if address < self.start or address + 8 > self.end:
            raise EvidenceError(f"stack read at 0x{address:X} is outside captured bytes [0x{self.start:X}, 0x{self.end:X})")
        return struct.unpack_from("<Q", self.data, address - self.start)[0]


def apply_unwind_codes(
    image: PeImage,
    info_rva: int,
    control_offset: int | None,
    registers: dict[str, int],
    memory: StackMemory,
    visited: set[int],
) -> int:
    if info_rva in visited:
        raise EvidenceError("chained unwind metadata contains a cycle")
    visited.add(info_rva)
    version, flags, prolog_size, frame, codes, chain = image.unwind_info(info_rva)
    frame_register = frame & 0xF
    frame_offset = (frame >> 4) * 16
    rsp = registers["RSP"]
    for code_offset, operation, info, operand in codes:
        if control_offset is not None and code_offset > control_offset:
            continue
        if operation == 0:  # UWOP_PUSH_NONVOL
            register = REGISTER_BY_UNWIND_NUMBER[info]
            registers[register] = memory.u64(rsp)
            rsp += 8
        elif operation == 1:  # UWOP_ALLOC_LARGE
            rsp += operand
        elif operation == 2:  # UWOP_ALLOC_SMALL
            rsp += operand
        elif operation == 3:  # UWOP_SET_FPREG
            if not frame_register:
                raise EvidenceError("UWOP_SET_FPREG has no frame register")
            rsp = registers[REGISTER_BY_UNWIND_NUMBER[frame_register]] - frame_offset
        elif operation in (4, 5):  # UWOP_SAVE_NONVOL / _FAR
            register = REGISTER_BY_UNWIND_NUMBER[info]
            registers[register] = memory.u64(rsp + operand)
        elif operation in (8, 9):
            # XMM register values are irrelevant to the caller attribution.  The saved
            # register slot is consumed from metadata, but does not change integer state.
            pass
        elif operation in (6, 7) and version == 2:
            # Version-2 epilogue markers and spare codes do not alter the caller state.
            pass
        elif operation == 10:
            raise EvidenceError("UWOP_PUSH_MACHFRAME requires a machine-frame context")
        else:
            raise EvidenceError(f"unsupported x64 unwind operation {operation}")
    if chain is not None:
        if chain.unwind_rva in visited:
            raise EvidenceError("chained unwind metadata contains a cycle")
        # The chained parent prologue completed before the current fragment began.
        registers["RSP"] = rsp
        rsp = apply_unwind_codes(image, chain.unwind_rva, None, registers, memory, visited)
    registers["RSP"] = rsp
    return rsp


def unwind_one(image: PeImage, rva: int, registers: dict[str, int], memory: StackMemory) -> tuple[int, int]:
    runtime = image.runtime_function(rva)
    if runtime is None:
        raise EvidenceError(f"no .pdata runtime-function entry covers RVA 0x{rva:X}")
    before = registers["RSP"]
    rsp = apply_unwind_codes(image, runtime.unwind_rva, rva - runtime.begin, registers, memory, set())
    if rsp < before:
        raise EvidenceError(f"unwind moved RSP backwards from 0x{before:X} to 0x{rsp:X}")
    return runtime.begin, rsp


def validate_return_address(
    image: PeImage,
    symbols: list[tuple[int, str]],
    calls: dict[int, tuple[int, str]],
    return_address: int,
) -> tuple[bool, str | None, tuple[int, str] | None]:
    return_rva = return_address - image.image_base
    callsite = calls.get(return_address)
    valid = bool(callsite and return_rva > 0 and image.executable_rva(return_rva - 1))
    return valid, (symbol_at(return_address - 1, symbols) if valid else None), callsite


def validate_stack_window(
    low: int, high: int, start: int, length: int, file_length: int, rsp: int, max_bytes: int = 8192
) -> tuple[bool, str | None]:
    if low >= high or high - low > 65536 or not low <= rsp < high:
        return False, "invalid-reported-bounds-or-rsp-outside-stack"
    if start != rsp or length != file_length or length > max_bytes or length > high - start:
        return False, "captured-range-is-not-the-bounded-rsp-window"
    return True, None


def decode_iterator_context(
    registers: dict[str, int], image: PeImage, symbols: list[tuple[int, str]], memory: StackMemory,
    low: int, high: int,
) -> dict[str, Any]:
    candidates = [(name, registers[name]) for name in ("RBX", "R14") if name in registers and low <= registers[name] < high]
    if not candidates:
        return {"status": "unavailable-or-outside-captured-window", "error": "no supported nonvolatile register points inside the reported stack bounds"}
    errors: list[str] = []
    for register_name, iterator in candidates:
      try:
        if iterator + 0x230 > memory.end:
            raise EvidenceError(f"{register_name} object extends beyond captured stack bytes")
        iterator_control_pc = memory.u64(iterator + 0x18)
        code_manager = memory.u64(iterator + 0x150)
        regdisplay_sp = memory.u64(iterator + 0x98)
        regdisplay_ip = memory.u64(iterator + 0xA0)
        previous_transition_frame = memory.u64(iterator + 0x228)
        control_rva = iterator_control_pc - image.image_base
        regdisplay_rva = regdisplay_ip - image.image_base
        if not image.executable_rva(control_rva) or not image.executable_rva(regdisplay_rva):
            raise EvidenceError(f"{register_name} candidate does not contain mapped executable ControlPC and REGDISPLAY.IP values")
        if not low <= regdisplay_sp < high:
            raise EvidenceError(f"{register_name} candidate REGDISPLAY.SP is outside the reported stack bounds")
        return {
            "status": "decoded-from-bounded-stack",
            "iteratorPointerRegister": register_name,
            "iteratorAddress": f"0x{iterator:016X}",
            "iteratorOffsetFromRsp": f"0x{iterator - registers['RSP']:X}",
            "controlPc": f"0x{iterator_control_pc:016X}",
            "controlPcSymbol": symbol_at(iterator_control_pc, symbols) if image.executable_rva(control_rva) else None,
            "controlPcInCandidateExecutableMapping": image.executable_rva(control_rva),
            "codeManagerPointer": f"0x{code_manager:016X}",
            "codeManagerPointerInCandidateImage": image.section_for_rva(code_manager - image.image_base) is not None,
            "regDisplaySp": f"0x{regdisplay_sp:016X}",
            "regDisplayIp": f"0x{regdisplay_ip:016X}",
            "regDisplayIpSymbol": symbol_at(regdisplay_ip, symbols) if image.executable_rva(regdisplay_rva) else None,
            "regDisplayIpInCandidateExecutableMapping": image.executable_rva(regdisplay_rva),
            "previousTransitionFrame": f"0x{previous_transition_frame:016X}",
            "methodInfo": "opaque NativeAOT MethodInfo storage; not decoded",
        }
      except EvidenceError as exc:
        errors.append(str(exc))
    return {"status": "unavailable-or-outside-captured-window", "error": "; ".join(errors)}


def load_map(path: Path) -> tuple[int, list[tuple[int, str]]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    base_match = re.search(r"(?im)^\s*Preferred load address is\s+([0-9A-Fa-f]+)\s*$", text)
    if not base_match:
        raise EvidenceError("candidate map has no preferred load address")
    base = int(base_match.group(1), 16)
    symbols: list[tuple[int, str]] = []
    row = re.compile(r"^\s*(?P<section>[0-9A-Fa-f]{4}):[0-9A-Fa-f]{8}\s+(?P<symbol>\S+)\s+(?P<address>[0-9A-Fa-f]{16})\s+f(?:\s|$)")
    for line in text.splitlines():
        match = row.match(line)
        if match and int(match.group("section"), 16) == 1:
            symbols.append((int(match.group("address"), 16), match.group("symbol")))
    symbols.sort(key=lambda item: item[0])
    return base, symbols


def symbol_at(address: int, symbols: list[tuple[int, str]]) -> str | None:
    if not symbols:
        return None
    starts = [item[0] for item in symbols]
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return None
    return symbols[index][1]


def find_objdump(override: str | None) -> str:
    if override:
        return override
    found = shutil.which("objdump")
    if found:
        return found
    known = Path(r"C:\mingw64\bin\objdump.exe")
    if known.is_file():
        return str(known)
    raise EvidenceError("GNU objdump is required to verify call-site instruction boundaries")


def disassemble(image_path: Path, objdump: str) -> tuple[dict[int, tuple[int, str]], dict[int, tuple[int, int, str]]]:
    result = subprocess.run([objdump, "-d", str(image_path)], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise EvidenceError(f"objdump failed ({result.returncode}): {result.stderr.strip()}")
    calls: dict[int, tuple[int, str]] = {}
    instructions: dict[int, tuple[int, int, str]] = {}
    row = re.compile(r"^\s*([0-9A-Fa-f]+):\s*((?:[0-9A-Fa-f]{2}\s+)+)(.*)$")
    for line in result.stdout.splitlines():
        match = row.match(line)
        if not match:
            continue
        assembly = match.group(3).strip()
        address = int(match.group(1), 16)
        byte_count = len(match.group(2).split())
        instructions[address] = (address, byte_count, assembly)
        if re.match(r"(?i)^call\b", assembly):
            calls[address + byte_count] = (address, assembly)
    return calls, instructions


def classify_stack_walk(symbol_names: list[str]) -> tuple[str, str, list[str]]:
    joined = " ".join(symbol_names).lower()
    if any(token in joined for token in ("gcscanrootsworker", "gcscanroots", "scanstackroots", "promoteroots")):
        return "A_GC_ROOT_ENUMERATION", "Caller chain contains a NativeAOT GC root-enumeration method.", [name for name in symbol_names if any(token in name.lower() for token in ("gcscanroots", "scanstackroots", "promoteroots"))]
    if any(token in joined for token in ("internalinitforeh", "exception", "rhpthrow", "unwindthrow")):
        return "B_EXCEPTION_DISPATCH_OR_UNWIND", "Caller chain contains an exception or EH unwind method.", [name for name in symbol_names if any(token in name.lower() for token in ("exception", "rhpthrow", "unwindthrow", "internalinitforeh"))]
    if any(token in joined for token in ("stacktrace", "walkstack", "stackwalkframes")):
        return "C_STACK_TRACE_OR_DIAGNOSTIC", "Caller chain contains an explicit stack-trace or diagnostic walk method.", [name for name in symbol_names if any(token in name.lower() for token in ("stacktrace", "walkstack", "stackwalkframes"))]
    if any(token in joined for token in ("suspend", "hijack", "trapthreads", "redirect")):
        return "D_SUSPENSION_HIJACK_OR_SYNCHRONIZATION", "Caller chain contains a thread suspension, hijack, or redirection method.", [name for name in symbol_names if any(token in name.lower() for token in ("suspend", "hijack", "trapthreads", "redirect"))]
    external_callers = [name for name in symbol_names[1:] if "stackframeiterator" not in name.lower()]
    if external_callers:
        return "E_OTHER_RUNTIME_OPERATION", "The stack walk has a recovered caller outside StackFrameIterator and the known GC, EH, diagnostic, and suspension groups.", external_callers
    return "F_CALLER_UNRESOLVED", "The initiating runtime operation is not present in the recovered caller chain.", []


def max_binary_search_updates_until_linear(span: int, threshold: int = 10) -> int:
    """Upper bound on remaining midpoint updates for the pinned lookup loop."""
    if span <= threshold:
        return 0
    midpoint = span // 2
    # LookupUnwindInfoForMethod narrows either high to midpoint - 1 or low to
    # midpoint.  When span > threshold, both branches strictly reduce the span.
    return 1 + max(
        max_binary_search_updates_until_linear(midpoint - 1, threshold),
        max_binary_search_updates_until_linear(span - midpoint, threshold),
    )


def lookup_loop_state(symbol: str, instruction: str | None, registers: dict[str, int]) -> dict[str, Any] | None:
    if "LookupUnwindInfoForMethod" not in symbol or not instruction or not re.match(r"(?i)^jg\b", instruction):
        return None
    required = ("R9", "R10", "RAX", "R11", "RFL")
    if any(name not in registers for name in required):
        return {"status": "lookup-loop-registers-incomplete"}
    low = registers["R10"] & 0xFFFFFFFF
    high = registers["R9"] & 0xFFFFFFFF
    observed_span = registers["RAX"] & 0xFFFFFFFF
    span = high - low
    flags = registers["RFL"]
    zero = bool(flags & (1 << 6))
    sign = bool(flags & (1 << 7))
    overflow = bool(flags & (1 << 11))
    branch_taken = not zero and sign == overflow
    consistent = high >= low and observed_span == span
    return {
        "status": "bounded-runtime-function-binary-search" if consistent else "lookup-loop-state-inconsistent",
        "algorithm": "binary search while high - low > 10, followed by a bounded linear scan",
        "lowIndexInclusive": low,
        "highIndexInclusive": high,
        "highMinusLow": span,
        "observedEaxDifference": observed_span,
        "boundsAndDifferenceConsistent": consistent,
        "conditionalBranch": instruction,
        "conditionalBranchTaken": branch_taken,
        "lookupRelativePc": f"0x{registers['R11'] & 0xFFFFFFFF:X}",
        "maxRemainingBinarySearchUpdates": max_binary_search_updates_until_linear(span) if consistent else None,
        "linearSearchElementLimit": 10,
        "temporalProgressProven": False,
    }


def metadata_fields(capture_path: Path) -> tuple[dict[str, Any], str, str, str, str, str, str]:
    capture = read_json(capture_path)
    if "candidate" in capture and isinstance(capture["candidate"], dict):  # R15 recovered schema
        candidate_hash = str(capture["candidate"].get("manifestSha256", ""))
        registers = capture.get("registers", {})
        serial = capture.get("serial", {})
        register_path = str(registers.get("path", ""))
        register_hash = str(registers.get("sha256", ""))
        serial_path = str(serial.get("path", ""))
        serial_hash = str(serial.get("sha256", ""))
        expected_root = str(capture["candidate"].get("root", ""))
    else:  # timeout capture schema emitted by the runner
        candidate_hash = str(capture.get("candidateManifestSha256", ""))
        register_path = str(capture.get("registersPath", ""))
        register_hash = str(capture.get("registersSha256", ""))
        serial_path = str(capture.get("serialPath", ""))
        serial_hash = str(capture.get("serialSha256", ""))
        expected_root = str(capture.get("candidateRoot", ""))
    if not re.fullmatch(r"[0-9A-Fa-f]{64}", candidate_hash):
        raise EvidenceError("capture metadata does not bind a candidate-manifest SHA256")
    if not register_path or not re.fullmatch(r"[0-9A-Fa-f]{64}", register_hash):
        raise EvidenceError("capture metadata does not bind the raw CPU register file")
    if not serial_path or not re.fullmatch(r"[0-9A-Fa-f]{64}", serial_hash):
        raise EvidenceError("capture metadata does not bind the post-exit serial file")
    return capture, candidate_hash.upper(), register_path, register_hash.upper(), serial_path, serial_hash.upper(), expected_root


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    candidate_root = args.candidate_root.resolve()
    capture_path = args.capture_json.resolve()
    capture, candidate_hash, register_value, register_hash, serial_value, serial_hash, expected_root = metadata_fields(capture_path)
    if expected_root and os.path.normcase(os.path.abspath(expected_root)) != os.path.normcase(str(candidate_root)):
        raise EvidenceError("capture metadata candidate root does not match the supplied preserved candidate")
    manifest_path = candidate_root / "candidate-manifest.json"
    image_path = candidate_root / "managed" / "artifacts" / "HostLogProof.exe"
    map_path = candidate_root / "managed" / "artifacts" / "HostLogProof.map"
    if sha256(manifest_path) != candidate_hash:
        raise EvidenceError("candidate manifest hash does not match the preserved CPU capture")
    candidate_manifest = read_json(manifest_path)
    if candidate_manifest.get("runtimePackSourceCommit") != "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3":
        raise EvidenceError("candidate does not identify the pinned NativeAOT runtime commit")
    artifact_hashes = candidate_manifest.get("artifactHashes", {})
    manifest_pe_hash = str(artifact_hashes.get("candidateNativeAotPe", "")).upper()
    manifest_map_hash = str(artifact_hashes.get("candidateNativeAotMap", "")).upper()
    if not re.fullmatch(r"[0-9A-F]{64}", manifest_pe_hash) or sha256(image_path) != manifest_pe_hash:
        raise EvidenceError("exact candidate PE hash does not match candidate-manifest artifactHashes")
    if not re.fullmatch(r"[0-9A-F]{64}", manifest_map_hash) or sha256(map_path) != manifest_map_hash:
        raise EvidenceError("exact candidate map hash does not match candidate-manifest artifactHashes")
    recovered = capture.get("candidate", {}) if isinstance(capture.get("candidate"), dict) else {}
    expected_pe = str(recovered.get("managedPeSha256", ""))
    expected_map = str(recovered.get("managedMapSha256", ""))
    if expected_pe and sha256(image_path) != expected_pe.upper():
        raise EvidenceError("candidate PE hash does not match the preserved R15 recovery metadata")
    if expected_map and sha256(map_path) != expected_map.upper():
        raise EvidenceError("candidate map hash does not match the preserved R15 recovery metadata")
    registers_path = path_from_metadata(register_value, capture_path.parent)
    serial_path = path_from_metadata(serial_value, capture_path.parent)
    if sha256(registers_path) != register_hash:
        raise EvidenceError("raw CPU register file hash does not match recovery metadata")
    if sha256(serial_path) != serial_hash:
        raise EvidenceError("serial file hash does not match post-exit recovery metadata")
    registers = parse_registers(registers_path)
    image = PeImage(image_path)
    map_base, symbols = load_map(map_path)
    if map_base != image.image_base:
        raise EvidenceError(f"candidate PE image base 0x{image.image_base:X} disagrees with exact candidate map 0x{map_base:X}")
    rva = registers["RIP"] - image.image_base
    if rva < 0 or not image.executable_rva(rva):
        raise EvidenceError("captured RIP is outside the exact candidate executable mapping")
    current_symbol = symbol_at(registers["RIP"], symbols)
    if current_symbol is None:
        raise EvidenceError("captured RIP does not resolve to a function in the exact candidate map")
    runtime = image.runtime_function(rva)
    if runtime is None:
        raise EvidenceError("exact candidate .pdata has no runtime-function entry for RIP")
    map_function_va = next((address for address, name in symbols if name == current_symbol), None)
    if map_function_va is None:
        raise EvidenceError("exact candidate map is missing the sampled function entry")
    map_function_rva = map_function_va - map_base
    map_entry = image.runtime_function(map_function_rva)
    if map_function_rva < 0 or map_entry is None or map_entry.begin != map_function_rva:
        raise EvidenceError("sampled map function start does not match the exact PE unwind table")
    observed_module_base = map_function_va - map_function_rva
    if observed_module_base != image.image_base:
        raise EvidenceError("captured address relocation does not agree with the candidate PE and map")
    version, flags, prolog_size, frame, unwind_codes, chain = image.unwind_info(runtime.unwind_rva)
    rflags = registers.get("RFL", registers.get("RFLAGS", 0))
    raw_register_text = registers_path.read_text(encoding="ascii", errors="replace")
    cpl_match = re.search(r"(?im)\bCPL=(\d+)\b", raw_register_text)
    cs64 = bool(re.search(r"(?im)^CS\s*=.*\bCS64\b", raw_register_text))
    rip_disassembly = None
    if "ripResolution" in capture:
        rip_info = capture["ripResolution"]
        disassembly_path = path_from_metadata(str(rip_info.get("disassemblyPath", "")), capture_path.parent)
        expected_disasm = str(rip_info.get("disassemblySha256", ""))
        if not expected_disasm or sha256(disassembly_path) != expected_disasm.upper():
            raise EvidenceError("R15 RIP disassembly hash does not match recovered CPU metadata")
        rip_disassembly = disassembly_path.read_text(encoding="utf-8", errors="replace")
    else:
        rip_disassembly = ""
    rip_text = f"{registers['RIP']:016X}"
    result_value = registers["RAX"] & 0xFF
    objdump = find_objdump(args.objdump)
    calls, instructions = disassemble(image_path, objdump)
    current_instruction = instructions.get(registers["RIP"])
    current_lookup_loop = lookup_loop_state(
        current_symbol, current_instruction[2] if current_instruction else None, registers
    )
    # The instruction bytes, rather than the frame-pointer chain, determine where the
    # return value from the code-manager call is tested.
    previous_call = next((
        (address, assembly) for end, (address, assembly) in calls.items() if end == registers["RIP"]
    ), None)
    code_manager_call_returned = bool(
        result_value == 1 and current_instruction and re.match(r"(?i)^test\b", current_instruction[2]) and
        previous_call and "NextInternal" in current_symbol and "0x28" in previous_call[1]
    )
    if rip_disassembly:
        code_manager_call_returned = code_manager_call_returned and bool(
            re.search(r"(?im)^\s*100057EC9:\s*(?:[0-9A-Fa-f]{2}\s+)+call\b", rip_disassembly)
            and re.search(r"(?im)^\s*100057ECC:\s*(?:[0-9A-Fa-f]{2}\s+)+test\s+%al,%al\b", rip_disassembly)
        )
    result: dict[str, Any] = {
        "schema": "C188R16_NATIVE_UNWIND_ANALYSIS_V1",
        "candidateManifestSha256": candidate_hash,
        "candidatePeSha256": sha256(image_path),
        "candidateMapSha256": sha256(map_path),
        "runtimeSourceCommit": candidate_manifest.get("runtimePackSourceCommit"),
        "candidateImageBase": f"0x{image.image_base:016X}",
        "candidateRelocationDirectory": {"rva": f"0x{image.reloc_rva:X}", "size": image.reloc_size},
        "observedRip": f"0x{registers['RIP']:016X}",
        "observedRipRva": f"0x{rva:X}",
        "observedRsp": f"0x{registers['RSP']:016X}",
        "observedRbp": f"0x{registers['RBP']:016X}",
        "observedRax": f"0x{registers['RAX']:016X}",
        "observedRbx": f"0x{registers['RBX']:016X}" if "RBX" in registers else None,
        "observedR14": f"0x{registers['R14']:016X}" if "R14" in registers else None,
        "executionContext": {
            "guestCpuIndex": 0,
            "cpl": int(cpl_match.group(1)) if cpl_match else None,
            "cs64": cs64,
            "cr0": f"0x{registers['CR0']:016X}" if "CR0" in registers else None,
            "cr3": f"0x{registers['CR3']:016X}" if "CR3" in registers else None,
            "cr4": f"0x{registers['CR4']:016X}" if "CR4" in registers else None,
            "efer": f"0x{registers['EFER']:016X}" if "EFER" in registers else None,
            "rflags": f"0x{rflags:016X}",
        },
        "ripSymbol": current_symbol,
        "sampledInstruction": current_instruction[2] if current_instruction else None,
        "precedingCall": {
            "address": f"0x{previous_call[0]:016X}",
            "instruction": previous_call[1],
            "nextInstructionAddress": f"0x{registers['RIP']:016X}",
        } if previous_call else None,
        "unwindMetadata": {
            "runtimeFunctionBeginRva": f"0x{runtime.begin:X}",
            "runtimeFunctionEndRva": f"0x{runtime.end:X}",
            "unwindInfoRva": f"0x{runtime.unwind_rva:X}",
            "version": version,
            "flags": flags,
            "prologSize": prolog_size,
            "chainedFunctionBeginRva": f"0x{chain.begin:X}" if chain else None,
        },
        "codeManagerUnwindReturnedTrue": code_manager_call_returned if "NextInternal" in current_symbol else None,
        "sampledFunctionEntry": f"0x{map_function_va:016X}",
        "sampledFunctionOffset": f"0x{registers['RIP'] - map_function_va:X}",
        "sampledPointAssessment": (
            "RIP is at NextInternal's post-code-manager-call test; AL=1 proves that sampled unwind returned true."
            if code_manager_call_returned else
            "RIP is in the exact mapped runtime function; this instruction does not by itself establish the code-manager call result."
        ),
        "lookupLoopState": current_lookup_loop,
        "ripMatchesExactCandidate": bool(rip_text and current_symbol),
        "stackBounds": None,
        "callerFrames": [],
        "callerResolution": "unresolved",
        "stackWalkClassification": "F_CALLER_UNRESOLVED",
        "classificationEvidence": "Caller purpose is classified only from callers reconstructed from the exact candidate PE unwind metadata and captured guest stack bytes.",
        "progressAssessment": "a single paused snapshot cannot prove sustained progress or a persistent stall",
    }
    serial_text = serial_path.read_text(encoding="utf-8", errors="replace")
    passing_handoffs = [line for line in serial_text.splitlines() if
        line.startswith("[C188R12R4R5R2R3_HANDOFF]") and
        re.search(r"\bstatus=00000000\b", line) and
        re.search(r"\bstackReady=00000001\b", line) and
        re.search(r"\bstackInBounds=00000001\b", line)]
    for line in reversed(passing_handoffs):
        match = re.search(r"\bstackLow=([0-9A-Fa-f]{16})\b.*\bstackHigh=([0-9A-Fa-f]{16})\b", line)
        if match:
            low, high = int(match.group(1), 16), int(match.group(2), 16)
            if low < high and high - low <= 65536 and low <= registers["RSP"] < high:
                result["stackBounds"] = {"low": f"0x{low:016X}", "high": f"0x{high:016X}", "rspInBounds": True, "sourceMarker": line}
                break
    if args.stack_json is None:
        result["callerResolution"] = "unresolved-stack-memory-not-captured"
        return result
    stack_meta_path = args.stack_json.resolve()
    stack_meta = read_json(stack_meta_path)
    stack_schema = str(stack_meta.get("schema", ""))
    if stack_schema not in ("C188R16_GUEST_STACK_TIMEOUT_CAPTURE_V1", "C188R17_GUEST_STACK_PROGRESS_CAPTURE_V1"):
        raise EvidenceError("stack metadata schema is not a supported bounded R16/R17 guest stack capture")
    if "sampleNumber" in capture and "sampleNumber" in stack_meta and int(capture["sampleNumber"]) != int(stack_meta["sampleNumber"]):
        raise EvidenceError("CPU and stack metadata refer to different time-separated sample numbers")
    if str(stack_meta.get("candidateManifestSha256", "")).upper() != candidate_hash:
        raise EvidenceError("stack capture metadata does not bind the exact candidate manifest")
    if str(stack_meta.get("registersSha256", "")).upper() != register_hash:
        raise EvidenceError("stack capture metadata does not bind the exact CPU registers")
    if str(stack_meta.get("serialSha256", "")).upper() != serial_hash:
        raise EvidenceError("stack capture metadata does not bind the completed serial file")
    if stack_meta.get("status") != "captured":
        result["callerResolution"] = f"unresolved-stack-capture-{stack_meta.get('status', 'status-missing')}"
        result["stackCaptureStatus"] = stack_meta.get("status")
        return result
    stack_path_value = str(stack_meta.get("stackPath", ""))
    stack_path = path_from_metadata(stack_path_value, stack_meta_path.parent)
    if not stack_path.is_file():
        raise EvidenceError("captured guest stack file is missing")
    expected_stack_hash = str(stack_meta.get("stackSha256", "")).upper()
    if not expected_stack_hash or sha256(stack_path) != expected_stack_hash:
        raise EvidenceError("captured guest stack hash does not match stack metadata")
    low = int(str(stack_meta.get("reportedStackLow", "")), 16)
    high = int(str(stack_meta.get("reportedStackHigh", "")), 16)
    start = int(str(stack_meta.get("startAddress", "")), 16)
    length = int(stack_meta.get("lengthBytesCaptured", 0))
    valid_window, window_error = validate_stack_window(
        low, high, start, length, stack_path.stat().st_size, registers["RSP"]
    )
    if not valid_window:
        raise EvidenceError(f"captured stack window is invalid: {window_error}")
    memory = StackMemory(stack_path, start)
    result["stackBounds"] = {"low": f"0x{low:016X}", "high": f"0x{high:016X}", "capturedStart": f"0x{start:016X}", "capturedLengthBytes": length}
    result["stackCaptureSchema"] = stack_schema
    result["stackSampleNumber"] = stack_meta.get("sampleNumber")
    result["activeIteratorContext"] = decode_iterator_context(registers, image, symbols, memory, low, high)
    context = dict(registers)
    frames: list[dict[str, Any]] = [{"kind": "sampled-rip", "ip": f"0x{registers['RIP']:016X}", "symbol": current_symbol}]
    previous_rsp = registers["RSP"]
    seen: set[tuple[int, int]] = set()
    max_frames = max(1, min(int(args.max_frames), 64))
    for _ in range(max_frames - 1):
        pc = context["RIP"]
        lookup_address = pc if not frames or frames[-1]["kind"] == "sampled-rip" else pc - 1
        lookup_rva = lookup_address - image.image_base
        if lookup_rva < 0 or not image.executable_rva(lookup_rva):
            result["callerResolution"] = "caller-left-candidate-executable-mapping"
            break
        try:
            runtime_begin, after_unwind_rsp = unwind_one(image, lookup_rva, context, memory)
            return_address = memory.u64(after_unwind_rsp)
        except EvidenceError as exc:
            result["callerResolution"] = "unresolved-incomplete-stack-or-unwind-context"
            result["callerResolutionError"] = str(exc)
            break
        caller_rsp = after_unwind_rsp + 8
        if caller_rsp <= previous_rsp or caller_rsp > high:
            result["callerResolution"] = "invalid-stack-pointer-transition"
            break
        if return_address in (0, 0xFFFFFFFFFFFFFFFF):
            result["callerResolution"] = "stack-walk-reached-terminator"
            break
        valid_return, caller_symbol, callsite = validate_return_address(image, symbols, calls, return_address)
        if not valid_return or callsite is None:
            frames.append({"kind": "unvalidated-return-address", "ip": f"0x{return_address:016X}", "callerRsp": f"0x{caller_rsp:016X}"})
            result["callerResolution"] = (
                "caller-chain-recovered-to-unvalidated-terminal" if len(frames) > 1 else
                "invalid-or-unvalidated-return-address"
            )
            break
        frames.append({
            "kind": "verified-call-return",
            "ip": f"0x{return_address:016X}",
            "symbol": caller_symbol,
            "callInstruction": f"0x{callsite[0]:016X}: {callsite[1]}",
            "callerRsp": f"0x{caller_rsp:016X}",
            "unwoundFunctionBeginRva": f"0x{runtime_begin:X}",
        })
        key = (return_address, caller_rsp)
        if key in seen:
            result["callerResolution"] = "repeated-frame"
            break
        seen.add(key)
        context["RIP"] = return_address
        context["RSP"] = caller_rsp
        previous_rsp = caller_rsp
        result["callerResolution"] = "caller-chain-recovered"
    else:
        result["callerResolution"] = "bounded-frame-limit-reached"
    result["callerFrames"] = frames
    result["verifiedCallerFrameCount"] = sum(frame.get("kind") == "verified-call-return" for frame in frames)
    names = [str(frame.get("symbol")) for frame in frames if frame.get("symbol")]
    classification, basis, support = classify_stack_walk(names)
    result["stackWalkClassification"] = classification
    result["classificationEvidence"] = {"basis": basis, "supportingSymbols": support}
    if code_manager_call_returned:
        result["progressAssessment"] = "the sampled code-manager unwind call returned true; one snapshot cannot establish sustained progress or a persistent stall"
    elif current_lookup_loop and current_lookup_loop.get("status") == "bounded-runtime-function-binary-search":
        result["progressAssessment"] = (
            "the sampled instruction is at the taken branch of the candidate's bounded runtime-function binary search; "
            f"the observed bounds require at most {current_lookup_loop['maxRemainingBinarySearchUpdates']} further midpoint updates "
            "before its small linear scan, but one snapshot cannot prove return from the lookup or collection completion"
        )
    elif classification == "A_GC_ROOT_ENUMERATION":
        result["progressAssessment"] = "the verified caller chain places the sampled CPU in NativeAOT GC root enumeration; one snapshot cannot establish sustained progress, collection completion, or a persistent stall"
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--capture-json", type=Path, required=True, help="R15 recovered metadata or runner CPU timeout metadata")
    parser.add_argument("--stack-json", type=Path, help="Optional R16 bounded stack capture sidecar")
    parser.add_argument("--objdump", help="GNU objdump executable; defaults to PATH or C:\\mingw64\\bin\\objdump.exe")
    parser.add_argument("--max-frames", type=int, default=16)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args)
    except (EvidenceError, OSError, ValueError, struct.error) as exc:
        print(json.dumps({"schema": "C188R16_NATIVE_UNWIND_ANALYSIS_V1", "status": "invalid-evidence", "error": str(exc)}, indent=2))
        return 2
    output = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

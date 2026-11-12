#!/usr/bin/env python3
"""Validate and replay the pinned NativeAOT AMD64 runtime-function lookup."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


PINNED_RUNTIME_COMMIT = "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
UINT32_MAX = 0xFFFFFFFF
UINT64_MAX = 0xFFFFFFFFFFFFFFFF
INT_MAX = 0x7FFFFFFF
LINEAR_THRESHOLD = 10


class QualificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def checked_add(left: int, right: int, maximum: int, label: str) -> int:
    if left < 0 or right < 0 or left > maximum or right > maximum - left:
        raise QualificationError(f"{label} overflows its address width")
    return left + right


def checked_slice(data: bytes, offset: int, size: int, label: str) -> memoryview:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise QualificationError(f"{label} is outside the candidate file")
    return memoryview(data)[offset : offset + size]


def validate_runtime_function_count(count: int) -> None:
    if count <= 0 or count > INT_MAX:
        raise QualificationError("runtime-function count is empty or cannot be represented by the pinned signed-index lookup")


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

    @property
    def readable(self) -> bool:
        return bool(self.characteristics & 0x40000000)

    @property
    def virtual_end(self) -> int:
        return self.virtual_address + max(self.virtual_size, self.raw_size)


@dataclass(frozen=True)
class RuntimeFunction:
    begin: int
    end: int
    unwind_rva: int


class PeImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        checked_slice(self.data, 0, 0x40, "DOS header")
        if self.data[:2] != b"MZ":
            raise QualificationError(f"not a PE image: {path}")
        pe_offset = struct.unpack_from("<I", self.data, 0x3C)[0]
        checked_slice(self.data, pe_offset, 24, "PE/COFF header")
        if self.data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise QualificationError("invalid PE signature")
        coff = pe_offset + 4
        machine, section_count = struct.unpack_from("<HH", self.data, coff)
        optional_size = struct.unpack_from("<H", self.data, coff + 16)[0]
        if machine != 0x8664:
            raise QualificationError(f"expected AMD64 PE machine, got 0x{machine:04X}")
        optional = coff + 20
        checked_slice(self.data, optional, optional_size, "optional header")
        if optional_size < 112 or struct.unpack_from("<H", self.data, optional)[0] != 0x20B:
            raise QualificationError("expected a complete PE32+ optional header")
        self.image_base = struct.unpack_from("<Q", self.data, optional + 24)[0]
        self.size_of_image = struct.unpack_from("<I", self.data, optional + 56)[0]
        self.size_of_headers = struct.unpack_from("<I", self.data, optional + 60)[0]
        directory_count = struct.unpack_from("<I", self.data, optional + 108)[0]
        if not self.size_of_image or not self.size_of_headers or self.size_of_headers > len(self.data):
            raise QualificationError("PE image or header size is invalid")
        if directory_count <= 5 or optional_size < 112 + min(directory_count, 16) * 8:
            raise QualificationError("PE data directories omit exception or relocation metadata")
        directory_base = optional + 112
        self.exception_rva, self.exception_size = struct.unpack_from("<II", self.data, directory_base + 3 * 8)
        self.reloc_rva, self.reloc_size = struct.unpack_from("<II", self.data, directory_base + 5 * 8)
        section_table = optional + optional_size
        checked_slice(self.data, section_table, section_count * 40, "section table")
        self.sections: list[Section] = []
        for index in range(section_count):
            offset = section_table + index * 40
            name = self.data[offset : offset + 8].split(b"\0", 1)[0].decode("ascii", errors="replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", self.data, offset + 8)
            characteristics = struct.unpack_from("<I", self.data, offset + 36)[0]
            checked_add(virtual_address, max(virtual_size, raw_size), UINT32_MAX + 1, f"section {name} virtual range")
            checked_slice(self.data, raw_offset, raw_size, f"section {name} raw range")
            if virtual_address + max(virtual_size, raw_size) > self.size_of_image:
                raise QualificationError(f"section {name} extends beyond SizeOfImage")
            self.sections.append(Section(name, virtual_address, virtual_size, raw_offset, raw_size, characteristics))
        if not self.exception_rva or not self.exception_size or self.exception_size % 12:
            raise QualificationError("AMD64 exception directory is missing or not a whole number of 12-byte records")
        checked_add(self.exception_rva, self.exception_size, UINT32_MAX + 1, "exception directory RVA range")
        if self.exception_rva + self.exception_size > self.size_of_image:
            raise QualificationError("exception directory extends beyond SizeOfImage")
        self.exception_offset = self.rva_to_offset(self.exception_rva, self.exception_size)
        if self.reloc_size:
            checked_add(self.reloc_rva, self.reloc_size, UINT32_MAX + 1, "relocation directory RVA range")
            if self.reloc_rva + self.reloc_size > self.size_of_image:
                raise QualificationError("relocation directory extends beyond SizeOfImage")
            self.rva_to_offset(self.reloc_rva, self.reloc_size)
        elif self.reloc_rva:
            raise QualificationError("empty relocation directory has a nonzero RVA")
        self.runtime_functions = self._read_runtime_functions()

    def rva_to_offset(self, rva: int, size: int = 1) -> int:
        if rva < 0 or size < 0 or rva > self.size_of_image or size > self.size_of_image - rva:
            raise QualificationError(f"RVA range 0x{rva:X}+0x{size:X} is outside SizeOfImage")
        if rva < self.size_of_headers:
            if size > self.size_of_headers - rva:
                raise QualificationError("RVA range crosses the end of PE headers")
            checked_slice(self.data, rva, size, "PE headers")
            return rva
        for section in self.sections:
            if section.virtual_address <= rva and rva - section.virtual_address <= max(section.virtual_size, section.raw_size):
                delta = rva - section.virtual_address
                if size > section.raw_size or delta > section.raw_size - size:
                    raise QualificationError(f"RVA range 0x{rva:X}+0x{size:X} is not file-backed")
                offset = section.raw_offset + delta
                checked_slice(self.data, offset, size, f"section {section.name}")
                return offset
        raise QualificationError(f"RVA 0x{rva:X} is outside PE sections")

    def section_for_range(self, rva: int, size: int) -> Section | None:
        if size <= 0:
            return None
        for section in self.sections:
            if section.virtual_address <= rva and rva - section.virtual_address <= section.virtual_end - section.virtual_address:
                if size <= section.virtual_end - rva:
                    return section
        return None

    def is_executable_range(self, rva: int, size: int) -> bool:
        section = self.section_for_range(rva, size)
        return section is not None and section.executable

    def validate_unwind_info(self, unwind_rva: int) -> None:
        if unwind_rva == 0 or unwind_rva & 3:
            raise QualificationError(f"unwind RVA 0x{unwind_rva:X} is zero or not 4-byte aligned")
        section = self.section_for_range(unwind_rva, 4)
        if section is None or not section.readable:
            raise QualificationError(f"unwind header RVA 0x{unwind_rva:X} is not in a readable mapped section")
        offset = self.rva_to_offset(unwind_rva, 4)
        version_flags, prolog_size, code_count, frame = struct.unpack_from("<BBBB", self.data, offset)
        version = version_flags & 0x7
        flags = version_flags >> 3
        if version not in (1, 2) or flags & ~0x7 or (flags & 0x4 and flags & 0x3):
            raise QualificationError(f"unwind header at RVA 0x{unwind_rva:X} has invalid version/flags")
        if code_count > 255:
            raise QualificationError("unwind code count exceeds its encoded width")
        code_bytes = code_count * 2
        aligned_code_bytes = (code_bytes + 3) & ~3
        extra_bytes = 12 if flags & 0x4 else (4 if flags & 0x3 else 0)
        total_size = 4 + aligned_code_bytes + extra_bytes
        self.rva_to_offset(unwind_rva, total_size)
        if frame & 0x0F == 0 and frame >> 4 != 0:
            raise QualificationError(f"unwind header at RVA 0x{unwind_rva:X} has frame offset without a frame register")
        # Validate the encoded unwind-code extent here. Opcode interpretation is
        # intentionally left to the pinned NativeAOT AMD64 unwinder; this tool
        # qualifies lookup metadata and must not invent a parallel unwind ABI.
        if flags & 0x4:
            chain_offset = offset + 4 + aligned_code_bytes
            chain_begin, chain_end, chain_unwind = struct.unpack_from("<III", self.data, chain_offset)
            if chain_begin >= chain_end or chain_end > self.size_of_image:
                raise QualificationError(f"chained runtime function at unwind RVA 0x{unwind_rva:X} is malformed")
            if not self.is_executable_range(chain_begin, chain_end - chain_begin):
                raise QualificationError(f"chained runtime function at unwind RVA 0x{unwind_rva:X} is not executable")
            if chain_unwind == 0 or chain_unwind & 3:
                raise QualificationError(f"chained unwind RVA 0x{chain_unwind:X} is invalid")
        elif flags & 0x3:
            handler_rva = struct.unpack_from("<I", self.data, offset + 4 + aligned_code_bytes)[0]
            if handler_rva == 0 or handler_rva >= self.size_of_image:
                raise QualificationError(f"exception handler RVA at unwind info 0x{unwind_rva:X} is outside the image")

    def _read_runtime_functions(self) -> list[RuntimeFunction]:
        count = self.exception_size // 12
        validate_runtime_function_count(count)
        functions: list[RuntimeFunction] = []
        for index in range(count):
            offset = self.exception_offset + index * 12
            begin, end, unwind = struct.unpack_from("<III", self.data, offset)
            if begin >= end:
                raise QualificationError(f"runtime-function entry {index} has an empty or reversed range")
            if end > self.size_of_image:
                raise QualificationError(f"runtime-function entry {index} ends beyond SizeOfImage")
            if not self.is_executable_range(begin, end - begin):
                raise QualificationError(f"runtime-function entry {index} is not fully inside one executable section")
            self.validate_unwind_info(unwind)
            current = RuntimeFunction(begin, end, unwind)
            if functions:
                previous = functions[-1]
                if current.begin <= previous.begin:
                    raise QualificationError(f"runtime-function entries are not strictly sorted at index {index}")
                if current.begin < previous.end:
                    raise QualificationError(f"runtime-function entries overlap at indices {index - 1} and {index}")
            functions.append(current)
        return functions


def validate_table(
    entries: list[RuntimeFunction],
    image_size: int,
    is_executable: Callable[[int, int], bool] | None = None,
    valid_unwind: Callable[[int], bool] | None = None,
) -> None:
    validate_runtime_function_count(len(entries))
    for index, entry in enumerate(entries):
        if not (0 <= entry.begin < entry.end <= UINT32_MAX):
            raise QualificationError(f"entry {index} has invalid 32-bit BeginAddress/EndAddress")
        if entry.end > image_size:
            raise QualificationError(f"entry {index} is outside SizeOfImage")
        if index:
            previous = entries[index - 1]
            if entry.begin <= previous.begin:
                raise QualificationError(f"table is unsorted or has duplicate starts at index {index}")
            if entry.begin < previous.end:
                raise QualificationError(f"entries {index - 1} and {index} overlap")
        if is_executable is not None and not is_executable(entry.begin, entry.end - entry.begin):
            raise QualificationError(f"entry {index} does not map wholly to executable code")
        if entry.unwind_rva <= 0 or entry.unwind_rva > UINT32_MAX or entry.unwind_rva & 3:
            raise QualificationError(f"entry {index} has invalid unwind metadata RVA")
        if valid_unwind is not None and not valid_unwind(entry.unwind_rva):
            raise QualificationError(f"entry {index} references invalid unwind metadata")


def replay_pinned_lookup(entries: list[RuntimeFunction], target_rva: int, low: int = 0, high: int | None = None) -> dict:
    """Literal translation of LookupUnwindInfoForMethod at the pinned revision."""
    count = len(entries)
    if high is None:
        high = count - 1
    if count == 0 or not (0 <= low <= high < count):
        raise QualificationError("lookup interval is outside the runtime-function table")
    if not (0 <= target_rva <= UINT32_MAX):
        raise QualificationError("relative PC is outside the production uint32_t domain")
    initial_low, initial_high = low, high
    midpoint_steps: list[dict[str, int | str]] = []
    scan_steps: list[dict[str, int | bool]] = []
    while high - low > LINEAR_THRESHOLD:
        middle = low + (high - low) // 2
        begin = entries[middle].begin
        if target_rva < begin:
            high = middle - 1
            direction = "high=middle-1"
        else:
            low = middle
            direction = "low=middle"
        midpoint_steps.append({"middle": middle, "beginAddress": begin, "direction": direction, "lowAfter": low, "highAfter": high})
    index = low
    while index < high:
        next_begin = entries[index + 1].begin
        before_next = target_rva < next_begin
        scan_steps.append({"index": index, "nextBeginAddress": next_begin, "targetBeforeNextBegin": before_next})
        if before_next:
            high = index
            break
        index += 1
    chosen = entries[high]
    starts_before_or_at_target = target_rva >= chosen.begin
    covered = chosen.begin <= target_rva < chosen.end
    status = "MATCH" if starts_before_or_at_target and covered else "SOURCE_ASSERT_INVALID_ADDRESS" if not starts_before_or_at_target else "PREDECESSOR_DOES_NOT_COVER_TARGET"
    return {
        "status": status,
        "initialInterval": {"lowInclusive": initial_low, "highInclusive": initial_high, "highMinusLow": initial_high - initial_low},
        "terminalInterval": {"lowInclusive": low, "highInclusive": high, "highMinusLow": high - low},
        "midpointSteps": midpoint_steps,
        "linearScanSteps": scan_steps,
        "finalBeginAddressCheck": starts_before_or_at_target,
        "matchingIndex": high if status == "MATCH" else None,
        "candidateIndex": high,
        "candidateEntry": {"begin": chosen.begin, "end": chosen.end, "unwindRva": chosen.unwind_rva},
        "targetCoveredByReturnedEntry": covered,
        "remainingMidpointUpdates": len(midpoint_steps),
        "linearScanComparisons": len(scan_steps),
        "tableComparisons": len(midpoint_steps) + len(scan_steps) + 1,
    }


def resolve_image_address(image_base: int, relative_pc: int, size_of_image: int) -> int:
    if not 0 <= image_base <= UINT64_MAX:
        raise QualificationError("module base is outside the uint64_t domain")
    if not 0 <= relative_pc <= UINT32_MAX:
        raise QualificationError("relative PC would be truncated by the production uint32_t cast")
    address = checked_add(image_base, relative_pc, UINT64_MAX, "module base plus relative PC")
    if relative_pc >= size_of_image:
        raise QualificationError("target RVA is outside SizeOfImage")
    return address


def parse_registers(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8", errors="replace")
    cpu = re.search(r"(?ms)^CPU#0\s*\r?\n(?P<body>.*?)(?=^CPU#\d+\s*$|\Z)", text)
    body = cpu.group("body") if cpu else text
    registers = {name.upper(): int(value, 16) for name, value in re.findall(r"\b([A-Z][A-Z0-9]*)\s*=\s*([0-9A-Fa-f]{1,16})\b", body)}
    for name in ("RIP", "RSP", "RAX", "RBX", "RDX", "R9", "R10", "R11", "RFL"):
        if name not in registers:
            raise QualificationError(f"captured CPU state is missing {name}")
    return registers


def map_base_and_lookup_symbol(map_path: Path) -> tuple[int, bool]:
    text = map_path.read_text(encoding="utf-8", errors="replace")
    base_match = re.search(r"(?im)^\s*Image base\s*:\s*([0-9A-Fa-f]+)", text)
    if not base_match:
        base_match = re.search(r"(?im)^\s*Preferred load address\s+(?:is\s+)?([0-9A-Fa-f]+)", text)
    if not base_match:
        raise QualificationError("candidate map has no image-base declaration")
    return int(base_match.group(1), 16), "LookupUnwindInfoForMethod" in text


def decode_instruction(objdump: str, image_path: Path, address: int) -> str:
    command = [objdump, "-d", "-Mintel", f"--start-address=0x{address:X}", f"--stop-address=0x{address + 16:X}", str(image_path)]
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode:
        raise QualificationError(f"objdump failed with exit code {result.returncode}: {result.stderr.strip()}")
    for line in result.stdout.splitlines():
        match = re.match(r"^\s*[0-9A-Fa-f]+:\s+(?:[0-9A-Fa-f]{2}\s+)+([a-zA-Z][a-zA-Z0-9.]*)\s*(.*)$", line)
        if match and int(line.split(":", 1)[0].strip(), 16) == address:
            return (match.group(1) + " " + match.group(2)).strip()
    raise QualificationError(f"objdump did not decode the captured RIP 0x{address:X}")


def qualify_candidate_table(candidate_root: Path) -> dict:
    """Qualify a fresh candidate's generated .pdata before a guest has sampled it."""
    candidate_root = candidate_root.resolve()
    manifest_path = candidate_root / "candidate-manifest.json"
    pe_path = candidate_root / "managed" / "artifacts" / "HostLogProof.exe"
    map_path = candidate_root / "managed" / "artifacts" / "HostLogProof.map"
    if not manifest_path.is_file() or not pe_path.is_file() or not map_path.is_file():
        raise QualificationError("candidate is missing its manifest, NativeAOT PE, or map")
    candidate = json.loads(manifest_path.read_text(encoding="utf-8"))
    if candidate.get("runtimePackSourceCommit") != PINNED_RUNTIME_COMMIT:
        raise QualificationError("candidate does not use the exact pinned NativeAOT source revision")
    if candidate.get("mode") != "NativeAotManagedGcRootProbe" or not candidate.get("c188R13ManagedGcRootProbe"):
        raise QualificationError("candidate is not the explicitly gated R13 managed-GC-root profile")
    artifact_hashes = candidate.get("artifactHashes", {})
    manifest_hash = sha256(manifest_path)
    pe_hash = sha256(pe_path)
    map_hash = sha256(map_path)
    if artifact_hashes.get("candidateNativeAotPe", "").upper() != pe_hash:
        raise QualificationError("NativeAOT PE hash does not match candidate manifest")
    if artifact_hashes.get("candidateNativeAotMap", "").upper() != map_hash:
        raise QualificationError("NativeAOT map hash does not match candidate manifest")
    image = PeImage(pe_path)
    map_base, map_has_lookup = map_base_and_lookup_symbol(map_path)
    if map_base != image.image_base or not map_has_lookup:
        raise QualificationError("candidate map does not agree with PE image base or omit the lookup symbol")
    def valid_unwind(unwind_rva: int) -> bool:
        image.validate_unwind_info(unwind_rva)
        return True

    validate_table(image.runtime_functions, image.size_of_image, image.is_executable_range, valid_unwind)
    table_address = checked_add(image.image_base, image.exception_rva, UINT64_MAX, "runtime-function table VA")
    comparison_candidates: set[int] = set()
    for entry in image.runtime_functions:
        comparison_candidates.add(entry.begin)
        comparison_candidates.add(entry.end - 1)
        if entry.begin:
            comparison_candidates.add(entry.begin - 1)
        if entry.end < image.size_of_image:
            comparison_candidates.add(entry.end)
    max_work = {"midpointUpdates": 0, "linearScanComparisons": 0, "tableComparisons": 0, "targetRva": None, "status": None}
    for target_rva in comparison_candidates:
        if target_rva > UINT32_MAX:
            continue
        replay = replay_pinned_lookup(image.runtime_functions, target_rva)
        if replay["tableComparisons"] > max_work["tableComparisons"]:
            max_work = {
                "midpointUpdates": replay["remainingMidpointUpdates"],
                "linearScanComparisons": replay["linearScanComparisons"],
                "tableComparisons": replay["tableComparisons"],
                "targetRva": f"0x{target_rva:X}",
                "status": replay["status"],
            }
    first, last = image.runtime_functions[0], image.runtime_functions[-1]
    return {
        "schema": "C188R17_RUNTIME_FUNCTION_TABLE_QUALIFICATION_V1",
        "result": "GC_LOOKUP_OFFLINE_QUALIFIED",
        "guestSampleAvailable": False,
        "candidateRoot": str(candidate_root),
        "candidateManifestSha256": manifest_hash,
        "candidatePeSha256": pe_hash,
        "candidateMapSha256": map_hash,
        "runtimeSourceCommit": PINNED_RUNTIME_COMMIT,
        "loadedImage": {
            "preferredImageBase": f"0x{image.image_base:016X}",
            "relocationDirectoryRva": f"0x{image.reloc_rva:X}",
            "relocationDirectorySize": image.reloc_size,
            "relocationsPresent": image.reloc_size != 0,
            "imageSize": image.size_of_image,
        },
        "runtimeFunctionTable": {
            "source": "PE AMD64 exception directory consumed by NativeAOT CoffNativeCodeManager::Initialize",
            "tableRva": f"0x{image.exception_rva:X}",
            "tableAddress": f"0x{table_address:016X}",
            "tableSizeBytes": image.exception_size,
            "entrySizeBytes": 12,
            "entryCount": len(image.runtime_functions),
            "firstEntry": {"begin": f"0x{first.begin:X}", "end": f"0x{first.end:X}"},
            "lastEntry": {"begin": f"0x{last.begin:X}", "end": f"0x{last.end:X}"},
            "originalOrderStrictlySorted": True,
            "overlapFree": True,
            "allBeginEndRangesExecutable": True,
            "allUnwindHeadersValidated": True,
        },
        "complexity": {
            "algorithm": "binary search while high-low > 10; inclusive midpoint updates followed by the source's low-to-high linear scan and final BeginAddress guard",
            "maximumMidpointUpdatesForActualTable": max_work["midpointUpdates"],
            "maximumLinearScanComparisonsForActualTable": max_work["linearScanComparisons"],
            "maximumBeginAddressComparisonsForActualTable": max_work["tableComparisons"],
            "maximumWorkWitnessTargetRva": max_work["targetRva"],
            "maximumWorkWitnessStatus": max_work["status"],
            "wallClockBoundClaimed": False,
        },
        "assessment": "Candidate table structure and source-translation work bounds are qualified offline; no guest progress or GC completion is inferred.",
    }


def qualify(args: argparse.Namespace) -> dict:
    candidate_root = args.candidate_root.resolve()
    capture_path = args.capture_json.resolve()
    manifest_path = candidate_root / "candidate-manifest.json"
    pe_path = candidate_root / "managed" / "artifacts" / "HostLogProof.exe"
    map_path = candidate_root / "managed" / "artifacts" / "HostLogProof.map"
    if not manifest_path.is_file() or not pe_path.is_file() or not map_path.is_file():
        raise QualificationError("preserved candidate is missing its manifest, NativeAOT PE, or map")
    capture = json.loads(capture_path.read_text(encoding="utf-8"))
    if capture.get("schema") != "C188R15_GUEST_CPU_TIMEOUT_STATE_V1" or capture.get("status") != "captured":
        raise QualificationError("CPU capture schema or status is not a valid R15/R16 snapshot")
    manifest_hash = sha256(manifest_path)
    if capture.get("candidateManifestSha256", "").upper() != manifest_hash:
        raise QualificationError("capture is not bound to this candidate manifest")
    candidate = json.loads(manifest_path.read_text(encoding="utf-8"))
    if candidate.get("runtimePackSourceCommit") != PINNED_RUNTIME_COMMIT:
        raise QualificationError("candidate does not use the exact pinned NativeAOT source revision")
    artifact_hashes = candidate.get("artifactHashes", {})
    pe_hash = sha256(pe_path)
    map_hash = sha256(map_path)
    if artifact_hashes.get("candidateNativeAotPe", "").upper() != pe_hash:
        raise QualificationError("NativeAOT PE hash does not match candidate manifest")
    if artifact_hashes.get("candidateNativeAotMap", "").upper() != map_hash:
        raise QualificationError("NativeAOT map hash does not match candidate manifest")
    registers_path = Path(capture.get("registersPath", ""))
    serial_path = Path(capture.get("serialPath", ""))
    if not registers_path.is_file() or not serial_path.is_file():
        raise QualificationError("captured register file or post-QEMU serial file is missing")
    if sha256(registers_path) != capture.get("registersSha256", "").upper():
        raise QualificationError("captured register hash does not match R16 metadata")
    if sha256(serial_path) != capture.get("serialSha256", "").upper() or capture.get("serialHashStatus") != "verified-after-qemu-exit":
        raise QualificationError("serial hash was not verified after QEMU released the file")
    registers = parse_registers(registers_path)
    image = PeImage(pe_path)
    map_base, map_has_lookup = map_base_and_lookup_symbol(map_path)
    if map_base != image.image_base:
        raise QualificationError("candidate PE and exact map disagree about the image base")
    if not map_has_lookup:
        raise QualificationError("exact candidate map does not contain LookupUnwindInfoForMethod")
    if capture.get("rip", "").upper() != f"{registers['RIP']:016X}":
        raise QualificationError("capture RIP field contradicts the preserved CPU register file")
    table_address = checked_add(image.image_base, image.exception_rva, UINT64_MAX, "runtime-function table VA")
    if registers["RBX"] != table_address or registers["RDX"] != table_address:
        raise QualificationError("captured runtime-function table pointer does not match the PE exception directory")
    target_rva = registers["R11"] & UINT32_MAX
    target_address = resolve_image_address(image.image_base, target_rva, image.size_of_image)
    if not image.is_executable_range(target_rva, 1):
        raise QualificationError("sampled target ControlPC is not executable in the candidate image")
    instruction_tool = args.objdump or shutil.which("objdump") or shutil.which("llvm-objdump")
    if not instruction_tool:
        raise QualificationError("objdump or llvm-objdump is required to verify the exact sampled instruction")
    instruction = decode_instruction(instruction_tool, pe_path, registers["RIP"])
    if not re.match(r"(?i)^jg\b", instruction):
        raise QualificationError(f"captured RIP is not the pinned loop's taken jg condition: {instruction}")
    low = registers["R10"] & UINT32_MAX
    high = registers["R9"] & UINT32_MAX
    observed_span = registers["RAX"] & UINT32_MAX
    if high < low or high - low != observed_span:
        raise QualificationError("captured low/high bounds contradict RAX or form an invalid interval")
    if high >= len(image.runtime_functions):
        raise QualificationError("captured lookup interval exceeds the runtime-function table")
    if not (registers["RFL"] & (1 << 6)) and bool(registers["RFL"] & (1 << 7)) == bool(registers["RFL"] & (1 << 11)):
        branch_taken = True
    else:
        branch_taken = False
    if not branch_taken:
        raise QualificationError("captured RFLAGS do not make the sampled jg branch taken")
    if observed_span <= LINEAR_THRESHOLD:
        raise QualificationError("captured taken jg contradicts the pinned loop threshold")
    sampled = replay_pinned_lookup(image.runtime_functions, target_rva, low, high)
    full = replay_pinned_lookup(image.runtime_functions, target_rva)
    if sampled["status"] != "MATCH":
        raise QualificationError("the exact pinned lookup does not resolve the captured target from sampled bounds")
    target_entry = image.runtime_functions[sampled["matchingIndex"]]
    if not (target_entry.begin <= target_rva < target_entry.end):
        raise QualificationError("replayed table entry does not cover the sampled target")
    comparison_candidates = set()
    for entry in image.runtime_functions:
        if entry.begin < image.size_of_image:
            comparison_candidates.add(entry.begin)
            if entry.begin:
                comparison_candidates.add(entry.begin - 1)
        if entry.end < image.size_of_image:
            comparison_candidates.add(entry.end)
            comparison_candidates.add(entry.end - 1)
    max_work = {"midpointUpdates": 0, "linearScanComparisons": 0, "tableComparisons": 0, "targetRva": None, "status": None}
    for candidate_rva in comparison_candidates:
        if candidate_rva > UINT32_MAX:
            continue
        result = replay_pinned_lookup(image.runtime_functions, candidate_rva)
        if result["tableComparisons"] > max_work["tableComparisons"]:
            max_work = {
                "midpointUpdates": result["remainingMidpointUpdates"],
                "linearScanComparisons": result["linearScanComparisons"],
                "tableComparisons": result["tableComparisons"],
                "targetRva": f"0x{candidate_rva:X}",
                "status": result["status"],
            }
    return {
        "schema": "C188R17_RUNTIME_FUNCTION_LOOKUP_QUALIFICATION_V1",
        "result": "GC_LOOKUP_OFFLINE_QUALIFIED",
        "candidateManifestSha256": manifest_hash,
        "candidatePeSha256": pe_hash,
        "candidateMapSha256": map_hash,
        "runtimeSourceCommit": PINNED_RUNTIME_COMMIT,
        "loadedImage": {
            "preferredImageBase": f"0x{image.image_base:016X}",
            "observedModuleBase": f"0x{image.image_base:016X}",
            "relocationDirectoryRva": f"0x{image.reloc_rva:X}",
            "relocationDirectorySize": image.reloc_size,
            "relocationsPresent": image.reloc_size != 0,
            "imageSize": image.size_of_image,
        },
        "runtimeFunctionTable": {
            "source": "PE AMD64 exception directory consumed by NativeAOT CoffNativeCodeManager::Initialize",
            "tableRva": f"0x{image.exception_rva:X}",
            "tableAddress": f"0x{table_address:016X}",
            "entrySizeBytes": 12,
            "entryCount": len(image.runtime_functions),
            "firstEntry": {"begin": f"0x{image.runtime_functions[0].begin:X}", "end": f"0x{image.runtime_functions[0].end:X}"},
            "lastEntry": {"begin": f"0x{image.runtime_functions[-1].begin:X}", "end": f"0x{image.runtime_functions[-1].end:X}"},
            "originalOrderStrictlySorted": True,
            "overlapFree": True,
            "allBeginEndRangesExecutable": True,
            "allUnwindHeadersValidated": True,
        },
        "sample": {
            "rip": f"0x{registers['RIP']:016X}",
            "instruction": instruction,
            "targetRelativePc": f"0x{target_rva:X}",
            "targetAddress": f"0x{target_address:016X}",
            "tablePointerRegisterRbx": f"0x{registers['RBX']:016X}",
            "tablePointerRegisterRdx": f"0x{registers['RDX']:016X}",
            "lowIndexInclusive": low,
            "highIndexInclusive": high,
            "highMinusLow": observed_span,
            "rflags": f"0x{registers['RFL']:X}",
            "conditionalBranchTaken": branch_taken,
            "registerFileSha256": sha256(registers_path),
            "serialSha256": sha256(serial_path),
        },
        "sampledIntervalReplay": sampled,
        "fullLookupReplay": full,
        "complexity": {
            "algorithm": "binary search while high-low > 10; inclusive midpoint updates followed by the source's low-to-high linear scan and final BeginAddress guard",
            "maximumMidpointUpdatesForActualTable": max_work["midpointUpdates"],
            "maximumLinearScanComparisonsForActualTable": max_work["linearScanComparisons"],
            "maximumBeginAddressComparisonsForActualTable": max_work["tableComparisons"],
            "maximumWorkWitnessTargetRva": max_work["targetRva"],
            "maximumWorkWitnessStatus": max_work["status"],
            "sampledRemainingMidpointUpdates": sampled["remainingMidpointUpdates"],
            "sampledRemainingLinearComparisons": sampled["linearScanComparisons"],
            "sampledRemainingBeginAddressComparisons": sampled["tableComparisons"],
            "wallClockBoundClaimed": False,
        },
        "assessment": "The preserved sample resolves to a valid entry in the exact candidate table. The pinned search has strictly shrinking midpoint bounds and a scan bounded by the threshold interval. This qualifies lookup structure and remaining operations only; it does not prove temporal progress, UnwindStackFrame success, iterator advancement, root-scan completion, or GC completion.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--capture-json", type=Path)
    parser.add_argument("--candidate-table-only", action="store_true",
                        help="qualify the exact candidate's generated .pdata without requiring a guest snapshot")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--objdump")
    args = parser.parse_args()
    try:
        if args.candidate_table_only:
            result = qualify_candidate_table(args.candidate_root)
        elif args.capture_json is None:
            raise QualificationError("--capture-json is required unless --candidate-table-only is selected")
        else:
            result = qualify(args)
    except (QualificationError, OSError, ValueError, KeyError, struct.error, json.JSONDecodeError) as exc:
        print(json.dumps({"schema": "C188R17_RUNTIME_FUNCTION_LOOKUP_QUALIFICATION_V1", "result": "GC_LOOKUP_OFFLINE_REJECTED", "error": str(exc)}, indent=2))
        return 2
    rendered = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())

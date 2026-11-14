#!/usr/bin/env python3
"""Hosted R24 verification of the pinned R23 linked NativeAOT image."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path


DEFAULT_CANDIDATE = Path(
    r"E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r23-settings-module-preparation"
    r"\candidate-20261114T174459288Z-fd3f057c4f7f4d9280d0a6d05e57831f"
)


class Checks:
    def __init__(self) -> None:
        self.count = 0

    def require(self, value: bool, label: str) -> None:
        self.count += 1
        if not value:
            raise AssertionError(label)


def load_segments(elf: bytes) -> list[tuple[int, int, int]]:
    if elf[:4] != b"\x7fELF" or elf[4] != 2 or elf[5] != 1:
        raise ValueError("expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", elf, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 54)
    result = []
    for index in range(phnum):
        kind, _flags, offset, vaddr, _paddr, filesz, _memsz, _align = struct.unpack_from(
            "<IIQQQQQQ", elf, phoff + index * phentsize
        )
        if kind == 1:
            result.append((vaddr, offset, filesz))
    return result


def read_va(elf: bytes, segments: list[tuple[int, int, int]], address: int, size: int) -> bytes:
    for vaddr, offset, filesz in segments:
        if vaddr <= address and address + size <= vaddr + filesz:
            return elf[offset + address - vaddr : offset + address - vaddr + size]
    raise ValueError(f"virtual address is not backed by a PT_LOAD segment: 0x{address:X}")


def relative_call_target(elf: bytes, segments: list[tuple[int, int, int]], address: int) -> int:
    instruction = read_va(elf, segments, address, 5)
    if instruction[0] != 0xE8:
        raise ValueError(f"expected direct rel32 call at 0x{address:X}")
    displacement = struct.unpack_from("<i", instruction, 1)[0]
    return address + 5 + displacement


def map_address(map_text: str, symbol: str) -> int:
    pattern = re.compile(rf"(?m)^\s*\S+\s+{re.escape(symbol)}\s+([0-9A-Fa-f]{{16}})\b")
    match = pattern.search(map_text)
    if not match:
        raise ValueError(f"map is missing {symbol}")
    return int(match.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-root", type=Path, default=DEFAULT_CANDIDATE)
    args = parser.parse_args()
    root = args.candidate_root.resolve()
    artifacts = root / "managed" / "nativeaot" / "artifacts"
    manifest_path = root / "candidate-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    map_path = Path(manifest["managedMap"])
    elf_path = Path(manifest["managedElf"])
    pe_path = Path(manifest["managedPe"])
    for candidate_path in (map_path, elf_path, pe_path):
        if not candidate_path.is_file():
            raise FileNotFoundError(candidate_path)
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    elf = elf_path.read_bytes()
    segments = load_segments(elf)
    checks = Checks()

    initializer = map_address(map_text, "InitializeModules")
    register = map_address(map_text, "RhpRegisterOsModule")
    allocator = map_address(map_text, "malloc")
    pal_virtual_alloc = map_address(map_text, "guidexos_nativeaot_pal_virtual_alloc")
    state_helper = map_address(map_text, "?initializeRuntimeState@?A0x1729e141@@YAXPEAE@Z")
    reverse_pinvoke = map_address(map_text, "RhpReversePInvoke")
    type_managers = map_address(map_text, "S_P_CoreLib_Internal_Runtime_CompilerHelpers_StartupCodeHelpers__CreateTypeManagers")
    modules_a = map_address(map_text, "__modules_a")
    modules_z = map_address(map_text, "__modules_z")
    checks.require(initializer == 0x1000AD660, "generated InitializeModules map address")
    checks.require(register == 0x1000F94D0, "actual RhpRegisterOsModule map address")
    checks.require(allocator == 0x1000F6640, "native malloc map address")
    checks.require(pal_virtual_alloc == 0x1000F55D0, "PAL virtual allocation provider map address")
    checks.require(state_helper == 0x1000F2F80, "shared helper map address")
    checks.require(type_managers == 0x1000AD820, "generated type-manager helper map address")
    checks.require((modules_z - modules_a) // 8 == 2 and (modules_z - modules_a) % 8 == 0,
                   "module header pointer/count describe two native entries")
    checks.require("Runtime.WorkstationGC:RuntimeInstance.cpp.obj" in map_text[map_text.index("RhpRegisterOsModule") : map_text.index("RhpRegisterOsModule") + 180],
                   "RhpRegisterOsModule RuntimeInstance.cpp object provenance")
    checks.require("guidexos_nativeaot_startup_imports.production.obj" in map_text,
                   "startup allocator production object provenance")

    checks.require(relative_call_target(elf, segments, state_helper + 0x211) == initializer,
                   "native call-site is direct REL32 to InitializeModules")
    checks.require(relative_call_target(elf, segments, initializer + 0x1B) == reverse_pinvoke,
                   "initializer prologue uses the linked reverse-P/Invoke transition")
    checks.require(relative_call_target(elf, segments, initializer + 0x23) == register,
                   "first import call-site resolves directly to RhpRegisterOsModule")
    checks.require(relative_call_target(elf, segments, register + 0x15) == allocator,
                   "RhpRegisterOsModule allocation call resolves to native malloc")
    checks.require(relative_call_target(elf, segments, allocator + 0x37) == pal_virtual_alloc,
                   "native malloc reaches the linked PAL virtual-allocation provider")
    checks.require(relative_call_target(elf, segments, initializer + 0x41) == type_managers,
                   "successful import path proceeds to generated type-manager creation")

    helper_prologue = read_va(elf, segments, state_helper, 4)
    checks.require(helper_prologue == bytes.fromhex("48 83 ec 68"),
                   "InitializeModules caller reserves 0x68 bytes and aligns pre-call RSP")
    call_arguments = read_va(elf, segments, state_helper + 0x211 - 24, 29)
    checks.require(call_arguments == bytes.fromhex(
        "c7 44 24 20 10 00 00 00 48 c1 fb 03 4d 8b cd 44 8b c3 49 8b d4 48 8b cd e8 ca a4 fb ff"
    ), "caller passes fifth count 16 on stack and module/classlib values in Windows x64 registers")

    entry = read_va(elf, segments, initializer, 0x40)
    checks.require(entry[:10] == bytes.fromhex("41 56 57 56 55 53 48 83 ec 40"),
                   "initializer saves five nonvolatile registers and reserves 0x40 bytes")
    checks.require(entry[10:26] == bytes.fromhex("48 8b d9 48 8b fa 41 8b f0 49 8b e9 48 8d 4c 24"),
                   "Windows x64 RCX/RDX/R8/R9 arguments are captured in preserved registers")
    checks.require(entry[26] == 0x30 and entry[27:31] == bytes.fromhex("e8 60 62 04"),
                   "reverse-P/Invoke entry transition is first generated prologue call")
    checks.require(read_va(elf, segments, initializer + 0x28, 8) == bytes.fromhex("44 8b b4 24 90 00 00 00"),
                   "fifth argument is loaded from entry RSP+0x90 after the 0x68-byte frame")
    checks.require(read_va(elf, segments, initializer + 0x30, 5) == bytes.fromhex("44 89 74 24 20"),
                   "fifth argument is placed in caller shadow-plus-argument slot")
    checks.require(read_va(elf, segments, initializer + 0x23, 5)[0] == 0xE8,
                   "Rhp call uses direct call instruction, not an IAT call")

    import_body = read_va(elf, segments, register, 0x61)
    checks.require(import_body[:22] == bytes.fromhex(
        "40 53 48 83 ec 20 48 8b d9 48 8d 15 e8 ea 0b 00 b9 10 00 00 00 e8"
    ), "registration entry preserves RBX, allocates shadow space, and requests a 16-byte entry")
    checks.require(import_body[0x1D:0x22] == bytes.fromhex("48 85 c0 74 39"),
                   "native allocation failure follows an explicit null-return branch")
    checks.require(bytes.fromhex("f0 49 0f b1 50 10") in import_body,
                   "module-list head publication uses locked compare-exchange")
    checks.require(import_body[0x52:0x5B] == bytes.fromhex("48 8b c3 48 83 c4 20 5b c3"),
                   "success path returns the original OS module handle and restores RBX")
    checks.require(import_body[0x5B:0x61] == bytes.fromhex("48 83 c4 20 5b c3"),
                   "allocation failure returns null and restores RBX")
    checks.require(import_body[0x1A:0x22] == bytes.fromhex("48 8b d0 48 85 c0 74 39"),
                   "allocation result is tested before entry initialization/publication")

    malloc_body = read_va(elf, segments, allocator, 0x80)
    checks.require(malloc_body[:4] == bytes.fromhex("48 83 ec 28"),
                   "native allocation provider reserves Windows x64 shadow space")
    checks.require(malloc_body[0x22:0x28] == bytes.fromhex("41 b9 04 00 00 00") and
                   malloc_body[0x2C:0x32] == bytes.fromhex("41 b8 00 30 00 00"),
                   "PAL call uses PAGE_READWRITE and MEM_RESERVE|MEM_COMMIT flags")
    checks.require(b"\x48\x89\x18" in malloc_body and b"\x48\x83\xc0\x10" in malloc_body,
                   "PAL allocation uses a 16-byte native malloc header")

    pe = pe_path.read_bytes()
    pe_offset = struct.unpack_from("<I", pe, 0x3C)[0]
    optional = pe_offset + 24
    checks.require(struct.unpack_from("<H", pe, optional)[0] == 0x20B, "candidate PE32+ format")
    import_size = struct.unpack_from("<I", pe, optional + 124)[0]
    checks.require(import_size == 0, "no PE import directory; linked runtime import is a direct call")

    repo_root = Path(__file__).resolve().parents[2]
    imports_source = (repo_root / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_startup_imports.cpp").read_text()
    pal_source = (repo_root / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.cpp").read_text()
    checks.require("void* __cdecl operator new(size_t size, const std::nothrow_t&) noexcept" in imports_source and
                   "guidexos_nativeaot_startup_malloc(size)" in imports_source,
                   "nothrow native operator new routes to startup allocator")
    checks.require("guidexos_nativeaot_pal_virtual_alloc(" in imports_source and
                   "GuidexosNativeAotMallocHeader" in imports_source,
                   "startup allocator uses PAL virtual allocation with its header")
    checks.require("guidexos_nativeaot_pal_virtual_alloc(void* preferred" in pal_source,
                   "PAL provider exposes the linked virtual-allocation implementation")

    print(f"R24_INITIALIZEMODULES_HOSTED_ASSERTIONS={checks.count} RESULT=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"R24_INITIALIZEMODULES_HOSTED_ASSERTIONS=FAILED ERROR={exc}", file=sys.stderr)
        raise

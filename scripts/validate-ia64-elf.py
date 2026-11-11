#!/usr/bin/env python3
"""Validate a linked IA-64 kernel ELF and its target-generated object files."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


EM_IA_64 = 50
ET_EXEC = 2
PT_LOAD = 1
PT_IA_64_UNWIND = 0x70000001
PF_X = 1
PF_W = 2
PF_R = 4
SHT_NOBITS = 8
SHT_REL = 9
SHT_RELA = 4
SHT_SYMTAB = 2
SHT_DYNSYM = 11
SHF_WRITE = 1
SHF_ALLOC = 2
SHF_EXECINSTR = 4
STACK_BYTES = 64 * 1024
IVT_BYTES = 32 * 1024
IVT_LONG_SLOT_BYTES = 0x400
IVT_SHORT_SLOT_BYTES = 0x100
IVT_VECTOR_NAMES = (
    "ivt_vhpt_translation", "ivt_itlb_miss", "ivt_dtlb_miss", "ivt_alt_itlb",
    "ivt_alt_dtlb", "ivt_nested_dtlb", "ivt_ikey_miss", "ivt_dkey_miss",
    "ivt_dirty_bit", "ivt_iaccess_bit", "ivt_daccess_bit", "ivt_break",
    "ivt_external_interrupt", "ivt_virtual_external_interrupt",
    "ivt_reserved_0e", "ivt_reserved_0f", "ivt_reserved_10", "ivt_reserved_11",
    "ivt_reserved_12", "ivt_reserved_13", "ivt_page_not_present",
    "ivt_key_permission", "ivt_ia_rights", "ivt_da_rights",
    "ivt_general_exception", "ivt_disabled_fp", "ivt_nat_consumption",
    "ivt_speculation", "ivt_reserved_1c", "ivt_debug", "ivt_unaligned_ref",
    "ivt_unsupported_data_ref", "ivt_fp_fault", "ivt_fp_trap", "ivt_lp_transfer",
    "ivt_taken_branch", "ivt_single_step", "ivt_virtualization",
    "ivt_reserved_26", "ivt_reserved_27", "ivt_reserved_28", "ivt_reserved_29",
    "ivt_reserved_2a", "ivt_reserved_2b", "ivt_reserved_2c", "ivt_ia32_exception",
    "ivt_ia32_intercept", "ivt_ia32_interrupt", "ivt_reserved_30", "ivt_reserved_31",
    "ivt_reserved_32", "ivt_reserved_33", "ivt_reserved_34", "ivt_reserved_35",
    "ivt_reserved_36", "ivt_reserved_37", "ivt_reserved_38", "ivt_reserved_39",
    "ivt_reserved_3a", "ivt_reserved_3b", "ivt_reserved_3c", "ivt_reserved_3d",
    "ivt_reserved_3e", "ivt_reserved_3f", "ivt_reserved_40", "ivt_reserved_41",
    "ivt_reserved_42", "ivt_reserved_43",
)


class InvalidElf(ValueError):
    pass


class Elf64:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 64 or self.data[:4] != b"\x7fELF":
            raise InvalidElf(f"{path}: missing ELF64 header")
        if self.data[4] != 2:
            raise InvalidElf(f"{path}: expected ELFCLASS64, got EI_CLASS={self.data[4]}")
        if self.data[5] != 1:
            raise InvalidElf(f"{path}: expected little-endian data, got EI_DATA={self.data[5]}")

        fields = struct.unpack_from("<HHIQQQIHHHHHH", self.data, 16)
        (self.type, self.machine, self.version, self.entry, self.phoff, self.shoff,
         self.flags, self.ehsize, self.phentsize, self.phnum, self.shentsize,
         self.shnum, self.shstrndx) = fields
        if (self.ehsize != 64 or (self.phnum and self.phentsize != 56) or
                (self.shnum and self.shentsize != 64)):
            raise InvalidElf(f"{path}: unexpected ELF64 structure sizes")
        self.programs = self._read_programs()
        self.sections = self._read_sections()
        self.symbols = self._read_symbols()

    def _slice(self, offset: int, size: int, label: str) -> bytes:
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise InvalidElf(f"{self.path}: {label} is outside the file")
        return self.data[offset:offset + size]

    def _read_programs(self) -> list[dict[str, int]]:
        records = []
        for index in range(self.phnum):
            off = self.phoff + index * self.phentsize
            values = struct.unpack("<IIQQQQQQ", self._slice(off, 56, "program header"))
            records.append(dict(zip(("type", "flags", "offset", "vaddr", "paddr",
                                     "filesz", "memsz", "align"), values)))
        return records

    def _read_sections(self) -> list[dict[str, int | str]]:
        raw = []
        for index in range(self.shnum):
            off = self.shoff + index * self.shentsize
            values = struct.unpack("<IIQQQQIIQQ", self._slice(off, 64, "section header"))
            raw.append(dict(zip(("nameoff", "type", "flags", "addr", "offset", "size",
                                 "link", "info", "align", "entsize"), values)))
            raw[-1]["index"] = index
        if not raw:
            raise InvalidElf(f"{self.path}: no section headers")
        if self.shstrndx >= len(raw):
            raise InvalidElf(f"{self.path}: invalid section-name string table index")
        names_section = raw[self.shstrndx]
        names = self._slice(names_section["offset"], names_section["size"], "section-name table")
        sections = []
        for section in raw:
            start = section["nameoff"]
            if start >= len(names):
                name = ""
            else:
                end = names.find(b"\0", start)
                name = names[start:end if end >= 0 else len(names)].decode("utf-8", "replace")
            section["name"] = name
            if section["type"] != SHT_NOBITS:
                self._slice(section["offset"], section["size"], f"section {name}")
            sections.append(section)
        return sections

    def _read_symbols(self) -> dict[str, dict[str, int]]:
        result: dict[str, dict[str, int]] = {}
        for section in self.sections:
            if section["type"] not in (SHT_SYMTAB, SHT_DYNSYM):
                continue
            entsize = section["entsize"] or 24
            if entsize != 24 or section["size"] % entsize:
                raise InvalidElf(f"{self.path}: malformed symbol table")
            if section["link"] >= len(self.sections):
                raise InvalidElf(f"{self.path}: invalid symbol string-table index")
            strings_section = self.sections[section["link"]]
            strings = self._slice(strings_section["offset"], strings_section["size"],
                                  "symbol string table")
            start = section["offset"]
            for off in range(start, start + section["size"], entsize):
                nameoff, info, other, shndx, value, size = struct.unpack(
                    "<IBBHQQ", self._slice(off, entsize, "symbol"))
                if nameoff >= len(strings):
                    continue
                end = strings.find(b"\0", nameoff)
                name = strings[nameoff:end if end >= 0 else len(strings)].decode("utf-8", "replace")
                if name:
                    result[name] = {"value": value, "size": size, "info": info,
                                     "other": other, "shndx": shndx}
        return result

    def section(self, name: str) -> dict[str, int | str] | None:
        return next((section for section in self.sections if section["name"] == name), None)


def check_artifact_manifest(path: Path, manifest_path: Path, profile: str) -> list[str]:
    errors: list[str] = []
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"ARTIFACT_MANIFEST=FAIL {exc}")
        return [f"ARTIFACT_MANIFEST: {exc}"]

    actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
    expected = {
        "schema": "guidexos-ia64-artifact-v1",
        "profile": profile,
        "elf": "bin/kernel.elf",
        "cpu_count_requirement": (
            "exactly-one-CPU-external-SKI-precondition-runtime-unverified"
            if profile == "early-exec" else "none"),
        "sha256": actual_hash,
        "cpu_state_checkpoint_schema": (
            "guidexos-ia64-cpu-state-checkpoints-v1" if profile == "early-exec" else "none"),
        "cpu_state_static_gates": "PASS" if profile == "early-exec" else "NOT_APPLICABLE",
        "fp_register_policy": {
            "runtime": "all-allocatable-registers-reserved",
            "compile-only": "abi-preserved-registers-fixed; scratch-registers-available",
            "early-exec": "abi-preserved-registers-fixed; scratch-registers-available",
        }.get(profile),
        "runtime_qualified": False,
        "scheduler_qualified": False,
        "interrupt_fp_preservation": False,
        "guest_execution_authorized": False,
    }
    if profile == "early-exec":
        expected.update({
            "boot_contract": "ski-physical-entry-single-cpu-no-preempt",
            "static_gates": "PASS",
            "nat_consumption_static_gates": "PASS",
            "nat_consumption_expected_faults": 1,
            "expected_exception_faults": 5,
        })
    else:
        expected.update({"boot_contract": "none", "static_gates": "NOT_APPLICABLE"})
    mismatches = [f"{key}={manifest.get(key)!r}; expected {value!r}"
                  for key, value in expected.items() if manifest.get(key) != value]
    expected_elf = manifest_path.parent / "bin" / "kernel.elf"
    if path.resolve() != expected_elf.resolve():
        mismatches.append(f"manifest location does not identify {path}")
    if mismatches:
        errors.extend(mismatches)
        print("ARTIFACT_MANIFEST=FAIL " + "; ".join(mismatches))
    else:
        print(f"ARTIFACT_MANIFEST=PASS profile={profile} sha256={actual_hash}")
    return errors


def check_kernel(path: Path, object_dir: Path | None, repo_root: Path,
                 profile: str = "runtime", manifest_path: Path | None = None) -> list[str]:
    elf = Elf64(path)
    errors: list[str] = []
    print(f"IA64_ELF_SHA256={hashlib.sha256(elf.data).hexdigest()}  {path}")

    def require(condition: bool, label: str, detail: str = "") -> None:
        status = "PASS" if condition else "FAIL"
        print(f"{label}={status}" + (f" {detail}" if detail else ""))
        if not condition:
            errors.append(f"{label}: {detail or 'validation failed'}")

    require(elf.machine == EM_IA_64, "ELF_MACHINE", f"{elf.machine} (expected EM_IA_64=50)")
    require(elf.type == ET_EXEC, "ELF_TYPE", f"{elf.type} (expected ET_EXEC=2)")
    require(elf.entry != 0, "ELF_ENTRY", f"0x{elf.entry:x}")

    entry_symbol = elf.symbols.get("_start")
    require(entry_symbol is not None and entry_symbol["value"] == elf.entry,
            "ENTRY_SYMBOL", f"_start={('0x%x' % entry_symbol['value']) if entry_symbol else 'missing'}")
    require("__gp" in elf.symbols and elf.symbols["__gp"]["value"] != 0,
            "GLOBAL_POINTER", f"__gp={('0x%x' % elf.symbols['__gp']['value']) if '__gp' in elf.symbols else 'missing'}")

    loads = [program for program in elf.programs if program["type"] == PT_LOAD]
    executable = [program for program in loads if program["flags"] & PF_X]
    writable = [program for program in loads if program["flags"] & PF_W]
    require(bool(loads), "LOAD_SEGMENTS", f"count={len(loads)}")
    require(bool(executable), "EXECUTABLE_SEGMENT", f"count={len(executable)}")
    require(bool(writable), "WRITABLE_SEGMENT", f"count={len(writable)}")
    wx = [program for program in loads if program["flags"] & PF_W and program["flags"] & PF_X]
    require(not wx, "W_X_SEPARATION", f"writable-executable LOAD count={len(wx)}")
    for index, program in enumerate(loads):
        perms = "".join(letter for bit, letter in ((PF_R, "R"), (PF_W, "W"), (PF_X, "X"))
                         if program["flags"] & bit)
        print(f"LOAD[{index}]=vaddr:0x{program['vaddr']:x} memsz:0x{program['memsz']:x} flags:{perms}")

    unmapped = []
    permission_mismatches = []
    for section in elf.sections:
        if not section["flags"] & SHF_ALLOC or section["size"] == 0:
            continue
        start = section["addr"]
        end = start + section["size"]
        covering = [program for program in loads
                    if program["vaddr"] <= start and end <= program["vaddr"] + program["memsz"]]
        if not covering:
            unmapped.append(section["name"])
            continue
        executable_section = bool(section["flags"] & SHF_EXECINSTR)
        writable_section = bool(section["flags"] & SHF_WRITE)
        if not any(bool(program["flags"] & PF_R) and
                   bool(program["flags"] & PF_X) == executable_section and
                   bool(program["flags"] & PF_W) == writable_section and
                   not (program["flags"] & PF_W and program["flags"] & PF_X)
                   for program in covering):
            permission_mismatches.append(section["name"])
    require(not unmapped, "ALLOC_SECTION_MAPPING", ",".join(unmapped) if unmapped else "all allocated sections mapped")
    require(not permission_mismatches, "SECTION_SEGMENT_PERMISSIONS",
            ",".join(permission_mismatches) if permission_mismatches else "section flags covered by compatible LOAD")

    unwind = elf.section(".IA_64.unwind")
    if unwind is not None and unwind["size"] > 0:
        unwind_segments = [program for program in elf.programs if program["type"] == PT_IA_64_UNWIND]
        unwind_covered = any(program["vaddr"] <= unwind["addr"] and
                             unwind["addr"] + unwind["size"] <= program["vaddr"] + program["memsz"] and
                             not program["flags"] & (PF_W | PF_X)
                             for program in unwind_segments)
        require(unwind_covered, "IA64_UNWIND_SEGMENT",
                f"segment count={len(unwind_segments)}; expected read-only PT_IA_64_UNWIND coverage")

    for name in (".boot", ".text"):
        section = elf.section(name)
        require(section is not None and section["size"] > 0 and
                bool(section["flags"] & SHF_EXECINSTR),
                f"SECTION_{name[1:].upper()}", "present, non-empty, executable")
        if section is not None:
            require(section["addr"] % 16 == 0 and section["align"] >= 16,
                    f"BUNDLE_ALIGNMENT_{name[1:].upper()}",
                    f"address=0x{section['addr']:x} align={section['align']}")

    ivt = elf.section(".ivt")
    ivt_ok = (ivt is not None and ivt["size"] == IVT_BYTES and
              ivt["addr"] % IVT_BYTES == 0 and ivt["align"] >= IVT_BYTES and
              bool(ivt["flags"] & SHF_EXECINSTR))
    ivt_detail = (f"address=0x{ivt['addr']:x} size=0x{ivt['size']:x} "
                  f"align={ivt['align']} (expected 0x8000 bytes, 0x8000 aligned executable section)"
                  if ivt is not None else "missing .ivt section")
    require(ivt_ok, "IVT_LAYOUT", ivt_detail)

    boot_source = repo_root / "kernel" / "arch" / "ia64" / "boot.s"
    source_slots: list[tuple[str, int, str, str]] = []
    try:
        in_ivt = False
        active_source = True
        early_if = False
        for line in boot_source.read_text(encoding="utf-8").splitlines():
            if line.strip().startswith('.section .ivt'):
                in_ivt = True
            elif line.strip().startswith('.section .boot'):
                in_ivt = False
            elif line.strip() == '.ifdef GXOS_IA64_EARLY_EXEC_PROFILE':
                early_if = True
                active_source = profile == "early-exec"
            elif early_if and line.strip() == '.else':
                active_source = profile != "early-exec"
            elif early_if and line.strip() == '.endif':
                early_if = False
                active_source = True
            elif in_ivt:
                macro = re.match(
                    r"\s*IVT_ENTRY_(HALT|BREAK|GENERAL_EXCEPTION|UNALIGNED_REFERENCE|NAT_CONSUMPTION)\s+([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
                    r"(0x[0-9a-fA-F]+)\s*,\s*(IVT_(?:LONG|SHORT)_SLOT_BYTES)\s*$",
                    line,
                )
                if macro and active_source:
                    source_slots.append((macro.group(2), int(macro.group(3), 16),
                                         macro.group(4), macro.group(1)))
    except OSError as exc:
        errors.append(f"IVT_VECTOR_LAYOUT: cannot read {boot_source}: {exc}")
        print(f"IVT_VECTOR_LAYOUT=FAIL cannot read {boot_source}")
    else:
        vector_errors = []
        if len(IVT_VECTOR_NAMES) != 68:
            vector_errors.append(f"validator vector map count={len(IVT_VECTOR_NAMES)} expected=68")
        if len(source_slots) != 68:
            vector_errors.append(f"source slot count={len(source_slots)} expected=68")
        equ_values = {}
        source_text = boot_source.read_text(encoding="utf-8")
        for name, value in re.findall(
                r"(?m)^\.equ\s+(IVT_(?:BYTES|LONG_SLOT_BYTES|SHORT_SLOT_BYTES))\s*,\s*(0x[0-9a-fA-F]+)\s*$",
                source_text):
            equ_values[name] = int(value, 16)
        if equ_values.get("IVT_BYTES") != IVT_BYTES:
            vector_errors.append(f"source IVT_BYTES={equ_values.get('IVT_BYTES')} expected=0x{IVT_BYTES:x}")
        if equ_values.get("IVT_LONG_SLOT_BYTES") != IVT_LONG_SLOT_BYTES:
            vector_errors.append("source long-slot size is not 0x400")
        if equ_values.get("IVT_SHORT_SLOT_BYTES") != IVT_SHORT_SLOT_BYTES:
            vector_errors.append("source short-slot size is not 0x100")

        base = elf.symbols.get("ia64_ivt")
        end = elf.symbols.get("ia64_ivt_end")
        ivt_index = ivt["index"] if ivt is not None else -1
        if base is None:
            vector_errors.append("missing ia64_ivt base symbol")
        elif (ivt is None or base["value"] != ivt["addr"] or base["shndx"] != ivt_index or
              base["size"] != IVT_BYTES):
            vector_errors.append("ia64_ivt symbol does not span the .ivt section")
        if end is None:
            vector_errors.append("missing ia64_ivt_end symbol")
        elif (base is None or end["value"] != base["value"] + IVT_BYTES or
              end["shndx"] != ivt_index):
            vector_errors.append("ia64_ivt_end is not exactly 0x8000 after the IVT base")

        for index, name in enumerate(IVT_VECTOR_NAMES):
            expected_offset = index * IVT_LONG_SLOT_BYTES if index < 20 else (
                0x5000 + (index - 20) * IVT_SHORT_SLOT_BYTES)
            expected_slot_name = "IVT_LONG_SLOT_BYTES" if index < 20 else "IVT_SHORT_SLOT_BYTES"
            expected_slot_size = IVT_LONG_SLOT_BYTES if index < 20 else IVT_SHORT_SLOT_BYTES
            if index < len(source_slots):
                source_name, source_offset, source_slot_name, source_kind = source_slots[index]
                expected_kind = (
                    "BREAK" if name == "ivt_break" and profile == "early-exec" else
                    "GENERAL_EXCEPTION" if name == "ivt_general_exception" and profile == "early-exec" else
                    "UNALIGNED_REFERENCE" if name == "ivt_unaligned_ref" and profile == "early-exec" else
                    "NAT_CONSUMPTION" if name == "ivt_nat_consumption" and profile == "early-exec" else
                    "HALT")
                if source_name != name or source_offset != expected_offset or \
                        source_slot_name != expected_slot_name or source_kind != expected_kind:
                    vector_errors.append(
                        f"source slot {index}: {source_kind} {source_name}@0x{source_offset:x}/"
                        f"{source_slot_name}; expected {expected_kind} {name}@0x{expected_offset:x}/"
                        f"{expected_slot_name}")
            symbol = elf.symbols.get(name)
            if symbol is None:
                vector_errors.append(f"missing vector symbol {name}")
                continue
            actual_offset = symbol["value"] - base["value"] if base is not None else -1
            if actual_offset != expected_offset:
                vector_errors.append(f"{name}: offset=0x{actual_offset:x} expected=0x{expected_offset:x}")
            end_symbol = elf.symbols.get(f"{name}_end")
            if end_symbol is None:
                vector_errors.append(f"missing handler boundary {name}_end")
            else:
                actual_end = symbol["value"] + symbol["size"]
                slot_end = (base["value"] + expected_offset + expected_slot_size
                            if base is not None else -1)
                if (symbol["size"] <= 0 or end_symbol["value"] != actual_end or
                        actual_end > slot_end or symbol["shndx"] != ivt_index or
                        end_symbol["shndx"] != ivt_index):
                    vector_errors.append(
                        f"{name}: symbol range 0x{symbol['value']:x}..0x{actual_end:x} "
                        f"does not fit its 0x{expected_slot_size:x}-byte slot")
        vector_detail = "; ".join(vector_errors[:8])
        if len(vector_errors) > 8:
            vector_detail += f"; ... {len(vector_errors) - 8} more"
        require(not vector_errors, "IVT_VECTOR_LAYOUT",
                vector_detail if vector_errors else
                "68 source slots, exact architectural offsets, and bounded handler symbols")

    profile_symbol = elf.symbols.get("__ia64_compile_only_profile")
    early_symbol = elf.symbols.get("__ia64_early_exec_profile")
    profile_value = 1 if profile == "compile-only" else 0
    early_value = 1 if profile == "early-exec" else 0
    profile_ok = (profile in ("runtime", "compile-only", "early-exec") and
                  profile_symbol is not None and profile_symbol["value"] == profile_value and
                  (profile != "early-exec" or
                   (early_symbol is not None and early_symbol["value"] == early_value)) and
                  (early_symbol is None or early_symbol["value"] == early_value))
    profile_detail = (f"profile={profile}; marker={profile_symbol['value']}"
                      if profile_symbol is not None else f"profile={profile}; marker missing")
    profile_detail += (f"; early_marker={early_symbol['value']}" if early_symbol is not None
                       else "; early_marker=missing")
    require(profile_ok, "IA64_BUILD_PROFILE", profile_detail)

    opd = elf.section(".opd")
    gp_symbol = elf.symbols.get("__gp")
    descriptor_errors = []
    descriptor_count = 0
    if opd is not None and opd["size"] > 0 and opd["size"] % 16 == 0:
        executable_ranges = [(section["addr"], section["addr"] + section["size"])
                             for section in elf.sections
                             if section["flags"] & SHF_ALLOC and section["flags"] & SHF_EXECINSTR]
        for offset in range(opd["offset"], opd["offset"] + opd["size"], 16):
            code_address, descriptor_gp = struct.unpack("<QQ", elf._slice(offset, 16, ".opd descriptor"))
            descriptor_count += 1
            if not any(start <= code_address < end for start, end in executable_ranges):
                descriptor_errors.append(f"descriptor {descriptor_count} code=0x{code_address:x} is not executable")
            if gp_symbol is None or descriptor_gp != gp_symbol["value"]:
                descriptor_errors.append(f"descriptor {descriptor_count} GP=0x{descriptor_gp:x} does not match __gp")
    opd_ok = (opd is not None and descriptor_count > 0 and opd["size"] % 16 == 0 and
              opd["addr"] % 16 == 0 and opd["align"] >= 16 and
              bool(opd["flags"] & SHF_ALLOC) and not bool(opd["flags"] & SHF_WRITE) and
              not descriptor_errors)
    descriptor_detail = (f"count={descriptor_count}; GP={('0x%x' % gp_symbol['value']) if gp_symbol else 'missing'}; "
                         f"errors={'; '.join(descriptor_errors[:4]) if descriptor_errors else 'none'}")
    require(opd_ok, "FUNCTION_DESCRIPTORS", descriptor_detail)

    bss = elf.section(".bss")
    require(bss is not None and bss["type"] == SHT_NOBITS and bool(bss["flags"] & SHF_WRITE),
            "BSS_SECTION", "writable SHT_NOBITS section")
    def check_region(label: str, start_name: str, end_name: str) -> None:
        start = elf.symbols.get(start_name)
        end = elf.symbols.get(end_name)
        good = (start is not None and end is not None and end["value"] > start["value"] and
                end["value"] - start["value"] == STACK_BYTES and start["value"] % 16 == 0 and
                end["value"] % 16 == 0 and bss is not None and
                bss["addr"] <= start["value"] < end["value"] <= bss["addr"] + bss["size"])
        detail = (f"0x{start['value']:x}..0x{end['value']:x} size=0x{end['value'] - start['value']:x}"
                  if start is not None and end is not None and end["value"] >= start["value"]
                  else f"missing {start_name}/{end_name}")
        require(good, label, detail + " (expected 64 KiB, 16-byte aligned inside .bss)")
    check_region("KERNEL_STACK", "stack_bottom", "stack_top")
    check_region("RSE_BACKING_STORE", "rse_backing_store_base", "rse_backing_store_top")
    stack_start = elf.symbols.get("stack_bottom", {}).get("value")
    stack_end = elf.symbols.get("stack_top", {}).get("value")
    rse_start = elf.symbols.get("rse_backing_store_base", {}).get("value")
    rse_end = elf.symbols.get("rse_backing_store_top", {}).get("value")
    require(None not in (stack_start, stack_end, rse_start, rse_end) and
            (stack_end <= rse_start or rse_end <= stack_start),
            "STACK_RSE_SEPARATION", "memory stack and RSE backing store must not overlap")

    relocation_sections = [section["name"] for section in elf.sections
                           if section["type"] in (SHT_REL, SHT_RELA) and section["size"] > 0]
    require(not relocation_sections, "REMAINING_RELOCATIONS",
            ",".join(relocation_sections) if relocation_sections else "none")
    undefined = sorted(name for name, symbol in elf.symbols.items()
                       if symbol["shndx"] == 0 and symbol["info"] >> 4 in (1, 2))
    require(not undefined, "UNRESOLVED_SYMBOLS", ",".join(undefined) if undefined else "none")

    if object_dir is not None:
        objects = sorted(object_dir.rglob("*.o"))
        object_errors = []
        for obj in objects:
            try:
                target = Elf64(obj)
                if target.machine != EM_IA_64:
                    object_errors.append(f"{obj}: e_machine={target.machine}")
            except (OSError, InvalidElf) as exc:
                object_errors.append(f"{obj}: {exc}")
        cpp_objects = list((object_dir / "core").glob("*.o")) if (object_dir / "core").is_dir() else []
        require(bool(objects), "TARGET_OBJECTS", f"count={len(objects)}")
        require(bool(cpp_objects), "CPP_TARGET_OBJECTS", f"core object count={len(cpp_objects)}")
        require(not object_errors, "OBJECT_ARCHITECTURE", "; ".join(object_errors) if object_errors else "all ELF64 little-endian EM_IA_64")

    if manifest_path is not None:
        manifest_errors = check_artifact_manifest(path, manifest_path, profile)
        if manifest_errors:
            errors.extend(f"ARTIFACT_MANIFEST: {error}" for error in manifest_errors)

    if errors:
        print(f"IA64_ELF_VALIDATION=FAIL count={len(errors)}")
        for error in errors:
            print(f"VALIDATION_ERROR={error}")
        return errors
    print("IA64_ELF_VALIDATION=PASS")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--object-dir", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--profile", choices=("runtime", "compile-only", "early-exec"), default="runtime")
    parser.add_argument("--manifest", type=Path,
                        help="verify profile and SHA-256 against an IA64 artifact manifest")
    args = parser.parse_args()
    try:
        errors = check_kernel(args.elf, args.object_dir, args.repo_root, args.profile,
                              args.manifest)
    except (OSError, InvalidElf, struct.error) as exc:
        print(f"IA64_ELF_VALIDATION=FAIL error={exc}", file=sys.stderr)
        return 1
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Static and runtime checks for the bounded stacked-register NaT/RSE test."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


class QualificationError(ValueError):
    pass


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_elf_validator(repo_root: Path) -> Any:
    import importlib.util

    source = repo_root / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_elf_validator", source)
    if spec is None or spec.loader is None:
        raise QualificationError(f"cannot load ELF validator: {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def function_disassembly(text: str, name: str, address: int, size: int) -> str:
    marker = f"<{name}>:"
    start = text.find(marker)
    if start < 0:
        raise QualificationError(f"disassembly is missing {name}")
    end_address = address + size
    selected = [text[start:start + len(marker)]]
    for line in text[start + len(marker):].splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+):", line)
        if match and address <= int(match.group(1), 16) < end_address:
            selected.append(line)
    return "\n".join(selected)


def validate_static(repo_root: Path, elf_path: Path, objdump: str,
                    probe: bool, manifest_path: Path | None = None,
                    expected_sha256: str | None = None,
                    kernel: bool = False) -> dict[str, int]:
    if probe and kernel:
        raise QualificationError("select either probe or kernel static validation")
    assembly_path = repo_root / "kernel" / "arch" / "ia64" / "stacked_nat_rse_test.s"
    source = assembly_path.read_text(encoding="utf-8")
    required_source = (
        "alloc r10 = ar.pfs, 0, 3, 0, 0",
        "mov r33 = r8",
        "tnat.nz p6, p7 = r33",
        "flushrs",
        "br.call.sptk.many b0 = ia64_stacked_nat_rse_pressure_child",
        "alloc r10 = ar.pfs, 0, 96, 0, 0",
        "mov r10 = ar.bspstore",
        "mov r10 = ar.rnat",
        "mov ar.pfs = r10",
        "br.ret.sptk.many b0",
    )
    missing = [fragment for fragment in required_source if fragment not in source]
    if missing:
        raise QualificationError("assembly source lacks required contract: " + ", ".join(missing))
    if re.search(r"\b(?:cover|loadrs)\b", source):
        raise QualificationError("test routine must not use cover or loadrs")
    parent_start = source.index("ia64_stacked_nat_rse_test:")
    parent = source[parent_start:source.index(".endp ia64_stacked_nat_rse_test", parent_start)]
    lazy_setup = parent.index("mov ar.rsc = r10")
    lazy_serialize = parent.index("srlz.d", lazy_setup)
    parent_bspstore_read = parent.index("mov r10 = ar.bspstore")
    if not lazy_setup < lazy_serialize < parent_bspstore_read:
        raise QualificationError("parent BSPSTORE read is not preceded by mode-0 setup and serialization")
    child_start = source.index("ia64_stacked_nat_rse_pressure_child:")
    child = source[child_start:]
    mode_guard = child.index("cmp.eq p6, p7 = r12, r0")
    mode_reject = child.index("stacked_nat_rse_child_no_observation")
    bspstore_read = child.index("mov r10 = ar.bspstore")
    rnat_read = child.index("mov r10 = ar.rnat")
    if not (mode_guard < mode_reject < bspstore_read < rnat_read):
        raise QualificationError("RSC.mode=0 guard does not dominate BSPSTORE/RNAT reads")
    if "mov ar.rnat" in source or "st8 [r12]" in source or "st8 [r13]" in source:
        raise QualificationError("test must not write RNAT or active backing-store memory")
    if child.index("mf") > child.index("ld8 r10 = [r12]"):
        raise QualificationError("backing-store sentinel is read without a preceding mf")
    if child.index("mf", child.index("stacked_nat_rse_check_memory_collection")) > \
            child.index("ld8 r12 = [r12]", child.index("stacked_nat_rse_check_memory_collection")):
        raise QualificationError("RNAT collection is read without a preceding mf")

    elf_validator = load_elf_validator(repo_root)
    elf = elf_validator.Elf64(elf_path)
    process = subprocess.run([objdump, "-dr", str(elf_path)], check=False,
                             capture_output=True, text=True)
    if process.returncode:
        raise QualificationError(f"objdump failed: {process.stderr.strip()}")
    parent_symbol = elf.symbols["ia64_stacked_nat_rse_test"]
    child_symbol = elf.symbols["ia64_stacked_nat_rse_pressure_child"]
    parent = function_disassembly(process.stdout, "ia64_stacked_nat_rse_test",
                                  parent_symbol["value"], parent_symbol["size"])
    child_dis = function_disassembly(process.stdout, "ia64_stacked_nat_rse_pressure_child",
                                     child_symbol["value"], child_symbol["size"])
    for fragment, text in (
            ("alloc r10=ar.pfs,3,3,0", parent),
            ("flushrs", parent),
            ("tnat.z p7,p6=r33", parent),
            ("br.call.sptk.many b0=", parent),
            ("br.ret.sptk.many b0", parent),
            ("alloc r10=ar.pfs,96,96,0", child_dis),
            ("mov.m r10=ar.bspstore", child_dis),
            ("mov.m r10=ar.rnat", child_dis),
            ("br.ret.sptk.many b0", child_dis)):
        if fragment not in text:
            raise QualificationError(f"target disassembly lacks {fragment!r}")

    if elf.machine != 50 or elf.type != 2 or elf.data[5] != 1:
        raise QualificationError("probe must be little-endian ELF64 EM_IA_64 ET_EXEC")
    required_symbols = (
        ("ia64_ivt", "ia64_ivt_end", "stacked_nat_rse_probe_run",
         "stacked_nat_rse_probe_spec_load_site", "ia64_stacked_nat_rse_test",
         "ia64_stacked_nat_rse_pressure_child", "ia64_stacked_nat_rse_record",
         "rse_backing_store_base", "rse_backing_store_top") if probe else
        ("_start", "kernel_main", "ia64_ivt", "ia64_stacked_nat_rse_boot_test",
         "ia64_stacked_nat_rse_test",
         "ia64_stacked_nat_rse_pressure_child", "ia64_stacked_nat_rse_record",
         "rse_backing_store_base", "rse_backing_store_top") if kernel else
        ("ia64_ivt", "ia64_stacked_nat_rse_test",
         "ia64_stacked_nat_rse_pressure_child", "ia64_stacked_nat_rse_record",
         "rse_backing_store_base", "rse_backing_store_top"))
    absent = [name for name in required_symbols if name not in elf.symbols]
    if absent:
        raise QualificationError("probe ELF lacks symbols: " + ", ".join(absent))
    symbols = {name: elf.symbols[name]["value"] for name in required_symbols}
    if probe:
        if symbols["ia64_ivt_end"] - symbols["ia64_ivt"] != 0x8000 or \
                symbols["ia64_ivt"] & 0x7fff:
            raise QualificationError("probe IVT size/alignment is invalid")
    if symbols["rse_backing_store_top"] - symbols["rse_backing_store_base"] != 65536:
        raise QualificationError("RSE backing store is not the bounded 64 KiB region")
    if elf.symbols["ia64_stacked_nat_rse_record"].get("size") != 320:
        raise QualificationError("stacked NaT/RSE record is not the fixed 320-byte schema")

    if kernel:
        boot_source = (repo_root / "kernel" / "arch" / "ia64" / "boot.s").read_text(
            encoding="utf-8")
        wrapper_path = repo_root / "kernel" / "arch" / "ia64" / "stacked_nat_rse_entry.s"
        wrapper_source = wrapper_path.read_text(encoding="utf-8")
        boot_test_call = "br.call.sptk.many b0 = ia64_stacked_nat_rse_boot_test"
        kernel_main_call = "br.call.sptk.many rp = kernel_main"
        bss_end = boot_source.index("(p7) br.cond.dptk clear_bss")
        test_call_at = boot_source.find(boot_test_call, bss_end)
        main_call_at = boot_source.find(kernel_main_call, bss_end)
        if test_call_at < 0 or main_call_at < 0 or not bss_end < test_call_at < main_call_at:
            raise QualificationError(
                "boot must run the stacked NaT/RSE wrapper after BSS clear and before kernel_main")
        profile_guard = boot_source.rfind(
            "#ifdef GXOS_IA64_EARLY_EXEC_PROFILE", bss_end, test_call_at)
        profile_end = boot_source.find("#endif", test_call_at, main_call_at)
        if profile_guard < bss_end or profile_end < test_call_at:
            raise QualificationError("boot qualification call is not scoped to early-exec")

        wrapper_required = (
            "movl r11 = 0x80000000000",
            "mov cr.ipsr = r10",
            "rfi",
            "ld8.s r8 = [r15]",
            "tnat.nz p6, p7 = r8",
            "br.call.sptk.many b0 = ia64_stacked_nat_rse_test",
            "movl r11 = ia64_stacked_nat_rse_record + STACKED_NAT_RSE_RESULT",
            "mov r8 = r0",
            "mov pr = r10, -1",
            "mov b0 = r10",
            "ld8 r13 = [r11]",
            "ld8 r12 = [r11]",
            "br.ret.sptk.many b0",
        )
        missing_wrapper = [item for item in wrapper_required if item not in wrapper_source]
        initial_check = wrapper_source.find("tnat.nz p6, p7 = r8")
        test_call = wrapper_source.find("br.call.sptk.many b0 = ia64_stacked_nat_rse_test")
        return_check = wrapper_source.find("tnat.nz p6, p7 = r8", initial_check + 1)
        clear_gr8 = wrapper_source.find("mov r8 = r0", return_check)
        if missing_wrapper or not (0 <= initial_check < test_call < return_check < clear_gr8):
            raise QualificationError(
                "boot wrapper must establish GR8.NaT, verify it around the RSE test, and clear GR8")

        wrapper_symbol = elf.symbols["ia64_stacked_nat_rse_boot_test"]
        wrapper_dis = function_disassembly(
            process.stdout, "ia64_stacked_nat_rse_boot_test",
            wrapper_symbol["value"], wrapper_symbol["size"])
        target_required = (
            "rfi", "ld8.s r8=[r15]", "tnat.z", "br.call.sptk.many b0=",
            "mov r8=r0", "mov pr=", "mov b0=", "br.ret.sptk.many b0")
        missing_target = [item for item in target_required if item not in wrapper_dis]
        target_test = wrapper_dis.find("ia64_stacked_nat_rse_test")
        target_clear = wrapper_dis.find("mov r8=r0", target_test)
        if missing_target or target_test < 0 or target_clear < target_test:
            raise QualificationError(
                "target wrapper disassembly does not preserve its GR8.NaT and boot ABI contract")

        boot_symbol = elf.symbols["_start"]
        boot_dis = function_disassembly(process.stdout, "_start",
                                        boot_symbol["value"], boot_symbol["size"])
        dis_test_call = boot_dis.find("ia64_stacked_nat_rse_boot_test")
        dis_main_call = boot_dis.find("kernel_main")
        if dis_test_call < 0 or dis_main_call < 0 or dis_test_call >= dis_main_call:
            raise QualificationError(
                "target _start disassembly must call the stacked wrapper before kernel_main")

    if probe:
        probe_entry = (repo_root / "tests/ia64_stacked_nat_rse/entry.s").read_text(
            encoding="utf-8")
        wrapper_call = probe_entry.find("br.call.sptk.many b0 = stacked_nat_rse_probe_run")
        cxx_call = probe_entry.find("br.call.sptk.many b0 = stacked_nat_rse_probe_main")
        if wrapper_call < 0 or cxx_call < 0 or wrapper_call >= cxx_call:
            raise QualificationError("probe must complete its RFI/RSE test before entering C++")
        if manifest_path is None or expected_sha256 is None:
            raise QualificationError("probe validation needs a manifest and expected hash")
        actual = digest(elf_path)
        if actual != expected_sha256:
            raise QualificationError(f"probe ELF hash mismatch: actual={actual}")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        expected = {
            "schema": "guidexos-ia64-stacked-nat-rse-probe-v1",
            "profile": "stacked-nat-rse-probe",
            "elf": elf_path.name,
            "sha256": actual,
            "guest_execution_authorized": "probe-only",
            "system_mode": "-forcesystem",
            "cpu_count": 1,
            "expected_faults": 0,
            "parent_frame_sof": 3,
            "child_frame_sof": 96,
            "rsc_mode_during_rse_observation": 0,
            "uses_active_backing_store_writes": False,
            "all_exception_vectors": "fail-stop",
        }
        mismatch = [f"{key}={manifest.get(key)!r}, expected {value!r}"
                    for key, value in expected.items() if manifest.get(key) != value]
        if mismatch:
            raise QualificationError("probe manifest contract mismatch: " + "; ".join(mismatch))
        if (manifest_path.parent / manifest["elf"]).resolve() != elf_path.resolve():
            raise QualificationError("probe manifest does not identify the selected ELF")
    return symbols


def parse_record_line(line: str) -> dict[str, int]:
    return {key: int(value, 16) for key, value in
            re.findall(r"([A-Za-z0-9_.]+)=0x([0-9A-Fa-f]+)", line)}


def validate_output(
        stdout: str,
        result_marker: str = "IA64_STACKED_NAT_RSE_PROBE_RESULT=PASS") -> dict[str, Any]:
    errors: list[str] = []
    record_lines = [line for line in stdout.splitlines()
                    if line.startswith("IA64_STACKED_NAT_RSE_RECORD ")]
    rnat_lines = [line for line in stdout.splitlines()
                  if line.startswith("IA64_STACKED_NAT_RSE_RNAT ")]
    validation_lines = [line for line in stdout.splitlines()
                        if line.startswith("IA64_STACKED_NAT_RSE_VALIDATION ")]
    if len(record_lines) != 1:
        errors.append(f"expected one record line, found {len(record_lines)}")
    if len(rnat_lines) != 1:
        errors.append(f"expected one RNAT line, found {len(rnat_lines)}")
    if len(validation_lines) != 1 or not re.fullmatch(
            r"IA64_STACKED_NAT_RSE_VALIDATION errors=0x0 negative_failures=0x0",
            validation_lines[0].strip() if validation_lines else ""):
        errors.append("guest validation or negative fixtures did not report zero errors")
    if stdout.count(result_marker) != 1 or \
            ("IA64_STACKED_NAT_RSE_PROBE_RESULT=FAIL" in stdout and "PROBE_RESULT=PASS" in result_marker) or \
            ("IA64_STACKED_NAT_RSE_KERNEL_RESULT=FAIL" in stdout and "KERNEL_RESULT=PASS" in result_marker):
        errors.append("stacked NaT/RSE qualification did not produce exactly one PASS result")

    fields: dict[str, int] = {}
    if len(record_lines) == 1:
        fields = parse_record_line(record_lines[0])
        required = {
            "magic": 0x53544E4154525345, "version": 1, "size": 320,
            "sequence": 1, "result": 0, "stage": 4,
            "static.NAT.before": 1, "stacked.NAT.before": 1,
            "stacked.NAT.after": 1, "static.NAT.after": 1,
            "ordinary.before": 0x4758534F534E4154,
            "ordinary.after": 0x4758534F534E4154,
            "frame.parent": 3, "frame.child": 96,
        }
        for name, value in required.items():
            if fields.get(name) != value:
                errors.append(f"record {name}={fields.get(name)!r}, expected {value:#x}")
        if fields.get("RSC.original", 0) & 3 != 3 or \
                fields.get("RSC.lazy", 1) & 3 != 0 or \
                fields.get("RSC.child", 1) & 3 != 0 or \
                fields.get("RSC.restored") != fields.get("RSC.original"):
            errors.append("RSC mode or complete restoration record is invalid")

        parent = fields.get("BSP.parent", 0)
        parent_store = fields.get("BSPSTORE.before", 0)
        child = fields.get("BSP.child", 0)
        child_store = fields.get("BSPSTORE.child", 0)
        if parent_store != parent or child <= parent or child_store < parent + 24:
            errors.append("record does not demonstrate a real three-register backing-store spill")
        if fields.get("BSP.return") != parent or fields.get("BSPSTORE.return") != parent_store:
            errors.append("child br.ret did not restore the parent BSP/BSPSTORE")
        if fields.get("spill.sentinel") != 0x4758534F534E4154:
            errors.append("spilled ordinary sentinel was not independently observed")

        slot = fields.get("NaT.slot.address", 0)
        index = fields.get("NaT.slot.index", 64)
        expected_index = (parent >> 3) & 0x3F
        expected_slot = parent + (16 if expected_index == 62 else 8)
        expected_collection = (expected_slot & ~0x1FF) | 0x1F8
        if index > 62 or expected_index > 62 or slot != expected_slot or \
                index != ((expected_slot >> 3) & 0x3F) or \
                fields.get("NaT.collection.address") != expected_collection:
            errors.append("recorded stacked NaT slot/RNAT collection address is inconsistent")
        if fields.get("RNAT.observed") != 1:
            errors.append("RNAT source did not report the GR33 NaT bit set")

    rnat_fields: dict[str, int] = {}
    if len(rnat_lines) == 1:
        rnat_fields = parse_record_line(rnat_lines[0])
        if len(fields) and fields.get("RNAT.source") == 1:
            valid_index = (fields.get("BSPSTORE.child", 0) >> 3) & 0x3F
            valid_mask = (1 << (min(valid_index, 62) + 1)) - 1
            if rnat_fields.get("valid_mask") != valid_mask or \
                    rnat_fields.get("value") is None or \
                    ((rnat_fields.get("value", 0) & valid_mask) >> fields["NaT.slot.index"]) & 1 != 1:
                errors.append("partial AR.RNAT observation used an invalid bit mask or missed GR33.NaT")
            if "collection_memory=not-required" not in rnat_lines[0]:
                errors.append("partial RNAT record misstates the backing-store collection status")
        elif len(fields) and fields.get("RNAT.source") == 2:
            if rnat_fields.get("valid_mask") != 0x7FFFFFFFFFFFFFFF or \
                    rnat_fields.get("collection") is None or \
                    ((rnat_fields.get("collection", 0) >> fields["NaT.slot.index"]) & 1) != 1 or \
                    "collection_memory=complete" not in rnat_lines[0]:
                errors.append("complete RNAT collection observation is incomplete or incorrect")
        else:
            errors.append("RNAT source is neither a valid partial register nor a complete collection")

    if errors:
        raise QualificationError("runtime output failed: " + "; ".join(errors))
    return {"record": fields, "rnat": rnat_fields, "errors": []}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--kernel", action="store_true",
                        help="validate the early-exec kernel integration site")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--expected-sha256")
    parser.add_argument("--stdout", type=Path)
    args = parser.parse_args()
    try:
        symbols = validate_static(args.repo_root.resolve(), args.elf.resolve(), args.objdump,
                                  args.probe, args.manifest, args.expected_sha256,
                                  args.kernel)
        if args.stdout:
            marker = ("IA64_STACKED_NAT_RSE_KERNEL_RESULT=PASS" if args.kernel else
                      "IA64_STACKED_NAT_RSE_PROBE_RESULT=PASS")
            runtime = validate_output(
                args.stdout.read_text(encoding="utf-8", errors="replace"), marker)
            print("STACKED_NAT_RSE_RUNTIME=PASS")
            print(f"STACKED_NAT_RSE_PARENT_BSP=0x{runtime['record']['BSP.parent']:x}")
            print(f"STACKED_NAT_RSE_CHILD_BSPSTORE=0x{runtime['record']['BSPSTORE.child']:x}")
            print(f"STACKED_NAT_RSE_RNAT_SOURCE={runtime['record']['RNAT.source']}")
        print("STACKED_NAT_RSE_STATIC=PASS")
        for name, value in symbols.items():
            print(f"STACKED_NAT_RSE_SYMBOL_{name}=0x{value:x}")
        return 0
    except (OSError, json.JSONDecodeError, QualificationError) as exc:
        print(f"[STACKED_NAT_RSE_BLOCKED] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

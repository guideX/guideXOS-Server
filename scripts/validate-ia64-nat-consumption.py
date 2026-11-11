#!/usr/bin/env python3
"""Static and record checks for the one-site IA-64 Register NaT Consumption contract."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

sys.dont_write_bytecode = True

VECTOR = 0x5600
REGISTER_NAT_CONSUMPTION = 43
ISR_LD8_SLOT0_SKI = 0x400000010
RECORD_VERSION = 1
RECORD_SIZE = 512
VALID = 0xBEEFC0DE4E415631
HANDLER_VALID = 0xBEEFC0DE4E414831
COMPLETION = 0xBEEFC0DE4E414331
RETURNED = 0xBEEFC0DE4E415231
POST_VALID = 0xBEEFC0DE4E415031
PSR_I = 1 << 14
PSR_IC = 1 << 13
PSR_AC = 1 << 3
PSR_ED = 1 << 43
PSR_RI = 3 << 41


class GateError(ValueError):
    pass


def load_elf_validator(repo_root: Path) -> Any:
    path = repo_root / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_nat_elf_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load ELF reader: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_disassembly(objdump: Path, elf: Path, symbol: str) -> str:
    result = subprocess.run([str(objdump), "-d", "--disassemble=" + symbol, str(elf)],
                            capture_output=True, text=True, check=False)
    if result.returncode:
        raise GateError(f"objdump failed for {symbol}: {result.stderr.strip()}")
    return result.stdout


def validate_record(record: dict[str, int], symbols: dict[str, int],
                    test_result: int = 0) -> list[str]:
    errors: list[str] = []

    def need(condition: bool, detail: str) -> None:
        if not condition:
            errors.append(detail)

    need(record.get("valid") == VALID and record.get("version") == RECORD_VERSION and
         record.get("size") == RECORD_SIZE and record.get("sequence") == 1,
         "record validity/version/size/sequence is invalid")
    need(record.get("vector") == VECTOR and record.get("subtype") == REGISTER_NAT_CONSUMPTION and
         record.get("site") == symbols.get("fault_site") and
         record.get("IIP") == symbols.get("fault_site") and record.get("slot") == 0 and
         ((record.get("IPSR", 0) >> 41) & 3) == 0 and
         record.get("spec.site") == symbols.get("spec_site"),
         "vector, Register NaT subtype, exact site, saved slot, or speculative site is wrong")

    need(record.get("IFS.valid") == 0 and ((record.get("IFS", 0) >> 63) & 1) == 0 and
         record.get("IFA.valid") == 0,
         "IFS.valid or IFA validity classification is wrong")
    need(record.get("ISR") == ISR_LD8_SLOT0_SKI and record.get("ISR.ed") == 0 and
         record.get("ISR.ei") == 0 and record.get("ISR.so") == 0 and
         record.get("ISR.ni") == 0 and record.get("ISR.ir") == 0 and
         record.get("ISR.rs") == 0 and record.get("ISR.sp") == 0 and
         record.get("ISR.na") == 0 and record.get("ISR.r") == 1 and
         record.get("ISR.w") == 0 and record.get("ISR.x") == 0,
         "ISR does not classify the authorized slot-0 ordinary read")

    saved = record.get("IPSR", 0)
    pre_psr = record.get("pre.PSR", 0)
    spec_psr = record.get("spec.PSR", 0)
    need(record.get("handler.PSR", 0) & (PSR_I | PSR_IC | PSR_AC) == 0 and
         record.get("handler.RSC") == 3 and record.get("PR") == 0x401 and
         record.get("handler.status") == HANDLER_VALID and
         symbols.get("rse_base", 0) <= record.get("handler.BSP", 0) < symbols.get("rse_top", 0) and
         record.get("pre.BSP") == record.get("handler.BSP") == record.get("post.BSP") and
         record.get("pre.RSC") == record.get("post.RSC") == 3 and pre_psr == 0x2000 and
         (saved & ~(PSR_ED | PSR_RI)) == (pre_psr & ~(PSR_ED | PSR_RI)) and
         (spec_psr & PSR_ED) != 0 and (spec_psr & PSR_RI) == 0 and
         (spec_psr & (PSR_I | PSR_IC)) == PSR_IC and
         (spec_psr & ~(PSR_ED | PSR_RI)) == (pre_psr & ~(PSR_ED | PSR_RI)) and
         (record.get("post.spec.PSR", 0) & PSR_ED) == 0,
         "PSR, RSC, BSP, predicate, or forced-restart state is invalid")

    need(record.get("source.GR") == 8 and record.get("destination.GR") == 9 and
         record.get("source.initial") == symbols.get("target") and
         record.get("source.NAT.before") == 1 and record.get("source.NAT.handler") == 1 and
         record.get("source.NAT.after") == 1 and record.get("source.NAT.cleared") == 0 and
         record.get("IIB0") == 0 and record.get("IIB1") == 0 and
         record.get("IIB0.available") == 0 and record.get("IIB1.available") == 0,
         "known GR/NaT state or bounded RSE observation is invalid")

    need(record.get("target") == symbols.get("continuation") and
         record.get("return.IPSR") == (saved & ~PSR_ED) and
         (record.get("return.IPSR", 0) & PSR_ED) == 0 and
         record.get("completion") == COMPLETION and record.get("count") == 1 and
         record.get("return") == RETURNED and record.get("test") == 0 and test_result == 0,
         "authorized return target/state or single-continuation markers are invalid")
    need(record.get("post.PSR") == 0x2000 and
         record.get("post.PSR", 0) & (PSR_I | PSR_IC | PSR_AC) == PSR_IC and
         record.get("post.IVA") == symbols.get("ivt") and
         record.get("post.valid") == POST_VALID,
         "immediate post-rfi PSR, IVA, or validity marker is invalid")
    need(record.get("pre.UNAT") == record.get("post.UNAT") and
         record.get("pre.GR") == record.get("post.GR"),
         "UNAT or non-NaTed live-register checksum changed")
    return errors


def parse_record_line(line: str) -> dict[str, int]:
    fields: dict[str, int] = {}
    pairs = re.findall(r"([^\s=]+)=((?:0x)?[0-9a-fA-F]+)", line)
    for key, value in pairs:
        if key in fields:
            raise GateError(f"duplicate machine record field: {key}")
        fields[key] = int(value, 16)
    return fields


def validate_output(stdout: str, symbols: dict[str, int], *, probe: bool = False
                    ) -> tuple[dict[str, int] | None, list[str]]:
    errors: list[str] = []
    record_lines = [line for line in stdout.splitlines()
                    if line.startswith("IA64_NAT_CONSUMPTION_RECORD")]
    result_prefix = ("IA64_NAT_CONSUMPTION_PROBE_RESULT=" if probe else
                     "IA64_NAT_CONSUMPTION_RESULT=")
    result_lines = [line for line in stdout.splitlines() if line.startswith(result_prefix)]
    pass_token = result_prefix + "PASS"
    fail_token = result_prefix + "FAIL"
    if len(record_lines) != 1:
        errors.append(f"expected one NaT Consumption frame, found {len(record_lines)}")
    if result_lines != [pass_token] or fail_token in stdout:
        errors.append("guest did not report exactly one successful NaT Consumption result")
    if len(record_lines) != 1:
        return None, errors
    try:
        record = parse_record_line(record_lines[0])
    except GateError as exc:
        return None, errors + [str(exc)]
    test_values = [parse_record_line(line).get("test") for line in stdout.splitlines()
                   if line.startswith("IA64_NAT_CONSUMPTION_VALIDATION")]
    test_result = test_values[0] if len(test_values) == 1 else -1
    if len(test_values) != 1:
        errors.append("guest did not emit exactly one machine validation result")
    errors.extend(validate_record(record, symbols, test_result=test_result))
    return record, errors


def validate_expected_fault_count(actual: int | None, expected: int) -> list[str]:
    return [] if actual == expected else [f"Ski fault count={actual!r}, expected {expected}"]


def validate_probe_manifest(elf_path: Path, manifest_path: Path, expected_sha256: str) -> None:
    import json
    actual = hashlib.sha256(elf_path.read_bytes()).hexdigest()
    if actual != expected_sha256:
        raise GateError(f"probe ELF SHA-256 mismatch: actual={actual}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"probe manifest is missing or invalid: {exc}") from exc
    if manifest.get("schema") != "guidexos-ia64-nat-consumption-probe-v1" or \
            manifest.get("profile") != "nat-consumption-probe" or \
            manifest.get("sha256") != actual or \
            manifest.get("guest_execution_authorized") != "probe-only" or \
            manifest.get("expected_faults") != 1 or manifest.get("expected_continuations") != 1 or \
            manifest.get("ivt_vector_offset") != VECTOR or \
            manifest.get("fault_subtype") != "register-nat-consumption" or \
            manifest.get("speculative_load_forced_by_psr_ed") is not True or \
            manifest.get("speculative_instruction") != "ld8.s r8=[r15]" or \
            manifest.get("non_speculative_consumer") != "ld8 r9=[r8]" or \
            manifest.get("source_register") != 8 or manifest.get("destination_register") != 9:
        raise GateError("probe manifest does not authorize only the bounded NaT probe")
    if elf_path.resolve() != (manifest_path.parent / manifest.get("elf", "")).resolve():
        raise GateError("probe manifest does not identify the selected ELF")


def validate_static(repo_root: Path, elf_path: Path, objdump: Path, *, probe: bool = False,
                    manifest_path: Path | None = None, expected_sha256: str | None = None
                    ) -> list[str]:
    failures: list[str] = []

    def check(ok: bool, label: str, detail: str) -> None:
        print(f"{label}={'PASS' if ok else 'FAIL'} {detail}")
        if not ok:
            failures.append(label)

    elf_module = load_elf_validator(repo_root)
    try:
        elf = elf_module.Elf64(elf_path)
    except (OSError, elf_module.InvalidElf) as exc:
        raise GateError(f"invalid IA-64 ELF: {exc}") from exc
    required = (
        "ia64_ivt", "ia64_ivt_end", "ivt_nat_consumption", "ia64_nat_consumption_entry",
        "ia64_nat_consumption_record", "ia64_nat_consumption_test",
        "ia64_nat_consumption_spec_load_site", "ia64_nat_consumption_fault_site",
        "ia64_nat_consumption_continuation", "ia64_nat_consumption_target",
        "rse_backing_store_base", "rse_backing_store_top",
    )
    missing = [name for name in required if name not in elf.symbols]
    ivt = elf.section(".ivt")
    geometry_ok = ivt is not None and ivt["size"] == 0x8000 and ivt["addr"] % 0x8000 == 0 and \
        ivt["align"] >= 0x8000 and elf.symbols["ia64_ivt"]["value"] == ivt["addr"] and \
        elf.symbols["ia64_ivt_end"]["value"] == ivt["addr"] + 0x8000 and \
        elf.symbols["ivt_nat_consumption"]["value"] == ivt["addr"] + VECTOR
    check(not missing and geometry_ok, "NAT_IVT_GEOMETRY",
          f"68-slot IVT, 32 KiB aligned/size, NaT vector at 0x5600; missing={missing}")
    if missing or not geometry_ok:
        failures.append("NAT_SYMBOLS")
        return failures

    sym = {name: elf.symbols[name]["value"] for name in required}
    symbol_map = {
        "ivt": sym["ia64_ivt"], "vector": sym["ivt_nat_consumption"],
        "entry": sym["ia64_nat_consumption_entry"], "record": sym["ia64_nat_consumption_record"],
        "spec_site": sym["ia64_nat_consumption_spec_load_site"],
        "fault_site": sym["ia64_nat_consumption_fault_site"],
        "continuation": sym["ia64_nat_consumption_continuation"],
        "target": sym["ia64_nat_consumption_target"],
        "rse_base": sym["rse_backing_store_base"], "rse_top": sym["rse_backing_store_top"],
    }
    check(elf.symbols["ia64_nat_consumption_record"].get("size") == RECORD_SIZE,
          "NAT_RECORD_SIZE", f"bounded version-1 record is {RECORD_SIZE} bytes")

    asm = (repo_root / "kernel/arch/ia64/nat_consumption_entry.s").read_text(encoding="utf-8")
    handler_start = asm.find("ia64_nat_consumption_entry:")
    handler_end = asm.find(".endp ia64_nat_consumption_entry", handler_start)
    handler = asm[handler_start:handler_end] if handler_start >= 0 and handler_end > handler_start else ""
    test_start = asm.find("ia64_nat_consumption_test:")
    test = asm[test_start:] if test_start >= 0 else ""
    test_code = re.sub(r"/\*.*?\*/|//.*", "", test, flags=re.S)
    handler_code = re.sub(r"/\*.*?\*/|//.*", "", handler, flags=re.S)
    uses_source = [line.strip() for line in handler_code.splitlines()
                   if re.search(r"\b(?:mov|cmp|add|adds|and|andcm|or|xor|ld\w*|st\w*)\b.*\br8\b", line)
                   and not re.search(r"\btnat(?:\.nz|\.z)\s+p\d+,\s*p\d+\s*=\s*r8\b", line)]
    check(bool(handler) and "tnat.nz p6, p7 = r8" in handler_code and not uses_source and
          not re.search(r"\b(?:alloc|br\.call|mov\s+ar\.(?:rnat|bspstore))\b", handler_code) and
          not re.search(r"\bmov\s+r\d+\s*=\s*cr\.iib[01]\b", handler_code) and
          "LOAD_REC r10, REC_PRE_BSP\n    LOAD_REC r11, REC_HANDLER_BSP\n    cmp.eq p6, p7 = r10, r11" in handler and
          "andcm r11 = r11, r10" in handler and "mov cr.ipsr = r11" in handler and
          "movl r10 = 0x400000010 ;;" in handler,
          "NAT_HANDLER_PRESERVATION", "assembly-only handler observes GR8 with tnat and leaves it untouched")
    ic_clear_before_restart = re.search(
        r"rsm\s+psr\.ic\s*;;\s*srlz\.d\s*;;\s*mov\s+cr\.iip\s*=\s*r11\s*;;\s*"
        r"mov\s+cr\.ipsr\s*=\s*r10\s*;;",
        test_code,
    ) is not None
    predicate_reset_before_consumer = re.search(
        r"tnat\.nz\s+p6,\s*p7\s*=\s*r8\s*;;.*?"
        r"STORE_REC\s+r10,\s*REC_NAT_BEFORE\s+"
        r"movl\s+r10\s*=\s*0x401\s*;;\s*mov\s+pr\s*=\s*r10,\s*-1\s*;;.*?"
        r"ld8\s+r9\s*=\s*\[r8\]",
        test_code,
        flags=re.S,
    ) is not None
    check("tnat.nz p6, p7 = r8" in test and "ld8.s r8 = [r15]" in test and
          ic_clear_before_restart and predicate_reset_before_consumer and
          "mov cr.iip = r11" in test and
          "mov cr.ipsr = r10" in test and "rfi ;;" in test and
          "ld8 r9 = [r8]" in test and "chk.s" not in test and
          not re.search(r"\b(?:ssm|rsm)\s+psr\.ed\b", asm),
          "NAT_STIMULUS_SOURCE", "saved IPSR.ed forces ld8.s as first restart; one non-spec consumer; no chk.s")
    abi_restore_ok = all(token in asm for token in (
        "ia64_nat_consumption_interrupted_r12", "ia64_nat_consumption_interrupted_r13",
        "st8 [r11] = r12", "st8 [r11] = r13", "ld8 r13 = [r11]", "ld8 r12 = [r11]"))
    check(abi_restore_ok, "NAT_ABI_PRESERVATION",
          "test leaves caller-saved GR8 available and restores the C++ stack/thread registers")

    if probe:
        ivt_source = (repo_root / "tests/ia64_nat_consumption/entry.s").read_text(encoding="utf-8")
        profile_ok = "ivt_nat_consumption:" in ivt_source and "0x5600" in ivt_source and \
            "rse_backing_store_base" in ivt_source
    else:
        boot = (repo_root / "kernel/arch/ia64/boot.s").read_text(encoding="utf-8")
        makefile = (repo_root / "kernel/Makefile").read_text(encoding="utf-8")
        main = (repo_root / "kernel/core/main.cpp").read_text(encoding="utf-8")
        profile_ok = re.search(
            r"\.ifdef GXOS_IA64_EARLY_EXEC_PROFILE\s+"
            r"IVT_ENTRY_NAT_CONSUMPTION ivt_nat_consumption,\s*0x5600.*?\.else\s+"
            r"IVT_ENTRY_HALT ivt_nat_consumption,\s*0x5600",
            boot, re.DOTALL) is not None and \
            "filter-out arch/ia64/nat_consumption.cpp" in makefile and \
            "filter-out arch/ia64/nat_consumption_entry.s" in makefile and \
            "run_nat_consumption_qualification()" in main and \
            main.find("run_unaligned_reference_qualification()") < \
            main.find("run_nat_consumption_qualification()")
    check(profile_ok, "NAT_EARLY_EXEC_PROFILE", "handler activation is limited to the isolated Ski test path")

    try:
        spec_dis = run_disassembly(objdump, elf_path, "ia64_nat_consumption_spec_load_site")
        fault_dis = run_disassembly(objdump, elf_path, "ia64_nat_consumption_fault_site")
        cont_dis = run_disassembly(objdump, elf_path, "ia64_nat_consumption_continuation")
        test_dis = run_disassembly(objdump, elf_path, "ia64_nat_consumption_test")
        handler_dis = run_disassembly(objdump, elf_path, "ia64_nat_consumption_entry")
        vector_dis = run_disassembly(objdump, elf_path, "ivt_nat_consumption")
        labels = all(f"<{name}>:" in text for name, text in (
            ("ia64_nat_consumption_spec_load_site", spec_dis),
            ("ia64_nat_consumption_fault_site", fault_dis),
            ("ia64_nat_consumption_continuation", cont_dis)))
        spec_first = bool(re.search(r"\[MII\]\s+ld8\.s r8=\[r15\]", spec_dis))
        consumer_first = bool(re.search(r"\[MII\]\s+ld8 r9=\[r8\]", fault_dis))
        continuation_first = bool(re.search(r"\[MII\]\s+mov r15=psr", cont_dis))
        ic_clear_disassembly = re.search(
            r"rsm\s+0x2000.*?srlz\.d.*?mov cr\.iip=r11.*?mov cr\.ipsr=r10.*?rfi",
            test_dis,
            flags=re.S,
        ) is not None
        return_sequence = ic_clear_disassembly and "mov cr.iip=r11" in test_dis and \
            "mov cr.ipsr=r10" in test_dis and "rfi" in test_dis and \
            spec_first and consumer_first
        no_unsafe_handler_ops = not re.search(r"\b(?:alloc|br\.call|mov\.ar\.rnat|mov\.ar\.bspstore)\b",
                                              handler_dis)
        vector_routes = "ia64_nat_consumption_entry" in vector_dis
        check(labels and spec_first and consumer_first and continuation_first and
              return_sequence and no_unsafe_handler_ops and vector_routes,
              "NAT_TARGET_DISASSEMBLY",
              "actual ELF has forced-restart ld8.s, exact slot-0 ld8 consumer, immediate PSR capture, and vector route")
    except (OSError, GateError) as exc:
        check(False, "NAT_TARGET_DISASSEMBLY", str(exc))

    if manifest_path is not None:
        if probe:
            try:
                validate_probe_manifest(elf_path, manifest_path, expected_sha256 or "")
                manifest_ok = True
            except (GateError, OSError) as exc:
                manifest_ok = False
                manifest_error = str(exc)
            check(manifest_ok, "NAT_PROBE_MANIFEST", manifest_error if not manifest_ok else
                  "probe-only identity and authorization match the isolated ELF")
        else:
            import json
            try:
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                actual = hashlib.sha256(elf_path.read_bytes()).hexdigest()
                manifest_ok = manifest.get("sha256") == actual and \
                    manifest.get("nat_consumption_static_gates") == "PASS" and \
                    manifest.get("nat_consumption_expected_faults") == 1 and \
                    manifest.get("break_exception_expected_faults") == 2 and \
                    manifest.get("general_exception_expected_faults") == 1 and \
                    manifest.get("unaligned_reference_expected_faults") == 1 and \
                    manifest.get("expected_exception_faults") == 5 and \
                    manifest.get("guest_execution_authorized") is False
                if expected_sha256:
                    manifest_ok = manifest_ok and actual == expected_sha256
            except (OSError, ValueError):
                manifest_ok = False
            check(manifest_ok, "NAT_KERNEL_MANIFEST",
                  "candidate identity, static gates, five-fault contract, and unauthorized state")
    if failures:
        print(f"IA64_NAT_CONSUMPTION_STATIC_GATES=FAIL count={len(failures)}")
    else:
        print("IA64_NAT_CONSUMPTION_STATIC_GATES=PASS")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--expected-sha256")
    args = parser.parse_args()
    try:
        failures = validate_static(args.repo_root, args.elf, args.objdump, probe=args.probe,
                                   manifest_path=args.manifest, expected_sha256=args.expected_sha256)
    except (OSError, GateError) as exc:
        print(f"IA64_NAT_CONSUMPTION_STATIC_GATES=FAIL error={exc}", file=sys.stderr)
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

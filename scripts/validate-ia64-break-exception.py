#!/usr/bin/env python3
"""Static source and target-disassembly gates for the bounded IA-64 Break test."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import importlib.util

_spec = importlib.util.spec_from_file_location(
    "ia64_elf_validator", Path(__file__).resolve().parent / "validate-ia64-elf.py")
if _spec is None or _spec.loader is None:
    raise SystemExit("cannot load IA-64 ELF reader")
_elf_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_elf_module)


class GateError(RuntimeError):
    pass


def check(condition: bool, label: str, detail: str, errors: list[str]) -> None:
    if not condition:
        errors.append(f"{label}: {detail}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--probe", action="store_true")
    args = parser.parse_args()
    repo = args.repo_root.resolve()
    errors: list[str] = []

    try:
        boot = (repo / "kernel/arch/ia64/boot.s").read_text(encoding="utf-8")
        source = (repo / "kernel/arch/ia64/break_exception_entry.s").read_text(encoding="utf-8")
        header = (repo / "kernel/arch/ia64/include/arch/break_exception.h").read_text(encoding="utf-8")
        makefile = (repo / "kernel/Makefile").read_text(encoding="utf-8")
        main_cpp = (repo / "kernel/core/main.cpp").read_text(encoding="utf-8")
        probe_entry = (repo / "tests/ia64_break_exception/entry.s").read_text(encoding="utf-8")
    except OSError as exc:
        print(f"IA64_BREAK_EXCEPTION_STATIC_GATES=FAIL error={exc}", file=sys.stderr)
        return 1

    handler_start = source.find("ia64_break_exception_entry:")
    handler_end = source.find(".endp ia64_break_exception_entry", handler_start)
    fail_start = source.find("break_exception_fail:", handler_end)
    fail_end = source.find(".size ia64_break_exception_entry", fail_start)
    handler = source[handler_start:handler_end] if handler_start >= 0 and handler_end > handler_start else ""
    fail_path = source[fail_start:fail_end] if fail_start >= 0 and fail_end > fail_start else ""
    handler_code = handler + fail_path
    forbidden_handler = re.findall(
        r"\b(?:br\.call|alloc|cover|flushrs|loadrs|st8\.spill|ld8\.fill|ar\.bspstore)\b",
        handler_code)
    check(bool(handler) and bool(fail_path), "HANDLER_BOUNDS", "entry and fail-stop body must exist", errors)
    check(not forbidden_handler, "HANDLER_NO_CALL_OR_RSE",
          f"forbidden handler operations: {forbidden_handler}", errors)
    for required in ("cr.iip", "cr.ipsr", "cr.ifs", "cr.iim", "cr.isr", "ar.rsc", "ar.bsp",
                     "mov pr = r16", "rfi", "BREAK_IMMEDIATE_SLOT1", "BREAK_IMMEDIATE_SLOT2"):
        check(required in handler_code, "HANDLER_CONTRACT", f"missing {required}", errors)
    if not args.probe:
        check("br.sptk.many ia64_break_exception_entry" in boot and
              ".ifdef GXOS_IA64_EARLY_EXEC_PROFILE" in boot and
              re.search(r"(?m)^\.else\s+IVT_ENTRY_HALT\s+ivt_break\b", boot) is not None,
              "IVT_PROFILE_SCOPE", "only early-exec selects the Break handler", errors)
    check("arch/ia64/break_exception.cpp" in makefile and
          "arch/ia64/break_exception_entry.s" in makefile and
          "ifneq ($(IA64_FP_PROFILE),early-exec)" in makefile,
          "EARLY_EXEC_ONLY_LINK", "break code must be excluded from other IA-64 profiles", errors)
    check("run_break_exception_qualification()" in main_cpp and
          "#if defined(GXOS_IA64_EARLY_EXEC_PROFILE)" in main_cpp,
          "EARLY_EXEC_TEST_REACHABILITY", "test must be reachable only from early-exec main", errors)
    check("0x6a17" in source.lower() and "0x6a18" in source.lower() and
          "break.i 0x80000" not in handler_code.lower(),
          "SKI_IMMEDIATE_CONFLICT", "handler/test immediates must be 0x6A17 and 0x6A18", errors)
    check("saved_predicates" in header and "BREAK_EXCEPTION_SCHEMA_VERSION" in header and
          "sizeof(BreakExceptionRecord) == 128" in header,
          "VERSIONED_RECORD", "versioned fixed-size record fields are required", errors)
    check("ssm psr.ic" in source and "rsm psr.ic" in source and
          re.search(r"\bssm\s+psr\.i\b", source.lower()) is None,
          "INTERRUPTS_REMAIN_MASKED", "test may toggle collection but must not enable interrupts", errors)
    check("bsw.1" in source and "bsw.0" in source,
          "BANKED_TEST_STATE", "test must explicitly select and restore bank 1", errors)
    check(re.search(r"rsm\s+psr\.ic\s*;;\s*bsw\.0\s*;;\s*ssm\s+psr\.ic\s*;;\s*"
                    r"srlz\.i\s*;;", source.lower()) is not None,
          "TEST_RESTORES_COLLECTION", "test cleanup must restore the caller's exception-collection state", errors)

    if args.probe:
        probe_slots = re.findall(r"(?m)^IVT_ENTRY_(HALT|BREAK)\s+([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
                                 r"(0x[0-9a-fA-F]+)\s*,\s*IVT_(?:LONG|SHORT)_SLOT_BYTES\s*$",
                                 probe_entry)
        check(len(probe_slots) == 68, "PROBE_IVT_COUNT",
              f"diagnostic source contains {len(probe_slots)} vector slots, expected 68", errors)
        probe_breaks = [slot for slot in probe_slots if slot[0] == "BREAK"]
        check(probe_breaks == [("BREAK", "ivt_break", "0x2c00")],
              "PROBE_ONLY_BREAK_VECTOR", f"diagnostic Break-vector entries={probe_breaks}", errors)
        check(all(kind == "HALT" for kind, name, _ in probe_slots if name != "ivt_break"),
              "PROBE_OTHER_VECTORS", "all non-Break vectors must remain fail-stop", errors)
        check('"guest_execution_authorized": "probe-only"' in
              (repo / "tests/ia64_break_exception/build.sh").read_text(encoding="utf-8"),
              "PROBE_AUTHORIZATION_SCOPE", "manifest must be probe-only", errors)

    try:
        result = subprocess.run([args.objdump, "-dr", str(args.elf)], capture_output=True,
                                text=True, check=False)
    except OSError as exc:
        errors.append(f"OBJDUMP: cannot execute {args.objdump}: {exc}")
        result = None
    disassembly = result.stdout if result is not None else ""
    if result is not None and result.returncode:
        errors.append(f"OBJDUMP: {result.stderr.strip()}")

    required_symbols = ("ia64_ivt", "ia64_ivt_end", "ivt_break",
                        "ia64_break_exception_entry", "ia64_break_exception_test",
                        "ia64_break_slot1_bundle", "ia64_break_slot2_bundle")
    try:
        elf = _elf_module.Elf64(args.elf)
        missing = [name for name in required_symbols if name not in elf.symbols]
        check(not missing, "TARGET_SYMBOLS", f"missing={missing}", errors)
        if not missing:
            base = elf.symbols["ia64_ivt"]["value"]
            end = elf.symbols["ia64_ivt_end"]["value"]
            brk = elf.symbols["ivt_break"]["value"]
            check(end - base == 0x8000 and brk == base + 0x2c00,
                  "TARGET_IVT_VECTOR", f"base=0x{base:x} end=0x{end:x} break=0x{brk:x}", errors)
            slot1 = elf.symbols["ia64_break_slot1_bundle"]["value"]
            slot2 = elf.symbols["ia64_break_slot2_bundle"]["value"]
            check(slot1 % 16 == 0 and slot2 % 16 == 0,
                  "TEST_BUNDLE_ALIGNMENT", f"slot1=0x{slot1:x} slot2=0x{slot2:x}", errors)
    except (OSError, _elf_module.InvalidElf) as exc:
        errors.append(f"TARGET_ELF: {exc}")
        slot1 = slot2 = 0

    instruction_rows = []
    for line in disassembly.splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+):.*?\b(break\.i|adds)\b(.*)$", line)
        if match:
            instruction_rows.append((int(match.group(1), 16), match.group(2).lower(), match.group(0).lower()))
    break_rows = [(addr, text) for addr, mnemonic, text in instruction_rows
                  if mnemonic == "break.i" and re.search(r"0x6a1[78]\b", text)]
    check(len(break_rows) == 2, "TARGET_BREAK_COUNT",
          f"found {len(break_rows)} candidate break instructions", errors)
    expected_breaks = [(slot1 + 6, "0x6a17"), (slot2 + 12, "0x6a18")]
    for (addr, text), (expected_addr, immediate) in zip(sorted(break_rows), expected_breaks):
        check(addr == expected_addr and immediate in text,
              "TARGET_BREAK_SLOT", f"instruction at 0x{addr:x} text={text.strip()}, expected "
              f"0x{expected_addr:x} {immediate}", errors)
    add_rows = [(addr, text) for addr, mnemonic, text in instruction_rows if mnemonic == "adds"]
    expected_adds = (
        (slot1 + 12, "p10", "r8=1,r8"),
        (slot1 + 22, "p11", "r8=2,r8"),
        (slot2 + 22, "p10", "r8=1,r8"),
        (slot2 + 38, "p11", "r8=2,r8"),
    )
    for expected_addr, expected_p, expression in expected_adds:
        check(any(addr == expected_addr and expected_p in text and expression in text
                  for addr, text in add_rows), "TARGET_RETURN_CONTINUATION",
              f"missing {expected_p} {expression} at 0x{expected_addr:x}", errors)
    check(any("rfi" in line.lower() for line in disassembly.splitlines()),
          "TARGET_RFI", "linked handler lacks rfi", errors)

    if errors:
        for error in errors:
            print(f"FAIL {error}")
        print(f"IA64_BREAK_EXCEPTION_STATIC_GATES=FAIL count={len(errors)}")
        return 1
    print("IA64_BREAK_EXCEPTION_STATIC_GATES=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, GateError) as exc:
        print(f"IA64_BREAK_EXCEPTION_STATIC_GATES=FAIL error={exc}", file=sys.stderr)
        raise SystemExit(1)

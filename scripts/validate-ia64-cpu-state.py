#!/usr/bin/env python3
"""Validate early IA-64 checkpoint source, target code, ELF storage, and guest records."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


CHECKPOINT_BYTES = 96
FIELD_BITS = {
    "PSR": 0x001,
    "RSC": 0x002,
    "BSP": 0x004,
    "BSPSTORE": 0x008,
    "RNAT": 0x010,
    "FPSR": 0x020,
    "IVA": 0x040,
    "PFS": 0x080,
    "UNAT": 0x100,
}
STAGE_MASKS = {"A": (1, 0x027), "B": (2, 0x02F), "C": (3, 0x1E7), "D": (4, 0x1E7)}
EXPECTED_FPSR = 0x0009804C0270033F
PF_X = 1
PF_W = 2
SHF_WRITE = 1
SHT_PROGBITS = 1


def load_elf_validator(repo_root: Path) -> Any:
    path = repo_root / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_cpu_state_elf_validator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load IA-64 ELF parser: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_sources(repo_root: Path) -> dict[str, str]:
    paths = {
        "boot": repo_root / "kernel/arch/ia64/boot.s",
        "asm": repo_root / "kernel/arch/ia64/cpu_state_checkpoint_readers.s",
        "header": repo_root / "kernel/arch/ia64/include/arch/cpu_state_checkpoint.h",
        "reporter": repo_root / "kernel/arch/ia64/cpu_state_checkpoint.cpp",
        "arch": repo_root / "kernel/arch/ia64/arch.cpp",
        "console": repo_root / "kernel/arch/ia64/ski_console.cpp",
        "main": repo_root / "kernel/core/main.cpp",
    }
    return {key: path.read_text(encoding="utf-8") for key, path in paths.items()}


def validate_checkpoint_sources(repo_root: Path,
                               source_override: dict[str, str] | None = None) -> list[str]:
    """Source-only gates also serve as negative-testable policy checks."""
    source = source_override if source_override is not None else read_sources(repo_root)
    boot = source["boot"]
    asm = source["asm"]
    header = source["header"]
    reporter = source["reporter"]
    arch = source["arch"]
    console = source["console"]
    main = source["main"]
    errors: list[str] = []

    if not ("struct alignas(16) CpuStateCheckpoint" in header and
            "sizeof(CpuStateCheckpoint) == 96" in header and
            all(f"offsetof(CpuStateCheckpoint, {field}) == {offset}" in header for field, offset in (
                ("stage", 0), ("valid_fields", 8), ("psr", 16), ("rsc", 24), ("bsp", 32),
                ("bspstore", 40), ("rnat", 48), ("fpsr", 56), ("iva", 64),
                ("pfs", 72), ("unat", 80)))):
        errors.append("checkpoint layout is not the fixed aligned 96-byte ABI")

    psr_i_start = reporter.find('ski_console::puts(" PSR.i=")')
    psr_ic_start = reporter.find('ski_console::puts(" PSR.ic=")', psr_i_start)
    if not (0 <= psr_i_start < psr_ic_start and
            "1ull << 14" in reporter[psr_i_start:psr_ic_start] and
            "1ull << 13" in reporter[psr_ic_start:]):
        errors.append("PSR.i/PSR.ic output does not use architectural bits 14/13")

    a_start = boot.find("/* ---- Checkpoint A:")
    bstore_init = boot.find("movl r14 = rse_backing_store_base")
    a_block = boot[a_start:bstore_init] if 0 <= a_start < bstore_init else ""
    if not a_block or not all(token in a_block for token in (
            "mov r15 = psr", "mov r15 = ar.rsc", "mov r15 = ar.bsp", "mov r15 = ar.fpsr")):
        errors.append("checkpoint A does not capture the declared safe startup registers")
    if re.search(r"\bar\.(?:bspstore|rnat)\b", a_block):
        errors.append("checkpoint A reads BSPSTORE or RNAT before enforcing lazy mode")

    bstore_write = boot.find("mov ar.bspstore = r14", bstore_init)
    eager = boot.find("mov ar.rsc = 3", bstore_write)
    b_block = boot[bstore_write:eager] if 0 <= bstore_write < eager else ""
    if not b_block or "Checkpoint B:" not in b_block or "mov r16 = ar.bspstore" not in b_block:
        errors.append("checkpoint B does not read BSPSTORE after backing-store initialization")
    if re.search(r"\bar\.rnat\b|\bbr\.call\b|break\.i|guidexos_ski_ssc", b_block):
        errors.append("checkpoint B performs an undefined RNAT read or a call/SSC in the RSE window")
    lazy_mode = boot.find("mov ar.rsc = 0", bstore_init)
    loadrs = boot.find("loadrs", bstore_init)
    if not (0 <= lazy_mode < loadrs < bstore_write < eager):
        errors.append("RSE transition no longer orders enforced-lazy, loadrs, BSPSTORE, and eager mode")

    for name in ("ia64_capture_checkpoint_c", "ia64_capture_checkpoint_d"):
        begin = asm.find(name + ":")
        end = asm.find(".endp " + name, begin)
        body = asm[begin:end] if 0 <= begin < end else ""
        if not body:
            errors.append(f"missing assembly leaf {name}")
            continue
        if re.search(r"\bar\.(?:bspstore|rnat)\b", body):
            errors.append(f"{name} accesses BSPSTORE/RNAT without an enforced-lazy precondition")
        if re.search(r"\b(?:alloc|br\.call)\b|mov\s+ar\.(?:pfs|unat)\s*=", body):
            errors.append(f"{name} changes the active RSE frame or ABI-preserved application state")
        for register in ("psr", "ar.rsc", "ar.bsp", "ar.fpsr", "cr.iva", "ar.pfs", "ar.unat"):
            if not re.search(r"\bmov\s+r\d+\s*=\s*" + re.escape(register) + r"\b", body):
                errors.append(f"{name} is missing its audited {register} read")

    arch_init = re.search(r"void init\(\)\s*\{([\s\S]*?)\n\}", arch)
    c_capture = main.find("kernel::arch::ia64::ia64_capture_checkpoint_c();")
    arch_call = main.rfind("kernel::arch::init();", 0, c_capture) if c_capture >= 0 else -1
    if not arch_init or "ssm psr.ic;;" not in arch_init.group(1) or \
            re.search(r"\bssm\s+psr\.i\b", arch_init.group(1)):
        errors.append("architecture init must enable collection while leaving external interrupts masked")
    if not (0 <= arch_call < c_capture and "report_startup_cpu_state_checkpoints();" in main and
            c_capture < main.find("report_startup_cpu_state_checkpoints();")):
        errors.append("checkpoint C must follow architecture initialization and report after capture")
    stop_start = console.find("void stop()")
    stop_end = console.find("\n}", stop_start)
    stop_body = console[stop_start:stop_end] if stop_start >= 0 and stop_end > stop_start else ""
    if not ("ia64_capture_checkpoint_d();" in stop_body and
            "report_preexit_cpu_state_checkpoint();" in stop_body and
            "ssc(SSC_EXIT);" in stop_body and
            stop_body.index("ia64_capture_checkpoint_d();") <
            stop_body.index("report_preexit_cpu_state_checkpoint();") < stop_body.index("ssc(SSC_EXIT);")):
        errors.append("pre-exit checkpoint is not captured and reported before intentional SSC exit")

    if "mov r15 = r32 ;;" not in (repo_root / "kernel/arch/ia64/ski_ssc.s").read_text(encoding="utf-8"):
        errors.append("corrected IA64-3R3 SSC bridge is missing")
    return errors


def parse_checkpoint_record(line: str) -> dict[str, Any]:
    """Parse one strict guest record and enforce availability-mask consistency."""
    tokens = line.strip().split()
    if not tokens or not tokens[0].startswith("IA64_CHECKPOINT_"):
        raise ValueError("missing checkpoint record prefix")
    name = tokens[0][len("IA64_CHECKPOINT_"):]
    if name not in STAGE_MASKS:
        raise ValueError(f"unknown checkpoint name {name!r}")
    values: dict[str, str] = {"name": name}
    for token in tokens[1:]:
        if token.count("=") != 1:
            raise ValueError(f"malformed checkpoint token {token!r}")
        key, value = token.split("=", 1)
        if key in values:
            raise ValueError(f"duplicate checkpoint field {key}")
        values[key] = value
    expected_keys = {"name", "stage", "valid", "PSR", "PSR.i", "PSR.ic", "RSC", "RSC.mode",
                     "BSP", "BSPSTORE", "RNAT", "FPSR", "IVA", "PFS", "UNAT",
                     "IVT_EXPECTED", "RSE_RANGE"}
    if set(values) != expected_keys:
        raise ValueError("checkpoint has missing or unexpected fields")
    for key in expected_keys - {"name", "RSE_RANGE", "PSR.i", "PSR.ic", "RSC.mode"}:
        value = values[key]
        if value != "UNVERIFIED" and not re.fullmatch(r"0x[0-9A-Fa-f]+", value):
            raise ValueError(f"{key} must be a hexadecimal value or UNVERIFIED")
    range_match = re.fullmatch(r"\[(0x[0-9A-Fa-f]+),(0x[0-9A-Fa-f]+)\)", values["RSE_RANGE"])
    if not range_match:
        raise ValueError("RSE_RANGE must be a non-empty half-open hexadecimal range")
    record = dict(values)
    record["stage_value"] = int(values["stage"], 16)
    record["valid_mask"] = int(values["valid"], 16)
    record["rse_base"] = int(range_match.group(1), 16)
    record["rse_top"] = int(range_match.group(2), 16)
    if record["rse_base"] >= record["rse_top"]:
        raise ValueError("RSE range is empty or reversed")
    stage, mask = STAGE_MASKS[name]
    if record["stage_value"] != stage or record["valid_mask"] != mask:
        raise ValueError("checkpoint stage or validity mask is invalid")
    for field, bit in FIELD_BITS.items():
        present = bool(mask & bit)
        if present != (values[field] != "UNVERIFIED"):
            raise ValueError(f"{field} value disagrees with validity mask")
    if "PSR.i" not in values or values["PSR.i"] not in ("0", "1"):
        raise ValueError("PSR.i observation is missing or malformed")
    if "PSR.ic" not in values or values["PSR.ic"] not in ("0", "1"):
        raise ValueError("PSR.ic observation is missing or malformed")
    if values["RSC"] == "UNVERIFIED" or "RSC.mode" not in values:
        raise ValueError("RSC observation is missing")
    rsc_mode = int(values["RSC"], 16) & 3
    if values["RSC.mode"] != str(rsc_mode):
        raise ValueError("RSC.mode disagrees with the captured RSC")
    record["psr_value"] = int(values["PSR"], 16)
    if int(values["PSR.i"]) != ((record["psr_value"] >> 14) & 1):
        raise ValueError("PSR.i field disagrees with the captured PSR")
    if int(values["PSR.ic"]) != ((record["psr_value"] >> 13) & 1):
        raise ValueError("PSR.ic field disagrees with the captured PSR")
    record["rsc_value"] = int(values["RSC"], 16)
    record["bsp_value"] = int(values["BSP"], 16)
    record["fpsr_value"] = int(values["FPSR"], 16)
    record["iva_value"] = int(values["IVA"], 16) if values["IVA"] != "UNVERIFIED" else None
    record["bspstore_value"] = int(values["BSPSTORE"], 16) if values["BSPSTORE"] != "UNVERIFIED" else None
    return record


def validate_checkpoint_output(stdout: str, *, ivt: int, rse_base: int,
                               rse_top: int) -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    parsed: dict[str, Any] = {}
    for line in stdout.splitlines():
        if not line.startswith("IA64_CHECKPOINT_"):
            continue
        try:
            record = parse_checkpoint_record(line)
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if record["name"] in parsed:
            errors.append(f"duplicate checkpoint {record['name']}")
        parsed[record["name"]] = record
    for name in STAGE_MASKS:
        if name not in parsed:
            errors.append(f"checkpoint {name} is missing from guest output")
    for name, record in parsed.items():
        if record["rse_base"] != rse_base or record["rse_top"] != rse_top:
            errors.append(f"checkpoint {name} reports an RSE range different from the linked ELF")
        if int(record["IVT_EXPECTED"], 16) != ivt:
            errors.append(f"checkpoint {name} IVT expectation differs from the linked ELF")
        if record["PSR.i"] != "0" or (record["psr_value"] & (1 << 14)):
            errors.append(f"checkpoint {name} reports external interrupts enabled")
        expected_ic = "0" if name in ("A", "B") else "1"
        if record["PSR.ic"] != expected_ic:
            errors.append(f"checkpoint {name} PSR.ic differs from its architecture-init stage")
        if record["name"] in ("B", "C", "D") and not (rse_base <= record["bsp_value"] < rse_top):
            errors.append(f"checkpoint {name} BSP is outside the candidate's RSE backing store")
        if record["name"] == "B":
            if record["rsc_value"] & 3:
                errors.append("checkpoint B was not captured in enforced-lazy RSE mode")
            if record["bspstore_value"] != rse_base:
                errors.append("checkpoint B BSPSTORE differs from the linked backing-store base")
        if record["name"] in ("C", "D"):
            if record["rsc_value"] != 3:
                errors.append(f"checkpoint {record['name']} RSC differs from configured eager mode 3")
            if record["iva_value"] != ivt:
                errors.append(f"checkpoint {record['name']} IVA differs from the linked IVT")
            if record["fpsr_value"] != EXPECTED_FPSR:
                errors.append(f"checkpoint {record['name']} FPSR differs from the early-exec configuration")
            if record["psr_value"] & ((1 << 18) | (1 << 19)):
                errors.append(f"checkpoint {record['name']} has PSR.dfl/dfh disabled")
    return parsed, errors


def run_disassembly(objdump: str, elf: Path, symbol: str) -> str:
    result = subprocess.run([objdump, "-d", "-C", f"--disassemble={symbol}", str(elf)],
                            capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"objdump failed for {symbol}: {result.stderr.strip()}")
    return result.stdout


def validate(repo_root: Path, elf_path: Path, objdump: str) -> list[str]:
    failures = validate_checkpoint_sources(repo_root)
    gate_count = 1
    for error in failures:
        print(f"CPU_STATE_SOURCE=FAIL {error}")
    if not failures:
        print("CPU_STATE_SOURCE=PASS layout, state restrictions, startup order, and ABI leaves")

    elf_validator = load_elf_validator(repo_root)
    elf = elf_validator.Elf64(elf_path)
    symbols = elf.symbols

    def require(condition: bool, label: str, detail: str) -> None:
        nonlocal gate_count
        gate_count += 1
        print(f"{label}={'PASS' if condition else 'FAIL'} {detail}")
        if not condition:
            failures.append(label)

    names = ("ia64_checkpoint_a", "ia64_checkpoint_b", "ia64_checkpoint_c", "ia64_checkpoint_d")
    checkpoints = [symbols.get(name) for name in names]
    addresses = [item["value"] for item in checkpoints if item]
    require(len(checkpoints) == 4 and all(checkpoints), "CHECKPOINT_ELF_SYMBOLS",
            "all four checkpoint records are linked")
    require(len(checkpoints) == 4 and all(item and item["size"] == CHECKPOINT_BYTES and
            item["value"] % 16 == 0 for item in checkpoints), "CHECKPOINT_ELF_LAYOUT",
            "each record is 96 bytes and 16-byte aligned")
    require(len(addresses) == 4 and addresses == list(range(addresses[0], addresses[0] + 4 * CHECKPOINT_BYTES,
            CHECKPOINT_BYTES)), "CHECKPOINT_ELF_CONTIGUOUS", "four records occupy exactly 384 bytes")

    checkpoint_sections_ok = True
    for item in checkpoints:
        if not item:
            checkpoint_sections_ok = False
            continue
        section = next((part for part in elf.sections if part["index"] == item["shndx"]), None)
        if not section or section["type"] != SHT_PROGBITS or not (section["flags"] & SHF_WRITE) or \
                section["flags"] & 4:
            checkpoint_sections_ok = False
    require(checkpoint_sections_ok, "CHECKPOINT_DATA_SECTION",
            "records are writable initialized data, outside the BSS clear and non-executable")

    ivt_symbol = symbols.get("ia64_ivt")
    ivt_end = symbols.get("ia64_ivt_end")
    base_symbol = symbols.get("rse_backing_store_base")
    top_symbol = symbols.get("rse_backing_store_top")
    stack_bottom = symbols.get("stack_bottom")
    stack_top = symbols.get("stack_top")
    symbols_ok = bool(ivt_symbol and ivt_end and base_symbol and top_symbol and stack_bottom and stack_top)
    require(symbols_ok, "CHECKPOINT_EXPECTATION_SYMBOLS",
            "candidate ELF exports IVT, stack, and RSE backing-store bounds")
    if symbols_ok:
        require(ivt_symbol["value"] % 0x8000 == 0 and
                ivt_end["value"] - ivt_symbol["value"] == 0x8000,
                "CHECKPOINT_LINKED_IVT", f"base=0x{ivt_symbol['value']:x} size=0x8000")
        require(top_symbol["value"] - base_symbol["value"] == 65536,
                "CHECKPOINT_LINKED_RSE_RANGE",
                f"[0x{base_symbol['value']:x},0x{top_symbol['value']:x}) is 64 KiB")
        require(stack_top["value"] - stack_bottom["value"] == 65536 and
                (stack_top["value"] <= base_symbol["value"] or
                 top_symbol["value"] <= stack_bottom["value"]),
                "CHECKPOINT_STACK_RSE_SEPARATION", "the 64 KiB stack and RSE store are disjoint")
        loads = [program for program in elf.programs if program["type"] == 1]
        def in_writable_load(start: int, end: int) -> bool:
            return any((program["flags"] & PF_W) and not (program["flags"] & PF_X) and
                       program["vaddr"] <= start and end <= program["vaddr"] + program["memsz"]
                       for program in loads)
        require(in_writable_load(base_symbol["value"], top_symbol["value"]),
                "CHECKPOINT_RSE_MEMORY_RANGE", "backing store is inside a candidate writable PT_LOAD")
        if len(addresses) == 4:
            require(in_writable_load(addresses[0], addresses[-1] + CHECKPOINT_BYTES),
                    "CHECKPOINT_STORAGE_MEMORY_RANGE", "checkpoint array is inside writable PT_LOAD")

    try:
        boot = run_disassembly(objdump, elf_path, "_start")
        reader_c = run_disassembly(objdump, elf_path, "ia64_capture_checkpoint_c")
        reader_d = run_disassembly(objdump, elf_path, "ia64_capture_checkpoint_d")
        compact_c = re.sub(r"\s+", "", reader_c)
        compact_d = re.sub(r"\s+", "", reader_d)
        def reads_register(disassembly: str, register: str, name: str) -> bool:
            return re.search(r"mov(?:\.[a-z]+)?" + re.escape(register) + r"=" +
                             re.escape(name), disassembly) is not None

        safe_reader = all(reads_register(compact_c, "r3", value) and
                          reads_register(compact_d, "r3", value) for value in (
                              "psr", "ar.rsc", "ar.bsp", "ar.fpsr", "cr.iva", "ar.pfs", "ar.unat"))
        restricted_reader = any(token in compact_c or token in compact_d for token in (
            "ar.bspstore", "ar.rnat", "alloc", "br.call", "movar.pfs=", "movar.unat="))
        require(safe_reader and not restricted_reader, "CHECKPOINT_READER_DISASSEMBLY",
                "C/D are no-frame read-only leaves and omit mode-restricted RSE registers")
        compact_boot = re.sub(r"\s+", "", boot)
        require(reads_register(compact_boot, "r16", "ar.bspstore") and "ar.rnat" not in compact_boot and
                "loadrs" in compact_boot,
                "CHECKPOINT_RSE_DISASSEMBLY", "target code reads BSPSTORE only in startup lazy mode")
        require(reads_register(compact_boot, "r15", "psr") and
                reads_register(compact_boot, "r15", "ar.rsc") and
                reads_register(compact_boot, "r15", "ar.bsp"),
                "CHECKPOINT_INITIAL_DISASSEMBLY", "target startup emits checkpoint A register reads")
    except (OSError, RuntimeError) as exc:
        require(False, "CHECKPOINT_TARGET_DISASSEMBLY", str(exc))

    if failures:
        print(f"IA64_CPU_STATE_STATIC_GATES=FAIL count={len(failures)}")
    else:
        print(f"IA64_CPU_STATE_STATIC_GATES=PASS count={gate_count}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", required=True)
    args = parser.parse_args()
    try:
        return 1 if validate(args.repo_root, args.elf, args.objdump) else 0
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"IA64_CPU_STATE_STATIC_GATES=FAIL error={exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

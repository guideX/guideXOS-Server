#!/usr/bin/env python3
"""Validate early IA-64 checkpoint source, target code, ELF storage, and guest records."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
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
RUNTIME_RECORD_FIELDS = (
    "name", "stage", "valid", "PSR", "PSR.i", "PSR.ic", "RSC", "RSC.mode",
    "BSP", "BSPSTORE", "RNAT", "FPSR", "IVA", "PFS", "UNAT",
    "IVT_EXPECTED", "RSE_RANGE",
)
SIMULATOR_FAULT_MARKERS = (
    "exception", "fault @", " fault\n", "unallocated page", "illegal address",
    "privileged operation fault", "not an elf", "kernel not for this processor",
)


def load_elf_validator(repo_root: Path) -> Any:
    path = repo_root / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_cpu_state_elf_validator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load IA-64 ELF parser: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_general_exception_validator(repo_root: Path) -> Any:
    path = repo_root / "scripts" / "validate-ia64-general-exception.py"
    spec = importlib.util.spec_from_file_location("ia64_cpu_state_general_validator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load General Exception validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_unaligned_reference_validator(repo_root: Path) -> Any:
    path = repo_root / "scripts" / "validate-ia64-unaligned-reference.py"
    spec = importlib.util.spec_from_file_location("ia64_cpu_state_unaligned_validator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load Unaligned Reference validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_nat_consumption_validator(repo_root: Path) -> Any:
    path = repo_root / "scripts" / "validate-ia64-nat-consumption.py"
    spec = importlib.util.spec_from_file_location("ia64_cpu_state_nat_validator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load Register NaT Consumption validator: {path}")
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
    observed_order: list[str] = []
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
        observed_order.append(record["name"])
    expected_order = list(STAGE_MASKS)
    if observed_order != expected_order:
        errors.append(f"checkpoint order differs from {','.join(expected_order)}: "
                      f"observed {','.join(observed_order)}")
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


def validate_runtime_evidence(stdout: str, stderr: str, instruction_counts: str,
                              run_result: dict[str, Any], expected_sha256: str, *,
                              ivt: int, rse_base: int, rse_top: int,
                              general_fault_site: int | None = None,
                              general_continuation: int | None = None,
                              unaligned_symbols: dict[str, int] | None = None,
                              nat_symbols: dict[str, int] | None = None) -> tuple[dict[str, Any], list[str]]:
    """Validate raw guest records together with the runner's captured result."""
    records, errors = validate_checkpoint_output(
        stdout, ivt=ivt, rse_base=rse_base, rse_top=rse_top)

    if run_result.get("label") != "guideXOS-kernel":
        errors.append("runner result does not identify a guideXOS kernel launch")
    if run_result.get("artifact_sha256") != expected_sha256:
        errors.append("runner artifact identity differs from the selected candidate")
    if run_result.get("cleanup_complete") is not True:
        errors.append("runner did not confirm simulator process cleanup")
    classification = run_result.get("classification")
    if classification not in ("GUEST_EXIT_ZERO", "BREAK_EXCEPTION_KERNEL_PASS",
                               "GENERAL_EXCEPTION_KERNEL_PASS",
                               "UNALIGNED_REFERENCE_KERNEL_PASS",
                               "NAT_CONSUMPTION_KERNEL_PASS") or \
            run_result.get("exit_code") != 0 or run_result.get("timed_out") is not False:
        errors.append("runner did not report successful intentional guest termination")
    expected_fault_count = (2 if classification == "BREAK_EXCEPTION_KERNEL_PASS" else
                            3 if classification == "GENERAL_EXCEPTION_KERNEL_PASS" else
                            4 if classification == "UNALIGNED_REFERENCE_KERNEL_PASS" else
                            5 if classification == "NAT_CONSUMPTION_KERNEL_PASS" else 0)
    if classification in ("BREAK_EXCEPTION_KERNEL_PASS", "GENERAL_EXCEPTION_KERNEL_PASS",
                          "UNALIGNED_REFERENCE_KERNEL_PASS", "NAT_CONSUMPTION_KERNEL_PASS"):
        if run_result.get("ski_fault_count") != expected_fault_count or \
                run_result.get("break_exception_expected_faults") != 2 or \
                run_result.get("break_exception_evidence_errors") != []:
            errors.append("runner did not bind the expected Break Instruction fault evidence")
        post_rfi = run_result.get("break_exception_post_rfi_observations")
        if not isinstance(post_rfi, dict) or set(post_rfi) != {"slot1", "slot2"}:
            errors.append("runner did not bind both immediate post-rfi PSR records")
        else:
            expected_post_rfi = {
                "slot1": (0x6a17, 2, 1, 0xBEEFC0DE4B505231),
                "slot2": (0x6a18, 0, 2, 0xBEEFC0DE4B505232),
            }
            for slot, (immediate, return_slot, order, valid) in expected_post_rfi.items():
                record = post_rfi[slot]
                psr = record.get("PSR") if isinstance(record, dict) else None
                if not isinstance(record, dict) or \
                        record.get("immediate") != immediate or \
                        record.get("return_slot") != return_slot or \
                        record.get("order") != order or record.get("valid") != valid or \
                        not isinstance(psr, int) or ((psr >> 14) & 1) != 0 or \
                        ((psr >> 13) & 1) != 1:
                    errors.append(f"runner {slot} post-rfi capture is missing, stale, out of order, or has wrong PSR")
        post_rfi_lines = [line for line in stdout.splitlines()
                          if line.startswith("IA64_BREAK_POST_RFI_PSR")]
        if len(post_rfi_lines) != 2:
            errors.append("raw guest output lacks exactly two immediate post-rfi PSR records")
    if classification in ("GENERAL_EXCEPTION_KERNEL_PASS", "UNALIGNED_REFERENCE_KERNEL_PASS",
                          "NAT_CONSUMPTION_KERNEL_PASS"):
        combined_expected = (5 if classification == "NAT_CONSUMPTION_KERNEL_PASS" else
                             4 if classification == "UNALIGNED_REFERENCE_KERNEL_PASS" else 3)
        if run_result.get("ski_fault_count") != combined_expected or \
                run_result.get("expected_exception_faults") != combined_expected or \
                run_result.get("break_exception_expected_faults") != 2 or \
                run_result.get("general_exception_expected_faults") != 1 or \
                run_result.get("general_exception_evidence_errors") != []:
            errors.append("runner did not bind the expected Break and General Exception faults")
        if general_fault_site is None or general_continuation is None:
            errors.append("General Exception output cannot be bound to candidate symbols")
        else:
            general_validator = load_general_exception_validator(Path(__file__).resolve().parents[1])
            expected_general = {
                "ivt": ivt,
                "fault_site": general_fault_site,
                "continuation": general_continuation,
                "rse_base": rse_base,
                "rse_top": rse_top,
            }
            general_record, general_errors = general_validator.validate_output(
                stdout, expected_general, probe=False)
            if general_errors:
                errors.extend(f"General Exception evidence: {error}" for error in general_errors)
            if not isinstance(general_record, dict) or \
                    run_result.get("general_exception_record_fields") != general_record:
                errors.append("runner General Exception frame differs from the raw guest record")
    if classification in ("UNALIGNED_REFERENCE_KERNEL_PASS", "NAT_CONSUMPTION_KERNEL_PASS"):
        if unaligned_symbols is None or \
                run_result.get("unaligned_reference_expected_faults") != 1 or \
                run_result.get("unaligned_reference_evidence_errors") != [] or \
                run_result.get("unaligned_reference_ssc_exit_function") != 66:
            errors.append("runner did not bind the Unaligned Reference fault/continuation contract")
        else:
            unaligned_validator = load_unaligned_reference_validator(
                Path(__file__).resolve().parents[1])
            unaligned_record, unaligned_errors = unaligned_validator.validate_output(
                stdout, unaligned_symbols, probe=False)
            if unaligned_errors:
                errors.extend(f"Unaligned Reference evidence: {error}"
                              for error in unaligned_errors)
            if not isinstance(unaligned_record, dict) or \
                    run_result.get("unaligned_reference_record_fields") != unaligned_record:
                errors.append("runner Unaligned Reference frame differs from the raw guest record")
    if classification == "NAT_CONSUMPTION_KERNEL_PASS":
        if nat_symbols is None or \
                run_result.get("nat_consumption_expected_faults") != 1 or \
                run_result.get("nat_consumption_evidence_errors") != [] or \
                run_result.get("nat_consumption_ssc_exit_function") != 66 or \
                run_result.get("expected_exception_faults") != 5:
            errors.append("runner did not bind the Register NaT Consumption fault/return contract")
        else:
            nat_validator = load_nat_consumption_validator(Path(__file__).resolve().parents[1])
            nat_record, nat_errors = nat_validator.validate_output(stdout, nat_symbols, probe=False)
            if nat_errors:
                errors.extend(f"Register NaT Consumption evidence: {error}" for error in nat_errors)
            if not isinstance(nat_record, dict) or \
                    run_result.get("nat_consumption_record_fields") != nat_record:
                errors.append("runner Register NaT Consumption frame differs from the raw guest record")
    elif classification == "GUEST_EXIT_ZERO" and run_result.get("ski_fault_count") not in (None, 0):
        errors.append("zero-fault kernel classification reports a simulator fault")
    simulator_output = stdout + "\n" + stderr
    if "kernel exited with status 0" not in simulator_output.lower():
        errors.append("guest output lacks the successful Ski SSC exit status")

    command = run_result.get("command")
    if not isinstance(command, list) or not all(item in command for item in (
            "-stats", "-icnt", "-forcesystem")):
        errors.append("runner command does not show stats, instruction capture, and system mode")

    # Expected synchronous faults are attested by strict guest records and runner
    # classification. Ignore their structured markers when scanning diagnostics.
    allowed_exception_prefixes = ()
    if classification in ("BREAK_EXCEPTION_KERNEL_PASS", "GENERAL_EXCEPTION_KERNEL_PASS",
                          "UNALIGNED_REFERENCE_KERNEL_PASS", "NAT_CONSUMPTION_KERNEL_PASS"):
        allowed_exception_prefixes += ("IA64_BREAK_",)
    if classification in ("GENERAL_EXCEPTION_KERNEL_PASS", "UNALIGNED_REFERENCE_KERNEL_PASS",
                          "NAT_CONSUMPTION_KERNEL_PASS"):
        allowed_exception_prefixes += ("IA64_GENERAL_EXCEPTION_",)
    if classification in ("UNALIGNED_REFERENCE_KERNEL_PASS", "NAT_CONSUMPTION_KERNEL_PASS"):
        allowed_exception_prefixes += ("IA64_UNALIGNED_REFERENCE_",)
    if classification == "NAT_CONSUMPTION_KERNEL_PASS":
        allowed_exception_prefixes += ("IA64_NAT_CONSUMPTION_",)
    diagnostic_lines = [line for line in simulator_output.splitlines()
                        if not line.startswith(allowed_exception_prefixes)]
    diagnostic_output = "\n".join(diagnostic_lines)
    summary = re.findall(
        r"(?im)^\s*(\d+)\s+insts?\s+\((\d+)\s+faults?\)",
        simulator_output + "\n" + instruction_counts,
    )
    if len(summary) != 1:
        errors.append("simulator captures lack one unambiguous instruction/fault summary")
    elif int(summary[0][1]) != expected_fault_count:
        if expected_fault_count == 0:
            errors.append(f"simulator recorded {summary[0][1]} unexpected fault(s)")
        else:
            errors.append(f"simulator recorded {summary[0][1]} fault(s), expected {expected_fault_count}")

    combined = diagnostic_output.lower()
    for marker in SIMULATOR_FAULT_MARKERS:
        if marker in combined:
            errors.append(f"simulator output contains a fault marker: {marker.strip()}")

    if run_result.get("cpu_state_checkpoint_errors") != []:
        errors.append("runner recorded CPU-state checkpoint validation errors")
    reported_records = run_result.get("cpu_state_checkpoints")
    if not isinstance(reported_records, dict) or set(reported_records) != set(STAGE_MASKS):
        errors.append("runner result does not contain exactly the four guest checkpoints")
    else:
        for name in STAGE_MASKS:
            reported = reported_records[name]
            parsed = records.get(name)
            if not isinstance(reported, dict) or parsed is None or any(
                    reported.get(field) != parsed.get(field) for field in RUNTIME_RECORD_FIELDS):
                errors.append(f"runner checkpoint {name} differs from the raw guest log")
    return records, errors


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


def validate_runtime_files(repo_root: Path, elf_path: Path, manifest_path: Path,
                           loader_record_path: Path, ski_prefix: Path,
                           invocation_path: Path, run_result_path: Path,
                           expected_sha256: str) -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        loader_record = json.loads(loader_record_path.read_text(encoding="utf-8"))
        run_result = json.loads(run_result_path.read_text(encoding="utf-8"))
        invocation = invocation_path.read_text(encoding="utf-8")
        actual_sha256 = hashlib.sha256(elf_path.read_bytes()).hexdigest()
    except (OSError, json.JSONDecodeError) as exc:
        return {}, [f"runtime evidence is missing or invalid: {exc}"]

    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha256):
        errors.append("expected candidate SHA-256 is malformed")
    if actual_sha256 != expected_sha256:
        errors.append("selected ELF SHA-256 differs from the expected candidate identity")
    if not isinstance(manifest, dict) or manifest.get("schema") != "guidexos-ia64-artifact-v1" or \
            manifest.get("profile") != "early-exec" or manifest.get("sha256") != actual_sha256 or \
            manifest.get("static_gates") != "PASS" or \
            manifest.get("cpu_state_static_gates") != "PASS" or \
            manifest.get("nat_consumption_static_gates") != "PASS" or \
            manifest.get("nat_consumption_expected_faults") != 1 or \
            manifest.get("expected_exception_faults") != 5 or \
            manifest.get("guest_execution_authorized") is not False or \
            manifest.get("runtime_qualified") is not False or \
            manifest.get("scheduler_qualified") is not False or \
            manifest.get("interrupt_fp_preservation") is not False:
        errors.append("candidate manifest does not bind the statically qualified early-exec ELF")
    elif not isinstance(manifest.get("elf"), str) or \
            (manifest_path.parent / manifest["elf"]).resolve() != elf_path.resolve():
        errors.append("candidate manifest path does not identify the selected ELF")

    if not isinstance(loader_record, dict):
        return {}, errors + ["loader compatibility record is not a JSON object"]

    try:
        simulator = (ski_prefix / "bin" / "ski").resolve(strict=True)
        bskinc = (ski_prefix / "bin" / "bskinc").resolve(strict=True)
        simulator_sha256 = hashlib.sha256(simulator.read_bytes()).hexdigest()
    except OSError as exc:
        simulator = None
        bskinc = None
        simulator_sha256 = ""
        errors.append(f"qualified Ski executable is unavailable: {exc}")

    if loader_record.get("schema") != "guidexos-ia64-ski-loader-v1" or \
            loader_record.get("outcome") != "SKI_LOADER_READY" or \
            loader_record.get("loader_kind") != "ski-direct-system-elf" or \
            loader_record.get("kernel_sha256") != actual_sha256 or \
            loader_record.get("kernel_profile") != "early-exec" or \
            loader_record.get("ski_sha256") != simulator_sha256 or \
            loader_record.get("kernel_execution_authorized") is not False or \
            loader_record.get("single_cpu_contract") != "SOURCE_ENFORCED_ONE_CPU":
        errors.append("loader record does not bind this candidate, simulator, and one-CPU contract")
    else:
        ski_source = loader_record.get("ski_source")
        direct_loader = loader_record.get("direct_loader")
        if not isinstance(ski_source, dict) or \
                ski_source.get("commit") != "dfc2902ea1423d9b32543a5daf8026213f2b37a1" or \
                ski_source.get("source_tree_clean") is not True or \
                loader_record.get("ski_version") != "1.5.1" or \
                not isinstance(direct_loader, dict) or \
                direct_loader.get("uses_system_mode_option") != "-forcesystem" or \
                direct_loader.get("candidate_paddr_equals_vaddr") is not True or \
                direct_loader.get("guest_resident_loader_ranges") != []:
            errors.append("loader record lacks the pinned Ski system-mode direct-loader qualification")

    if not isinstance(run_result, dict):
        return {}, errors + ["runner result is not a JSON object"]
    if run_result.get("classification") in ("UNALIGNED_REFERENCE_KERNEL_PASS",
                                               "NAT_CONSUMPTION_KERNEL_PASS"):
        probe = manifest.get("unaligned_reference_probe") if isinstance(manifest, dict) else None
        if not isinstance(manifest, dict) or \
                manifest.get("unaligned_reference_static_gates") != "PASS" or \
                manifest.get("unaligned_reference_expected_faults") != 1 or \
                manifest.get("expected_exception_faults") != 5 or \
                manifest.get("nat_consumption_static_gates") != "PASS" or \
                manifest.get("nat_consumption_expected_faults") != 1 or \
                not isinstance(probe, dict) or probe.get("qualified") is not True or \
                probe.get("expected_faults") != 1 or probe.get("expected_continuations") != 1 or \
                run_result.get("unaligned_reference_probe_artifact_sha256") != \
                probe.get("artifact_sha256") or \
                run_result.get("unaligned_reference_probe_simulator_sha256") != simulator_sha256:
            errors.append("candidate manifest/run does not bind the qualified Unaligned Reference probe")
    if run_result.get("classification") == "NAT_CONSUMPTION_KERNEL_PASS":
        probe = manifest.get("nat_consumption_probe") if isinstance(manifest, dict) else None
        if not isinstance(manifest, dict) or \
                manifest.get("nat_consumption_static_gates") != "PASS" or \
                manifest.get("nat_consumption_expected_faults") != 1 or \
                manifest.get("expected_exception_faults") != 5 or \
                not isinstance(probe, dict) or probe.get("qualified") is not True or \
                probe.get("expected_faults") != 1 or probe.get("expected_continuations") != 1 or \
                run_result.get("nat_consumption_probe_artifact_sha256") != \
                probe.get("artifact_sha256") or \
                run_result.get("nat_consumption_probe_simulator_sha256") != simulator_sha256:
            errors.append("candidate manifest/run does not bind the qualified Register NaT Consumption probe")
    command = run_result.get("command")
    if isinstance(command, list) and command:
        try:
            command_simulator = Path(command[0]).resolve(strict=True)
            if bskinc is None or simulator is None or command_simulator != bskinc or bskinc != simulator:
                errors.append("runner command does not use the qualified Ski batch executable")
        except OSError as exc:
            errors.append(f"runner simulator command is unavailable: {exc}")
    else:
        errors.append("runner command is missing")

    required_invocation_parts = (
        "--authorize-kernel-guest-execution", "--profile early-exec",
        f"--expected-sha256 {expected_sha256}", str(elf_path), str(manifest_path),
        str(loader_record_path),
    )
    if any(part not in invocation for part in required_invocation_parts):
        errors.append("saved invocation lacks explicit authorization or candidate-bound arguments")

    try:
        for key in ("stdout", "stderr", "instruction_counts"):
            value = run_result.get(key)
            if not isinstance(value, str) or not Path(value).is_file():
                raise OSError(f"runner result does not identify an existing {key} capture")
            expected_hash = run_result.get(f"{key}_sha256")
            actual_hash = hashlib.sha256(Path(value).read_bytes()).hexdigest()
            if expected_hash != actual_hash:
                errors.append(f"runner {key} hash differs from the preserved capture")
        stdout = Path(run_result["stdout"]).read_text(encoding="utf-8", errors="replace")
        stderr = Path(run_result["stderr"]).read_text(encoding="utf-8", errors="replace")
        instruction_counts = Path(run_result["instruction_counts"]).read_text(
            encoding="utf-8", errors="replace")
    except OSError as exc:
        return {}, errors + [f"runner output capture is missing: {exc}"]

    elf_validator = load_elf_validator(repo_root)
    try:
        candidate = elf_validator.Elf64(elf_path)
    except (OSError, ValueError, RuntimeError) as exc:
        return {}, errors + [f"candidate ELF cannot supply runtime invariants: {exc}"]
    symbols = candidate.symbols
    required_symbols = ("ia64_ivt", "rse_backing_store_base", "rse_backing_store_top",
                        "ia64_break_slot1_bundle", "ia64_break_slot2_bundle",
                        "ia64_break_slot1_post_rfi_capture",
                        "ia64_break_slot2_continuation",
                        "ia64_nat_consumption_record",
                        "ia64_nat_consumption_fault_site",
                        "ia64_nat_consumption_continuation")
    if any(name not in symbols for name in required_symbols):
        return {}, errors + ["candidate ELF lacks IVT or RSE expectation symbols"]
    gp_symbol = symbols.get("__gp")
    expected_entry = f"0x{candidate.entry:x}"
    expected_gp = f"0x{gp_symbol['value']:x}" if gp_symbol else ""
    if loader_record.get("kernel_entry") != expected_entry or \
            loader_record.get("kernel_gp") != expected_gp:
        errors.append("loader record entry or GP differs from the candidate ELF symbols")
    program_loads = [program for program in candidate.programs if program["type"] == 1]
    flag_text = lambda flags: ("R" if flags & 4 else "") + ("W" if flags & 2 else "") + \
        ("X" if flags & 1 else "")
    expected_loads = [{
        "start": hex(program["vaddr"]),
        "end_exclusive": hex(program["vaddr"] + program["memsz"]),
        "file_size": hex(program["filesz"]),
        "memory_size": hex(program["memsz"]),
        "flags": flag_text(program["flags"]),
    } for program in program_loads]
    if loader_record.get("kernel_load_segments") != expected_loads:
        errors.append("loader record PT_LOAD ranges differ from the candidate ELF")
    if run_result.get("artifact_manifest_sha256") != hashlib.sha256(
            manifest_path.read_bytes()).hexdigest():
        errors.append("runner manifest hash differs from the selected candidate manifest")
    if run_result.get("loader_record_sha256") != hashlib.sha256(
            loader_record_path.read_bytes()).hexdigest():
        errors.append("runner loader-record hash differs from the selected loader record")
    if run_result.get("loader_record_kernel_sha256") != actual_sha256:
        errors.append("runner loader record does not bind the selected candidate ELF")
    if run_result.get("simulator_sha256") != simulator_sha256:
        errors.append("runner simulator identity differs from the qualified Ski executable")
    if run_result.get("execution_mode") != "-forcesystem" or run_result.get("cpu_count") != 1:
        errors.append("runner did not record one-CPU direct system-mode loading")
    limits = run_result.get("execution_limits")
    if not isinstance(limits, dict) or \
            limits.get("address_space_soft_bytes", 2**63) > 2 * 1024 * 1024 * 1024 or \
            limits.get("address_space_hard_bytes", 2**63) > 2 * 1024 * 1024 * 1024 or \
            limits.get("cpu_soft_seconds", 2**63) > 120 or \
            limits.get("cpu_hard_seconds", 2**63) > 120 or \
            limits.get("wall_clock_seconds", 2**63) > 60:
        errors.append("runner did not record the qualified 2 GiB/120-second/60-second limits")
    records, runtime_errors = validate_runtime_evidence(
        stdout, stderr, instruction_counts, run_result, expected_sha256,
        ivt=symbols["ia64_ivt"]["value"],
        rse_base=symbols["rse_backing_store_base"]["value"],
        rse_top=symbols["rse_backing_store_top"]["value"],
        general_fault_site=symbols.get("ia64_general_exception_fault_site", {}).get("value"),
        general_continuation=symbols.get("ia64_general_exception_continuation", {}).get("value"),
        unaligned_symbols={
            "ivt": symbols.get("ia64_ivt", {}).get("value", 0),
            "vector": symbols.get("ivt_unaligned_ref", {}).get("value", 0),
            "entry": symbols.get("ia64_unaligned_reference_entry", {}).get("value", 0),
            "record": symbols.get("ia64_unaligned_reference_record", {}).get("value", 0),
            "buffer": symbols.get("ia64_unaligned_reference_buffer", {}).get("value", 0),
            "fault_site": symbols.get("ia64_unaligned_reference_fault_site", {}).get("value", 0),
            "continuation": symbols.get("ia64_unaligned_reference_continuation", {}).get("value", 0),
            "test": symbols.get("ia64_unaligned_reference_test", {}).get("value", 0),
            "rse_base": symbols["rse_backing_store_base"]["value"],
            "rse_top": symbols["rse_backing_store_top"]["value"],
        },
        nat_symbols={
            "ivt": symbols.get("ia64_ivt", {}).get("value", 0),
            "vector": symbols.get("ivt_nat_consumption", {}).get("value", 0),
            "entry": symbols.get("ia64_nat_consumption_entry", {}).get("value", 0),
            "record": symbols.get("ia64_nat_consumption_record", {}).get("value", 0),
            "spec_site": symbols.get("ia64_nat_consumption_spec_load_site", {}).get("value", 0),
            "fault_site": symbols.get("ia64_nat_consumption_fault_site", {}).get("value", 0),
            "continuation": symbols.get("ia64_nat_consumption_continuation", {}).get("value", 0),
            "target": symbols.get("ia64_nat_consumption_target", {}).get("value", 0),
            "rse_base": symbols["rse_backing_store_base"]["value"],
            "rse_top": symbols["rse_backing_store_top"]["value"],
        },
    )
    errors.extend(runtime_errors)
    if run_result.get("classification") in (
            "BREAK_EXCEPTION_KERNEL_PASS", "GENERAL_EXCEPTION_KERNEL_PASS",
            "UNALIGNED_REFERENCE_KERNEL_PASS", "NAT_CONSUMPTION_KERNEL_PASS"):
        runner_path = repo_root / "scripts" / "run-ia64-ski.py"
        spec = importlib.util.spec_from_file_location("ia64_postflight_ski_runner", runner_path)
        if spec is None or spec.loader is None:
            errors.append("cannot load the Break exception runtime validator")
        else:
            runner = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(runner)
            contract = runner.make_break_contract(
                {"break_slot1": symbols["ia64_break_slot1_bundle"]["value"],
                 "break_slot2": symbols["ia64_break_slot2_bundle"]["value"],
                 "post_rfi_slot1_continuation": symbols[
                     "ia64_break_slot1_post_rfi_capture"]["value"],
                 "post_rfi_slot2_continuation": symbols[
                     "ia64_break_slot2_continuation"]["value"],
                 "rse_base": symbols["rse_backing_store_base"]["value"],
                 "rse_top": symbols["rse_backing_store_top"]["value"]},
                "BREAK_EXCEPTION_KERNEL_PASS", "IA64_BREAK_RECORD",
                "IA64_BREAK_EXCEPTION_RESULT=PASS", "IA64_BREAK_EXCEPTION_RESULT=FAIL",
                "IA64_BREAK_EXCEPTION_VALIDATION")
            contract["stderr_text"] = stderr
            if run_result.get("classification") in (
                    "GENERAL_EXCEPTION_KERNEL_PASS", "UNALIGNED_REFERENCE_KERNEL_PASS",
                    "NAT_CONSUMPTION_KERNEL_PASS"):
                contract["general_exception_contract"] = {"revalidated_by_cpu_postflight": True}
            if run_result.get("classification") in (
                    "UNALIGNED_REFERENCE_KERNEL_PASS", "NAT_CONSUMPTION_KERNEL_PASS"):
                contract["unaligned_reference_contract"] = {"revalidated_by_cpu_postflight": True}
            break_errors = runner.validate_break_output(stdout, contract)
            if break_errors:
                errors.extend(f"Break exception evidence: {error}" for error in break_errors)
            observed_psr, observation_errors = runner.parse_break_post_rfi_psr_observations(
                stdout, contract)
            if observation_errors:
                errors.extend(f"Break exception evidence: {error}" for error in observation_errors)
            if run_result.get("break_exception_post_rfi_observations") != observed_psr:
                errors.append("runner post-rfi capture records differ from raw guest output")
            if run_result.get("classification") in (
                    "UNALIGNED_REFERENCE_KERNEL_PASS", "NAT_CONSUMPTION_KERNEL_PASS"):
                unaligned_validator = load_unaligned_reference_validator(repo_root)
                unaligned_record, unaligned_errors = unaligned_validator.validate_output(
                    stdout, {
                        "ivt": symbols["ia64_ivt"]["value"],
                        "vector": symbols["ivt_unaligned_ref"]["value"],
                        "entry": symbols["ia64_unaligned_reference_entry"]["value"],
                        "record": symbols["ia64_unaligned_reference_record"]["value"],
                        "buffer": symbols["ia64_unaligned_reference_buffer"]["value"],
                        "fault_site": symbols["ia64_unaligned_reference_fault_site"]["value"],
                        "continuation": symbols["ia64_unaligned_reference_continuation"]["value"],
                        "test": symbols["ia64_unaligned_reference_test"]["value"],
                        "rse_base": symbols["rse_backing_store_base"]["value"],
                        "rse_top": symbols["rse_backing_store_top"]["value"],
                    }, probe=False)
                if unaligned_errors or \
                        run_result.get("unaligned_reference_record_fields") != unaligned_record:
                    errors.extend(f"Unaligned Reference evidence: {error}"
                                  for error in unaligned_errors)
                    if run_result.get("unaligned_reference_record_fields") != unaligned_record:
                        errors.append("runner Unaligned Reference capture differs from raw guest output")
            if run_result.get("classification") == "NAT_CONSUMPTION_KERNEL_PASS":
                nat_validator = load_nat_consumption_validator(repo_root)
                nat_record, nat_errors = nat_validator.validate_output(
                    stdout, {
                        "ivt": symbols["ia64_ivt"]["value"],
                        "vector": symbols["ivt_nat_consumption"]["value"],
                        "entry": symbols["ia64_nat_consumption_entry"]["value"],
                        "record": symbols["ia64_nat_consumption_record"]["value"],
                        "spec_site": symbols["ia64_nat_consumption_spec_load_site"]["value"],
                        "fault_site": symbols["ia64_nat_consumption_fault_site"]["value"],
                        "continuation": symbols["ia64_nat_consumption_continuation"]["value"],
                        "target": symbols["ia64_nat_consumption_target"]["value"],
                        "rse_base": symbols["rse_backing_store_base"]["value"],
                        "rse_top": symbols["rse_backing_store_top"]["value"],
                    }, probe=False)
                if nat_errors or run_result.get("nat_consumption_record_fields") != nat_record:
                    errors.extend(f"Register NaT Consumption evidence: {error}"
                                  for error in nat_errors)
                    if run_result.get("nat_consumption_record_fields") != nat_record:
                        errors.append("runner Register NaT Consumption capture differs from raw guest output")
    source_errors = validate_checkpoint_sources(repo_root)
    if source_errors:
        errors.extend(f"checkpoint source contract: {error}" for error in source_errors)
    console_header = (repo_root / "kernel/arch/ia64/include/arch/ski_console.h").read_text(
        encoding="utf-8")
    stop_source = (repo_root / "kernel/arch/ia64/ski_console.cpp").read_text(encoding="utf-8")
    if not re.search(r"\bSSC_EXIT\s*=\s*66\b", console_header) or \
            "ssc(SSC_EXIT);" not in stop_source:
        errors.append("source does not bind the intentional stop path to SSC_EXIT=66")
    return records, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--runtime-run-result", type=Path)
    parser.add_argument("--runtime-manifest", type=Path)
    parser.add_argument("--runtime-loader-record", type=Path)
    parser.add_argument("--runtime-ski-prefix", type=Path)
    parser.add_argument("--runtime-invocation", type=Path)
    parser.add_argument("--expected-sha256")
    args = parser.parse_args()
    try:
        runtime_inputs = (
            args.runtime_manifest, args.runtime_loader_record,
            args.runtime_ski_prefix, args.runtime_invocation, args.expected_sha256,
        )
        if args.runtime_run_result:
            if any(value is None for value in runtime_inputs):
                parser.error("runtime validation requires manifest, loader record, Ski prefix, "
                             "invocation, and expected SHA-256")
            records, errors = validate_runtime_files(
                args.repo_root, args.elf, args.runtime_manifest,
                args.runtime_loader_record, args.runtime_ski_prefix,
                args.runtime_invocation, args.runtime_run_result, args.expected_sha256)
            for name in STAGE_MASKS:
                record = records.get(name)
                if record:
                    print(f"CHECKPOINT_{name}=PASS PSR={record['PSR']} PSR.i={record['PSR.i']} "
                          f"PSR.ic={record['PSR.ic']} RSC={record['RSC']} BSP={record['BSP']} "
                          f"BSPSTORE={record['BSPSTORE']} IVA={record['IVA']} "
                          f"FPSR={record['FPSR']} RNAT={record['RNAT']}")
            for error in errors:
                print(f"RUNTIME_VALIDATION=FAIL {error}", file=sys.stderr)
            if errors:
                print(f"IA64_CPU_STATE_RUNTIME_VALIDATION=FAIL count={len(errors)}")
                return 1
            print("CHECKPOINT_ORDER=PASS A,B,C,D")
            print("RAW_GUEST_RECORDS=PASS exactly four ordered records, matching runner capture")
            print("RUNNER_EXIT=PASS intentional SSC_EXIT=66, process status 0, no timeout")
            print("PROCESS_CLEANUP=PASS simulator process was reaped")
            run_record = json.loads(args.runtime_run_result.read_text(encoding="utf-8"))
            if run_record.get("classification") == "BREAK_EXCEPTION_KERNEL_PASS":
                print("BREAK_EXCEPTION_EVIDENCE=PASS two records, exact vector/immediates/slots, "
                      "distinct return markers, and one continuation per slot")
                print("POST_RFI_PSR=PASS two ordered direct captures show PSR.i=0, PSR.ic=1 before cleanup")
                print("SIMULATOR_FAULTS=PASS total=2 expected_break=2 unexpected=0")
            elif run_record.get("classification") == "GENERAL_EXCEPTION_KERNEL_PASS":
                print("BREAK_EXCEPTION_EVIDENCE=PASS two records, exact vector/immediates/slots, "
                      "distinct return markers, and one continuation per slot")
                print("POST_RFI_PSR=PASS two ordered direct captures show PSR.i=0, PSR.ic=1 before cleanup")
                print("GENERAL_EXCEPTION_EVIDENCE=PASS one exact Illegal Operation site and continuation")
                print("SIMULATOR_FAULTS=PASS total=3 expected_break=2 expected_general=1 unexpected=0")
            elif run_record.get("classification") == "UNALIGNED_REFERENCE_KERNEL_PASS":
                print("BREAK_EXCEPTION_EVIDENCE=PASS two records, exact vector/immediates/slots, "
                      "distinct return markers, and one continuation per slot")
                print("POST_RFI_PSR=PASS two ordered direct captures show PSR.i=0, PSR.ic=1 before cleanup")
                print("GENERAL_EXCEPTION_EVIDENCE=PASS one exact Illegal Operation site and continuation")
                print("UNALIGNED_REFERENCE_EVIDENCE=PASS vector=0x5a00 exact IFA/ISR and one bounded continuation")
                print("SIMULATOR_FAULTS=PASS total=4 expected_break=2 expected_general=1 "
                      "expected_unaligned=1 unexpected=0")
            elif run_record.get("classification") == "NAT_CONSUMPTION_KERNEL_PASS":
                print("BREAK_EXCEPTION_EVIDENCE=PASS two records, exact vector/immediates/slots, "
                      "distinct return markers, and one continuation per slot")
                print("POST_RFI_PSR=PASS two ordered direct captures show PSR.i=0, PSR.ic=1 before cleanup")
                print("GENERAL_EXCEPTION_EVIDENCE=PASS one exact Illegal Operation site and continuation")
                print("UNALIGNED_REFERENCE_EVIDENCE=PASS vector=0x5a00 exact IFA/ISR and one bounded continuation")
                print("NAT_CONSUMPTION_EVIDENCE=PASS vector=0x5600 one exact Register NaT Consumption continuation")
                print("SIMULATOR_FAULTS=PASS total=5 expected_break=2 expected_general=1 "
                      "expected_unaligned=1 expected_nat=1 unexpected=0")
            else:
                print("SIMULATOR_FAULTS=PASS total=0")
            print("CANDIDATE_IDENTITY=PASS ELF, manifest, loader record, invocation, and runner hash agree")
            print("IA64_CPU_STATE_RUNTIME_VALIDATION=PASS")
            return 0
        return 1 if validate(args.repo_root, args.elf, args.objdump) else 0
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"IA64_CPU_STATE_STATIC_GATES=FAIL error={exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

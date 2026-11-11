#!/usr/bin/env python3
"""Static and runtime gates for the one-site IA-64 General Exception test."""

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

ROOT = Path(__file__).resolve().parents[1]
VECTOR_OFFSET = 0x5400
FAULT_SUBTYPE_ILLEGAL_OPERATION = 0
RECORD_VALID = 0xBEEFC0DE47455631
HANDLER_VALID = 0xBEEFC0DE47454831
COMPLETION = 0xBEEFC0DE47454331
RETURN = 0xBEEFC0DE47455231
POST_RFI_VALID = 0xBEEFC0DE47505331
PROBE_SCHEMA = "guidexos-ia64-general-exception-probe-v1"


class GateError(ValueError):
    pass


def load_elf_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_general_elf_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load ELF validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ELF_VALIDATOR = load_elf_validator()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"manifest is missing or invalid: {exc}") from exc
    if not isinstance(manifest, dict):
        raise GateError("manifest must be a JSON object")
    return manifest


def validate_manifest_identity(artifact: Path, manifest_path: Path,
                               expected_sha256: str) -> dict[str, Any]:
    manifest = load_manifest(manifest_path)
    actual = sha256(artifact)
    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha256) or actual != expected_sha256:
        raise GateError(f"artifact identity mismatch: actual={actual}")
    if manifest.get("sha256") != actual or \
            artifact.resolve() != (manifest_path.parent / str(manifest.get("elf", ""))).resolve():
        raise GateError("manifest identity does not bind the selected General Exception ELF")
    if manifest.get("schema") == PROBE_SCHEMA and \
            manifest.get("profile") == "general-exception-probe":
        if manifest.get("guest_execution_authorized") != "probe-only" or \
                manifest.get("system_mode") != "-forcesystem" or \
                manifest.get("cpu_count") != 1 or \
                manifest.get("ivt_vector_offset") != VECTOR_OFFSET or \
                manifest.get("fault_subtype") != "illegal-operation" or \
                manifest.get("faulting_instruction") != "mov r14=ar.bspstore" or \
                manifest.get("expected_faults") != 1 or \
                manifest.get("expected_continuations") != 1 or \
                manifest.get("post_rfi_capture_required") is not True or \
                manifest.get("all_other_vectors") != "fail-stop":
            raise GateError("General Exception probe manifest contract is incomplete")
    elif manifest.get("schema") == "guidexos-ia64-artifact-v1" and \
            manifest.get("profile") == "early-exec":
        if manifest.get("guest_execution_authorized") is not False or \
                manifest.get("general_exception_static_gates") != "PASS" or \
                manifest.get("general_exception_expected_faults") != 1 or \
                manifest.get("expected_exception_faults") not in (3, 4, 5) or \
                (manifest.get("expected_exception_faults") in (4, 5) and
                 (manifest.get("unaligned_reference_static_gates") != "PASS" or
                  manifest.get("unaligned_reference_expected_faults") != 1)) or \
                (manifest.get("expected_exception_faults") == 5 and
                 (manifest.get("nat_consumption_static_gates") != "PASS" or
                  manifest.get("nat_consumption_expected_faults") != 1)):
            raise GateError("early-exec manifest does not retain the General Exception static/execution contract")
    else:
        raise GateError("manifest schema/profile is not a General Exception probe or early-exec candidate")
    return manifest


def parse_record_line(line: str) -> dict[str, int]:
    fields = {key: int(value, 16) for key, value in
              re.findall(r"([A-Za-z0-9_.]+)=(0x[0-9A-Fa-f]+)", line)}
    required = {
        "valid", "version", "size", "vector", "subtype", "IIP", "slot", "IPSR",
        "IFS", "IFS.valid", "ISR", "handler.PSR", "handler.RSC", "handler.BSP",
        "PR", "site", "target", "handler.status", "pre.BSP", "pre.RSC", "pre.UNAT",
        "pre.GR", "completion", "count", "return", "post.PSR", "post.RSC",
        "post.BSP", "post.IVA", "post.UNAT", "post.GR", "post.valid",
    }
    missing = required - fields.keys()
    if missing:
        raise GateError("General Exception record is missing: " + ", ".join(sorted(missing)))
    return fields


def validate_record_fields(fields: dict[str, int], expected: dict[str, int]) -> list[str]:
    errors: list[str] = []
    fixed = {
        "valid": RECORD_VALID,
        "version": 1,
        "size": 256,
        "vector": VECTOR_OFFSET,
        "subtype": FAULT_SUBTYPE_ILLEGAL_OPERATION,
        "slot": 0,
        "IFS": 0,
        "IFS.valid": 0,
        "ISR": 0,
        "handler.RSC": 3,
        "PR": 0x401,
        "handler.status": HANDLER_VALID,
        "pre.RSC": 3,
        "completion": COMPLETION,
        "count": 1,
        "return": RETURN,
        "post.PSR": 0x2000,
        "post.RSC": 3,
        "post.IVA": expected["ivt"],
        "post.valid": POST_RFI_VALID,
    }
    fixed.update({
        "IIP": expected["fault_site"],
        "site": expected["fault_site"],
        "target": expected["continuation"],
        "post.BSP": fields.get("handler.BSP", -1),
        "pre.BSP": fields.get("handler.BSP", -1),
        "pre.UNAT": fields.get("post.UNAT", -1),
        "pre.GR": fields.get("post.GR", -1),
    })
    for key, value in fixed.items():
        if fields.get(key) != value:
            errors.append(f"record {key}={fields.get(key)!r}, expected {value:#x}")

    ipsr = fields.get("IPSR", 0)
    if ((ipsr >> 41) & 3) != 0 or ((ipsr >> 13) & 1) != 1 or \
            ((ipsr >> 14) & 1) != 0 or ((ipsr >> 32) & 3) != 0 or \
            ((ipsr >> 44) & 1) != 0 or ((ipsr >> 46) & 1) != 0:
        errors.append("saved IPSR does not match the authorized slot-0 state")
    handler_psr = fields.get("handler.PSR", 0)
    if handler_psr & 0x6000:
        errors.append("handler PSR did not enter with i=0 and ic=0")
    post_psr = fields.get("post.PSR", 0)
    if (post_psr & 0x6000) != 0x2000:
        errors.append("immediate post-rfi PSR did not have i=0 and ic=1")
    if fields.get("post.UNAT") != fields.get("pre.UNAT"):
        errors.append("UNAT changed across the controlled return")
    if fields.get("post.GR") != fields.get("pre.GR"):
        errors.append("live GP/fault-destination checksum changed across the return")
    bsp = fields.get("handler.BSP", 0)
    if not expected["rse_base"] <= bsp < expected["rse_top"]:
        errors.append("handler BSP is outside the candidate RSE backing-store bounds")
    if fields.get("post.BSP") != bsp or fields.get("pre.BSP") != bsp:
        errors.append("RSE BSP changed across the fault/return")
    if fields.get("post.RSC") != fields.get("handler.RSC") or \
            fields.get("pre.RSC") != fields.get("handler.RSC"):
        errors.append("RSC changed across the fault/return")
    return errors


def validate_output(stdout_text: str, expected: dict[str, int], *,
                    probe: bool = False) -> tuple[dict[str, int] | None, list[str]]:
    errors: list[str] = []
    record_lines = [line for line in stdout_text.splitlines()
                    if line.startswith("IA64_GENERAL_EXCEPTION_RECORD")]
    fields = None
    if len(record_lines) != 1:
        errors.append(f"expected exactly one General Exception record, found {len(record_lines)}")
    else:
        try:
            fields = parse_record_line(record_lines[0])
            errors.extend(validate_record_fields(fields, expected))
        except GateError as exc:
            errors.append(str(exc))

    pre_lines = [line for line in stdout_text.splitlines()
                 if line.startswith("IA64_GENERAL_EXCEPTION_PRE_FAULT ")]
    if len(pre_lines) != 1:
        errors.append("expected exactly one General Exception pre-fault marker")
    elif not re.fullmatch(
            rf"IA64_GENERAL_EXCEPTION_PRE_FAULT vector=0x{VECTOR_OFFSET:x} "
            rf"subtype=illegal-operation site=0x{expected['fault_site']:x} "
            rf"target=0x{expected['continuation']:x} expected_faults=1",
            pre_lines[0].strip(), re.IGNORECASE):
        errors.append("pre-fault marker does not bind the authorized site, subtype, vector, and count")
    post_lines = [line for line in stdout_text.splitlines()
                  if line.startswith("IA64_GENERAL_EXCEPTION_POST_RETURN ")]
    if len(post_lines) != 1:
        errors.append("expected exactly one General Exception post-return marker")
    elif not re.fullmatch(
            rf"IA64_GENERAL_EXCEPTION_POST_RETURN target=0x{expected['continuation']:x} "
            r"count=0x1 PSR=0x2000", post_lines[0].strip(), re.IGNORECASE):
        errors.append("post-return marker does not bind one authorized continuation and PSR=0x2000")
    if stdout_text.count("IA64_GENERAL_EXCEPTION_RESULT=PASS") != 1 or \
            "IA64_GENERAL_EXCEPTION_RESULT=FAIL" in stdout_text:
        errors.append("guest General Exception result marker did not pass exactly once")
    validation_lines = [line for line in stdout_text.splitlines()
                        if line.startswith("IA64_GENERAL_EXCEPTION_VALIDATION ")]
    if len(validation_lines) != 1 or \
            not re.search(r"IA64_GENERAL_EXCEPTION_VALIDATION errors=0x0 test=0x0\b", stdout_text):
        errors.append("guest validation did not report a zero error/test status")
    if probe and (stdout_text.count("IA64_GENERAL_EXCEPTION_PROBE_RESULT=PASS") != 1 or
                  "IA64_GENERAL_EXCEPTION_PROBE_RESULT=FAIL" in stdout_text):
        errors.append("independent probe did not emit its unique pass marker")
    return fields, errors


def exception_symbols(elf_path: Path) -> dict[str, int]:
    elf = ELF_VALIDATOR.Elf64(elf_path)
    names = {
        "ivt": "ia64_ivt",
        "fault_site": "ia64_general_exception_fault_site",
        "continuation": "ia64_general_exception_continuation",
        "rse_base": "rse_backing_store_base",
        "rse_top": "rse_backing_store_top",
    }
    missing = [symbol for symbol in names.values() if symbol not in elf.symbols]
    if missing:
        raise GateError("General Exception ELF is missing symbols: " + ", ".join(missing))
    return {key: elf.symbols[value]["value"] for key, value in names.items()}


def validate_static(repo_root: Path, elf_path: Path, objdump: Path) -> list[str]:
    errors: list[str] = []
    try:
        elf = ELF_VALIDATOR.Elf64(elf_path)
    except (OSError, ELF_VALIDATOR.InvalidElf) as exc:
        return [f"invalid IA-64 ELF: {exc}"]
    if elf.machine != 50 or elf.type != 2 or elf.data[5] != 1:
        errors.append("artifact must be little-endian ELF64 ET_EXEC EM_IA_64")
    sections = {section["name"]: section for section in elf.sections}
    ivt = sections.get(".ivt")
    if ivt is None or ivt["size"] != 0x8000 or ivt["addr"] & 0x7fff:
        errors.append("ELF IVT must be aligned and exactly 0x8000 bytes")

    symbols = elf.symbols
    names = ("ia64_ivt", "ia64_ivt_end", "ivt_general_exception",
             "ia64_general_exception_entry", "ia64_general_exception_record",
             "ia64_general_exception_test", "ia64_general_exception_fault_site",
             "ia64_general_exception_continuation", "rse_backing_store_base",
             "rse_backing_store_top")
    if any(name not in symbols for name in names):
        errors.append("ELF is missing a required vector, frame, test, or RSE symbol")
    else:
        base = symbols["ia64_ivt"]["value"]
        if symbols["ia64_ivt_end"]["value"] != base + 0x8000 or \
                symbols["ivt_general_exception"]["value"] != base + VECTOR_OFFSET:
            errors.append("General Exception vector does not occupy architectural offset 0x5400")
        for name in ("ia64_general_exception_fault_site",
                     "ia64_general_exception_continuation"):
            if symbols[name]["value"] & 0xf:
                errors.append(f"{name} must be bundle aligned")
        if symbols["ia64_general_exception_record"]["value"] & 0xf:
            errors.append("exception frame must be 16-byte aligned")
        if symbols["rse_backing_store_top"]["value"] <= \
                symbols["rse_backing_store_base"]["value"]:
            errors.append("ELF RSE backing-store range is invalid")
        ivt_names = [name for name in symbols
                     if name.startswith("ivt_") and not name.endswith("_end")]
        if len(ivt_names) != 68:
            errors.append("ELF must retain exactly 68 architectural IVT entries")
        else:
            for index, name in enumerate(ELF_VALIDATOR.IVT_VECTOR_NAMES):
                expected_offset = index * 0x400 if index < 20 else 0x5000 + (index - 20) * 0x100
                if name not in symbols or symbols[name]["value"] != base + expected_offset:
                    errors.append(f"ELF vector {name} is missing or outside offset 0x{expected_offset:x}")

    assembly = (repo_root / "kernel" / "arch" / "ia64" /
                "general_exception_entry.s").read_text(encoding="utf-8")
    boot = (repo_root / "kernel" / "arch" / "ia64" / "boot.s").read_text(encoding="utf-8")
    probe_entry = (repo_root / "tests" / "ia64_general_exception" /
                   "entry.s").read_text(encoding="utf-8")
    probe_main = (repo_root / "tests" / "ia64_general_exception" /
                  "probe_main.cpp").read_text(encoding="utf-8")
    handler = assembly.split("ia64_general_exception_entry:", 1)[1].split(
        ".endp ia64_general_exception_entry", 1)[0]
    required_source = (
        "mov r14 = ar.bspstore", "mov cr.iip = r10", "mov cr.ipsr = r15",
        "mov pr = r8, -1", "rfi", "general_exception_fail_spin",
        "mov r15 = psr", "REC_POST_PSR", "GENERAL_EXCEPTION_POST_RFI_VALID",
    )
    for fragment in required_source:
        if fragment not in assembly:
            errors.append(f"source is missing required exception contract text: {fragment}")
    if "ivt_general_exception,           0x5400" not in boot or \
            ".ifdef GXOS_IA64_EARLY_EXEC_PROFILE\nIVT_ENTRY_GENERAL_EXCEPTION" not in boot:
        errors.append("kernel General Exception stub is not isolated to early-exec")
    if "ski_console::stop();" not in probe_main:
        errors.append("independent probe does not terminate through its intentional Ski SSC path")
    probe_slots = re.findall(
        r"(?m)^\s*IVT_(HALT|GENERAL)\s+(ivt_[A-Za-z0-9_]+)\s*,\s*(0x[0-9a-fA-F]+)",
        probe_entry)
    if len(probe_slots) != 68:
        errors.append(f"independent probe defines {len(probe_slots)} IVT slots, expected 68")
    else:
        unexpected = [(kind, name, offset) for kind, name, offset in probe_slots
                      if (name == "ivt_general_exception" and
                          (kind != "GENERAL" or int(offset, 16) != VECTOR_OFFSET)) or
                      (name != "ivt_general_exception" and kind != "HALT")]
        if unexpected:
            errors.append("independent probe enables a vector other than General Exception: " +
                          repr(unexpected[:4]))
    if re.search(r"\b(br\.call|alloc|ld8\.fill|st8\.spill|loadrs)\b", handler):
        errors.append("exception entry uses calls, a stack frame, spills, or RSE loads")
    if re.search(r"\b(?:mov|adds|add|ld8|ld8\.fill)\s+r14\s*=", handler):
        errors.append("exception entry writes R14, the controlled fault destination")
    if "mov ar.bspstore" in handler or "mov ar.rnat" in handler:
        errors.append("exception entry accesses mode-restricted RSE registers")

    try:
        result = subprocess.run([str(objdump), "-dr", str(elf_path)],
                                capture_output=True, text=True, check=False)
    except OSError as exc:
        return errors + [f"cannot run target objdump: {exc}"]
    if result.returncode:
        return errors + [f"target objdump failed: {result.stderr.strip()}"]
    disassembly = result.stdout
    for name in ("ivt_general_exception", "ia64_general_exception_entry",
                 "ia64_general_exception_fault_site", "ia64_general_exception_continuation"):
        if f"<{name}>:" not in disassembly:
            errors.append(f"target disassembly is missing {name}")
    if not re.search(
            r"<ivt_general_exception>:[\s\S]{0,240}?br(?:\.[A-Za-z0-9_.]+)?\s+"
            r"(?:0x)?[0-9a-fA-F]+\s+<ia64_general_exception_entry>",
            disassembly, re.IGNORECASE):
        errors.append("target General Exception IVT stub does not branch to the qualified entry")
    if not re.search(r"ia64_general_exception_fault_site>:[\s\S]{0,500}?mov(?:\.[a-z]+)?\s*r14\s*=\s*ar\.bspstore",
                     disassembly, re.IGNORECASE):
        errors.append("target disassembly does not place the authorized BSPSTORE read at its site")
    if not re.search(r"ia64_general_exception_continuation>:[\s\S]{0,240}?mov(?:\.[a-z]+)?\s*r15\s*=\s*psr",
                     disassembly, re.IGNORECASE):
        errors.append("target continuation does not capture PSR as its first instruction")
    entry_body = disassembly.split("<ia64_general_exception_entry>:", 1)
    entry_body = entry_body[1].split("<general_exception_fail>:", 1)[0] if len(entry_body) == 2 else ""
    if not re.search(r"\brfi\b", entry_body, re.IGNORECASE):
        errors.append("target exception entry has no bounded rfi instruction")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=ROOT)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--expected-sha256")
    args = parser.parse_args(argv)

    errors = validate_static(args.repo_root, args.elf, args.objdump)
    if args.manifest or args.expected_sha256:
        if not args.manifest or not args.expected_sha256:
            errors.append("manifest identity requires both --manifest and --expected-sha256")
        else:
            try:
                validate_manifest_identity(args.elf, args.manifest, args.expected_sha256)
            except GateError as exc:
                errors.append(str(exc))
    if errors:
        for error in errors:
            print(f"IA64_GENERAL_EXCEPTION_GATE_FAIL: {error}", file=sys.stderr)
        return 1
    print("IA64_GENERAL_EXCEPTION_STATIC_GATES=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

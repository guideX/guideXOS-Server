#!/usr/bin/env python3
"""Static, identity, and runtime gates for the one-site IA-64 unaligned load."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
VECTOR_OFFSET = 0x5A00
ACCESS_WIDTH = 8
BUFFER_OFFSET = 1
RECORD_SIZE = 384
RECORD_VALID = 0xBEEFC0DE55415631
HANDLER_VALID = 0xBEEFC0DE55414831
COMPLETION = 0xBEEFC0DE55434331
RETURN = 0xBEEFC0DE55455231
CLEANUP = 0xBEEFC0DE55434C31
POST_RFI_VALID = 0xBEEFC0DE55505631
ISR_READ = 1 << 34
PROBE_SCHEMA = "guidexos-ia64-unaligned-reference-probe-v1"


class GateError(ValueError):
    pass


def load_elf_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_unaligned_elf_validator", path)
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


def validate_expected_fault_count(actual: int | None, expected: int = 1) -> list[str]:
    if actual != expected:
        return [f"simulator fault count={actual!r}, expected exactly {expected}"]
    return []


def validate_ac_lifecycle_source(handler_text: str) -> list[str]:
    errors: list[str] = []
    if handler_text.count("ssm psr.ac ;;") != 1 or \
            handler_text.count("rsm psr.ac ;;") != 1:
        errors.append("AC must be enabled once and explicitly cleared once in the isolated test")
    if "ssm psr.ac ;;\n    srlz.d ;;" not in handler_text or \
            "rsm psr.ac ;;\n    srlz.d ;;" not in handler_text:
        errors.append("AC setup and cleanup lack immediate data serialization")
    continuation = re.search(
        r"ia64_unaligned_reference_continuation:\s*(.*?)"
        r"\.endp ia64_unaligned_reference_test", handler_text, re.DOTALL)
    code = (re.sub(r"/\*.*?\*/", "", continuation.group(1), flags=re.DOTALL)
            if continuation is not None else "")
    first_cleanup = code.find("rsm psr.ac")
    first_load = min((pos for token in ("ld8 ", "ld4 ", "ld2 ")
                      if (pos := code.find(token)) >= 0), default=-1)
    if continuation is None or not re.search(
            r"^\s*\{\s*\.mii\s+mov r15\s*=\s*psr", code,
            re.MULTILINE | re.DOTALL):
        errors.append("the first continuation instruction must capture PSR immediately after rfi")
    if first_cleanup < 0 or "srlz.d" not in code[first_cleanup:] or \
            (first_load >= 0 and first_cleanup > first_load):
        errors.append("post-RFI AC cleanup is missing or occurs after a data read")
    if "ld8 r15 = [r14]" in code:
        errors.append("continuation consumes the faulting load destination")
    return errors


def early_exec_ivt_source(source: str) -> str:
    """Select the production IVT branch for the build profile being checked."""
    in_ivt = False
    active = True
    stack: list[tuple[bool, bool]] = []
    selected: list[str] = []
    for line in source.splitlines():
        stripped = line.strip()
        if stripped.startswith(".section .ivt"):
            in_ivt = True
        elif stripped.startswith(".section .boot"):
            in_ivt = False
        if not in_ivt:
            continue
        if stripped == ".ifdef GXOS_IA64_EARLY_EXEC_PROFILE":
            parent = active
            condition = True
            stack.append((parent, condition))
            active = parent and condition
        elif stripped == ".else" and stack:
            parent, condition = stack[-1]
            active = parent and not condition
        elif stripped == ".endif" and stack:
            parent, _ = stack.pop()
            active = parent
        elif active:
            selected.append(line)
    return "\n".join(selected)


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"manifest is missing or invalid: {exc}") from exc
    if not isinstance(value, dict):
        raise GateError("manifest must be a JSON object")
    return value


def validate_manifest_identity(artifact: Path, manifest_path: Path,
                               expected_sha256: str) -> dict[str, Any]:
    manifest = load_manifest(manifest_path)
    actual = sha256(artifact)
    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha256) or actual != expected_sha256:
        raise GateError(f"artifact identity mismatch: actual={actual}")
    if manifest.get("sha256") != actual or \
            artifact.resolve() != (manifest_path.parent / str(manifest.get("elf", ""))).resolve():
        raise GateError("manifest identity does not bind the selected unaligned-reference ELF")
    if manifest.get("schema") == PROBE_SCHEMA and \
            manifest.get("profile") == "unaligned-reference-probe":
        expected = {
            "guest_execution_authorized": "probe-only",
            "system_mode": "-forcesystem",
            "cpu_count": 1,
            "ivt_vector_offset": VECTOR_OFFSET,
            "fault_subtype": "unaligned-data-reference",
            "faulting_instruction": "ld8 r15=[r14]",
            "access_width": ACCESS_WIDTH,
            "buffer_address_offset": BUFFER_OFFSET,
            "psr_ac_enabled_for_faulting_access": True,
            "expected_faults": 1,
            "expected_continuations": 1,
            "post_rfi_capture_required": True,
            "all_other_vectors": "fail-stop",
        }
        if any(manifest.get(key) != value for key, value in expected.items()):
            raise GateError("Unaligned Reference probe manifest contract is incomplete")
    elif manifest.get("schema") == "guidexos-ia64-artifact-v1" and \
            manifest.get("profile") == "early-exec":
        expected = {
            "guest_execution_authorized": False,
            "unaligned_reference_static_gates": "PASS",
            "unaligned_reference_expected_faults": 1,
            "nat_consumption_static_gates": "PASS",
            "nat_consumption_expected_faults": 1,
            "expected_exception_faults": 5,
        }
        if any(manifest.get(key) != value for key, value in expected.items()):
            raise GateError("early-exec manifest does not retain the Unaligned Reference contract")
    else:
        raise GateError("manifest schema/profile is not a probe or early-exec artifact")
    return manifest


def parse_record_line(line: str) -> dict[str, int]:
    fields = {key: int(value, 16) for key, value in
              re.findall(r"([A-Za-z0-9_.]+)=(0x[0-9A-Fa-f]+)", line)}
    required = {
        "valid", "version", "size", "sequence", "vector", "subtype", "site", "IIP",
        "slot", "IPSR", "IFS", "IFS.valid", "IFA", "ISR", "ISR.ed", "ISR.ei",
        "ISR.ni", "ISR.sp", "ISR.r", "ISR.w", "handler.PSR", "handler.RSC",
        "handler.BSP", "PR", "pre.PSR", "pre.RSC", "pre.BSP", "pre.UNAT", "address",
        "buffer", "buffer.size", "width", "target", "return.IPSR", "handler.status",
        "completion", "count", "return", "post.PSR", "post.RSC", "post.BSP", "post.IVA",
        "post.UNAT", "pre.GR", "post.GR", "cleanup", "post.valid", "reserved",
    }
    missing = required - fields.keys()
    if missing:
        raise GateError("Unaligned Reference record is missing: " + ", ".join(sorted(missing)))
    return fields


def validate_record_fields(fields: dict[str, int], expected: dict[str, int]) -> list[str]:
    errors: list[str] = []
    fixed = {
        "valid": RECORD_VALID,
        "version": 1,
        "size": RECORD_SIZE,
        "sequence": 1,
        "vector": VECTOR_OFFSET,
        "subtype": 0,
        "site": expected["fault_site"],
        "IIP": expected["fault_site"],
        "slot": 0,
        "IFS.valid": 0,
        "IFA": expected["buffer"] + BUFFER_OFFSET,
        "ISR": ISR_READ,
        "ISR.ed": 0,
        "ISR.ei": 0,
        "ISR.ni": 0,
        "ISR.sp": 0,
        "ISR.r": 1,
        "ISR.w": 0,
        "handler.RSC": 3,
        "PR": 0x401,
        "pre.PSR": 0x2000,
        "pre.RSC": 3,
        "address": expected["buffer"] + BUFFER_OFFSET,
        "buffer": expected["buffer"],
        "buffer.size": 16,
        "width": ACCESS_WIDTH,
        "target": expected["continuation"],
        "handler.status": HANDLER_VALID,
        "completion": COMPLETION,
        "count": 1,
        "return": RETURN,
        "post.PSR": 0x2000,
        "post.RSC": 3,
        "post.IVA": expected["ivt"],
        "cleanup": CLEANUP,
        "post.valid": POST_RFI_VALID,
        "reserved": 0,
    }
    fixed.update({
        "handler.BSP": fields.get("pre.BSP", -1),
        "post.BSP": fields.get("pre.BSP", -1),
        "pre.UNAT": fields.get("post.UNAT", -1),
        "pre.GR": fields.get("post.GR", -1),
    })
    for key, value in fixed.items():
        if fields.get(key) != value:
            errors.append(f"record {key}={fields.get(key)!r}, expected {value:#x}")

    ipsr = fields.get("IPSR", 0)
    if ((ipsr >> 41) & 3) != 0 or ((ipsr >> 13) & 1) != 1 or \
            ((ipsr >> 14) & 1) != 0 or ((ipsr >> 3) & 1) != 1 or \
            ((ipsr >> 32) & 3) != 0 or ((ipsr >> 44) & 1) != 0 or \
            ((ipsr >> 46) & 1) != 0:
        errors.append("saved IPSR does not contain the authorized slot-0 AC-enabled fault state")
    return_ipsr = fields.get("return.IPSR", 0)
    if return_ipsr != (ipsr & ~(1 << 3)) or ((return_ipsr >> 3) & 1) != 0:
        errors.append("pre-return IPSR did not clear only PSR.ac")
    if (fields.get("handler.PSR", 0) & 0x6008) != 0:
        errors.append("handler-entry PSR did not clear i, ic, and ac")
    if (fields.get("post.PSR", 0) & 0x6008) != 0x2000:
        errors.append("immediate post-rfi PSR did not restore i=0, ic=1, ac=0")
    address = fields.get("address", 0)
    width = fields.get("width", 0)
    base = fields.get("buffer", 0)
    size = fields.get("buffer.size", 0)
    if width != ACCESS_WIDTH or address < base or address + width > base + size or \
            address + width < address or (address & (width - 1)) == 0:
        errors.append("IFA does not identify the precise misaligned address within the test buffer")
    bsp = fields.get("handler.BSP", 0)
    if not expected["rse_base"] <= bsp < expected["rse_top"] or \
            fields.get("post.BSP") != bsp or fields.get("pre.BSP") != bsp or \
            fields.get("post.RSC") != fields.get("handler.RSC") or \
            fields.get("pre.RSC") != fields.get("handler.RSC"):
        errors.append("RSE observations changed or lie outside the candidate backing store")
    if fields.get("post.GR") != fields.get("pre.GR"):
        errors.append("live GP/test-address checksum changed across the return")
    if fields.get("post.UNAT") != fields.get("pre.UNAT"):
        errors.append("UNAT changed across the controlled return")
    return errors


def validate_output(stdout_text: str, expected: dict[str, int], *,
                    probe: bool = False) -> tuple[dict[str, int] | None, list[str]]:
    errors: list[str] = []
    record_lines = [line for line in stdout_text.splitlines()
                    if line.startswith("IA64_UNALIGNED_REFERENCE_RECORD")]
    fields: dict[str, int] | None = None
    if len(record_lines) != 1:
        errors.append(f"expected exactly one Unaligned Reference record, found {len(record_lines)}")
    else:
        try:
            fields = parse_record_line(record_lines[0])
            errors.extend(validate_record_fields(fields, expected))
        except GateError as exc:
            errors.append(str(exc))

    pre_lines = [line for line in stdout_text.splitlines()
                 if line.startswith("IA64_UNALIGNED_REFERENCE_PRE_FAULT ")]
    if len(pre_lines) != 1 or not re.fullmatch(
            rf"IA64_UNALIGNED_REFERENCE_PRE_FAULT vector=0x5a00 "
            rf"subtype=unaligned-data-reference site=0x{expected['fault_site']:x} "
            rf"address=0x{expected['buffer'] + 1:x} width=0x8 "
            rf"target=0x{expected['continuation']:x} expected_faults=1",
            pre_lines[0].strip() if pre_lines else "", re.IGNORECASE):
        errors.append("pre-fault marker does not bind the authorized vector, site, address, width, and target")
    post_lines = [line for line in stdout_text.splitlines()
                  if line.startswith("IA64_UNALIGNED_REFERENCE_POST_RETURN ")]
    if len(post_lines) != 1 or not re.fullmatch(
            rf"IA64_UNALIGNED_REFERENCE_POST_RETURN target=0x{expected['continuation']:x} "
            r"count=0x1 PSR=0x2000 cleanup=0xbeefc0de55434c31",
            post_lines[0].strip() if post_lines else "", re.IGNORECASE):
        errors.append("post-return marker does not bind one safe continuation and completed AC cleanup")
    if stdout_text.count("IA64_UNALIGNED_REFERENCE_RESULT=PASS") != 1 or \
            "IA64_UNALIGNED_REFERENCE_RESULT=FAIL" in stdout_text:
        errors.append("guest Unaligned Reference result marker did not pass exactly once")
    validation_lines = [line for line in stdout_text.splitlines()
                        if line.startswith("IA64_UNALIGNED_REFERENCE_VALIDATION ")]
    if len(validation_lines) != 1 or not re.search(
            r"IA64_UNALIGNED_REFERENCE_VALIDATION errors=0x0 test=0x0\b", stdout_text):
        errors.append("guest validation did not report a zero error/test status")
    if probe and (stdout_text.count("IA64_UNALIGNED_REFERENCE_PROBE_RESULT=PASS") != 1 or
                  "IA64_UNALIGNED_REFERENCE_PROBE_RESULT=FAIL" in stdout_text):
        errors.append("independent probe did not emit its unique pass marker")
    return fields, errors


def exception_symbols(elf_path: Path) -> dict[str, int]:
    elf = ELF_VALIDATOR.Elf64(elf_path)
    names = {
        "ivt": "ia64_ivt",
        "vector": "ivt_unaligned_ref",
        "entry": "ia64_unaligned_reference_entry",
        "record": "ia64_unaligned_reference_record",
        "buffer": "ia64_unaligned_reference_buffer",
        "fault_site": "ia64_unaligned_reference_fault_site",
        "continuation": "ia64_unaligned_reference_continuation",
        "test": "ia64_unaligned_reference_test",
        "rse_base": "rse_backing_store_base",
        "rse_top": "rse_backing_store_top",
    }
    missing = [symbol for symbol in names.values() if symbol not in elf.symbols]
    if missing:
        raise GateError("Unaligned Reference ELF is missing symbols: " + ", ".join(missing))
    return {key: elf.symbols[value]["value"] for key, value in names.items()}


def _disassembly(elf_path: Path, objdump: Path) -> str:
    result = subprocess.run([str(objdump), "-dr", str(elf_path)], capture_output=True,
                            text=True, check=False)
    if result.returncode:
        raise GateError("target objdump failed: " + result.stderr.strip())
    return result.stdout


def validate_static(repo_root: Path, elf_path: Path, objdump: Path, *,
                    probe: bool = False) -> list[str]:
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
        errors.append("ELF IVT must be 32-KiB aligned and exactly 0x8000 bytes")

    try:
        symbols = exception_symbols(elf_path)
    except GateError as exc:
        return errors + [str(exc)]
    if symbols["vector"] != symbols["ivt"] + VECTOR_OFFSET:
        errors.append("Unaligned Reference vector stub is not at IVT offset 0x5a00")
    if symbols["record"] & 0xf:
        errors.append("versioned exception frame is not 16-byte aligned")
    record_symbol = elf.symbols["ia64_unaligned_reference_record"]
    if record_symbol["size"] != RECORD_SIZE:
        errors.append("exception frame symbol has the wrong bounded record size")
    buffer_symbol = elf.symbols["ia64_unaligned_reference_buffer"]
    if buffer_symbol["size"] != 16 or buffer_symbol["value"] & 0xf:
        errors.append("initialized test buffer must be exactly 16 bytes and naturally aligned")
    if elf.sections[elf.symbols["ia64_unaligned_reference_buffer"]["shndx"]]["name"] != ".data":
        errors.append("test access buffer is not in initialized writable data")

    ivt_source = (repo_root / "tests" / "ia64_unaligned_reference" / "entry.s"
                  if probe else repo_root / "kernel" / "arch" / "ia64" / "boot.s")
    handler_source = repo_root / "kernel" / "arch" / "ia64" / "unaligned_reference_entry.s"
    try:
        ivt_text = ivt_source.read_text(encoding="utf-8")
        handler_text = handler_source.read_text(encoding="utf-8")
    except OSError as exc:
        return errors + [f"IA-64 source contract is unavailable: {exc}"]
    if probe:
        vector_entries = re.findall(r"^IVT_(?:HALT|UNALIGNED)\s+\w+,\s*(0x[0-9a-fA-F]+)",
                                   ivt_text, re.MULTILINE)
        if len(vector_entries) != 68 or len({int(value, 16) for value in vector_entries}) != 68:
            errors.append("standalone probe source must retain all 68 distinct IVT slot offsets")
        if not re.search(r"IVT_UNALIGNED\s+ivt_unaligned_ref,\s*0x5a00", ivt_text):
            errors.append("standalone probe does not activate only the Unaligned Reference slot")
    else:
        raw_ivt_text = ivt_text
        ivt_text = early_exec_ivt_source(ivt_text)
        vector_entries = re.findall(
            r"^IVT_ENTRY_(?:HALT|BREAK|GENERAL_EXCEPTION|UNALIGNED_REFERENCE|NAT_CONSUMPTION)\s+\w+,\s*(0x[0-9a-fA-F]+)",
            ivt_text, re.MULTILINE)
        if len(vector_entries) != 68 or len({int(value, 16) for value in vector_entries}) != 68:
            errors.append("kernel source must retain all 68 distinct architectural IVT slots")
        if not re.search(r"\.ifdef GXOS_IA64_EARLY_EXEC_PROFILE\s+IVT_ENTRY_UNALIGNED_REFERENCE\s+"
                         r"ivt_unaligned_ref,\s*0x5a00.*?\.else\s+IVT_ENTRY_HALT\s+"
                         r"ivt_unaligned_ref,\s*0x5a00", raw_ivt_text, re.DOTALL):
            errors.append("kernel activates the vector outside the early-exec profile or fails to stop")

    stimulus = re.search(r"ia64_unaligned_reference_fault_site:\s*(.*?)"
                         r"ia64_unaligned_reference_continuation:", handler_text, re.DOTALL)
    continuation = re.search(r"ia64_unaligned_reference_continuation:\s*(.*?)"
                             r"\.endp ia64_unaligned_reference_test", handler_text, re.DOTALL)
    if stimulus is None or not re.search(r"ld8\s+r15\s*=\s*\[r14\]", stimulus.group(1)):
        errors.append("authorized fault site is not the single ordinary ld8 r15=[r14]")
    errors.extend(validate_ac_lifecycle_source(handler_text))

    try:
        disassembly = _disassembly(elf_path, objdump)
    except (OSError, GateError) as exc:
        return errors + [str(exc)]
    fault_line = re.search(
        rf"^\s*{symbols['fault_site']:x}:.*\[MII\].*\bld8 r15=\[r14\]",
        disassembly, re.MULTILINE | re.IGNORECASE)
    if fault_line is None:
        errors.append("linked fault-site disassembly does not match the documented ld8 encoding")
    target_line = re.search(
        rf"^\s*{symbols['continuation']:x}:.*\[MII\].*\bmov r15=psr",
        disassembly, re.MULTILINE | re.IGNORECASE)
    if target_line is None:
        errors.append("linked continuation does not begin with the immediate PSR capture")
    if not re.search(r"\brfi\b", disassembly):
        errors.append("linked handler has no architectural rfi")
    handler_start = symbols["entry"]
    handler_end = handler_start + elf.symbols["ia64_unaligned_reference_entry"]["size"]
    handler_lines = []
    for line in disassembly.splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+):", line)
        if match and handler_start <= int(match.group(1), 16) < handler_end:
            handler_lines.append(line)
    handler_region = "\n".join(handler_lines)
    if not handler_region or re.search(r"\b(?:br\.call|br\.ret|alloc)\b", handler_region):
        errors.append("handler uses a call, return, or register-stack allocation before rfi")
    if re.search(r"\bmov\s+b[0-7]\s*=", handler_region):
        errors.append("handler changes branch-register state")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=ROOT)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--expected-sha256")
    args = parser.parse_args()
    if args.manifest is not None or args.expected_sha256 is not None:
        if args.manifest is None or args.expected_sha256 is None:
            parser.error("--manifest and --expected-sha256 must be supplied together")
        validate_manifest_identity(args.elf, args.manifest, args.expected_sha256)
    errors = validate_static(args.repo_root, args.elf, args.objdump, probe=args.probe)
    if errors:
        for error in errors:
            print(f"[UNALIGNED_REFERENCE_GATE_FAIL] {error}")
        return 1
    print("[UNALIGNED_REFERENCE_GATE_PASS] vector=0x5a00 access=ld8 width=8 PSR.ac=bounded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Run only hash-bound IA-64 Ski smoke or explicitly authorized kernel ELFs."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from typing import Any

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SKI_PREFIX = Path("/home/guidex/opt/ski-dfc2902")
DEFAULT_LOADER_RECORD = ROOT / "docs" / "ia64-3r1-ski-loader-record.json"
QUALIFIED_SKI_COMMIT = "dfc2902ea1423d9b32543a5daf8026213f2b37a1"
QUALIFIED_SKI_VERSION = "1.5.1"
QUALIFIED_SKI_SHA256 = "ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45"
MAX_PA_BITS = 63  # Ski's documented and built default is -palen 63.
PT_LOAD = 1
PF_X = 1
PF_W = 2
EM_IA_64 = 50
ET_EXEC = 2
SMOKE_MARKER = "SKI_SYSTEM_MODE_SMOKE_PASS"
FAULT_MARKERS = (
    "exception", "fault @", " fault\n", "unallocated page", "illegal address",
    "privileged operation fault", "not an elf", "kernel not for this processor",
)


class GateError(ValueError):
    pass


def load_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-elf.py"
    spec = importlib.util.spec_from_file_location("ia64_elf_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load independent ELF validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ELF_VALIDATOR = load_validator()


def load_cpu_state_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-cpu-state.py"
    spec = importlib.util.spec_from_file_location("ia64_cpu_state_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load CPU-state checkpoint validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CPU_STATE_VALIDATOR = load_cpu_state_validator()


def load_general_exception_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-general-exception.py"
    spec = importlib.util.spec_from_file_location("ia64_general_exception_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load General Exception validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GENERAL_EXCEPTION_VALIDATOR = load_general_exception_validator()


def load_unaligned_reference_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-unaligned-reference.py"
    spec = importlib.util.spec_from_file_location("ia64_unaligned_reference_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load Unaligned Reference validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


UNALIGNED_REFERENCE_VALIDATOR = load_unaligned_reference_validator()


def load_nat_consumption_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-nat-consumption.py"
    spec = importlib.util.spec_from_file_location("ia64_nat_consumption_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load Register NaT Consumption validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


NAT_CONSUMPTION_VALIDATOR = load_nat_consumption_validator()


def load_stacked_nat_rse_validator() -> Any:
    path = ROOT / "scripts" / "validate-ia64-stacked-nat-rse.py"
    spec = importlib.util.spec_from_file_location("ia64_stacked_nat_rse_validator", path)
    if spec is None or spec.loader is None:
        raise GateError(f"cannot load stacked NaT/RSE validator: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


STACKED_NAT_RSE_VALIDATOR = load_stacked_nat_rse_validator()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def ranges_overlap(left: tuple[int, int], right: tuple[int, int]) -> bool:
    return left[0] < right[1] and right[0] < left[1]


@contextmanager
def verified_snapshot(source: Path, expected_sha256: str):
    """Run from a hash-checked private copy to close the validation/load race."""
    with tempfile.TemporaryDirectory(prefix="guidexos-ia64-ski-elf-") as temp:
        snapshot = Path(temp) / "selected.elf"
        shutil.copyfile(source, snapshot)
        actual = sha256(snapshot)
        if actual != expected_sha256:
            raise GateError(f"artifact changed while taking execution snapshot: actual={actual}")
        snapshot.chmod(0o444)
        yield snapshot


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"{label} is missing or invalid: {exc}") from exc
    if not isinstance(value, dict):
        raise GateError(f"{label} must be a JSON object")
    return value


def check_manifest(artifact: Path, manifest_path: Path, profile: str,
                   expected_sha256: str, schema: str = "guidexos-ia64-artifact-v1") -> dict[str, Any]:
    manifest = read_json(manifest_path, "artifact manifest")
    actual = sha256(artifact)
    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha256) or actual != expected_sha256:
        raise GateError(f"artifact SHA-256 mismatch: actual={actual}")
    if manifest.get("schema") != schema:
        raise GateError(f"artifact manifest schema must be {schema}")
    if manifest.get("profile") != profile:
        raise GateError(f"declared profile {profile!r} does not match the manifest")
    if manifest.get("sha256") != actual:
        raise GateError("manifest SHA-256 does not match the selected ELF")
    elf_name = manifest.get("elf")
    if not isinstance(elf_name, str) or not elf_name:
        raise GateError("manifest does not declare an ELF path")
    if artifact.resolve() != (manifest_path.parent / elf_name).resolve():
        raise GateError("the manifest path does not identify the selected ELF")
    return manifest


def validate_direct_system_elf(artifact: Path) -> list[dict[str, int]]:
    """Check exactly the ranges Ski's system ELF loader will copy by p_vaddr."""
    try:
        elf = ELF_VALIDATOR.Elf64(artifact)
    except (OSError, ELF_VALIDATOR.InvalidElf) as exc:
        raise GateError(f"invalid IA-64 ELF: {exc}") from exc
    if elf.machine != EM_IA_64 or elf.type != ET_EXEC:
        raise GateError("direct Ski loading requires ELF64 EM_IA_64 ET_EXEC")
    if elf.data[5] != 1:
        raise GateError("direct Ski loading requires little-endian ELF")

    loads: list[dict[str, int]] = []
    pa_limit = 1 << MAX_PA_BITS
    for index, header in enumerate(elf.programs):
        if header["type"] != PT_LOAD:
            continue
        p_offset = header["offset"]
        vaddr = header["vaddr"]
        paddr = header["paddr"]
        filesz = header["filesz"]
        memsz = header["memsz"]
        align = header["align"]
        flags = header["flags"]
        if memsz == 0 or filesz > memsz:
            raise GateError(f"PT_LOAD[{index}] has invalid filesz/memsz")
        if p_offset + filesz > len(elf.data):
            raise GateError(f"PT_LOAD[{index}] file bytes exceed the ELF image")
        if vaddr != paddr:
            raise GateError(f"PT_LOAD[{index}] p_vaddr/p_paddr differ; Ski loads by p_vaddr")
        if vaddr >= pa_limit or vaddr + memsz > pa_limit:
            raise GateError(f"PT_LOAD[{index}] is outside Ski's -palen 63 address range")
        if align > 1 and (align & (align - 1) or vaddr % align != p_offset % align):
            raise GateError(f"PT_LOAD[{index}] has invalid ELF alignment")
        if flags & PF_W and flags & PF_X:
            raise GateError(f"PT_LOAD[{index}] is writable and executable")
        loads.append({"start": vaddr, "end": vaddr + memsz,
                      "filesz": filesz, "memsz": memsz,
                      "flags": flags, "index": index})

    if not loads:
        raise GateError("ELF has no PT_LOAD segments")
    loads.sort(key=lambda segment: (segment["start"], segment["end"]))
    for previous, current in zip(loads, loads[1:]):
        if ranges_overlap((previous["start"], previous["end"]),
                          (current["start"], current["end"])):
            raise GateError(f"PT_LOAD ranges overlap: [{previous['start']:#x},{previous['end']:#x}) "
                            f"and [{current['start']:#x},{current['end']:#x})")
    if not any(segment["flags"] & PF_X and
               segment["start"] <= elf.entry < segment["end"] for segment in loads):
        raise GateError("ELF entry is not inside an executable PT_LOAD segment")
    for segment in loads:
        print(f"PT_LOAD[{segment['index']}]=[0x{segment['start']:x},0x{segment['end']:x}) "
              f"filesz=0x{segment['filesz']:x} memsz=0x{segment['memsz']:x} "
              f"flags=0x{segment['flags']:x}")
    return loads


def validate_break_probe_elf(artifact: Path) -> dict[str, int]:
    """Check the diagnostic's IVT and exact return-test symbols before Ski loads it."""
    try:
        elf = ELF_VALIDATOR.Elf64(artifact)
    except (OSError, ELF_VALIDATOR.InvalidElf) as exc:
        raise GateError(f"invalid Break probe ELF: {exc}") from exc
    ivt = elf.section(".ivt")
    required = ("ia64_ivt", "ia64_ivt_end", "ivt_break",
                "ia64_break_exception_entry", "ia64_break_exception_test",
                "ia64_break_slot1_bundle", "ia64_break_slot2_bundle",
                "ia64_break_slot1_post_rfi_capture", "ia64_break_slot2_continuation",
                "rse_backing_store_base", "rse_backing_store_top")
    missing = [name for name in required if name not in elf.symbols]
    if missing:
        raise GateError(f"Break probe ELF lacks required symbols: {', '.join(missing)}")
    base = elf.symbols["ia64_ivt"]["value"]
    end = elf.symbols["ia64_ivt_end"]["value"]
    vector = elf.symbols["ivt_break"]["value"]
    if ivt is None or ivt["size"] != 0x8000 or ivt["addr"] % 0x8000 or \
            ivt["align"] < 0x8000 or base != ivt["addr"] or end != base + 0x8000 or \
            vector != base + 0x2c00:
        raise GateError("Break probe IVT geometry or 0x2c00 vector address is invalid")
    return {"ivt": base,
            "break_slot1": elf.symbols["ia64_break_slot1_bundle"]["value"],
            "break_slot2": elf.symbols["ia64_break_slot2_bundle"]["value"],
            "post_rfi_slot1_continuation": elf.symbols[
                "ia64_break_slot1_post_rfi_capture"]["value"],
            "post_rfi_slot2_continuation": elf.symbols[
                "ia64_break_slot2_continuation"]["value"],
            "rse_base": elf.symbols["rse_backing_store_base"]["value"],
            "rse_top": elf.symbols["rse_backing_store_top"]["value"]}


def extract_ski_counts(stdout_text: str, stderr_text: str, counts_path: Path) -> tuple[int | None, int | None]:
    combined = stdout_text + "\n" + stderr_text
    fault_values = [int(value) for value in re.findall(r"\((\d+) faults?\)", combined, re.IGNORECASE)]
    instruction_values = [int(value) for value in re.findall(r"\b(\d+) insts\b", combined, re.IGNORECASE)]
    if counts_path.is_file():
        counts_text = counts_path.read_text(encoding="utf-8", errors="replace")
        fault_values += [int(value, 16) for value in
                         re.findall(r"(?m)^ski_\$faults\$\s+\d+\s+([0-9a-fA-F]+)\s*$", counts_text)]
        instruction_values += [int(value, 16) for value in
                               re.findall(r"(?m)^ski_\$insts\$\s+\d+\s+([0-9a-fA-F]+)\s*$", counts_text)]
    faults = fault_values[-1] if fault_values else None
    instructions = instruction_values[-1] if instruction_values else None
    return faults, instructions


def parse_break_post_rfi_psr_observations(
        stdout_text: str, contract: dict[str, Any]) -> tuple[dict[str, dict[str, int]], list[str]]:
    """Parse and attest both immediate post-rfi capture records."""
    marker = "IA64_BREAK_POST_RFI_PSR"
    lines = [line for line in stdout_text.splitlines() if line.startswith(marker)]
    errors: list[str] = []
    observations: dict[str, dict[str, int]] = {}
    if len(lines) != 2:
        errors.append("expected exactly two direct post-rfi PSR observations")
    expected_records = (
        ("slot1", 0x6a17, contract["post_rfi_slot1_continuation"], 2, 1,
         0xBEEFC0DE4B505231),
        ("slot2", 0x6a18, contract["post_rfi_slot2_continuation"], 0, 2,
         0xBEEFC0DE4B505232),
    )
    for index, line in enumerate(lines):
        match = re.fullmatch(
            r"IA64_BREAK_POST_RFI_PSR slot=(slot1|slot2) "
            r"immediate=(0x[0-9A-Fa-f]+) continuation=(0x[0-9A-Fa-f]+) "
            r"return_slot=(0x[0-9A-Fa-f]+) order=(0x[0-9A-Fa-f]+) "
            r"valid=(0x[0-9A-Fa-f]+) PSR=(0x[0-9A-Fa-f]+)",
            line.strip())
        if not match:
            errors.append("malformed direct post-rfi PSR record")
            continue
        slot, *raw_values = match.groups()
        if slot in observations:
            errors.append(f"duplicate direct post-rfi PSR record for {slot}")
            continue
        immediate, continuation, return_slot, order, valid, psr = (
            int(value, 16) for value in raw_values)
        observations[slot] = {
            "immediate": immediate,
            "continuation": continuation,
            "return_slot": return_slot,
            "order": order,
            "valid": valid,
            "PSR": psr,
        }
        if index < len(expected_records):
            expected_slot, expected_immediate, expected_continuation, expected_return_slot, \
                expected_order, expected_valid = expected_records[index]
            if slot != expected_slot:
                errors.append("post-rfi capture order contradicts the instruction path")
            expected_fields = {
                "immediate": expected_immediate,
                "continuation": expected_continuation,
                "return_slot": expected_return_slot,
                "order": expected_order,
                "valid": expected_valid,
            }
            for field, expected in expected_fields.items():
                actual = observations[slot][field]
                if actual != expected:
                    errors.append(f"{slot} post-rfi {field}={actual:#x}, expected {expected:#x}")
        if ((psr >> 14) & 1) != 0 or ((psr >> 13) & 1) != 1:
            errors.append(f"{slot} post-rfi PSR did not restore i=0 and ic=1")
    missing = {"slot1", "slot2"} - observations.keys()
    if missing:
        errors.append("missing direct post-rfi PSR observation(s): " + ", ".join(sorted(missing)))
    return observations, errors


def validate_exception_return_state(saved_ipsr: int, expected_slot: int,
                                    post_rfi_psr: int | None = None) -> list[str]:
    """Check incoming IPSR and, when available, direct state immediately after rfi."""
    errors: list[str] = []
    if ((saved_ipsr >> 41) & 3) != expected_slot or \
            ((saved_ipsr >> 13) & 1) != 1 or ((saved_ipsr >> 14) & 1) != 0 or \
            ((saved_ipsr >> 44) & 1) != 1 or ((saved_ipsr >> 32) & 3) != 0 or \
            ((saved_ipsr >> 46) & 1) != 0:
        errors.append("saved IPSR does not match the expected faulting state")
    if post_rfi_psr is not None and \
            (((post_rfi_psr >> 14) & 1) != 0 or ((post_rfi_psr >> 13) & 1) != 1):
        errors.append("post-rfi PSR did not restore i=0 and ic=1")
    return errors


def validate_break_output(stdout_text: str, contract: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    post_rfi_observations, post_rfi_errors = parse_break_post_rfi_psr_observations(
        stdout_text, contract)
    errors.extend(post_rfi_errors)
    pass_marker = contract["pass_marker"]
    fail_marker = contract["fail_marker"]
    if stdout_text.count(pass_marker) != 1:
        errors.append(f"expected exactly one {pass_marker}")
    if fail_marker in stdout_text:
        errors.append(f"guest emitted {fail_marker}")
    validation_marker = contract["validation_marker"]
    validation_lines = [line for line in stdout_text.splitlines()
                        if line.startswith(validation_marker)]
    if len(validation_lines) != 1 or not re.search(
            re.escape(validation_marker) + r"\s+errors=0x0\s+test=0x0\b", stdout_text):
        errors.append("guest-side exception record validation did not report zero errors and test status")

    prefix = contract["record_prefix"]
    expected_records = (
        ("slot1", 0x6a17, contract["break_slot1"], 1,
         0xBEEFC0DE4B000001, 0xBEEFC0DE4B100001),
        ("slot2", 0x6a18, contract["break_slot2"], 2,
         0xBEEFC0DE4B000002, 0xBEEFC0DE4B100002),
    )
    for name, immediate, iip, slot, completion, returned in expected_records:
        lines = [line for line in stdout_text.splitlines()
                 if line.startswith(f"{prefix} {name} ")]
        if len(lines) != 1:
            if not lines:
                errors.append(f"missing {prefix} {name} record output")
            else:
                errors.append(f"duplicate {prefix} {name} record output")
            continue
        line = lines[0]
        if not line:
            errors.append(f"missing {prefix} {name} record output")
            continue
        fields = {key: int(value, 16) for key, value in
                  re.findall(r"([A-Za-z.]+)=0x([0-9A-Fa-f]+)", line)}
        expected = {
            "valid": 0xBEEFC0DE4B56414C, "vector": 0x2c00,
            "immediate": immediate, "IIP": iip, "slot": slot,
            "IFS.valid": 0, "completion": completion,
            "return": returned, "count": 1, "PR": 0x401,
        }
        for field, value in expected.items():
            if fields.get(field) != value:
                errors.append(f"{name} record {field}={fields.get(field)!r}, expected {value:#x}")
        ipsr = fields.get("IPSR")
        isr = fields.get("ISR")
        handler_psr = fields.get("handler.PSR")
        rsc = fields.get("RSC")
        bsp = fields.get("BSP")
        if ipsr is None:
            errors.append(f"{name} saved IPSR does not match the expected faulting state")
        else:
            for state_error in validate_exception_return_state(
                    ipsr, slot,
                    post_rfi_observations.get(name, {}).get("PSR")):
                errors.append(f"{name} {state_error}")
        if isr is None or ((isr >> 41) & 7) != slot or (isr & (1 << 38)) or (isr & 0xffff):
            errors.append(f"{name} saved ISR does not match the expected faulting slot")
        if handler_psr is None or handler_psr & 0x6000:
            errors.append(f"{name} handler-entry PSR was not masked with IC clear")
        if rsc != 3:
            errors.append(f"{name} handler-entry RSC was not 3")
        if bsp is None or not (contract["rse_base"] <= bsp <= contract["rse_top"]):
            errors.append(f"{name} handler-entry BSP lies outside the diagnostic RSE range")

    # The break records are the only allowed expected faults. Ski's statistics
    # must expose the exact count; marker text by itself is insufficient.
    generic_fault_lines = []
    for line in (stdout_text + "\n" + contract.get("stderr_text", "")).splitlines():
        if line.startswith("IA64_BREAK_") or \
                (contract.get("general_exception_contract") is not None and
                 line.startswith("IA64_GENERAL_EXCEPTION_")) or \
                (contract.get("unaligned_reference_contract") is not None and
                 line.startswith("IA64_UNALIGNED_REFERENCE_")) or \
                (contract.get("nat_consumption_contract") is not None and
                 line.startswith("IA64_NAT_CONSUMPTION_")):
            continue
        lowered = line.lower()
        if any(marker in lowered for marker in FAULT_MARKERS):
            generic_fault_lines.append(line)
    if generic_fault_lines:
        errors.append("unexpected Ski fault/exception diagnostic: " + generic_fault_lines[0][:180])
    return errors


def verify_ski(prefix: Path, expected_hash: str) -> Path:
    bindir = prefix / "bin"
    paths = {name: bindir / name for name in ("ski", "bski", "bskinc")}
    for name, path in paths.items():
        if not path.exists() or not path.is_file():
            raise GateError(f"verified Ski executable is missing: {path}")
    resolved = {name: path.resolve() for name, path in paths.items()}
    if len(set(resolved.values())) != 1:
        raise GateError("ski, bski, and bskinc do not resolve to the same installed simulator")
    simulator = resolved["ski"]
    actual = sha256(simulator)
    if actual != expected_hash:
        raise GateError(f"installed Ski SHA-256 mismatch: actual={actual}")
    if not os.access(paths["bskinc"], os.X_OK):
        raise GateError("installed bskinc is not executable")
    return paths["bskinc"]


def verify_loader_record(path: Path, artifact_hash: str, profile: str,
                         simulator_hash: str) -> dict[str, Any]:
    record = read_json(path, "loader compatibility record")
    if record.get("schema") != "guidexos-ia64-ski-loader-v1":
        raise GateError("loader compatibility record has an unsupported schema")
    if record.get("outcome") != "SKI_LOADER_READY":
        raise GateError("loader compatibility record is not SKI_LOADER_READY")
    if record.get("loader_kind") != "ski-direct-system-elf":
        raise GateError("loader record does not qualify Ski's direct system-mode ELF path")
    if record.get("kernel_sha256") != artifact_hash or record.get("kernel_profile") != profile:
        raise GateError("loader evidence does not bind this kernel hash and profile")
    if record.get("ski_sha256") != simulator_hash:
        raise GateError("loader evidence does not bind this Ski executable hash")
    ski_source = record.get("ski_source")
    if not isinstance(ski_source, dict) or \
            ski_source.get("commit") != QUALIFIED_SKI_COMMIT or \
            ski_source.get("source_tree_clean") is not True or \
            record.get("ski_version") != QUALIFIED_SKI_VERSION:
        raise GateError("loader evidence does not bind the pinned clean Ski source/version")
    if record.get("system_mode_smoke") != "PASS" or record.get("single_cpu_contract") != "SOURCE_ENFORCED_ONE_CPU":
        raise GateError("loader evidence lacks a passing system-mode smoke or one-CPU contract")
    direct_loader = record.get("direct_loader")
    if not isinstance(direct_loader, dict) or \
            direct_loader.get("uses_system_mode_option") != "-forcesystem" or \
            direct_loader.get("candidate_paddr_equals_vaddr") is not True or \
            direct_loader.get("guest_resident_loader_ranges") != []:
        raise GateError("loader record lacks the qualified direct system-ELF contract")
    if record.get("kernel_execution_authorized") is not False:
        raise GateError("loader record must remain separate from guest-execution authorization")
    return record


def _process_group_exists(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def _kill_process_group(process: subprocess.Popen[bytes], grace_seconds: float = 1.0) -> bool:
    pgid = process.pid  # start_new_session=True makes the child the group leader.
    try:
        os.killpg(pgid, signal.SIGTERM)
    except ProcessLookupError:
        return process.poll() is not None

    if process.poll() is None:
        try:
            process.wait(timeout=grace_seconds)
        except subprocess.TimeoutExpired:
            pass

    deadline = time.monotonic() + grace_seconds
    while _process_group_exists(pgid) and time.monotonic() < deadline:
        time.sleep(0.05)

    if _process_group_exists(pgid):
        try:
            os.killpg(pgid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    if process.poll() is None:
        process.wait()
    deadline = time.monotonic() + grace_seconds
    while _process_group_exists(pgid) and time.monotonic() < deadline:
        time.sleep(0.05)
    return process.poll() is not None and not _process_group_exists(pgid)


def _terminal_mode(fd: int) -> dict[str, bool | int]:
    """Return only the terminal settings relevant to Ski's SSC console path."""
    import termios

    attributes = termios.tcgetattr(fd)
    control_chars = attributes[6]
    vmin = control_chars[termios.VMIN]
    vtime = control_chars[termios.VTIME]
    return {
        "canonical": bool(attributes[3] & termios.ICANON),
        "echo": bool(attributes[3] & termios.ECHO),
        "signals": bool(attributes[3] & termios.ISIG),
        "vmin": int(vmin if isinstance(vmin, int) else vmin[0]),
        "vtime": int(vtime if isinstance(vtime, int) else vtime[0]),
    }


def _observe_child_pty_stdin(process: subprocess.Popen[bytes], slave_fd: int) -> dict[str, Any]:
    """Attest the launched process's fd 0 against the PTY slave passed by the runner."""
    slave_path = os.ttyname(slave_fd)
    slave_stat = os.fstat(slave_fd)
    parent_is_tty = os.isatty(slave_fd)
    child_fd_path = Path(f"/proc/{process.pid}/fd/0")
    child_stat_path = Path(f"/proc/{process.pid}/stat")
    target = None
    child_is_character_device = False
    same_device = False
    is_dev_null = False
    controlling_tty_number = None
    observation_error = None
    try:
        target = os.readlink(child_fd_path)
        child_stat = os.stat(child_fd_path)
        child_is_character_device = stat.S_ISCHR(child_stat.st_mode)
        same_device = child_is_character_device and child_stat.st_rdev == slave_stat.st_rdev
        is_dev_null = child_is_character_device and \
            child_stat.st_rdev == os.stat(os.devnull).st_rdev
        # /proc/PID/stat field 7 is tty_nr. A zero value means the process has
        # no controlling terminal; Ski's no-console SSC path only needs tty
        # ioctls on fd 0, which do not require a controlling terminal.
        stat_fields = child_stat_path.read_text(encoding="ascii").rsplit(")", 1)[1].split()
        controlling_tty_number = int(stat_fields[4])
    except (OSError, IndexError, ValueError) as exc:
        observation_error = f"{type(exc).__name__}: {exc}"
    return {
        "parent_slave_path": slave_path,
        "parent_slave_is_tty": parent_is_tty,
        "parent_slave_rdev": slave_stat.st_rdev,
        "child_fd0_path": str(child_fd_path),
        "child_fd0_target": target,
        "child_fd0_is_character_device": child_is_character_device,
        "child_fd0_same_device_as_slave": same_device,
        "child_fd0_is_dev_null": is_dev_null,
        "child_has_controlling_terminal": (
            controlling_tty_number != 0 if controlling_tty_number is not None else None),
        "child_tty_number": controlling_tty_number,
        "observation_error": observation_error,
        "passed": parent_is_tty and same_device and not is_dev_null and
                  controlling_tty_number is not None,
    }


def run_simulator(command: list[str], timeout: float, capture_dir: Path,
                  label: str, success_marker: str | None = None,
                  artifact_hash: str | None = None,
                  checkpoint_expectations: dict[str, int] | None = None,
                  break_exception_contract: dict[str, Any] | None = None,
                  general_exception_contract: dict[str, Any] | None = None,
                  unaligned_reference_contract: dict[str, Any] | None = None,
                  nat_consumption_contract: dict[str, Any] | None = None,
                  resource_limits: dict[str, int] | None = None,
                  stdin_pty: bool = False) -> int:
    if timeout <= 0 or timeout > 300:
        raise GateError("timeout must be greater than zero and no more than 300 seconds")
    if capture_dir.exists():
        if not capture_dir.is_dir() or any(capture_dir.iterdir()):
            raise GateError(f"capture directory must be absent or empty: {capture_dir}")
    else:
        capture_dir.mkdir(parents=True)

    stdout_path = capture_dir / "stdout.log"
    stderr_path = capture_dir / "stderr.log"
    counts_path = capture_dir / "instruction-counts.txt"
    command = command.copy()
    command[1:1] = ["-stats", "-icnt", str(counts_path)]
    preexec_fn = None
    if resource_limits is not None:
        address_space_bytes = resource_limits["address_space_bytes"]
        cpu_soft_seconds = resource_limits["cpu_soft_seconds"]
        cpu_hard_seconds = resource_limits["cpu_hard_seconds"]
        if address_space_bytes <= 0 or cpu_soft_seconds <= 0 or \
                cpu_hard_seconds < cpu_soft_seconds:
            raise GateError("invalid Ski process resource limits")

        def apply_resource_limits() -> None:
            import resource
            resource.setrlimit(resource.RLIMIT_AS,
                               (address_space_bytes, address_space_bytes))
            resource.setrlimit(resource.RLIMIT_CPU,
                               (cpu_soft_seconds, cpu_hard_seconds))

        preexec_fn = apply_resource_limits
    started = time.monotonic()
    timed_out = False
    stdin_master = None
    stdin_slave = None
    pty_stdin_observation = None
    pty_termios_before = None
    pty_termios_after = None
    pty_verification_error = None
    try:
        if stdin_pty:
            import pty
            stdin_master, stdin_slave = pty.openpty()
            pty_termios_before = _terminal_mode(stdin_slave)
            stdin_value: int | Any = stdin_slave
        else:
            stdin_value = subprocess.DEVNULL
        with stdout_path.open("xb") as stdout_file, stderr_path.open("xb") as stderr_file:
            process = subprocess.Popen(command, stdin=stdin_value,
                                       stdout=stdout_file, stderr=stderr_file,
                                       start_new_session=True, close_fds=True,
                                       preexec_fn=preexec_fn)
            if stdin_slave is not None:
                pty_stdin_observation = _observe_child_pty_stdin(process, stdin_slave)
                if not pty_stdin_observation["passed"]:
                    pty_verification_error = "launched process fd 0 did not attest as the PTY slave"
                    process_group_cleanup_complete = _kill_process_group(process)
                else:
                    try:
                        process.wait(timeout=timeout)
                    except subprocess.TimeoutExpired:
                        timed_out = True
                    finally:
                        process_group_cleanup_complete = _kill_process_group(process)
            else:
                try:
                    process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    timed_out = True
                finally:
                    process_group_cleanup_complete = _kill_process_group(process)
    finally:
        if stdin_slave is not None:
            try:
                pty_termios_after = _terminal_mode(stdin_slave)
            except OSError:
                pty_termios_after = None
        if stdin_slave is not None:
            os.close(stdin_slave)
        if stdin_master is not None:
            os.close(stdin_master)
    elapsed = time.monotonic() - started
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    combined = (stdout_text + "\n" + stderr_text).lower()
    return_code = process.returncode
    termination_signal = None
    if return_code is not None and return_code < 0:
        try:
            termination_signal = signal.Signals(-return_code).name
        except ValueError:
            termination_signal = f"SIGNAL_{-return_code}"
    fault_count, instruction_count = extract_ski_counts(stdout_text, stderr_text, counts_path)
    break_evidence_errors: list[str] = []
    general_evidence_errors: list[str] = []
    general_record_fields: dict[str, int] | None = None
    unaligned_reference_evidence_errors: list[str] = []
    unaligned_reference_record_fields: dict[str, int] | None = None
    nat_consumption_evidence_errors: list[str] = []
    nat_consumption_record_fields: dict[str, int] | None = None
    expected_exception_faults = (
        (break_exception_contract["expected_faults"] if break_exception_contract else 0) +
        (general_exception_contract["expected_faults"] if general_exception_contract else 0) +
        (unaligned_reference_contract["expected_faults"]
         if unaligned_reference_contract else 0) +
        (nat_consumption_contract["expected_faults"]
         if nat_consumption_contract else 0))

    if pty_verification_error is not None:
        classification = "PTY_STDIN_VERIFICATION_FAIL"
    elif timed_out:
        classification = "TIMEOUT"
    elif break_exception_contract is not None or general_exception_contract is not None or \
            unaligned_reference_contract is not None or nat_consumption_contract is not None:
        if break_exception_contract is not None:
            output_contract = dict(break_exception_contract)
            output_contract["stderr_text"] = stderr_text
            output_contract["general_exception_contract"] = general_exception_contract
            output_contract["unaligned_reference_contract"] = unaligned_reference_contract
            output_contract["nat_consumption_contract"] = nat_consumption_contract
            break_evidence_errors = validate_break_output(stdout_text, output_contract)
        if general_exception_contract is not None:
            general_record_fields, general_evidence_errors = \
                GENERAL_EXCEPTION_VALIDATOR.validate_output(
                    stdout_text, general_exception_contract["symbols"],
                    probe=general_exception_contract["probe"])
            generic_fault_lines = []
            for line in (stdout_text + "\n" + stderr_text).splitlines():
                if line.startswith("IA64_GENERAL_EXCEPTION_") or \
                        line.startswith("IA64_UNALIGNED_REFERENCE_") or \
                    line.startswith("IA64_NAT_CONSUMPTION_") or \
                        (break_exception_contract is not None and line.startswith("IA64_BREAK_")):
                    continue
                lowered = line.lower()
                if any(marker in lowered for marker in FAULT_MARKERS):
                    generic_fault_lines.append(line)
            if generic_fault_lines:
                general_evidence_errors.append(
                    "unexpected Ski fault/exception diagnostic: " + generic_fault_lines[0][:180])
            if general_exception_contract.get("require_intentional_exit") and \
                    "kernel exited with status 0" not in combined and \
                    "processor 0 exited with status 0" not in combined:
                general_evidence_errors.append(
                    "Ski did not report the intentional SSC exit after the General Exception test")
        if unaligned_reference_contract is not None:
            unaligned_reference_record_fields, unaligned_reference_evidence_errors = \
                UNALIGNED_REFERENCE_VALIDATOR.validate_output(
                    stdout_text, unaligned_reference_contract["symbols"],
                    probe=unaligned_reference_contract["probe"])
            generic_fault_lines = []
            for line in (stdout_text + "\n" + stderr_text).splitlines():
                if line.startswith("IA64_UNALIGNED_REFERENCE_") or \
                        line.startswith("IA64_NAT_CONSUMPTION_") or \
                        (break_exception_contract is not None and line.startswith("IA64_BREAK_")) or \
                        (general_exception_contract is not None and
                         line.startswith("IA64_GENERAL_EXCEPTION_")):
                    continue
                lowered = line.lower()
                if any(marker in lowered for marker in FAULT_MARKERS):
                    generic_fault_lines.append(line)
            if generic_fault_lines:
                unaligned_reference_evidence_errors.append(
                    "unexpected Ski fault/exception diagnostic: " + generic_fault_lines[0][:180])
            if unaligned_reference_contract.get("require_intentional_exit") and \
                    "kernel exited with status 0" not in combined and \
                    "processor 0 exited with status 0" not in combined:
                unaligned_reference_evidence_errors.append(
                    "Ski did not report the intentional SSC exit after the Unaligned Reference test")
        if nat_consumption_contract is not None:
            nat_consumption_record_fields, nat_consumption_evidence_errors = \
                NAT_CONSUMPTION_VALIDATOR.validate_output(
                    stdout_text, nat_consumption_contract["symbols"],
                    probe=nat_consumption_contract["probe"])
            generic_fault_lines = []
            for line in (stdout_text + "\n" + stderr_text).splitlines():
                if line.startswith("IA64_NAT_CONSUMPTION_") or \
                        line.startswith("IA64_UNALIGNED_REFERENCE_") or \
                        (break_exception_contract is not None and line.startswith("IA64_BREAK_")) or \
                        (general_exception_contract is not None and
                         line.startswith("IA64_GENERAL_EXCEPTION_")):
                    continue
                lowered = line.lower()
                if any(marker in lowered for marker in FAULT_MARKERS):
                    generic_fault_lines.append(line)
            if generic_fault_lines:
                nat_consumption_evidence_errors.append(
                    "unexpected Ski fault/exception diagnostic: " + generic_fault_lines[0][:180])
            if nat_consumption_contract.get("require_intentional_exit") and \
                    "kernel exited with status 0" not in combined and \
                    "processor 0 exited with status 0" not in combined:
                nat_consumption_evidence_errors.append(
                    "Ski did not report the intentional SSC exit after the Register NaT test")
        failure_classification = (
            "NAT_CONSUMPTION_QUALIFICATION_FAIL"
            if nat_consumption_contract is not None else
            "UNALIGNED_REFERENCE_QUALIFICATION_FAIL"
            if unaligned_reference_contract is not None else
            "GENERAL_EXCEPTION_QUALIFICATION_FAIL"
            if general_exception_contract is not None else
            "BREAK_EXCEPTION_QUALIFICATION_FAIL")
        if return_code != 0:
            classification = "SIMULATOR_ERROR"
        elif fault_count != expected_exception_faults:
            message = f"Ski fault count={fault_count!r}, expected {expected_exception_faults}"
            if break_exception_contract is not None:
                break_evidence_errors.append(message)
            if general_exception_contract is not None:
                general_evidence_errors.append(message)
            if unaligned_reference_contract is not None:
                unaligned_reference_evidence_errors.extend(
                    UNALIGNED_REFERENCE_VALIDATOR.validate_expected_fault_count(
                        fault_count, expected_exception_faults))
            if nat_consumption_contract is not None:
                nat_consumption_evidence_errors.extend(
                    NAT_CONSUMPTION_VALIDATOR.validate_expected_fault_count(
                        fault_count, expected_exception_faults))
            classification = failure_classification
        elif instruction_count is None:
            message = "Ski instruction count was not present in captured statistics"
            if break_exception_contract is not None:
                break_evidence_errors.append(message)
            if general_exception_contract is not None:
                general_evidence_errors.append(message)
            if unaligned_reference_contract is not None:
                unaligned_reference_evidence_errors.append(message)
            if nat_consumption_contract is not None:
                nat_consumption_evidence_errors.append(message)
            classification = failure_classification
        elif process.poll() is None:
            message = "simulator process group was not reaped"
            if break_exception_contract is not None:
                break_evidence_errors.append(message)
            if general_exception_contract is not None:
                general_evidence_errors.append(message)
            if unaligned_reference_contract is not None:
                unaligned_reference_evidence_errors.append(message)
            if nat_consumption_contract is not None:
                nat_consumption_evidence_errors.append(message)
            classification = failure_classification
        elif (break_evidence_errors or general_evidence_errors or
              unaligned_reference_evidence_errors or nat_consumption_evidence_errors):
            classification = failure_classification
        else:
            classification = (
                nat_consumption_contract["success_classification"]
                if nat_consumption_contract is not None else
                unaligned_reference_contract["success_classification"]
                if unaligned_reference_contract is not None else
                general_exception_contract["success_classification"]
                if general_exception_contract is not None else
                break_exception_contract["success_classification"])
    elif any(marker in combined for marker in FAULT_MARKERS):
        classification = "GUEST_FAULT"
    elif return_code != 0:
        classification = "SIMULATOR_ERROR"
    elif success_marker and success_marker not in stdout_text:
        classification = "SMOKE_MARKER_MISSING"
    elif success_marker:
        classification = "SMOKE_PASS"
    elif "kernel exited with status 0" in combined or "processor 0 exited with status 0" in combined:
        classification = "GUEST_EXIT_ZERO"
    else:
        classification = "SIMULATOR_EXIT_UNCLASSIFIED"

    checkpoint_records = None
    checkpoint_errors: list[str] = []
    if checkpoint_expectations is not None:
        checkpoint_records, checkpoint_errors = CPU_STATE_VALIDATOR.validate_checkpoint_output(
            stdout_text,
            ivt=checkpoint_expectations["ivt"],
            rse_base=checkpoint_expectations["rse_base"],
            rse_top=checkpoint_expectations["rse_top"],
        )
        if classification in ("GUEST_EXIT_ZERO", "BREAK_EXCEPTION_KERNEL_PASS",
                              "GENERAL_EXCEPTION_KERNEL_PASS",
                              "UNALIGNED_REFERENCE_KERNEL_PASS",
                              "NAT_CONSUMPTION_KERNEL_PASS") and checkpoint_errors:
            classification = "CPU_STATE_CHECKPOINT_FAIL"

    record = {
        "label": label,
        "command": command,
        "pid": process.pid,
        "pgid": process.pid,
        "stdin": "PTY" if stdin_pty else "DEVNULL",
        "stdin_is_tty": bool(pty_stdin_observation and
                              pty_stdin_observation.get("parent_slave_is_tty")),
        "pty_stdin_observation": pty_stdin_observation,
        "pty_stdin_verification_error": pty_verification_error,
        "pty_termios_before": pty_termios_before,
        "pty_termios_after": pty_termios_after,
        "pty_master_held_open_until_child_exit": stdin_pty,
        "stdout_stderr_capture": "regular files",
        "pty_master_used_for_output": False,
        "artifact_sha256": artifact_hash,
        "simulator_sha256": QUALIFIED_SKI_SHA256,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "simulator_frontend": Path(command[0]).name,
        "classification": classification,
        "exit_code": return_code,
        "termination_signal": termination_signal,
        "timed_out": timed_out,
        "timeout_reason": "wall_clock" if timed_out else None,
        "cleanup_complete": process.poll() is not None and process_group_cleanup_complete,
        "process_group_cleanup_complete": process_group_cleanup_complete,
        "elapsed_seconds": round(elapsed, 3),
        "stdout": str(stdout_path),
        "stdout_sha256": sha256(stdout_path),
        "stderr": str(stderr_path),
        "stderr_sha256": sha256(stderr_path),
        "instruction_counts": str(counts_path) if counts_path.is_file() else None,
        "instruction_counts_sha256": sha256(counts_path) if counts_path.is_file() else None,
        "cpu_state_checkpoints": checkpoint_records,
        "cpu_state_checkpoint_errors": checkpoint_errors,
        "ski_instruction_count": instruction_count,
        "ski_fault_count": fault_count,
        "resource_limits": resource_limits,
        "break_exception_evidence_errors": break_evidence_errors,
        "break_exception_expected_faults": (
            break_exception_contract["expected_faults"] if break_exception_contract else None),
        "break_exception_post_rfi_observations": (
            parse_break_post_rfi_psr_observations(
                stdout_text, break_exception_contract)[0]
            if break_exception_contract else None),
        "expected_exception_faults": expected_exception_faults or None,
        "general_exception_evidence_errors": general_evidence_errors,
        "general_exception_expected_faults": (
            general_exception_contract["expected_faults"]
            if general_exception_contract else None),
        "general_exception_record_fields": general_record_fields,
        "general_exception_ssc_exit_function": (
            general_exception_contract.get("ssc_exit_function")
            if general_exception_contract else None),
        "unaligned_reference_evidence_errors": unaligned_reference_evidence_errors,
        "unaligned_reference_expected_faults": (
            unaligned_reference_contract["expected_faults"]
            if unaligned_reference_contract else None),
        "unaligned_reference_record_fields": unaligned_reference_record_fields,
        "unaligned_reference_ssc_exit_function": (
            unaligned_reference_contract.get("ssc_exit_function")
            if unaligned_reference_contract else None),
        "nat_consumption_evidence_errors": nat_consumption_evidence_errors,
        "nat_consumption_expected_faults": (
            nat_consumption_contract["expected_faults"]
            if nat_consumption_contract else None),
        "nat_consumption_record_fields": nat_consumption_record_fields,
        "nat_consumption_ssc_exit_function": (
            nat_consumption_contract.get("ssc_exit_function")
            if nat_consumption_contract else None),
    }
    (capture_dir / "run-result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"[{classification}] exit={return_code} elapsed={elapsed:.3f}s capture={capture_dir}")
    return 0 if classification in ("SMOKE_PASS", "GUEST_EXIT_ZERO",
                                    "BREAK_EXCEPTION_PROBE_PASS",
                                    "BREAK_EXCEPTION_KERNEL_PASS",
                                    "GENERAL_EXCEPTION_PROBE_PASS",
                                    "GENERAL_EXCEPTION_KERNEL_PASS",
                                    "UNALIGNED_REFERENCE_PROBE_PASS",
                                    "UNALIGNED_REFERENCE_KERNEL_PASS",
                                    "NAT_CONSUMPTION_PROBE_PASS",
                                    "NAT_CONSUMPTION_KERNEL_PASS") else 1


def require_linux() -> None:
    if not sys.platform.startswith("linux"):
        raise GateError("the verified Ski runner must run inside the Ubuntu WSL2 environment")


def setup_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)

    smoke = subparsers.add_parser("smoke", help="run the separately authorized Ski smoke ELF")
    smoke.add_argument("--artifact", type=Path, required=True)
    smoke.add_argument("--manifest", type=Path, required=True)
    smoke.add_argument("--expected-sha256", required=True)
    smoke.add_argument("--simulator-sha256", required=True)
    smoke.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    smoke.add_argument("--capture-dir", type=Path)
    smoke.add_argument("--timeout", type=float, default=30)
    smoke.add_argument("--mode", choices=("system",), default="system")

    probe = subparsers.add_parser("break-probe", help="qualify the isolated Break-vector ELF")
    probe.add_argument("--artifact", type=Path, required=True)
    probe.add_argument("--manifest", type=Path, required=True)
    probe.add_argument("--expected-sha256", required=True)
    probe.add_argument("--simulator-sha256", required=True)
    probe.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    probe.add_argument("--capture-dir", type=Path)
    probe.add_argument("--timeout", type=float, default=60)
    probe.add_argument("--mode", choices=("system",), default="system")
    probe.add_argument("--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")

    general_probe = subparsers.add_parser(
        "general-exception-probe", help="qualify the isolated General Exception return ELF")
    general_probe.add_argument("--artifact", type=Path, required=True)
    general_probe.add_argument("--manifest", type=Path, required=True)
    general_probe.add_argument("--expected-sha256", required=True)
    general_probe.add_argument("--simulator-sha256", required=True)
    general_probe.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    general_probe.add_argument("--capture-dir", type=Path)
    general_probe.add_argument("--timeout", type=float, default=60)
    general_probe.add_argument("--mode", choices=("system",), default="system")
    general_probe.add_argument("--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")
    general_probe.add_argument("--ski-init-file", type=Path,
                               help="optional pinned-Ski debugger command file for failed probe diagnosis")

    unaligned_probe = subparsers.add_parser(
        "unaligned-reference-probe", help="qualify the isolated Unaligned Reference return ELF")
    unaligned_probe.add_argument("--artifact", type=Path, required=True)
    unaligned_probe.add_argument("--manifest", type=Path, required=True)
    unaligned_probe.add_argument("--expected-sha256", required=True)
    unaligned_probe.add_argument("--simulator-sha256", required=True)
    unaligned_probe.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    unaligned_probe.add_argument("--capture-dir", type=Path)
    unaligned_probe.add_argument("--timeout", type=float, default=60)
    unaligned_probe.add_argument("--mode", choices=("system",), default="system")
    unaligned_probe.add_argument(
        "--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")

    nat_probe = subparsers.add_parser(
        "nat-consumption-probe", help="qualify the isolated Register NaT Consumption ELF")
    nat_probe.add_argument("--artifact", type=Path, required=True)
    nat_probe.add_argument("--manifest", type=Path, required=True)
    nat_probe.add_argument("--expected-sha256", required=True)
    nat_probe.add_argument("--simulator-sha256", required=True)
    nat_probe.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    nat_probe.add_argument("--capture-dir", type=Path)
    nat_probe.add_argument("--timeout", type=float, default=60)
    nat_probe.add_argument("--mode", choices=("system",), default="system")
    nat_probe.add_argument(
        "--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")
    nat_probe.add_argument("--ski-init-file", type=Path,
                           help="optional pinned-Ski debugger command file for probe diagnosis")

    stacked_nat_probe = subparsers.add_parser(
        "stacked-nat-rse-probe", help="qualify the isolated stacked NaT/RSE preservation ELF")
    stacked_nat_probe.add_argument("--artifact", type=Path, required=True)
    stacked_nat_probe.add_argument("--manifest", type=Path, required=True)
    stacked_nat_probe.add_argument("--expected-sha256", required=True)
    stacked_nat_probe.add_argument("--simulator-sha256", required=True)
    stacked_nat_probe.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    stacked_nat_probe.add_argument("--capture-dir", type=Path)
    stacked_nat_probe.add_argument("--timeout", type=float, default=60)
    stacked_nat_probe.add_argument("--mode", choices=("system",), default="system")
    stacked_nat_probe.add_argument(
        "--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")

    cooperative_context_probe = subparsers.add_parser(
        "cooperative-context-probe",
        help="qualify the isolated two-context cooperative RSE switch ELF")
    cooperative_context_probe.add_argument("--artifact", type=Path, required=True)
    cooperative_context_probe.add_argument("--manifest", type=Path, required=True)
    cooperative_context_probe.add_argument("--expected-sha256", required=True)
    cooperative_context_probe.add_argument("--simulator-sha256", required=True)
    cooperative_context_probe.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    cooperative_context_probe.add_argument("--capture-dir", type=Path)
    cooperative_context_probe.add_argument("--timeout", type=float, default=35)
    cooperative_context_probe.add_argument("--mode", choices=("system",), default="system")
    cooperative_context_probe.add_argument(
        "--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")
    cooperative_context_probe.add_argument("--static-only", action="store_true",
                                           help="run artifact and disassembly gates without Ski")

    kernel = subparsers.add_parser("kernel", help="run a kernel only after separate authorization")
    kernel.add_argument("--artifact", type=Path, required=True)
    kernel.add_argument("--manifest", type=Path, required=True)
    kernel.add_argument("--profile", choices=("runtime", "compile-only", "early-exec"), required=True)
    kernel.add_argument("--expected-sha256", required=True)
    kernel.add_argument("--loader-record", type=Path, default=DEFAULT_LOADER_RECORD)
    kernel.add_argument("--ski-prefix", type=Path, default=DEFAULT_SKI_PREFIX)
    kernel.add_argument("--capture-dir", type=Path)
    kernel.add_argument("--timeout", type=float, default=60)
    kernel.add_argument("--mode", choices=("system",), default="system")
    kernel.add_argument("--objdump", default="/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")
    kernel.add_argument("--authorize-kernel-guest-execution", action="store_true",
                        help="explicit authorization for the selected kernel hash")
    return parser


def qualify_smoke_capture(capture_dir: Path, run_record: dict[str, Any]) -> int:
    """Qualify a preserved smoke capture; Ski writes SSC_EXIT status to stderr."""
    stdout_bytes = (capture_dir / "stdout.log").read_bytes()
    stderr_bytes = (capture_dir / "stderr.log").read_bytes()
    marker_bytes = SMOKE_MARKER.encode("ascii")
    marker_occurrences = stdout_bytes.count(marker_bytes)
    stdout_text = stdout_bytes.decode("utf-8", errors="replace")
    stderr_text = stderr_bytes.decode("utf-8", errors="replace")
    combined_text = stdout_text + "\n" + stderr_text
    guest_exit_match = re.search(
        r"(?m)^kernel exited with status (-?\d+)\s*$", combined_text)
    guest_exit_status = int(guest_exit_match.group(1)) if guest_exit_match else None
    if run_record.get("timed_out") is True:
        simulator_classification = "TIMEOUT"
    elif any(marker in combined_text.lower() for marker in FAULT_MARKERS):
        simulator_classification = "GUEST_FAULT"
    elif run_record.get("exit_code") != 0:
        simulator_classification = "SIMULATOR_ERROR"
    elif marker_bytes not in stdout_bytes:
        simulator_classification = "SMOKE_MARKER_MISSING"
    else:
        simulator_classification = "SMOKE_PASS"
    qualification_errors: list[str] = []
    if simulator_classification != "SMOKE_PASS":
        qualification_errors.append(
            f"simulator classification was {simulator_classification!r}")
    if marker_occurrences != 1:
        qualification_errors.append(
            f"expected one SSC_PUTCHAR marker, observed {marker_occurrences}")
    if guest_exit_status != 0:
        qualification_errors.append("Ski did not report intentional SSC_EXIT status 0")
    if run_record.get("ski_fault_count") != 0:
        qualification_errors.append(
            f"expected zero Ski faults, observed {run_record.get('ski_fault_count')!r}")
    if run_record.get("ski_instruction_count") is None:
        qualification_errors.append("Ski instruction count is unavailable")
    if run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("process_group_cleanup_complete") is not True:
        qualification_errors.append("smoke timeout or process cleanup gate failed")
    if (run_record.get("pty_stdin_observation") or {}).get("passed") is not True:
        qualification_errors.append("launched simulator fd 0 was not verified as the PTY slave")
    run_record.update({
        "simulator_classification": simulator_classification,
        "simulator_frontend_arguments": ["-noconsole", "-forcesystem"],
        "console_initialization": (
            "PASS: guest returned from SSC_CONSOLE_INIT before emitting the marker"
            if marker_occurrences == 1 else "not_observed"),
        "ssc_putchar_marker_ascii": SMOKE_MARKER,
        "ssc_putchar_marker_hex": marker_bytes.hex(),
        "ssc_putchar_marker_stream": "stdout.log",
        "ssc_putchar_marker_occurrences": marker_occurrences,
        "ssc_exit_function": 66,
        "ssc_guest_exit_status": guest_exit_status,
        "ssc_guest_exit_message_stream": (
            "stdout.log" if "kernel exited with status" in stdout_text else
            "stderr.log" if "kernel exited with status" in stderr_text else None),
        "qualification_errors": qualification_errors,
    })
    if qualification_errors:
        run_record["classification"] = "SMOKE_QUALIFICATION_FAIL"
        return 1
    run_record["classification"] = "SMOKE_PASS"
    return 0


def run_smoke(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected smoke ELF does not exist: {args.artifact}")
    manifest = check_manifest(args.artifact, args.manifest, "system-mode-smoke",
                              args.expected_sha256, "guidexos-ia64-ski-smoke-v1")
    if manifest.get("guest_execution_authorized") != "smoke-only" or \
            manifest.get("privileged_instruction_test") != "rsm psr.i; mov r8=psr" or \
            manifest.get("console_marker") != SMOKE_MARKER or manifest.get("exit_ssc") != 66:
        raise GateError("smoke manifest does not declare the bounded privilege/console/exit contract")
    validate_direct_system_elf(args.artifact)
    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(prefix="guidexos-ia64-ski-smoke-"))
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator), "-noconsole", "-forcesystem", str(snapshot)]
        run_simulator(
            command, args.timeout, capture_dir, "system-mode-smoke",
            SMOKE_MARKER, args.expected_sha256,
            resource_limits={
                "address_space_bytes": 2 * 1024 * 1024 * 1024,
                "cpu_soft_seconds": 15,
                "cpu_hard_seconds": 20,
            },
            stdin_pty=True)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "system-mode smoke run result")
    status = qualify_smoke_capture(capture_dir, run_record)
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    print(f"[{run_record['classification']}] capture={capture_dir}")
    return status


def validate_probe_manifest_fields(manifest: dict[str, Any]) -> None:
    expected = {
        "schema": "guidexos-ia64-break-exception-probe-v2",
        "profile": "break-probe",
        "guest_execution_authorized": "probe-only",
        "system_mode": "-forcesystem",
        "cpu_count": 1,
        "ivt_vector_offset": 0x2c00,
        "break_immediates": [0x6a17, 0x6a18],
        "expected_break_faults": 2,
        "expected_continuations": [1, 1],
        "post_rfi_capture_required": True,
        "post_rfi_capture_order": ["slot1", "slot2"],
        "post_rfi_return_slots": [2, 0],
        "post_rfi_before_cleanup": True,
        "all_other_vectors": "fail-stop",
    }
    mismatches = [f"{key}={manifest.get(key)!r}, expected {value!r}"
                  for key, value in expected.items() if manifest.get(key) != value]
    if mismatches:
        raise GateError("Break probe manifest contract is incomplete: " + "; ".join(mismatches))


def make_break_contract(symbols: dict[str, int], success_classification: str,
                        record_prefix: str, pass_marker: str,
                        fail_marker: str, validation_marker: str) -> dict[str, Any]:
    return {
        "expected_faults": 2,
        "pass_marker": pass_marker,
        "fail_marker": fail_marker,
        "validation_marker": validation_marker,
        "record_prefix": record_prefix,
        "break_slot1": symbols["break_slot1"],
        "break_slot2": symbols["break_slot2"],
        "post_rfi_slot1_continuation": symbols["post_rfi_slot1_continuation"],
        "post_rfi_slot2_continuation": symbols["post_rfi_slot2_continuation"],
        "rse_base": symbols["rse_base"],
        "rse_top": symbols["rse_top"],
        "success_classification": success_classification,
    }


def run_break_probe(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected Break probe ELF does not exist: {args.artifact}")
    manifest = check_manifest(args.artifact, args.manifest, "break-probe",
                              args.expected_sha256,
                              "guidexos-ia64-break-exception-probe-v2")
    validate_probe_manifest_fields(manifest)
    symbols = validate_break_probe_elf(args.artifact)
    validate_direct_system_elf(args.artifact)

    validation_env = os.environ.copy()
    validation_env["PYTHONDONTWRITEBYTECODE"] = "1"
    gate_cmd = [sys.executable, str(ROOT / "scripts" / "validate-ia64-break-exception.py"),
                "--repo-root", str(ROOT), "--elf", str(args.artifact),
                "--objdump", args.objdump, "--probe"]
    result = subprocess.run(gate_cmd, capture_output=True, text=True,
                            check=False, env=validation_env)
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr, file=sys.stderr, end="")
        raise GateError("Break probe source/disassembly gates failed")

    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(prefix="guidexos-ia64-break-probe-"))
    contract = make_break_contract(
        symbols, "BREAK_EXCEPTION_PROBE_PASS", "IA64_BREAK_PROBE_RECORD",
        "IA64_BREAK_PROBE_RESULT=PASS", "IA64_BREAK_PROBE_RESULT=FAIL",
        "IA64_BREAK_PROBE_VALIDATION")
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator), "-forcesystem", str(snapshot)]
        status = run_simulator(command, args.timeout, capture_dir,
                               "ia64-break-exception-probe", artifact_hash=args.expected_sha256,
                               break_exception_contract=contract)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "Break probe run result")
    run_record.update({
        "artifact_manifest": str(args.manifest.resolve()),
        "artifact_manifest_sha256": sha256(args.manifest),
        "simulator_sha256": args.simulator_sha256,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "execution_mode": "-forcesystem",
        "cpu_count": 1,
        "load_ranges": validate_direct_system_elf(args.artifact),
        "break_record_symbols": {
            "slot1": symbols["break_slot1"], "slot2": symbols["break_slot2"],
            "post_rfi_slot1_continuation": symbols["post_rfi_slot1_continuation"],
            "post_rfi_slot2_continuation": symbols["post_rfi_slot2_continuation"],
            "rse_base": symbols["rse_base"], "rse_top": symbols["rse_top"],
        },
    })
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    return status


def run_general_exception_probe(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected General Exception probe ELF does not exist: {args.artifact}")
    GENERAL_EXCEPTION_VALIDATOR.validate_manifest_identity(
        args.artifact, args.manifest, args.expected_sha256)
    symbols = GENERAL_EXCEPTION_VALIDATOR.exception_symbols(args.artifact)
    validate_direct_system_elf(args.artifact)

    validation_env = os.environ.copy()
    validation_env["PYTHONDONTWRITEBYTECODE"] = "1"
    gate_cmd = [sys.executable, str(ROOT / "scripts" / "validate-ia64-general-exception.py"),
                "--repo-root", str(ROOT), "--elf", str(args.artifact),
                "--objdump", args.objdump, "--manifest", str(args.manifest),
                "--expected-sha256", args.expected_sha256]
    result = subprocess.run(gate_cmd, capture_output=True, text=True,
                            check=False, env=validation_env)
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr, file=sys.stderr, end="")
        raise GateError("General Exception probe source/disassembly/identity gates failed")

    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(
        prefix="guidexos-ia64-general-exception-probe-"))
    contract = {
        "expected_faults": 1,
        "symbols": symbols,
        "probe": True,
        "success_classification": "GENERAL_EXCEPTION_PROBE_PASS",
        "require_intentional_exit": True,
        "ssc_exit_function": 66,
    }
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator)]
        if args.ski_init_file is not None:
            if not args.ski_init_file.is_file():
                raise GateError(f"selected Ski debugger init file does not exist: {args.ski_init_file}")
            command.extend(("-i", str(args.ski_init_file)))
        command.extend(("-forcesystem", str(snapshot)))
        status = run_simulator(
            command, args.timeout, capture_dir, "ia64-general-exception-probe",
            artifact_hash=args.expected_sha256, general_exception_contract=contract)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "General Exception probe run result")
    run_record.update({
        "artifact_manifest": str(args.manifest.resolve()),
        "artifact_manifest_sha256": sha256(args.manifest),
        "simulator_sha256": args.simulator_sha256,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "execution_mode": "-forcesystem",
        "cpu_count": 1,
        "load_ranges": validate_direct_system_elf(args.artifact),
        "general_exception_symbols": symbols,
    })
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    return status


def run_unaligned_reference_probe(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected Unaligned Reference probe ELF does not exist: {args.artifact}")
    UNALIGNED_REFERENCE_VALIDATOR.validate_manifest_identity(
        args.artifact, args.manifest, args.expected_sha256)
    symbols = UNALIGNED_REFERENCE_VALIDATOR.exception_symbols(args.artifact)
    validate_direct_system_elf(args.artifact)

    validation_env = os.environ.copy()
    validation_env["PYTHONDONTWRITEBYTECODE"] = "1"
    gate_cmd = [
        sys.executable, str(ROOT / "scripts" / "validate-ia64-unaligned-reference.py"),
        "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump,
        "--probe", "--manifest", str(args.manifest), "--expected-sha256", args.expected_sha256,
    ]
    result = subprocess.run(gate_cmd, capture_output=True, text=True,
                            check=False, env=validation_env)
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr, file=sys.stderr, end="")
        raise GateError("Unaligned Reference probe source/disassembly/identity gates failed")

    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(
        prefix="guidexos-ia64-unaligned-reference-probe-"))
    contract = {
        "expected_faults": 1,
        "symbols": symbols,
        "probe": True,
        "success_classification": "UNALIGNED_REFERENCE_PROBE_PASS",
        "require_intentional_exit": True,
        "ssc_exit_function": 66,
    }
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator), "-forcesystem", str(snapshot)]
        status = run_simulator(
            command, args.timeout, capture_dir, "ia64-unaligned-reference-probe",
            artifact_hash=args.expected_sha256,
            unaligned_reference_contract=contract)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "Unaligned Reference probe run result")
    run_record.update({
        "artifact_manifest": str(args.manifest.resolve()),
        "artifact_manifest_sha256": sha256(args.manifest),
        "simulator_sha256": args.simulator_sha256,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "execution_mode": "-forcesystem",
        "cpu_count": 1,
        "load_ranges": validate_direct_system_elf(args.artifact),
        "unaligned_reference_symbols": symbols,
    })
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    return status


def nat_consumption_symbols(elf: Any) -> dict[str, int]:
    required = (
        "ia64_ivt", "ivt_nat_consumption", "ia64_nat_consumption_entry",
        "ia64_nat_consumption_record", "ia64_nat_consumption_spec_load_site",
        "ia64_nat_consumption_fault_site", "ia64_nat_consumption_continuation",
        "ia64_nat_consumption_target", "rse_backing_store_base", "rse_backing_store_top",
    )
    missing = [name for name in required if name not in elf.symbols]
    if missing:
        raise GateError("NaT Consumption ELF lacks required symbols: " + ", ".join(missing))
    return {
        "ivt": elf.symbols["ia64_ivt"]["value"],
        "vector": elf.symbols["ivt_nat_consumption"]["value"],
        "entry": elf.symbols["ia64_nat_consumption_entry"]["value"],
        "record": elf.symbols["ia64_nat_consumption_record"]["value"],
        "spec_site": elf.symbols["ia64_nat_consumption_spec_load_site"]["value"],
        "fault_site": elf.symbols["ia64_nat_consumption_fault_site"]["value"],
        "continuation": elf.symbols["ia64_nat_consumption_continuation"]["value"],
        "target": elf.symbols["ia64_nat_consumption_target"]["value"],
        "rse_base": elf.symbols["rse_backing_store_base"]["value"],
        "rse_top": elf.symbols["rse_backing_store_top"]["value"],
    }


def run_nat_consumption_probe(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected Register NaT Consumption probe ELF does not exist: {args.artifact}")
    NAT_CONSUMPTION_VALIDATOR.validate_probe_manifest(
        args.artifact, args.manifest, args.expected_sha256)
    probe_elf = ELF_VALIDATOR.Elf64(args.artifact)
    symbols = nat_consumption_symbols(probe_elf)
    load_ranges = validate_direct_system_elf(args.artifact)

    validation_env = os.environ.copy()
    validation_env["PYTHONDONTWRITEBYTECODE"] = "1"
    gate_cmd = [
        sys.executable, str(ROOT / "scripts" / "validate-ia64-nat-consumption.py"),
        "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump,
        "--probe", "--manifest", str(args.manifest), "--expected-sha256", args.expected_sha256,
    ]
    result = subprocess.run(gate_cmd, capture_output=True, text=True,
                            check=False, env=validation_env)
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr, file=sys.stderr, end="")
        raise GateError("Register NaT Consumption probe static gates failed")

    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(
        prefix="guidexos-ia64-nat-consumption-probe-"))
    contract = {
        "expected_faults": 1,
        "symbols": symbols,
        "probe": True,
        "success_classification": "NAT_CONSUMPTION_PROBE_PASS",
        "require_intentional_exit": True,
        "ssc_exit_function": 66,
    }
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator)]
        if args.ski_init_file is not None:
            if not args.ski_init_file.is_file():
                raise GateError(f"selected Ski debugger init file does not exist: {args.ski_init_file}")
            command.extend(("-i", str(args.ski_init_file)))
        command.extend(("-forcesystem", str(snapshot)))
        status = run_simulator(
            command, args.timeout, capture_dir, "ia64-register-nat-consumption-probe",
            artifact_hash=args.expected_sha256, nat_consumption_contract=contract)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "Register NaT Consumption probe run result")
    run_record.update({
        "artifact_manifest": str(args.manifest.resolve()),
        "artifact_manifest_sha256": sha256(args.manifest),
        "simulator_sha256": args.simulator_sha256,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "execution_mode": "-forcesystem",
        "cpu_count": 1,
        "load_ranges": load_ranges,
        "nat_consumption_symbols": symbols,
    })
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    return status


def run_stacked_nat_rse_probe(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected stacked NaT/RSE probe ELF does not exist: {args.artifact}")
    symbols = STACKED_NAT_RSE_VALIDATOR.validate_static(
        ROOT, args.artifact.resolve(), args.objdump, True,
        args.manifest.resolve(), args.expected_sha256)
    load_ranges = validate_direct_system_elf(args.artifact)
    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(
        prefix="guidexos-ia64-stacked-nat-rse-probe-"))

    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        status = run_simulator(
            [str(simulator), "-forcesystem", str(snapshot)], args.timeout, capture_dir,
            "ia64-stacked-nat-rse-probe",
            success_marker="IA64_STACKED_NAT_RSE_PROBE_RESULT=PASS",
            artifact_hash=args.expected_sha256)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "stacked NaT/RSE probe run result")
    qualification_errors: list[str] = []
    stdout_path = Path(run_record.get("stdout", ""))
    try:
        runtime = STACKED_NAT_RSE_VALIDATOR.validate_output(
            stdout_path.read_text(encoding="utf-8", errors="replace"))
    except (OSError, STACKED_NAT_RSE_VALIDATOR.QualificationError) as exc:
        runtime = None
        qualification_errors.append(str(exc))
    if run_record.get("classification") != "SMOKE_PASS":
        qualification_errors.append(
            f"simulator classification was {run_record.get('classification')!r}")
    if run_record.get("ski_fault_count") != 0:
        qualification_errors.append(
            f"probe Ski fault count was {run_record.get('ski_fault_count')!r}, expected 0")
    if run_record.get("ski_instruction_count") is None:
        qualification_errors.append("probe instruction count is unavailable")
    if run_record.get("exit_code") != 0 or run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True:
        qualification_errors.append("probe did not exit cleanly within its bound")

    run_record.update({
        "classification": "STACKED_NAT_RSE_PROBE_PASS" if not qualification_errors
        else "STACKED_NAT_RSE_PROBE_FAIL",
        "qualification_errors": qualification_errors,
        "expected_faults": 0,
        "artifact_manifest": str(args.manifest.resolve()),
        "artifact_manifest_sha256": sha256(args.manifest),
        "simulator_sha256": args.simulator_sha256,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "execution_mode": "-forcesystem",
        "cpu_count": 1,
        "load_ranges": load_ranges,
        "stacked_nat_rse_symbols": symbols,
        "stacked_nat_rse_runtime": runtime,
    })
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    classification = run_record["classification"]
    print(f"[{classification}] capture={capture_dir}")
    return 0 if classification == "STACKED_NAT_RSE_PROBE_PASS" else (status or 1)


COOP_CONTEXT_EVENT_NAMES = (
    "A_TO_B", "B_FIRST_ENTRY", "B_TO_A_FIRST", "A_RESUMED_FIRST",
    "A_TO_B_SECOND", "B_RESUMED", "B_TO_A_SECOND", "A_RESUMED_FINAL",
)
COOP_CONTEXT_EVENT_IDS = (0xA, 0xB, 0xB, 0xA, 0xA, 0xB, 0xB, 0xA)


def cooperative_context_probe_command(simulator: Path, snapshot: Path) -> list[str]:
    """Select the documented headless batch frontend while forcing system mode."""
    return [str(simulator), "-noconsole", "-forcesystem", str(snapshot)]


def capture_cooperative_context_probe_observations(stdout_text: str,
                                                   stderr_text: str) -> dict[str, Any]:
    """Retain partial guest evidence even when the strict qualification gate fails."""
    combined = stdout_text + "\n" + stderr_text
    event_pattern = re.compile(
        r"^IA64_COOP_EVENT sequence=0x([0-9a-fA-F]+) "
        r"context=0x([0-9a-fA-F]+) event=([A-Z_]+)$")
    event_lines = [line for line in stdout_text.splitlines()
                   if line.startswith("IA64_COOP_EVENT")]
    events = []
    malformed_events = []
    for line in event_lines:
        match = event_pattern.fullmatch(line)
        if match is None:
            malformed_events.append(line)
            continue
        events.append({"sequence": int(match.group(1), 16),
                       "context_id": int(match.group(2), 16),
                       "event": match.group(3)})

    address_pattern = re.compile(
        r"(?P<kind>\b(?:IIP|IIPA|IP))\s*=\s*(?P<address>0x[0-9a-fA-F]+)\b|"
        r"(?P<fault_kind>\bfault\s*@\s*)(?P<fault_address>0x[0-9a-fA-F]+)\b",
        re.IGNORECASE)
    address_matches = list(address_pattern.finditer(combined))
    last_address = None
    if address_matches:
        match = address_matches[-1]
        raw_address = match.group("address") or match.group("fault_address")
        last_address = {
            "address": int(raw_address, 16),
            "source": "fault diagnostic" if match.group("fault_address") else
            match.group("kind").upper(),
        }

    return {
        "transition_record_lines": event_lines,
        "transition_records": events,
        "malformed_transition_record_lines": malformed_events,
        "context_state_lines": [line for line in stdout_text.splitlines()
                                 if line.startswith("IA64_COOP_CONTEXT_STATE id=")],
        "architectural_state_lines": [line for line in stdout_text.splitlines()
                                      if line.startswith("IA64_COOP_CONTEXT_VALIDATION")],
        "result_marker_lines": [line for line in stdout_text.splitlines()
                                if line.startswith("IA64_COOP_CONTEXT_PROBE_RESULT=")],
        "last_observed_guest_address": last_address,
    }


def validate_cooperative_context_probe_manifest(manifest: dict[str, Any]) -> None:
    expected_fields = {
        "guest_execution_authorized": "probe-only",
        "execution_count_authorized": 1,
        "system_mode": "-forcesystem",
        "cpu_count": 1,
        "ivt_bytes": 0x8000,
        "expected_faults": 0,
        "expected_events": list(COOP_CONTEXT_EVENT_NAMES),
        "intentional_exit_ssc": 66,
        "scheduler_enabled": False,
        "external_interrupts_enabled": False,
    }
    mismatches = [f"{key}={manifest.get(key)!r}, expected {value!r}"
                  for key, value in expected_fields.items() if manifest.get(key) != value]
    if mismatches:
        raise GateError("cooperative context probe manifest contract failed: " + "; ".join(mismatches))


def validate_cooperative_probe_setup_source(sequence_source: str) -> None:
    """Require setup's caller link and PFS to survive its nested helper call."""
    start = sequence_source.find("ia64_coop_probe_setup:")
    end = sequence_source.find(".endp ia64_coop_probe_setup", start)
    if start < 0 or end < 0:
        raise GateError("cooperative probe setup procedure is missing")
    body = sequence_source[start:end]
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.DOTALL)
    compact = re.sub(r"\s+", "", body).lower()
    required = (
        "allocr16=ar.pfs,0,2,0,0;;",
        "movr32=r16;;",
        "movr33=b0;;",
        "br.call.sptk.manyb0=ia64_coop_set_nat8;;",
        "movar.pfs=r32;;",
        "movb0=r33;;",
        "br.ret.sptk.manyb0;;",
    )
    positions = [compact.find(instruction) for instruction in required]
    if min(positions) < 0 or positions != sorted(positions):
        raise GateError(
            "probe setup must save PFS and incoming b0 in stacked locals before its helper, "
            "then restore both before returning"
        )
    if len(re.findall(r"br\.call\b", body, re.IGNORECASE)) != 1 or \
            len(re.findall(r"br\.ret\b", body, re.IGNORECASE)) != 1:
        raise GateError("probe setup must have one nested call and one final return")
    call_end = positions[3] + len(required[3])
    pfs_restore = positions[4]
    if re.search(r"movr(?:32|33)=", compact[call_end:pfs_restore]):
        raise GateError("probe setup overwrites a saved stacked continuation across its helper")
    return_match = re.search(r"movb0=r33;;br\.ret\.sptk\.manyb0;;$", compact)
    if return_match is None:
        raise GateError("probe setup final return does not branch through the restored incoming b0")

    setup_fields = (
        "ia64_coop_set_nat8", "ia64_coop_context_a", "ia64_coop_context_b",
        "c_stack_base", "c_stack_limit", "c_rbs_base", "c_rbs_limit", "c_sp",
        "c_bsp", "c_bspstore", "c_rp", "c_pfs", "c_rsc", "c_rnat",
        "c_first_entry", "c_context_id", "c_profile_magic", "c_code_base",
        "c_code_limit", "ia64_coop_b_first_entry", "ia64_coop_stack_a_guard_low",
        "ia64_coop_stack_b_guard_high", "ia64_coop_rbs_a_guard_low",
        "ia64_coop_rbs_b_guard_high",
    )
    missing = [field for field in setup_fields if field.lower() not in body.lower()]
    if missing:
        raise GateError("probe setup lost required A/B initialization: " + ", ".join(missing))

    storage_contracts = (
        (
            "A",
            (
                "movlr14=ia64_coop_context_a;;",
                "movlr15=ia64_coop_stack_a_bottom;;",
                "set64r14,c_stack_base,r15",
                "movlr15=ia64_coop_stack_a_top;;",
                "set64r14,c_stack_limit,r15",
                "movlr15=ia64_coop_rbs_a_base;;",
                "set64r14,c_rbs_base,r15",
                "movlr15=ia64_coop_rbs_a_limit;;",
                "set64r14,c_rbs_limit,r15",
                "movlr15=ia64_coop_stack_a_top;;",
                "set64r14,c_sp,r15",
                "movlr15=ia64_coop_rbs_a_base;;",
                "set64r14,c_bsp,r15",
                "set64r14,c_bspstore,r15",
            ),
        ),
        (
            "B",
            (
                "movlr14=ia64_coop_context_b;;",
                "movlr15=ia64_coop_stack_b_bottom;;",
                "set64r14,c_stack_base,r15",
                "movlr15=ia64_coop_stack_b_top;;",
                "set64r14,c_stack_limit,r15",
                "set64r14,c_sp,r15",
                "movlr15=ia64_coop_rbs_b_base;;",
                "set64r14,c_rbs_base,r15",
                "set64r14,c_bsp,r15",
                "set64r14,c_bspstore,r15",
                "movlr15=ia64_coop_rbs_b_limit;;",
                "set64r14,c_rbs_limit,r15",
            ),
        ),
    )
    for identity, instructions in storage_contracts:
        cursor = 0
        for instruction in instructions:
            position = compact.find(instruction, cursor)
            if position < 0:
                raise GateError(
                    f"Context {identity} setup does not bind its stack and RBS fields "
                    "to the matching linker symbols"
                )
            cursor = position + len(instruction)


def validate_cooperative_probe_setup_disassembly(disassembly: str) -> None:
    """Check the linked IA-64 instructions preserve setup's return link and PFS."""
    start = disassembly.find("<ia64_coop_probe_setup>:")
    end = disassembly.find("<ia64_coop_set_nat8>:", start)
    body = disassembly[start:end if end >= 0 else None] if start >= 0 else ""
    patterns = (
        r"\balloc\s+r16\s*=\s*ar\.pfs,\s*2,\s*2,\s*0\b",
        r"\bmov(?:\.i)?\s+r32\s*=\s*r16\b",
        r"\bmov(?:\.i)?\s+r33\s*=\s*b0\b",
        r"\bbr\.call[^\n]*ia64_coop_set_nat8\b",
        r"\bmov(?:\.i)?\s+ar\.pfs\s*=\s*r32\b",
        r"\bmov(?:\.i)?\s+b0\s*=\s*r33\b",
        r"\bbr\.ret[^\n]*\bb0\b",
    )
    positions = [re.search(pattern, body, re.IGNORECASE) for pattern in patterns]
    if not body or any(match is None for match in positions):
        raise GateError("target setup disassembly lacks the required stacked b0/PFS save and restore")
    offsets = [match.start() for match in positions if match is not None]
    if offsets != sorted(offsets):
        raise GateError("target setup disassembly restores PFS or b0 in an invalid order")
    if len(re.findall(r"\bbr\.call\b", body, re.IGNORECASE)) != 1 or \
            len(re.findall(r"\bbr\.ret\b", body, re.IGNORECASE)) != 1:
        raise GateError("target setup disassembly has unexpected call/return control flow")


def validate_cooperative_bsp_bounds_source(sequence_source: str) -> None:
    """Require a mode-safe BSP read and exact current-frame start check for B."""
    start = sequence_source.find("ia64_coop_b_first_entry:")
    end = sequence_source.find(".global ia64_coop_a_test", start)
    b_start = sequence_source.find("ia64_coop_b_test:")
    b_end = sequence_source.find(".endp ia64_coop_b_test", b_start)
    if min(start, end, b_start, b_end) < 0:
        raise GateError("Context B first-entry or test procedure is missing")
    first_entry = sequence_source[start:end]
    body = sequence_source[b_start:b_end]
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.DOTALL)
    compact = re.sub(r"\s+", "", body).lower()
    check_start = compact.find("ia64_coop_b_bsp_check:")
    check_end = compact.find("ia64_coop_b_bsp_bounds_valid:", check_start)
    check = compact[check_start:check_end] if check_start >= 0 and check_end >= 0 else ""
    required = (
        "movr14=ar.bsp;;",
        "movlr15=ia64_coop_rbs_b_base;;",
        "cmp.geup6,p7=r14,r15;;(p7)br.cond.sptkia64_coop_test_fail;;",
        "movlr15=ia64_coop_rbs_b_limit;;",
        "cmp.ltup6,p7=r14,r15;;(p7)br.cond.sptkia64_coop_test_fail;;",
        "movlr15=ia64_coop_rbs_b_base;;",
        "cmp.eqp6,p7=r14,r15;;(p7)br.cond.sptkia64_coop_test_fail;;",
    )
    positions: list[int] = []
    cursor = 0
    for instruction in required:
        found = check.find(instruction, cursor)
        positions.append(found)
        if found >= 0:
            cursor = found + len(instruction)
    if not check or min(positions) < 0 or positions != sorted(positions):
        raise GateError(
            "Context B must validate mode-independent BSP against its RBS bounds and "
            "the expected current-frame start"
        )
    if re.search(r"\.equ\s+B_TEST_BSP_FRAME_BYTES\s*,\s*96\b", sequence_source,
                 re.IGNORECASE) is None:
        raise GateError("Context B's twelve-register frame must occupy 96 backing-store bytes")
    if "ar.bspstore" in body.lower() or "ar.rnat" in body.lower() or \
            re.search(r"\b(loadrs|flushrs)\b", body, re.IGNORECASE):
        raise GateError("Context B diagnostics contain an RSE mode-sensitive access")
    frame_allocations = re.findall(
        r"\balloc\s+r16\s*=\s*ar\.pfs\s*,\s*6\s*,\s*4\s*,\s*2\s*,\s*0\b",
        body, re.IGNORECASE)
    if len(frame_allocations) != 1 or len(re.findall(r"\balloc\b", body, re.IGNORECASE)) != 1:
        raise GateError("Context B first test frame must remain 12 stacked registers")
    if re.search(r"\balloc\b", first_entry, re.IGNORECASE):
        raise GateError("Context B bootstrap must not add an unaccounted stacked frame")


def validate_cooperative_rse_mode_accesses(entry_source: str, sequence_source: str,
                                            switch_source: str) -> None:
    """Verify all guest BSPSTORE/RNAT/loadrs accesses follow an RSC.mode=0 handoff."""
    entry = re.sub(r"/\*.*?\*/|//[^\n]*", "", entry_source, flags=re.DOTALL)
    sequence = re.sub(r"/\*.*?\*/|//[^\n]*", "", sequence_source, flags=re.DOTALL)
    switch = re.sub(r"/\*.*?\*/|//[^\n]*", "", switch_source, flags=re.DOTALL)
    entry_compact = re.sub(r"\s+", "", entry).lower()
    entry_order = (
        "movr14=r0;;movar.rsc=r14;;loadrs;;movlr14=ia64_coop_rbs_a_base;;"
        "movar.bspstore=r14;;movar.rsc=3;;"
    )
    if entry_order not in entry_compact:
        raise GateError("entry loadrs/BSPSTORE initialization must remain in enforced lazy mode")
    b_body_start = sequence.find("ia64_coop_b_test:")
    b_body_end = sequence.find(".endp ia64_coop_b_test", b_body_start)
    b_body = sequence[b_body_start:b_body_end] if b_body_start >= 0 and b_body_end >= 0 else ""
    if re.search(r"\bmov(?:\.m)?\s+(?:r\d+\s*=\s*ar\.(?:bspstore|rnat)|"
                 r"ar\.(?:bspstore|rnat)\s*=)", sequence + b_body, re.IGNORECASE) or \
            re.search(r"\b(loadrs|flushrs)\b", sequence + b_body, re.IGNORECASE):
        raise GateError("ordinary probe diagnostics must not access BSPSTORE, RNAT, loadrs, or flushrs")

    compact = re.sub(r"\s+", "", switch).lower()
    required = (
        "flushrs;;mf;;mov.mar.rsc=r0;;srlz.d;;movr24=ar.rnat;;movr23=ar.bspstore;;",
        "invala;;mov.mar.rsc=r0;;srlz.d;;load64c_bspstore,r28,r20",
        "movar.bspstore=r20;;srlz.d;;load64c_rnat,r28,r21movar.rnat=r21;;",
        "mov.mar.rsc=r20;;srlz.d;;br.ret.sptk.manyb0;;",
    )
    positions = [compact.find(item) for item in required]
    if min(positions) < 0 or positions != sorted(positions):
        raise GateError("switch BSPSTORE/RNAT accesses are not ordered under RSC.mode=0")


def cooperative_disassembly_instruction_address(disassembly: str, pattern: str,
                                                label: str) -> int:
    for line in disassembly.splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+):\s+.*", line)
        if match is not None and re.search(pattern, line, re.IGNORECASE):
            return int(match.group(1), 16)
    raise GateError(f"target switch disassembly lacks the {label} instruction")


def validate_cooperative_bsp_bounds_disassembly(disassembly: str, bsp_base: int,
                                                bsp_limit: int, expected_bsp_offset: int,
                                                expected_check: int,
                                                expected_valid: int) -> int:
    """Validate the linked BSP read, bounds, and exact current-frame start comparison."""
    start = disassembly.find("<ia64_coop_b_test>:")
    end = disassembly.find("<ia64_coop_b_unexpected_resume>:", start)
    body = disassembly[start:end if end >= 0 else None] if start >= 0 else ""
    if not body or re.search(r"\bar\.bspstore\b", body, re.IGNORECASE):
        raise GateError("Context B target diagnostics contain a BSPSTORE access")
    instructions: list[tuple[int, str]] = []
    for line in body.splitlines():
        address_match = re.match(r"\s*([0-9a-fA-F]+):\s+", line)
        if address_match is None:
            continue
        pieces = line.split("\t")
        if len(pieces) < 3:
            continue
        text = re.sub(r"^\[[A-Z]+\]\s*", "", pieces[-1].strip())
        instructions.append((int(address_match.group(1), 16), text))

    alloc_indices = [index for index, (_, instruction) in enumerate(instructions)
                     if re.search(r"\balloc\s+r16\s*=\s*ar\.pfs,12,10,0\b",
                                  instruction, re.IGNORECASE)]

    base = f"0x{bsp_base:x}"
    limit = f"0x{bsp_limit:x}"
    expected_bsp = f"0x{bsp_base + expected_bsp_offset:x}"
    patterns = (
        r"\bmov\.m\s+r14\s*=\s*ar\.bsp\b",
        rf"\bmovl\s+r15\s*=\s*{base}\b",
        r"\bcmp\.ltu\s+p7,p6\s*=\s*r14,r15\b",
        r"\(p0?7\)\s+br\.cond[^\s]*\s+[^\n]*<ia64_coop_test_fail>",
        rf"\bmovl\s+r15\s*=\s*{limit}\b",
        r"\bcmp\.ltu\s+p6,p7\s*=\s*r14,r15\b",
        r"\(p0?7\)\s+br\.cond[^\s]*\s+[^\n]*<ia64_coop_test_fail>",
        rf"\bmovl\s+r15\s*=\s*{expected_bsp}\b",
        r"\bcmp\.eq\s+p6,p7\s*=\s*r14,r15\b",
        r"\(p0?7\)\s+br\.cond[^\s]*\s+[^\n]*<ia64_coop_test_fail>",
    )
    positions: list[int] = []
    for pattern in patterns:
        found = next((index for index, (_, instruction) in enumerate(instructions)
                      if index >= (positions[-1] + 1 if positions else 0) and
                      re.search(pattern, instruction, re.IGNORECASE)), None)
        if found is None:
            raise GateError("target Context B BSP check does not match its source contract")
        positions.append(found)
    read_address = instructions[positions[0]][0]
    if len(alloc_indices) != 1 or alloc_indices[0] >= positions[0]:
        raise GateError("target Context B BSP check is not preceded by its sole 12-register frame")
    labels = {int(match.group(1), 16): match.group(2) for match in re.finditer(
        r"(?m)^\s*([0-9a-fA-F]+)\s+<([^>]+)>:", body)}
    if read_address != expected_check or labels.get(expected_valid) != \
            "ia64_coop_b_bsp_bounds_valid":
        raise GateError("Context B BSP symbols do not match the linked check frontiers")
    return read_address


def validate_cooperative_context_switch_source(switch_source: str) -> None:
    if "STORE64 C_PFS, r16" not in switch_source or "mov ar.pfs = r22" not in switch_source:
        raise GateError("switch source does not save and restore PFS")

    first_dispatch_start = switch_source.find("adds r28 = C_FIRST_ENTRY, r15")
    first_entry_start = switch_source.find(".Lfirst_entry_context:")
    saved_context_start = switch_source.find(".Lsaved_context:")
    target_checked_start = switch_source.find(".Ltarget_checked:")
    if min(first_dispatch_start, first_entry_start, saved_context_start, target_checked_start) < 0 or \
            not first_dispatch_start < first_entry_start < saved_context_start < target_checked_start:
        raise GateError("switch source lacks distinct first-entry and saved-context validation paths")

    dispatch = switch_source[first_dispatch_start:first_entry_start]
    first_entry = switch_source[first_entry_start:saved_context_start]
    saved_context = switch_source[saved_context_start:target_checked_start]
    if not re.search(
            r"cmp\.eq p6, p7 = r20, r0 ;;\s*\(p6\) br\.cond\.sptk \.Lsaved_context",
            dispatch):
        raise GateError("switch source does not route first-entry and resumed contexts separately")
    if not re.search(
            r"cmp\.eq p6, p7 = r20, r0 ;;\s*\(p7\) br\.cond\.sptk ia64_coop_switch_fail",
            first_entry):
        raise GateError("first-entry bootstrap must require its defined zero PFS")
    if not re.search(
            r"cmp\.eq p6, p7 = r20, r0 ;;\s*\(p6\) br\.cond\.sptk ia64_coop_switch_fail",
            saved_context):
        raise GateError("saved context must require a nonzero PFS")

    rsc_start = switch_source.find("adds r28 = C_RSC, r15")
    first_flag_start = switch_source.find("adds r28 = C_FIRST_ENTRY, r15")
    rsc_validation = switch_source[rsc_start:first_flag_start] if rsc_start >= 0 and \
        first_flag_start >= 0 else ""
    if not re.search(
            r"mov r21 = 3 ;;\s*cmp\.eq p6, p7 = r20, r21 ;;\s*"
            r"\(p7\) br\.cond\.sptk ia64_coop_switch_fail",
            rsc_validation):
        raise GateError("switch source does not fail-stop unsupported RSC profiles")


def validate_cooperative_context_probe_static(artifact: Path, manifest_path: Path,
                                               expected_hash: str, objdump: str
                                               ) -> dict[str, Any]:
    manifest = check_manifest(
        artifact, manifest_path, "cooperative-context-switch-probe", expected_hash,
        "guidexos-ia64-cooperative-context-probe-v1")
    validate_cooperative_context_probe_manifest(manifest)

    elf = ELF_VALIDATOR.Elf64(artifact)
    if elf.machine != EM_IA_64 or elf.type != ET_EXEC or elf.data[5] != 1:
        raise GateError("cooperative context probe must be little-endian ELF64 EM_IA_64 ET_EXEC")
    ivt = elf.section(".ivt")
    if ivt is None or ivt["size"] != 0x8000 or ivt["addr"] % 0x8000 or \
            ivt["align"] < 0x8000 or elf.symbols.get("ia64_ivt", {}).get("value") != ivt["addr"] or \
            elf.symbols.get("ia64_ivt_end", {}).get("value") != ivt["addr"] + 0x8000:
        raise GateError("cooperative probe IVT geometry is invalid")

    names = (
        "_start", "ia64_coop_context_switch", "ia64_coop_probe_run",
        "ia64_coop_probe_setup", "ia64_coop_setup_nested_call",
        "ia64_coop_setup_nested_return", "ia64_coop_setup_exit",
        "ia64_coop_probe_after_setup", "ia64_coop_probe_after_a_test",
        "ia64_coop_a_test", "ia64_coop_a_resumed_first",
        "ia64_coop_a_resumed_final", "ia64_coop_a_final_exit",
        "ia64_coop_b_first_entry", "ia64_coop_b_test", "ia64_coop_b_resumed",
        "ia64_coop_b_unexpected_resume", "ia64_coop_test_fail", "ia64_coop_b_bsp_check",
        "ia64_coop_b_bsp_bounds_valid",
        "__text_start", "__text_end",
        "ia64_coop_context_a", "ia64_coop_context_b", "ia64_coop_probe_record",
        "ia64_coop_stack_a_guard_low", "ia64_coop_stack_a_bottom",
        "ia64_coop_stack_a_top", "ia64_coop_stack_a_guard_high",
        "ia64_coop_stack_b_guard_low", "ia64_coop_stack_b_bottom",
        "ia64_coop_stack_b_top", "ia64_coop_stack_b_guard_high",
        "ia64_coop_rbs_a_guard_low", "ia64_coop_rbs_a_base",
        "ia64_coop_rbs_a_limit", "ia64_coop_rbs_a_guard_high",
        "ia64_coop_rbs_b_guard_low", "ia64_coop_rbs_b_base",
        "ia64_coop_rbs_b_limit", "ia64_coop_rbs_b_guard_high", "__gp",
    )
    missing = [name for name in names if name not in elf.symbols]
    if missing:
        raise GateError("cooperative probe ELF lacks required symbols: " + ", ".join(missing))
    symbol = lambda name: elf.symbols[name]["value"]
    size = lambda name: elf.symbols[name]["size"]
    if size("ia64_coop_context_a") != 1024 or size("ia64_coop_context_b") != 1024 or \
            size("ia64_coop_probe_record") != 1632:
        raise GateError("cooperative context or event-record geometry changed")

    ranges: dict[str, tuple[int, int]] = {}
    for name in ("a", "b"):
        stack_base = symbol(f"ia64_coop_stack_{name}_bottom")
        stack_limit = symbol(f"ia64_coop_stack_{name}_top")
        rbs_base = symbol(f"ia64_coop_rbs_{name}_base")
        rbs_limit = symbol(f"ia64_coop_rbs_{name}_limit")
        if stack_limit - stack_base != 65536 or stack_base % 16:
            raise GateError(f"context {name.upper()} stack geometry is invalid")
        if rbs_limit - rbs_base != 65536 or rbs_base % 512 or rbs_limit % 512:
            raise GateError(f"context {name.upper()} RBS geometry is invalid")
        if symbol(f"ia64_coop_stack_{name}_guard_low") + 8 != stack_base or \
                symbol(f"ia64_coop_stack_{name}_guard_high") != stack_limit or \
                symbol(f"ia64_coop_rbs_{name}_guard_low") + 8 != rbs_base or \
                symbol(f"ia64_coop_rbs_{name}_guard_high") != rbs_limit:
            raise GateError(f"context {name.upper()} boundary canaries do not bracket their ranges")
        ranges[f"stack_{name}"] = (symbol(f"ia64_coop_stack_{name}_guard_low"), stack_limit + 8)
        ranges[f"rbs_{name}"] = (symbol(f"ia64_coop_rbs_{name}_guard_low"), rbs_limit + 8)
    for left_name, left in ranges.items():
        for right_name, right in ranges.items():
            if left_name < right_name and ranges_overlap(left, right):
                raise GateError(f"independent probe ranges overlap: {left_name} and {right_name}")
    context_a = (symbol("ia64_coop_context_a"), symbol("ia64_coop_context_a") + 1024)
    context_b = (symbol("ia64_coop_context_b"), symbol("ia64_coop_context_b") + 1024)
    if context_a[0] % 512 or context_b[0] % 512 or ranges_overlap(context_a, context_b):
        raise GateError("context records are not separate 512-byte aligned objects")
    if symbol("__text_start") >= symbol("__text_end"):
        raise GateError("cooperative probe text bounds are invalid")

    script_dir = ROOT / "tests" / "ia64_cooperative_context_switch"
    switch_source = (script_dir / "context_switch.s").read_text(encoding="utf-8")
    sequence_source = (script_dir / "probe_sequence.s").read_text(encoding="utf-8")
    entry_source = (script_dir / "entry.s").read_text(encoding="utf-8")
    build_source = (script_dir / "build.sh").read_text(encoding="utf-8")
    required_switch_terms = (
        "flushrs", "mf", "mov.m ar.rsc = r0", "mov r24 = ar.rnat", "invala",
        "mov ar.bspstore = r20", "mov ar.rnat = r21", "mov ar.pfs = r22",
        "st8.spill", "ld8.fill", "stf.spill", "ldf.fill", "ar.unat",
        "ar.lc", "ar.ec", "ar.ccv", "ar.fpsr", "mov pr = r20, -1",
        "mov b1 = r20", "mov b5 = r20", "mov.m ar.rsc = r20", "br.ret.sptk.many b0",
        "C_CODE_BASE", "C_CODE_LIMIT", "C_FIRST_ENTRY", "C_PFS", ".Lsaved_context",
        "STORE64 C_PFS, r16",
    )
    missing_terms = [term for term in required_switch_terms if term not in switch_source]
    if missing_terms:
        raise GateError("switch source is missing required state/order operations: " +
                        ", ".join(missing_terms))
    validate_cooperative_context_switch_source(switch_source)
    validate_cooperative_probe_setup_source(sequence_source)
    validate_cooperative_bsp_bounds_source(sequence_source)
    validate_cooperative_rse_mode_accesses(entry_source, sequence_source, switch_source)
    fp_slots = re.findall(r"(?m)^\s*STORE_FP_AT\s+[0-9]+,\s*f([0-9]+)\s*$",
                          sequence_source)
    if sorted(map(int, fp_slots)) != [2, 3, 4, 5, *range(16, 32)]:
        raise GateError("FP image capture must cover f2-f5 and every ABI-preserved f16-f31")
    required_fp_captures = (
        "CAPTURE_FP_SET R_FP_A_FIRST_RESUME",
        "CAPTURE_FP_SET R_FP_A_FINAL_RESUME",
        "CAPTURE_FP_SET R_FP_B_FIRST_ENTRY",
        "CAPTURE_FP_SET R_FP_B_RESUME",
    )
    if any(sequence_source.count(capture) != 1 for capture in required_fp_captures) or \
            sequence_source.count("CHECK_CONTEXT_GP ia64_coop_context_a") != 2 or \
            sequence_source.count("CHECK_CONTEXT_GP ia64_coop_context_b") != 2:
        raise GateError("probe must check each context GP and capture its FP state at both resumes")
    source_order = (
        "flushrs", "mf", "mov.m ar.rsc = r0", "mov r24 = ar.rnat", "invala",
        "mov ar.bspstore = r20", "mov ar.rnat = r21", "mov ar.pfs = r22",
        "mov.m ar.rsc = r20", "br.ret.sptk.many b0",
    )
    offset = -1
    for term in source_order:
        offset = switch_source.find(term, offset + 1)
        if offset < 0:
            raise GateError("switch source no longer follows the required ordered RSE handoff")
    if "rfi" in switch_source.lower() or "-mfixed-range=f2-f5,f16-f31" not in build_source:
        raise GateError("switch must remain synchronous and retain the declared FP codegen policy")
    if "adds r12 = -4096, r12" not in sequence_source or \
            "STACK_USE_A_LOW" not in sequence_source or "STACK_USE_B_LOW" not in sequence_source or \
            "tnat.nz p6, p7 = r33" not in sequence_source:
        raise GateError("A/B probe does not exercise its private memory stacks and stacked NaT state")
    if entry_source.count(".skip 65536") != 4 or "IVT_HALT" not in entry_source or \
            "rsm psr.i | psr.ic" not in entry_source:
        raise GateError("probe entry lacks fixed independent maps, fail-stop IVT, or masked interrupts")

    objdump_path = Path(objdump)
    if not objdump_path.is_file():
        raise GateError(f"IA-64 objdump is unavailable: {objdump}")
    try:
        result = subprocess.run([str(objdump_path), "-dr", "--disassemble=ia64_coop_context_switch",
                                 str(artifact)], check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise GateError(f"cannot inspect target switch disassembly: {exc}") from exc
    start = result.stdout.find("<ia64_coop_context_switch>:")
    end = result.stdout.find("<ia64_coop_switch_fail>:", start)
    disassembly = result.stdout[start:end if end >= 0 else None] if start >= 0 else ""
    if not disassembly:
        raise GateError("target disassembly does not contain the cooperative switch symbol")
    disassembly_order = (
        "flushrs", "mf", "mov.m ar.rsc=r0", "mov.m r24=ar.rnat", "invala",
        "mov.m ar.rsc=r0", "mov.m ar.bspstore=r20", "mov.m ar.rnat=r21",
        "mov.i ar.pfs=r22", "mov.m ar.rsc=r20", "br.ret.sptk.many b0",
    )
    offset = -1
    for term in disassembly_order:
        offset = disassembly.find(term, offset + 1)
        if offset < 0:
            raise GateError(f"target switch disassembly is missing or reorders {term!r}")
    if "rfi" in disassembly:
        raise GateError("synchronous switch disassembly unexpectedly contains RFI")
    switch_flushrs = cooperative_disassembly_instruction_address(
        disassembly, r"\bflushrs\b", "Context A RSE save")
    switch_bspstore = cooperative_disassembly_instruction_address(
        disassembly, r"\bmov\.m\s+ar\.bspstore\s*=\s*r20\b", "incoming BSPSTORE handoff")
    switch_return = cooperative_disassembly_instruction_address(
        disassembly, r"\bbr\.ret\.sptk\.many\s+b0\b", "switch return")
    bsp_base = symbol("ia64_coop_rbs_b_base")
    bsp_limit = symbol("ia64_coop_rbs_b_limit")
    # alloc updates CFM but leaves AR[BSP] unchanged. BSP identifies where
    # current GR32 would be stored; the twelve-register frame spans 96 bytes
    # starting at that address.
    bsp_expected_offset = 0
    bsp_frame_bytes = 12 * 8
    bsp_base_collection_index = (bsp_base >> 3) & 0x3F
    bsp_nat_collection_slots = (bsp_base_collection_index + 12) // 63
    if bsp_base_collection_index != 0 or bsp_nat_collection_slots != 0 or \
            bsp_expected_offset != 0 or bsp_frame_bytes != 96 or \
            bsp_base + bsp_frame_bytes > bsp_limit:
        raise GateError("Context B frame start/span violates RBS/NaT geometry")
    try:
        b_test_result = subprocess.run(
            [str(objdump_path), "-dr", "--disassemble=ia64_coop_b_test", str(artifact)],
            check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise GateError(f"cannot inspect target Context B BSP disassembly: {exc}") from exc
    bsp_read_instruction = validate_cooperative_bsp_bounds_disassembly(
        b_test_result.stdout, bsp_base, bsp_limit, bsp_expected_offset,
        symbol("ia64_coop_b_bsp_check"), symbol("ia64_coop_b_bsp_bounds_valid"))
    try:
        setup_result = subprocess.run(
            [str(objdump_path), "-dr", "--disassemble=ia64_coop_probe_setup", str(artifact)],
            check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise GateError(f"cannot inspect target setup disassembly: {exc}") from exc
    setup_disassembly = setup_result.stdout
    validate_cooperative_probe_setup_disassembly(setup_disassembly)
    setup_start = setup_disassembly.find("<ia64_coop_probe_setup>:")
    setup_end = setup_disassembly.find("<ia64_coop_set_nat8>:", setup_start)
    setup_body = setup_disassembly[setup_start:setup_end if setup_end >= 0 else None]
    setup_call_instruction = cooperative_disassembly_instruction_address(
        setup_body, r"\bbr\.call[^\n]*ia64_coop_set_nat8", "setup nested call")
    setup_return_instruction = cooperative_disassembly_instruction_address(
        setup_body, r"\bbr\.ret\.sptk\.many\s+b0\b", "setup return")
    return {"ivt": ivt["addr"], "text": [symbol("__text_start"), symbol("__text_end")],
            "context_a": symbol("ia64_coop_context_a"),
            "context_b": symbol("ia64_coop_context_b"),
            "stack_a": [symbol("ia64_coop_stack_a_bottom"), symbol("ia64_coop_stack_a_top")],
            "stack_b": [symbol("ia64_coop_stack_b_bottom"), symbol("ia64_coop_stack_b_top")],
            "rbs_a": [symbol("ia64_coop_rbs_a_base"), symbol("ia64_coop_rbs_a_limit")],
            "rbs_b": [symbol("ia64_coop_rbs_b_base"), symbol("ia64_coop_rbs_b_limit")],
            "setup": symbol("ia64_coop_probe_setup"),
            "setup_nested_call": symbol("ia64_coop_setup_nested_call"),
            "setup_nested_return": symbol("ia64_coop_setup_nested_return"),
            "setup_exit": symbol("ia64_coop_setup_exit"),
            "setup_call_instruction": setup_call_instruction,
            "setup_return_instruction": setup_return_instruction,
            "probe_after_setup": symbol("ia64_coop_probe_after_setup"),
            "probe_after_a_test": symbol("ia64_coop_probe_after_a_test"),
            "a_resume_first": symbol("ia64_coop_a_resumed_first"),
            "b_resume_second": symbol("ia64_coop_b_resumed"),
            "bsp_check_instruction": bsp_read_instruction,
            "bsp_bounds_valid": symbol("ia64_coop_b_bsp_bounds_valid"),
            "test_fail": symbol("ia64_coop_test_fail"),
            "bsp_current_frame_offset": bsp_expected_offset,
            "bsp_expected_value": bsp_base + bsp_expected_offset,
            "bsp_frame_bytes": bsp_frame_bytes,
            "probe_record_size": size("ia64_coop_probe_record"),
            "a_resume_final": symbol("ia64_coop_a_resumed_final"),
            "a_final_exit": symbol("ia64_coop_a_final_exit"),
            "b_unexpected_resume": symbol("ia64_coop_b_unexpected_resume"),
            "switch_flushrs": switch_flushrs,
            "switch_bspstore": switch_bspstore,
            "switch_return": switch_return,
            "setup_disassembly": setup_disassembly,
            "switch_source": str((script_dir / "context_switch.s").resolve())}


def cooperative_context_probe_expected_layout(
        static_symbols: dict[str, Any]) -> dict[str, dict[str, tuple[int, int]]]:
    """Bind runtime context maps to the stack/RBS ranges in the selected ELF."""
    return {
        identity: {
            "stack": tuple(static_symbols[f"stack_{identity.lower()}"]),
            "rbs": tuple(static_symbols[f"rbs_{identity.lower()}"]),
        }
        for identity in ("A", "B")
    }


def validate_cooperative_context_probe_output(
        stdout_text: str,
        expected_layout: dict[str, dict[str, tuple[int, int]]] | None = None
        ) -> dict[str, Any]:
    def parse_hex_fields(text: str, record_name: str) -> dict[str, int]:
        pairs = re.findall(r"([a-z][a-z0-9_]*)=0x([0-9a-fA-F]+)", text)
        values: dict[str, int] = {}
        for key, value in pairs:
            if key in values:
                raise GateError(f"cooperative probe {record_name} repeats field {key!r}")
            values[key] = int(value, 16)
        return values

    event_pattern = re.compile(
        r"^IA64_COOP_EVENT sequence=0x([0-9a-fA-F]+) context=0x([0-9a-fA-F]+) event=([A-Z_]+)$",
        re.MULTILINE)
    events = [{"sequence": int(match.group(1), 16), "context_id": int(match.group(2), 16),
               "event": match.group(3)} for match in event_pattern.finditer(stdout_text)]
    event_lines = [line for line in stdout_text.splitlines()
                   if line.startswith("IA64_COOP_EVENT")]
    if len(event_lines) != len(events):
        raise GateError("cooperative probe contains a malformed transition record")
    expected = [{"sequence": i + 1, "context_id": COOP_CONTEXT_EVENT_IDS[i],
                 "event": COOP_CONTEXT_EVENT_NAMES[i]} for i in range(8)]
    if events != expected:
        raise GateError(f"cooperative probe transition log is missing, duplicated, or reordered: {events!r}")
    context_lines = [line for line in stdout_text.splitlines()
                     if line.startswith("IA64_COOP_CONTEXT_STATE id=")]
    context_records: dict[str, dict[str, int]] = {}
    for line in context_lines:
        match = re.match(r"IA64_COOP_CONTEXT_STATE id=([AB]) (.*)$", line)
        if match is None or match.group(1) in context_records:
            raise GateError("cooperative probe context map is malformed or duplicated")
        values = parse_hex_fields(match.group(2), f"context {match.group(1)}")
        context_records[match.group(1)] = values
    if set(context_records) != {"A", "B"}:
        raise GateError("cooperative probe did not report both final context maps")
    if expected_layout is not None and set(expected_layout) != {"A", "B"}:
        raise GateError("selected ELF context layout must define exactly contexts A and B")

    required_context_fields = {
        "stack_base", "stack_limit", "sp", "rp", "code_base", "code_limit",
        "rbs_base", "rbs_limit", "switches", "first_entries", "resumes", "bsp",
        "bspstore", "rnat_defined", "rnat_valid_mask", "observed_psr", "gp",
    }
    for identity, values in context_records.items():
        missing = required_context_fields - values.keys()
        if missing:
            raise GateError(f"context {identity} state record is missing {sorted(missing)!r}")
        stack_base, stack_limit, sp = (values["stack_base"], values["stack_limit"], values["sp"])
        rbs_base, rbs_limit = values["rbs_base"], values["rbs_limit"]
        bsp, bspstore = values["bsp"], values["bspstore"]
        rnats = values["rnat_defined"]
        rnat_mask = values["rnat_valid_mask"]
        rnat_index = (bspstore >> 3) & 0x3f
        expected_rnat_mask = ((1 << (rnat_index + 1)) - 1) if rnat_index < 63 else 0
        if stack_base >= stack_limit or not stack_base <= sp <= stack_limit or sp & 15:
            raise GateError(f"context {identity} has invalid stack bounds or alignment")
        if rbs_base >= rbs_limit or rbs_base & 511 or rbs_limit & 511 or \
                not rbs_base <= bsp <= rbs_limit or bsp != bspstore:
            raise GateError(f"context {identity} has invalid backing-store bounds or pointers")
        if expected_layout is not None:
            expected_stack = tuple(expected_layout[identity].get("stack", ()))
            expected_rbs = tuple(expected_layout[identity].get("rbs", ()))
            if (stack_base, stack_limit) != expected_stack or \
                    (rbs_base, rbs_limit) != expected_rbs:
                raise GateError(
                    f"context {identity} stack or RBS identity does not match the selected ELF"
                )
        if not values["code_base"] <= values["rp"] < values["code_limit"] or values["rp"] & 15:
            raise GateError(f"context {identity} has an invalid saved return continuation")
        if values["switches"] != 2 or values["observed_psr"] & 0x4000:
            raise GateError(f"context {identity} switch count or interrupt state is invalid")
        if (rnat_mask != expected_rnat_mask or rnat_mask == 0 or
                rnats & ~rnat_mask):
            raise GateError(f"context {identity} defined RNAT bits do not match the valid mask")
    if (context_records["A"]["first_entries"], context_records["A"]["resumes"]) != (0, 2) or \
            (context_records["B"]["first_entries"], context_records["B"]["resumes"]) != (1, 1):
        raise GateError("cooperative probe restarted a context or missed a resumed entry")
    if context_records["A"]["gp"] == 0 or context_records["B"]["gp"] != 0:
        raise GateError("cooperative probe GP context identities are invalid")

    ranges = {
        "stack A": (context_records["A"]["stack_base"], context_records["A"]["stack_limit"]),
        "stack B": (context_records["B"]["stack_base"], context_records["B"]["stack_limit"]),
        "RBS A": (context_records["A"]["rbs_base"], context_records["A"]["rbs_limit"]),
        "RBS B": (context_records["B"]["rbs_base"], context_records["B"]["rbs_limit"]),
    }
    range_items = list(ranges.items())
    for index, (left_name, left) in enumerate(range_items):
        for right_name, right in range_items[index + 1:]:
            if ranges_overlap(left, right):
                raise GateError(f"cooperative probe memory maps overlap: {left_name} and {right_name}")

    validation = re.search(r"IA64_COOP_CONTEXT_VALIDATION errors=0x0\b([^\r\n]*)", stdout_text)
    if validation is None:
        raise GateError("cooperative probe context validation did not report zero errors")
    fp_validation = re.search(
        r"^IA64_COOP_FP_VALIDATION images=0x50 registers_per_context=0x14 valid=0x1$",
        stdout_text, re.MULTILINE)
    if fp_validation is None:
        raise GateError("cooperative probe did not validate all 20 preserved FP images per context")
    state = validation.group(1)
    values = parse_hex_fields(state, "architectural state")
    required_state = {
        "nat_a_before", "nat_a_after", "nat_b_before", "nat_b_after",
        "pfs_a_before", "pfs_a_after", "pfs_b_before", "pfs_b_after",
        "psr_a", "psr_b", "fp_a_low", "fp_a_high", "fp_b_first_low",
        "fp_b_first_high", "fp_b_resume_low", "fp_b_resume_high",
    }
    if required_state - values.keys():
        raise GateError("cooperative probe architectural state record is incomplete")
    if (values["nat_a_before"], values["nat_a_after"],
            values["nat_b_before"], values["nat_b_after"]) != (1, 1, 0, 0):
        raise GateError("cooperative probe lost or unexpectedly changed a tested NaT state")
    if not values["pfs_a_before"] or values["pfs_a_before"] != values["pfs_a_after"] or \
            not values["pfs_b_before"] or values["pfs_b_before"] != values["pfs_b_after"]:
        raise GateError("cooperative probe PFS state is missing or changed across a resume")
    if values["psr_a"] & 0x4000 or values["psr_b"] & 0x4000:
        raise GateError("cooperative probe resumed with PSR.i enabled")
    if (values["fp_a_low"], values["fp_a_high"], values["fp_b_first_low"],
            values["fp_b_first_high"], values["fp_b_resume_low"],
            values["fp_b_resume_high"]) != (
                0x415f46505f303031, 0x3fff800000000000,
                0x425f46505f303032, 0x3fff800000000000,
                0x425f46505f303032, 0x3fff800000000000):
        raise GateError("cooperative probe preserved floating-point images do not match sentinels")
    if "IA64_COOP_CONTEXT_PROBE_RESULT=PASS" not in stdout_text or \
            "IA64_COOP_CONTEXT_PROBE_RESULT=FAIL" in stdout_text:
        raise GateError("cooperative probe success marker is absent or contradictory")
    return {"events": events, "context_state_lines": context_lines,
            "architectural_state": state.strip(),
            "fp_image_validation": fp_validation.group(0)}


def run_cooperative_context_probe(args: argparse.Namespace) -> int:
    require_linux()
    if not args.artifact.is_file():
        raise GateError(f"selected cooperative context probe ELF does not exist: {args.artifact}")
    static_symbols = validate_cooperative_context_probe_static(
        args.artifact.resolve(), args.manifest.resolve(), args.expected_sha256, args.objdump)
    if args.static_only:
        print(f"[COOPERATIVE_CONTEXT_STATIC_PASS] {json.dumps(static_symbols, sort_keys=True)}")
        return 0
    load_ranges = validate_direct_system_elf(args.artifact)
    simulator = verify_ski(args.ski_prefix, args.simulator_sha256)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(
        prefix="guidexos-ia64-cooperative-context-probe-"))
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        status = run_simulator(
            cooperative_context_probe_command(simulator, snapshot), args.timeout, capture_dir,
            "ia64-cooperative-context-probe",
            success_marker="IA64_COOP_CONTEXT_PROBE_RESULT=PASS",
            artifact_hash=args.expected_sha256,
            resource_limits={
                "address_space_bytes": 2 * 1024 * 1024 * 1024,
                "cpu_soft_seconds": 25,
                "cpu_hard_seconds": 30,
            },
            stdin_pty=True)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "cooperative context probe run result")
    qualification_errors: list[str] = []
    stdout_path = Path(run_record.get("stdout", ""))
    stderr_path = Path(run_record.get("stderr", ""))
    try:
        stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
        stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
        observations = capture_cooperative_context_probe_observations(stdout_text, stderr_text)
        runtime = validate_cooperative_context_probe_output(
            stdout_text, cooperative_context_probe_expected_layout(static_symbols))
    except (OSError, GateError) as exc:
        runtime = None
        observations = {"transition_record_lines": [], "transition_records": [],
                        "malformed_transition_record_lines": [], "context_state_lines": [],
                        "architectural_state_lines": [], "result_marker_lines": [],
                        "last_observed_guest_address": None}
        qualification_errors.append(str(exc))
        if "stdout_text" in locals() and "stderr_text" in locals():
            observations = capture_cooperative_context_probe_observations(
                stdout_text, stderr_text)
    console_error = "Unable to create console window" in \
        (stdout_text + "\n" + stderr_text) if "stdout_text" in locals() and \
        "stderr_text" in locals() else False
    console_started = "IA64_COOP_CONTEXT_PROBE_START" in stdout_text \
        if "stdout_text" in locals() else False
    if console_error:
        console_initialization = "failed"
    elif console_started:
        console_initialization = (
            "passed; report marker follows the completed cooperative context sequence")
    else:
        console_initialization = (
            "not_observed_in_probe_capture; the standalone PTY console smoke is separate evidence")
    if run_record.get("classification") != "SMOKE_PASS":
        qualification_errors.append(f"simulator classification was {run_record.get('classification')!r}")
    if run_record.get("ski_fault_count") != 0:
        qualification_errors.append(f"probe fault count was {run_record.get('ski_fault_count')!r}, expected 0")
    if run_record.get("ski_instruction_count") is None:
        qualification_errors.append("probe instruction count is unavailable")
    if run_record.get("exit_code") != 0 or run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("process_group_cleanup_complete") is not True:
        qualification_errors.append("probe did not exit cleanly within its bound")

    run_record.update({
        "classification": "COOPERATIVE_CONTEXT_PROBE_PASS" if not qualification_errors
        else "COOPERATIVE_CONTEXT_PROBE_FAIL",
        "qualification_errors": qualification_errors,
        "expected_faults": 0,
        "execution_count_authorized": 1,
        "artifact": str(args.artifact.resolve()),
        "artifact_sha256": args.expected_sha256,
        "artifact_manifest": str(args.manifest.resolve()),
        "artifact_manifest_sha256": sha256(args.manifest),
        "simulator_executable": str(simulator),
        "simulator_sha256": args.simulator_sha256,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "simulator_frontend": "bskinc",
        "console_options": ["-noconsole"],
        "console_output_stream": "stdout.log",
        "console_initialization": console_initialization,
        "diagnostic_capture_limitations": [
            "Ski SSC_PUTCHAR output is captured directly to stdout.log during execution.",
            "The probe emits its START, transition, context, and preservation records only after ia64_coop_probe_run returns.",
            "If the batch process is forced to exit before Ski writes -icnt statistics, this frontend may provide no final IP or register snapshot.",
        ],
        "execution_mode": "-forcesystem",
        "cpu_count": 1,
        "cooperative_context_observations": observations,
        "load_ranges": load_ranges,
        "cooperative_context_symbols": static_symbols,
        "cooperative_context_runtime": runtime,
    })
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    classification = run_record["classification"]
    print(f"[{classification}] capture={capture_dir}")
    return 0 if classification == "COOPERATIVE_CONTEXT_PROBE_PASS" else (status or 1)


def verify_break_probe_evidence(manifest: dict[str, Any]) -> dict[str, Any]:
    evidence = manifest.get("break_exception_probe")
    if not isinstance(evidence, dict) or \
            evidence.get("schema") != "guidexos-ia64-break-probe-evidence-v2" or \
            evidence.get("qualified") is not True or \
            evidence.get("expected_faults") != 2:
        raise GateError("candidate manifest lacks a passing independent Break probe record")

    try:
        probe_elf = Path(evidence["artifact"]).resolve(strict=True)
        probe_manifest_path = Path(evidence["manifest"]).resolve(strict=True)
        run_result_path = Path(evidence["run_result"]).resolve(strict=True)
        stdout_path = Path(evidence["stdout"]).resolve(strict=True)
        stderr_path = Path(evidence["stderr"]).resolve(strict=True)
        counts_path = Path(evidence["instruction_counts"]).resolve(strict=True)
    except (KeyError, OSError, TypeError) as exc:
        raise GateError(f"independent Break probe evidence paths are invalid: {exc}") from exc

    if sha256(probe_elf) != evidence.get("artifact_sha256"):
        raise GateError("Break probe ELF changed after its qualifying run")
    if sha256(probe_manifest_path) != evidence.get("manifest_sha256"):
        raise GateError("Break probe manifest changed after its qualifying run")
    if sha256(run_result_path) != evidence.get("run_result_sha256"):
        raise GateError("Break probe run-result changed after candidate binding")
    if sha256(stdout_path) != evidence.get("stdout_sha256"):
        raise GateError("Break probe stdout changed after candidate binding")
    if sha256(stderr_path) != evidence.get("stderr_sha256"):
        raise GateError("Break probe stderr changed after candidate binding")
    if sha256(counts_path) != evidence.get("instruction_counts_sha256"):
        raise GateError("Break probe instruction statistics changed after candidate binding")

    probe_manifest = check_manifest(probe_elf, probe_manifest_path, "break-probe",
                                    evidence["artifact_sha256"],
                                    "guidexos-ia64-break-exception-probe-v2")
    validate_probe_manifest_fields(probe_manifest)
    probe_symbols = validate_break_probe_elf(probe_elf)
    run_record = read_json(run_result_path, "Break probe run result")
    if run_record.get("classification") != "BREAK_EXCEPTION_PROBE_PASS" or \
            run_record.get("artifact_sha256") != evidence["artifact_sha256"] or \
            run_record.get("artifact_manifest_sha256") != evidence["manifest_sha256"] or \
            run_record.get("ski_fault_count") != 2 or \
            run_record.get("break_exception_expected_faults") != 2 or \
            run_record.get("ski_instruction_count") is None or \
            run_record.get("exit_code") != 0 or \
            run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("execution_mode") != "-forcesystem" or \
            run_record.get("cpu_count") != 1 or \
            run_record.get("simulator_sha256") != QUALIFIED_SKI_SHA256 or \
            run_record.get("ski_source_commit") != QUALIFIED_SKI_COMMIT or \
            run_record.get("ski_version") != QUALIFIED_SKI_VERSION or \
            run_record.get("break_exception_evidence_errors") != []:
        raise GateError("independent Break probe result is incomplete or did not qualify")
    if Path(run_record.get("stdout", "")).resolve() != stdout_path or \
            Path(run_record.get("stderr", "")).resolve() != stderr_path or \
            Path(run_record.get("instruction_counts", "")).resolve() != counts_path or \
            run_record.get("stdout_sha256") != evidence["stdout_sha256"] or \
            run_record.get("stderr_sha256") != evidence["stderr_sha256"] or \
            run_record.get("instruction_counts_sha256") != evidence["instruction_counts_sha256"]:
        raise GateError("Break probe run-result does not bind the selected capture files")
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    contract = make_break_contract(
        probe_symbols, "BREAK_EXCEPTION_PROBE_PASS", "IA64_BREAK_PROBE_RECORD",
        "IA64_BREAK_PROBE_RESULT=PASS", "IA64_BREAK_PROBE_RESULT=FAIL",
        "IA64_BREAK_PROBE_VALIDATION")
    contract["stderr_text"] = stderr_text
    errors = validate_break_output(stdout_text, contract)
    if errors:
        raise GateError("independent Break probe output failed revalidation: " + "; ".join(errors))
    observations, observation_errors = parse_break_post_rfi_psr_observations(stdout_text, contract)
    if observation_errors or run_record.get("break_exception_post_rfi_observations") != observations:
        raise GateError("Break probe run-result does not bind both direct post-rfi captures")
    probe_load_ranges = validate_direct_system_elf(probe_elf)
    if run_record.get("load_ranges") != probe_load_ranges:
        raise GateError("Break probe result does not bind the current system-mode PT_LOAD ranges")
    if run_record.get("break_record_symbols") != {
            "slot1": probe_symbols["break_slot1"],
            "slot2": probe_symbols["break_slot2"],
            "post_rfi_slot1_continuation": probe_symbols["post_rfi_slot1_continuation"],
            "post_rfi_slot2_continuation": probe_symbols["post_rfi_slot2_continuation"],
            "rse_base": probe_symbols["rse_base"],
            "rse_top": probe_symbols["rse_top"]}:
        raise GateError("Break probe result does not bind its exception-record/RSE symbols")
    return {"artifact": probe_elf, "artifact_sha256": evidence["artifact_sha256"],
            "manifest": probe_manifest_path,
            "run_result": run_result_path, "symbols": probe_symbols,
            "simulator_sha256": run_record.get("simulator_sha256")}


def verify_general_exception_probe_evidence(manifest: dict[str, Any]) -> dict[str, Any]:
    evidence = manifest.get("general_exception_probe")
    if not isinstance(evidence, dict) or \
            evidence.get("schema") != "guidexos-ia64-general-exception-probe-evidence-v1" or \
            evidence.get("qualified") is not True or evidence.get("expected_faults") != 1 or \
            evidence.get("expected_continuations") != 1:
        raise GateError("candidate manifest lacks a passing independent General Exception probe")

    try:
        probe_elf = Path(evidence["artifact"]).resolve(strict=True)
        probe_manifest_path = Path(evidence["manifest"]).resolve(strict=True)
        run_result_path = Path(evidence["run_result"]).resolve(strict=True)
        stdout_path = Path(evidence["stdout"]).resolve(strict=True)
        stderr_path = Path(evidence["stderr"]).resolve(strict=True)
        counts_path = Path(evidence["instruction_counts"]).resolve(strict=True)
    except (KeyError, OSError, TypeError) as exc:
        raise GateError(f"independent General Exception probe paths are invalid: {exc}") from exc

    for path, key, label in (
            (probe_elf, "artifact_sha256", "ELF"),
            (probe_manifest_path, "manifest_sha256", "manifest"),
            (run_result_path, "run_result_sha256", "run-result"),
            (stdout_path, "stdout_sha256", "stdout"),
            (stderr_path, "stderr_sha256", "stderr"),
            (counts_path, "instruction_counts_sha256", "instruction statistics")):
        if sha256(path) != evidence.get(key):
            raise GateError(f"General Exception probe {label} changed after its qualifying run")

    try:
        GENERAL_EXCEPTION_VALIDATOR.validate_manifest_identity(
            probe_elf, probe_manifest_path, evidence["artifact_sha256"])
        probe_symbols = GENERAL_EXCEPTION_VALIDATOR.exception_symbols(probe_elf)
    except (KeyError, GENERAL_EXCEPTION_VALIDATOR.GateError) as exc:
        raise GateError(f"General Exception probe artifact/manifest no longer qualifies: {exc}") from exc
    run_record = read_json(run_result_path, "General Exception probe run result")
    if run_record.get("classification") != "GENERAL_EXCEPTION_PROBE_PASS" or \
            run_record.get("artifact_sha256") != evidence["artifact_sha256"] or \
            run_record.get("artifact_manifest_sha256") != evidence["manifest_sha256"] or \
            run_record.get("ski_fault_count") != 1 or \
            run_record.get("general_exception_expected_faults") != 1 or \
            run_record.get("expected_exception_faults") != 1 or \
            run_record.get("ski_instruction_count") is None or \
            run_record.get("exit_code") != 0 or run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("execution_mode") != "-forcesystem" or \
            run_record.get("cpu_count") != 1 or \
            run_record.get("simulator_sha256") != QUALIFIED_SKI_SHA256 or \
            run_record.get("ski_source_commit") != QUALIFIED_SKI_COMMIT or \
            run_record.get("ski_version") != QUALIFIED_SKI_VERSION or \
            run_record.get("general_exception_ssc_exit_function") != 66 or \
            run_record.get("general_exception_evidence_errors") != []:
        raise GateError("independent General Exception probe run result is incomplete or did not qualify")
    if Path(run_record.get("stdout", "")).resolve() != stdout_path or \
            Path(run_record.get("stderr", "")).resolve() != stderr_path or \
            Path(run_record.get("instruction_counts", "")).resolve() != counts_path or \
            run_record.get("stdout_sha256") != evidence["stdout_sha256"] or \
            run_record.get("stderr_sha256") != evidence["stderr_sha256"] or \
            run_record.get("instruction_counts_sha256") != evidence["instruction_counts_sha256"]:
        raise GateError("General Exception probe run-result does not bind its capture files")
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    _, output_errors = GENERAL_EXCEPTION_VALIDATOR.validate_output(
        stdout_text, probe_symbols, probe=True)
    unexpected_faults = [line for line in (stdout_text + "\n" + stderr_text).splitlines()
                         if not line.startswith("IA64_GENERAL_EXCEPTION_") and
                         any(marker in line.lower() for marker in FAULT_MARKERS)]
    if unexpected_faults:
        output_errors.append("unexpected Ski fault/exception diagnostic: " + unexpected_faults[0][:180])
    if output_errors:
        raise GateError("independent General Exception probe output failed revalidation: " +
                        "; ".join(output_errors))
    if run_record.get("general_exception_record_fields") is None:
        raise GateError("General Exception probe did not retain its parsed interruption frame")
    if validate_direct_system_elf(probe_elf) != run_record.get("load_ranges"):
        raise GateError("General Exception probe result does not bind its current system-mode PT_LOAD ranges")
    if run_record.get("general_exception_symbols") != probe_symbols:
        raise GateError("General Exception probe result does not bind its frame/continuation/RSE symbols")
    return {"artifact": probe_elf, "artifact_sha256": evidence["artifact_sha256"],
            "manifest": probe_manifest_path, "run_result": run_result_path,
            "symbols": probe_symbols, "simulator_sha256": run_record.get("simulator_sha256"),
            "record_fields": run_record["general_exception_record_fields"]}


def verify_unaligned_reference_probe_evidence(manifest: dict[str, Any],
                                              objdump: str) -> dict[str, Any]:
    evidence = manifest.get("unaligned_reference_probe")
    if not isinstance(evidence, dict) or \
            evidence.get("schema") != "guidexos-ia64-unaligned-reference-probe-evidence-v1" or \
            evidence.get("qualified") is not True or evidence.get("expected_faults") != 1 or \
            evidence.get("expected_continuations") != 1:
        raise GateError("candidate manifest lacks a passing independent Unaligned Reference probe")

    try:
        probe_elf = Path(evidence["artifact"]).resolve(strict=True)
        probe_manifest_path = Path(evidence["manifest"]).resolve(strict=True)
        run_result_path = Path(evidence["run_result"]).resolve(strict=True)
        stdout_path = Path(evidence["stdout"]).resolve(strict=True)
        stderr_path = Path(evidence["stderr"]).resolve(strict=True)
        counts_path = Path(evidence["instruction_counts"]).resolve(strict=True)
    except (KeyError, OSError, TypeError) as exc:
        raise GateError(f"independent Unaligned Reference probe paths are invalid: {exc}") from exc

    for path, key, label in (
            (probe_elf, "artifact_sha256", "ELF"),
            (probe_manifest_path, "manifest_sha256", "manifest"),
            (run_result_path, "run_result_sha256", "run-result"),
            (stdout_path, "stdout_sha256", "stdout"),
            (stderr_path, "stderr_sha256", "stderr"),
            (counts_path, "instruction_counts_sha256", "instruction statistics")):
        if sha256(path) != evidence.get(key):
            raise GateError(f"Unaligned Reference probe {label} changed after its qualifying run")

    try:
        UNALIGNED_REFERENCE_VALIDATOR.validate_manifest_identity(
            probe_elf, probe_manifest_path, evidence["artifact_sha256"])
        probe_symbols = UNALIGNED_REFERENCE_VALIDATOR.exception_symbols(probe_elf)
        static_errors = UNALIGNED_REFERENCE_VALIDATOR.validate_static(
            ROOT, probe_elf, Path(objdump), probe=True)
    except (KeyError, UNALIGNED_REFERENCE_VALIDATOR.GateError) as exc:
        raise GateError(f"Unaligned Reference probe artifact/manifest no longer qualifies: {exc}") from exc
    if static_errors:
        raise GateError("independent Unaligned Reference probe static gates failed: " +
                        "; ".join(static_errors))

    run_record = read_json(run_result_path, "Unaligned Reference probe run result")
    if run_record.get("classification") != "UNALIGNED_REFERENCE_PROBE_PASS" or \
            run_record.get("artifact_sha256") != evidence["artifact_sha256"] or \
            run_record.get("artifact_manifest_sha256") != evidence["manifest_sha256"] or \
            run_record.get("ski_fault_count") != 1 or \
            run_record.get("unaligned_reference_expected_faults") != 1 or \
            run_record.get("expected_exception_faults") != 1 or \
            run_record.get("ski_instruction_count") is None or \
            run_record.get("exit_code") != 0 or run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("execution_mode") != "-forcesystem" or \
            run_record.get("cpu_count") != 1 or \
            run_record.get("simulator_sha256") != QUALIFIED_SKI_SHA256 or \
            run_record.get("ski_source_commit") != QUALIFIED_SKI_COMMIT or \
            run_record.get("ski_version") != QUALIFIED_SKI_VERSION or \
            run_record.get("unaligned_reference_ssc_exit_function") != 66 or \
            run_record.get("unaligned_reference_evidence_errors") != []:
        raise GateError("independent Unaligned Reference probe result is incomplete or did not qualify")
    if Path(run_record.get("stdout", "")).resolve() != stdout_path or \
            Path(run_record.get("stderr", "")).resolve() != stderr_path or \
            Path(run_record.get("instruction_counts", "")).resolve() != counts_path or \
            run_record.get("stdout_sha256") != evidence["stdout_sha256"] or \
            run_record.get("stderr_sha256") != evidence["stderr_sha256"] or \
            run_record.get("instruction_counts_sha256") != evidence["instruction_counts_sha256"]:
        raise GateError("Unaligned Reference probe run-result does not bind its capture files")

    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    fields, output_errors = UNALIGNED_REFERENCE_VALIDATOR.validate_output(
        stdout_text, probe_symbols, probe=True)
    unexpected_faults = [line for line in (stdout_text + "\n" + stderr_text).splitlines()
                         if not line.startswith("IA64_UNALIGNED_REFERENCE_") and
                         any(marker in line.lower() for marker in FAULT_MARKERS)]
    if unexpected_faults:
        output_errors.append("unexpected Ski fault/exception diagnostic: " +
                             unexpected_faults[0][:180])
    if output_errors:
        raise GateError("independent Unaligned Reference probe output failed revalidation: " +
                        "; ".join(output_errors))
    if run_record.get("unaligned_reference_record_fields") != fields:
        raise GateError("Unaligned Reference probe frame differs from its raw guest output")
    if validate_direct_system_elf(probe_elf) != run_record.get("load_ranges"):
        raise GateError("Unaligned Reference probe result does not bind current system-mode PT_LOAD ranges")
    if run_record.get("unaligned_reference_symbols") != probe_symbols:
        raise GateError("Unaligned Reference probe result does not bind its frame/continuation/RSE symbols")
    return {"artifact": probe_elf, "artifact_sha256": evidence["artifact_sha256"],
            "manifest": probe_manifest_path, "run_result": run_result_path,
            "symbols": probe_symbols, "simulator_sha256": run_record.get("simulator_sha256"),
            "record_fields": fields}


def verify_nat_consumption_probe_evidence(manifest: dict[str, Any],
                                          objdump: str) -> dict[str, Any]:
    evidence = manifest.get("nat_consumption_probe")
    if not isinstance(evidence, dict) or \
            evidence.get("schema") != "guidexos-ia64-nat-consumption-probe-evidence-v1" or \
            evidence.get("qualified") is not True or evidence.get("expected_faults") != 1 or \
            evidence.get("expected_continuations") != 1:
        raise GateError("candidate manifest lacks a passing independent Register NaT Consumption probe")
    try:
        probe_elf = Path(evidence["artifact"]).resolve(strict=True)
        probe_manifest_path = Path(evidence["manifest"]).resolve(strict=True)
        run_result_path = Path(evidence["run_result"]).resolve(strict=True)
        stdout_path = Path(evidence["stdout"]).resolve(strict=True)
        stderr_path = Path(evidence["stderr"]).resolve(strict=True)
        counts_path = Path(evidence["instruction_counts"]).resolve(strict=True)
    except (KeyError, OSError, TypeError) as exc:
        raise GateError(f"independent Register NaT Consumption probe paths are invalid: {exc}") from exc

    for path, key, label in (
            (probe_elf, "artifact_sha256", "ELF"),
            (probe_manifest_path, "manifest_sha256", "manifest"),
            (run_result_path, "run_result_sha256", "run-result"),
            (stdout_path, "stdout_sha256", "stdout"),
            (stderr_path, "stderr_sha256", "stderr"),
            (counts_path, "instruction_counts_sha256", "instruction statistics")):
        if sha256(path) != evidence.get(key):
            raise GateError(f"Register NaT Consumption probe {label} changed after its qualifying run")
    try:
        NAT_CONSUMPTION_VALIDATOR.validate_probe_manifest(
            probe_elf, probe_manifest_path, evidence["artifact_sha256"])
        static_errors = NAT_CONSUMPTION_VALIDATOR.validate_static(
            ROOT, probe_elf, Path(objdump), probe=True, manifest_path=probe_manifest_path,
            expected_sha256=evidence["artifact_sha256"])
        probe_symbols = nat_consumption_symbols(ELF_VALIDATOR.Elf64(probe_elf))
    except (KeyError, NAT_CONSUMPTION_VALIDATOR.GateError) as exc:
        raise GateError(f"independent Register NaT Consumption probe no longer qualifies: {exc}") from exc
    if static_errors:
        raise GateError("independent Register NaT Consumption probe static gates failed: " +
                        "; ".join(static_errors))

    run_record = read_json(run_result_path, "Register NaT Consumption probe run result")
    if run_record.get("classification") != "NAT_CONSUMPTION_PROBE_PASS" or \
            run_record.get("artifact_sha256") != evidence["artifact_sha256"] or \
            run_record.get("artifact_manifest_sha256") != evidence["manifest_sha256"] or \
            run_record.get("ski_fault_count") != 1 or \
            run_record.get("nat_consumption_expected_faults") != 1 or \
            run_record.get("expected_exception_faults") != 1 or \
            run_record.get("ski_instruction_count") is None or \
            run_record.get("exit_code") != 0 or run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("execution_mode") != "-forcesystem" or \
            run_record.get("cpu_count") != 1 or \
            run_record.get("simulator_sha256") != QUALIFIED_SKI_SHA256 or \
            run_record.get("ski_source_commit") != QUALIFIED_SKI_COMMIT or \
            run_record.get("ski_version") != QUALIFIED_SKI_VERSION or \
            run_record.get("nat_consumption_ssc_exit_function") != 66 or \
            run_record.get("nat_consumption_evidence_errors") != []:
        raise GateError("independent Register NaT Consumption probe result is incomplete or did not qualify")
    if Path(run_record.get("stdout", "")).resolve() != stdout_path or \
            Path(run_record.get("stderr", "")).resolve() != stderr_path or \
            Path(run_record.get("instruction_counts", "")).resolve() != counts_path or \
            run_record.get("stdout_sha256") != evidence["stdout_sha256"] or \
            run_record.get("stderr_sha256") != evidence["stderr_sha256"] or \
            run_record.get("instruction_counts_sha256") != evidence["instruction_counts_sha256"]:
        raise GateError("Register NaT Consumption probe run-result does not bind its capture files")
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    fields, output_errors = NAT_CONSUMPTION_VALIDATOR.validate_output(
        stdout_text, probe_symbols, probe=True)
    unexpected_faults = [line for line in (stdout_text + "\n" + stderr_text).splitlines()
                         if not line.startswith("IA64_NAT_CONSUMPTION_") and
                         any(marker in line.lower() for marker in FAULT_MARKERS)]
    if unexpected_faults:
        output_errors.append("unexpected Ski fault/exception diagnostic: " +
                             unexpected_faults[0][:180])
    if output_errors:
        raise GateError("independent Register NaT Consumption output failed revalidation: " +
                        "; ".join(output_errors))
    if run_record.get("nat_consumption_record_fields") != fields:
        raise GateError("Register NaT Consumption frame differs from its raw guest output")
    if validate_direct_system_elf(probe_elf) != run_record.get("load_ranges"):
        raise GateError("probe result does not bind current system-mode PT_LOAD ranges")
    if run_record.get("nat_consumption_symbols") != probe_symbols:
        raise GateError("probe result does not bind vector, frame, source, and continuation symbols")
    return {"artifact": probe_elf, "artifact_sha256": evidence["artifact_sha256"],
            "manifest": probe_manifest_path, "run_result": run_result_path,
            "symbols": probe_symbols, "simulator_sha256": run_record.get("simulator_sha256"),
            "record_fields": fields}


def verify_stacked_nat_rse_probe_evidence(manifest: dict[str, Any],
                                          objdump: str) -> dict[str, Any]:
    evidence = manifest.get("stacked_nat_rse_probe")
    if not isinstance(evidence, dict) or \
            evidence.get("schema") != "guidexos-ia64-stacked-nat-rse-probe-evidence-v1" or \
            evidence.get("qualified") is not True or evidence.get("expected_faults") != 0 or \
            evidence.get("expected_continuations") != 0:
        raise GateError("candidate manifest lacks a passing independent stacked NaT/RSE probe")
    try:
        probe_elf = Path(evidence["artifact"]).resolve(strict=True)
        probe_manifest_path = Path(evidence["manifest"]).resolve(strict=True)
        run_result_path = Path(evidence["run_result"]).resolve(strict=True)
        stdout_path = Path(evidence["stdout"]).resolve(strict=True)
        stderr_path = Path(evidence["stderr"]).resolve(strict=True)
        counts_path = Path(evidence["instruction_counts"]).resolve(strict=True)
    except (KeyError, OSError, TypeError) as exc:
        raise GateError(f"independent stacked NaT/RSE probe paths are invalid: {exc}") from exc

    for path, key, label in (
            (probe_elf, "artifact_sha256", "ELF"),
            (probe_manifest_path, "manifest_sha256", "manifest"),
            (run_result_path, "run_result_sha256", "run-result"),
            (stdout_path, "stdout_sha256", "stdout"),
            (stderr_path, "stderr_sha256", "stderr"),
            (counts_path, "instruction_counts_sha256", "instruction statistics")):
        if sha256(path) != evidence.get(key):
            raise GateError(f"independent stacked NaT/RSE probe {label} changed after qualification")
    try:
        probe_symbols = STACKED_NAT_RSE_VALIDATOR.validate_static(
            ROOT, probe_elf, objdump, True, probe_manifest_path, evidence["artifact_sha256"])
    except (KeyError, STACKED_NAT_RSE_VALIDATOR.QualificationError) as exc:
        raise GateError(f"independent stacked NaT/RSE probe no longer qualifies: {exc}") from exc

    run_record = read_json(run_result_path, "stacked NaT/RSE probe run result")
    if run_record.get("classification") != "STACKED_NAT_RSE_PROBE_PASS" or \
            run_record.get("artifact_sha256") != evidence["artifact_sha256"] or \
            run_record.get("artifact_manifest_sha256") != evidence["manifest_sha256"] or \
            run_record.get("ski_fault_count") != 0 or \
            run_record.get("expected_faults") != 0 or \
            run_record.get("ski_instruction_count") is None or \
            run_record.get("exit_code") != 0 or run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("execution_mode") != "-forcesystem" or \
            run_record.get("cpu_count") != 1 or \
            run_record.get("simulator_sha256") != QUALIFIED_SKI_SHA256 or \
            run_record.get("ski_source_commit") != QUALIFIED_SKI_COMMIT or \
            run_record.get("ski_version") != QUALIFIED_SKI_VERSION or \
            run_record.get("qualification_errors") != []:
        raise GateError("independent stacked NaT/RSE probe result is incomplete or failed")
    if Path(run_record.get("stdout", "")).resolve() != stdout_path or \
            Path(run_record.get("stderr", "")).resolve() != stderr_path or \
            Path(run_record.get("instruction_counts", "")).resolve() != counts_path or \
            run_record.get("stdout_sha256") != evidence["stdout_sha256"] or \
            run_record.get("stderr_sha256") != evidence["stderr_sha256"] or \
            run_record.get("instruction_counts_sha256") != evidence["instruction_counts_sha256"]:
        raise GateError("stacked NaT/RSE probe run-result does not bind its capture files")

    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    try:
        runtime = STACKED_NAT_RSE_VALIDATOR.validate_output(stdout_text)
    except STACKED_NAT_RSE_VALIDATOR.QualificationError as exc:
        raise GateError(f"stacked NaT/RSE probe output failed revalidation: {exc}") from exc
    unexpected_faults = [line for line in (stdout_text + "\n" + stderr_text).splitlines()
                         if not line.startswith("IA64_STACKED_NAT_RSE_") and
                         any(marker in line.lower() for marker in FAULT_MARKERS)]
    if unexpected_faults:
        raise GateError("stacked NaT/RSE probe emitted a fault diagnostic: " +
                        unexpected_faults[0][:180])
    if run_record.get("stacked_nat_rse_runtime") != runtime or \
            validate_direct_system_elf(probe_elf) != run_record.get("load_ranges") or \
            run_record.get("stacked_nat_rse_symbols") != probe_symbols:
        raise GateError("stacked NaT/RSE probe result does not bind current ELF/runtime evidence")
    return {"artifact": probe_elf, "artifact_sha256": evidence["artifact_sha256"],
            "manifest": probe_manifest_path, "run_result": run_result_path,
            "symbols": probe_symbols, "simulator_sha256": run_record.get("simulator_sha256"),
            "runtime": runtime}


def verify_kernel_resource_limits(timeout: float) -> dict[str, int | float]:
    if timeout <= 0 or timeout > 60:
        raise GateError("kernel execution requires a wall-clock timeout no greater than 60 seconds")
    try:
        import resource
    except ImportError as exc:
        raise GateError("kernel execution requires Linux RLIMIT_AS and RLIMIT_CPU support") from exc

    address_space = resource.getrlimit(resource.RLIMIT_AS)
    cpu_time = resource.getrlimit(resource.RLIMIT_CPU)
    address_cap = 2 * 1024 * 1024 * 1024
    cpu_cap = 120
    infinity = resource.RLIM_INFINITY
    if any(limit == infinity or limit > address_cap for limit in address_space):
        raise GateError("kernel runner must inherit the established 2 GiB address-space limit")
    if any(limit == infinity or limit > cpu_cap for limit in cpu_time):
        raise GateError("kernel runner must inherit the established 120-second CPU limit")
    return {
        "address_space_soft_bytes": int(address_space[0]),
        "address_space_hard_bytes": int(address_space[1]),
        "cpu_soft_seconds": int(cpu_time[0]),
        "cpu_hard_seconds": int(cpu_time[1]),
        "wall_clock_seconds": timeout,
    }


def run_kernel(args: argparse.Namespace) -> int:
    require_linux()
    if args.profile == "compile-only":
        raise GateError("compile-only artifacts are permanently non-executable through this runner")
    if not args.artifact.is_file():
        raise GateError(f"selected kernel ELF does not exist: {args.artifact}")
    manifest = check_manifest(args.artifact, args.manifest, args.profile, args.expected_sha256)
    if not args.authorize_kernel_guest_execution:
        raise GateError("kernel guest execution requires --authorize-kernel-guest-execution")

    simulator_hash = None
    try:
        simulator_hash = sha256((args.ski_prefix / "bin" / "ski").resolve(strict=True))
    except OSError as exc:
        raise GateError(f"installed Ski binary is unavailable: {exc}") from exc
    loader_record = verify_loader_record(args.loader_record, manifest["sha256"], args.profile, simulator_hash)
    simulator = verify_ski(args.ski_prefix, simulator_hash)
    validate_direct_system_elf(args.artifact)

    if args.profile == "early-exec":
        if manifest.get("static_gates") != "PASS" or manifest.get("runtime_qualified") is not False or \
                manifest.get("scheduler_qualified") is not False or \
                manifest.get("interrupt_fp_preservation") is not False or \
                manifest.get("cpu_state_checkpoint_schema") != "guidexos-ia64-cpu-state-checkpoints-v1" or \
                manifest.get("cpu_state_static_gates") != "PASS" or \
                manifest.get("break_exception_static_gates") != "PASS" or \
                manifest.get("general_exception_static_gates") != "PASS" or \
                manifest.get("unaligned_reference_static_gates") != "PASS" or \
                manifest.get("nat_consumption_static_gates") != "PASS" or \
                manifest.get("stacked_nat_rse_static_gates") != "PASS" or \
                manifest.get("post_rfi_static_gates") != "PASS" or \
                manifest.get("expected_exception_faults") != 5 or \
                manifest.get("general_exception_expected_faults") != 1 or \
                manifest.get("unaligned_reference_expected_faults") != 1 or \
                manifest.get("nat_consumption_expected_faults") != 1 or \
                manifest.get("stacked_nat_rse_expected_faults") != 0 or \
                manifest.get("guest_execution_authorized") is not False:
            raise GateError("early-exec manifest or static gates are incomplete")
        probe_evidence = verify_break_probe_evidence(manifest)
        general_probe_evidence = verify_general_exception_probe_evidence(manifest)
        unaligned_probe_evidence = verify_unaligned_reference_probe_evidence(
            manifest, args.objdump)
        nat_probe_evidence = verify_nat_consumption_probe_evidence(manifest, args.objdump)
        stacked_nat_rse_probe_evidence = verify_stacked_nat_rse_probe_evidence(
            manifest, args.objdump)
        if probe_evidence["simulator_sha256"] != simulator_hash or \
                general_probe_evidence["simulator_sha256"] != simulator_hash or \
                unaligned_probe_evidence["simulator_sha256"] != simulator_hash or \
                nat_probe_evidence["simulator_sha256"] != simulator_hash or \
                stacked_nat_rse_probe_evidence["simulator_sha256"] != simulator_hash:
            raise GateError("kernel and all independent exception probes must use the same qualified Ski binary")
        validator_cmd = [sys.executable, str(ROOT / "scripts" / "validate-ia64-elf.py"),
                         "--elf", str(args.artifact), "--repo-root", str(ROOT),
                         "--profile", args.profile, "--manifest", str(args.manifest)]
        validation_env = os.environ.copy()
        validation_env["PYTHONDONTWRITEBYTECODE"] = "1"
        result = subprocess.run(validator_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("independent IA-64 ELF/profile validation failed")
        gate_cmd = [sys.executable, str(ROOT / "scripts" / "validate-ia64-early-exec.py"),
                    "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump]
        result = subprocess.run(gate_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("early-exec static source/disassembly gates failed")
        exception_gate_cmd = [sys.executable,
                              str(ROOT / "scripts" / "validate-ia64-break-exception.py"),
                              "--repo-root", str(ROOT), "--elf", str(args.artifact),
                              "--objdump", args.objdump]
        result = subprocess.run(exception_gate_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("Break exception static source/disassembly gates failed")
        general_exception_gate_cmd = [
            sys.executable, str(ROOT / "scripts" / "validate-ia64-general-exception.py"),
            "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump,
            "--manifest", str(args.manifest), "--expected-sha256", args.expected_sha256]
        result = subprocess.run(general_exception_gate_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("General Exception static source/disassembly/identity gates failed")
        unaligned_reference_gate_cmd = [
            sys.executable, str(ROOT / "scripts" / "validate-ia64-unaligned-reference.py"),
            "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump,
            "--manifest", str(args.manifest), "--expected-sha256", args.expected_sha256]
        result = subprocess.run(unaligned_reference_gate_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("Unaligned Reference static source/disassembly/identity gates failed")
        nat_consumption_gate_cmd = [
            sys.executable, str(ROOT / "scripts" / "validate-ia64-nat-consumption.py"),
            "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump,
            "--manifest", str(args.manifest), "--expected-sha256", args.expected_sha256]
        result = subprocess.run(nat_consumption_gate_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("Register NaT Consumption static source/disassembly/identity gates failed")
        stacked_nat_rse_gate_cmd = [
            sys.executable, str(ROOT / "scripts" / "validate-ia64-stacked-nat-rse.py"),
            "--kernel", "--repo-root", str(ROOT), "--elf", str(args.artifact),
            "--objdump", args.objdump]
        result = subprocess.run(stacked_nat_rse_gate_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("stacked NaT/RSE kernel integration gates failed")
        cpu_state_cmd = [sys.executable, str(ROOT / "scripts" / "validate-ia64-cpu-state.py"),
                         "--repo-root", str(ROOT), "--elf", str(args.artifact), "--objdump", args.objdump]
        result = subprocess.run(cpu_state_cmd, capture_output=True, text=True,
                                check=False, env=validation_env)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr, end="")
            raise GateError("CPU-state checkpoint static gates failed")
    elif manifest.get("runtime_qualified") is not True or \
            manifest.get("scheduler_qualified") is not True or \
            manifest.get("interrupt_fp_preservation") is not True or \
            manifest.get("guest_execution_authorized") is not True:
        raise GateError("runtime profile lacks complete runtime and guest qualification records")

    execution_limits = verify_kernel_resource_limits(args.timeout)
    capture_dir = args.capture_dir or Path(tempfile.mkdtemp(prefix="guidexos-ia64-kernel-"))
    candidate_load_ranges = validate_direct_system_elf(args.artifact)
    checkpoint_expectations = None
    break_exception_contract = None
    general_exception_contract = None
    unaligned_reference_contract = None
    nat_consumption_contract = None
    if args.profile == "early-exec":
        candidate_elf = ELF_VALIDATOR.Elf64(args.artifact)
        required_symbols = ("ia64_ivt", "rse_backing_store_base", "rse_backing_store_top",
                            "ia64_break_slot1_bundle", "ia64_break_slot2_bundle",
                            "ia64_break_slot1_post_rfi_capture",
                            "ia64_break_slot2_continuation",
                            "ia64_general_exception_fault_site",
                            "ia64_general_exception_continuation",
                            "ia64_unaligned_reference_record",
                            "ia64_unaligned_reference_buffer",
                            "ia64_unaligned_reference_fault_site",
                            "ia64_unaligned_reference_continuation",
                            "ia64_nat_consumption_record",
                            "ia64_nat_consumption_fault_site",
                            "ia64_nat_consumption_continuation",
                            "ia64_stacked_nat_rse_record",
                            "ia64_stacked_nat_rse_test",
                            "ia64_stacked_nat_rse_pressure_child")
        if any(name not in candidate_elf.symbols for name in required_symbols):
            raise GateError("candidate ELF lacks CPU-state checkpoint expectation symbols")
        checkpoint_expectations = {
            "ivt": candidate_elf.symbols["ia64_ivt"]["value"],
            "rse_base": candidate_elf.symbols["rse_backing_store_base"]["value"],
            "rse_top": candidate_elf.symbols["rse_backing_store_top"]["value"],
        }
        break_exception_contract = make_break_contract(
            {"break_slot1": candidate_elf.symbols["ia64_break_slot1_bundle"]["value"],
             "break_slot2": candidate_elf.symbols["ia64_break_slot2_bundle"]["value"],
             "post_rfi_slot1_continuation": candidate_elf.symbols[
                 "ia64_break_slot1_post_rfi_capture"]["value"],
             "post_rfi_slot2_continuation": candidate_elf.symbols[
                 "ia64_break_slot2_continuation"]["value"],
             "rse_base": candidate_elf.symbols["rse_backing_store_base"]["value"],
             "rse_top": candidate_elf.symbols["rse_backing_store_top"]["value"]},
            "BREAK_EXCEPTION_KERNEL_PASS", "IA64_BREAK_RECORD",
            "IA64_BREAK_EXCEPTION_RESULT=PASS", "IA64_BREAK_EXCEPTION_RESULT=FAIL",
            "IA64_BREAK_EXCEPTION_VALIDATION")
        general_exception_contract = {
            "expected_faults": 1,
            "symbols": {
                "ivt": candidate_elf.symbols["ia64_ivt"]["value"],
                "fault_site": candidate_elf.symbols[
                    "ia64_general_exception_fault_site"]["value"],
                "continuation": candidate_elf.symbols[
                    "ia64_general_exception_continuation"]["value"],
                "rse_base": candidate_elf.symbols["rse_backing_store_base"]["value"],
                "rse_top": candidate_elf.symbols["rse_backing_store_top"]["value"],
            },
            "probe": False,
            "success_classification": "GENERAL_EXCEPTION_KERNEL_PASS",
            "require_intentional_exit": True,
            "ssc_exit_function": 66,
        }
        unaligned_reference_contract = {
            "expected_faults": 1,
            "symbols": {
                "ivt": candidate_elf.symbols["ia64_ivt"]["value"],
                "vector": candidate_elf.symbols["ivt_unaligned_ref"]["value"],
                "entry": candidate_elf.symbols["ia64_unaligned_reference_entry"]["value"],
                "record": candidate_elf.symbols["ia64_unaligned_reference_record"]["value"],
                "buffer": candidate_elf.symbols["ia64_unaligned_reference_buffer"]["value"],
                "fault_site": candidate_elf.symbols[
                    "ia64_unaligned_reference_fault_site"]["value"],
                "continuation": candidate_elf.symbols[
                    "ia64_unaligned_reference_continuation"]["value"],
                "test": candidate_elf.symbols["ia64_unaligned_reference_test"]["value"],
                "rse_base": candidate_elf.symbols["rse_backing_store_base"]["value"],
                "rse_top": candidate_elf.symbols["rse_backing_store_top"]["value"],
            },
            "probe": False,
            "success_classification": "UNALIGNED_REFERENCE_KERNEL_PASS",
            "require_intentional_exit": True,
            "ssc_exit_function": 66,
        }
        nat_consumption_contract = {
            "expected_faults": 1,
            "symbols": nat_consumption_symbols(candidate_elf),
            "probe": False,
            "success_classification": "NAT_CONSUMPTION_KERNEL_PASS",
            "require_intentional_exit": True,
            "ssc_exit_function": 66,
        }
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator), "-forcesystem", str(snapshot)]
        status = run_simulator(command, args.timeout, capture_dir,
                               "guideXOS-kernel", artifact_hash=args.expected_sha256,
                               checkpoint_expectations=checkpoint_expectations,
                               break_exception_contract=break_exception_contract,
                               general_exception_contract=general_exception_contract,
                               unaligned_reference_contract=unaligned_reference_contract,
                               nat_consumption_contract=nat_consumption_contract)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "kernel run result")
    stacked_runtime = None
    stacked_errors: list[str] = []
    if args.profile == "early-exec":
        try:
            stacked_runtime = STACKED_NAT_RSE_VALIDATOR.validate_output(
                Path(run_record.get("stdout", "")).read_text(
                    encoding="utf-8", errors="replace"),
                "IA64_STACKED_NAT_RSE_KERNEL_RESULT=PASS")
        except (OSError, STACKED_NAT_RSE_VALIDATOR.QualificationError) as exc:
            stacked_errors.append(str(exc))
        if stacked_errors:
            run_record["classification_before_stacked_nat_rse"] = run_record.get("classification")
            run_record["classification"] = "STACKED_NAT_RSE_KERNEL_FAIL"
            status = 1
        run_record["stacked_nat_rse_expected_faults"] = 0
        run_record["stacked_nat_rse_evidence_errors"] = stacked_errors
        run_record["stacked_nat_rse_runtime"] = stacked_runtime
    run_record.update({
        "artifact_manifest": str(args.manifest.resolve()),
        "artifact_manifest_sha256": sha256(args.manifest),
        "loader_record": str(args.loader_record.resolve()),
        "loader_record_sha256": sha256(args.loader_record),
        "loader_record_kernel_sha256": loader_record["kernel_sha256"],
        "simulator_sha256": simulator_hash,
        "ski_source_commit": QUALIFIED_SKI_COMMIT,
        "ski_version": QUALIFIED_SKI_VERSION,
        "execution_mode": "-forcesystem",
        "cpu_count": 1,
        "load_ranges": candidate_load_ranges,
        "execution_limits": execution_limits,
    })
    if args.profile == "early-exec":
        run_record["break_record_symbols"] = {
            "slot1": candidate_elf.symbols["ia64_break_slot1_bundle"]["value"],
            "slot2": candidate_elf.symbols["ia64_break_slot2_bundle"]["value"],
            "rse_base": candidate_elf.symbols["rse_backing_store_base"]["value"],
            "rse_top": candidate_elf.symbols["rse_backing_store_top"]["value"],
        }
        run_record["break_probe_artifact_sha256"] = probe_evidence["artifact_sha256"]
        run_record["break_probe_run_result"] = str(probe_evidence["run_result"])
        run_record["break_probe_simulator_sha256"] = probe_evidence["simulator_sha256"]
        run_record["general_exception_probe_artifact_sha256"] = \
            general_probe_evidence["artifact_sha256"]
        run_record["general_exception_probe_run_result"] = \
            str(general_probe_evidence["run_result"])
        run_record["general_exception_probe_simulator_sha256"] = \
            general_probe_evidence["simulator_sha256"]
        run_record["general_exception_expected_faults"] = 1
        run_record["unaligned_reference_probe_artifact_sha256"] = \
            unaligned_probe_evidence["artifact_sha256"]
        run_record["unaligned_reference_probe_run_result"] = \
            str(unaligned_probe_evidence["run_result"])
        run_record["unaligned_reference_probe_simulator_sha256"] = \
            unaligned_probe_evidence["simulator_sha256"]
        run_record["unaligned_reference_expected_faults"] = 1
        run_record["nat_consumption_probe_artifact_sha256"] = nat_probe_evidence["artifact_sha256"]
        run_record["nat_consumption_probe_run_result"] = str(nat_probe_evidence["run_result"])
        run_record["nat_consumption_probe_simulator_sha256"] = \
            nat_probe_evidence["simulator_sha256"]
        run_record["nat_consumption_expected_faults"] = 1
        run_record["stacked_nat_rse_probe_artifact_sha256"] = \
            stacked_nat_rse_probe_evidence["artifact_sha256"]
        run_record["stacked_nat_rse_probe_run_result"] = \
            str(stacked_nat_rse_probe_evidence["run_result"])
        run_record["stacked_nat_rse_probe_simulator_sha256"] = \
            stacked_nat_rse_probe_evidence["simulator_sha256"]
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    return status


def main(argv: list[str] | None = None) -> int:
    parser = setup_parser()
    args = parser.parse_args(argv)
    try:
        if args.mode != "system":
            raise GateError("only the verified Ski system mode is supported")
        if args.operation == "smoke":
            return run_smoke(args)
        if args.operation == "break-probe":
            return run_break_probe(args)
        if args.operation == "general-exception-probe":
            return run_general_exception_probe(args)
        if args.operation == "unaligned-reference-probe":
            return run_unaligned_reference_probe(args)
        if args.operation == "nat-consumption-probe":
            return run_nat_consumption_probe(args)
        if args.operation == "stacked-nat-rse-probe":
            return run_stacked_nat_rse_probe(args)
        if args.operation == "cooperative-context-probe":
            return run_cooperative_context_probe(args)
        return run_kernel(args)
    except GateError as exc:
        print(f"[RUN_BLOCKED] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

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


def validate_break_output(stdout_text: str, contract: dict[str, Any]) -> list[str]:
    errors: list[str] = []
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
        if ipsr is None or ((ipsr >> 41) & 3) != slot or not (ipsr & (1 << 13)) or \
                (ipsr & (1 << 14)) or not (ipsr & (1 << 44)) or \
                (ipsr & (3 << 32)) or (ipsr & (1 << 46)):
            errors.append(f"{name} saved IPSR does not match the expected faulting state")
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
        if line.startswith("IA64_BREAK_"):
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


def _kill_process_group(process: subprocess.Popen[bytes], grace_seconds: float = 1.0) -> None:
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=grace_seconds)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    # Kill helper children too, including xterm if an unexpected build opens one.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    time.sleep(0.05)
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def run_simulator(command: list[str], timeout: float, capture_dir: Path,
                  label: str, success_marker: str | None = None,
                  artifact_hash: str | None = None,
                  checkpoint_expectations: dict[str, int] | None = None,
                  break_exception_contract: dict[str, Any] | None = None) -> int:
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
    started = time.monotonic()
    timed_out = False
    with stdout_path.open("xb") as stdout_file, stderr_path.open("xb") as stderr_file:
        process = subprocess.Popen(command, stdout=stdout_file, stderr=stderr_file,
                                   start_new_session=True, close_fds=True)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
        finally:
            _kill_process_group(process)
    elapsed = time.monotonic() - started
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
    combined = (stdout_text + "\n" + stderr_text).lower()
    return_code = process.returncode
    fault_count, instruction_count = extract_ski_counts(stdout_text, stderr_text, counts_path)
    break_evidence_errors: list[str] = []

    if timed_out:
        classification = "TIMEOUT"
    elif break_exception_contract is not None:
        output_contract = dict(break_exception_contract)
        output_contract["stderr_text"] = stderr_text
        break_evidence_errors = validate_break_output(stdout_text, output_contract)
        if return_code != 0:
            classification = "SIMULATOR_ERROR"
        elif fault_count != break_exception_contract["expected_faults"]:
            break_evidence_errors.append(
                f"Ski fault count={fault_count!r}, expected {break_exception_contract['expected_faults']}")
            classification = "BREAK_EXCEPTION_QUALIFICATION_FAIL"
        elif instruction_count is None:
            break_evidence_errors.append("Ski instruction count was not present in captured statistics")
            classification = "BREAK_EXCEPTION_QUALIFICATION_FAIL"
        elif process.poll() is None:
            break_evidence_errors.append("simulator process group was not reaped")
            classification = "BREAK_EXCEPTION_QUALIFICATION_FAIL"
        elif break_evidence_errors:
            classification = "BREAK_EXCEPTION_QUALIFICATION_FAIL"
        else:
            classification = break_exception_contract["success_classification"]
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
        if classification in ("GUEST_EXIT_ZERO", "BREAK_EXCEPTION_KERNEL_PASS") and checkpoint_errors:
            classification = "CPU_STATE_CHECKPOINT_FAIL"

    record = {
        "label": label,
        "command": command,
        "artifact_sha256": artifact_hash,
        "classification": classification,
        "exit_code": return_code,
        "timed_out": timed_out,
        "cleanup_complete": process.poll() is not None,
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
        "break_exception_evidence_errors": break_evidence_errors,
        "break_exception_expected_faults": (
            break_exception_contract["expected_faults"] if break_exception_contract else None),
    }
    (capture_dir / "run-result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"[{classification}] exit={return_code} elapsed={elapsed:.3f}s capture={capture_dir}")
    return 0 if classification in ("SMOKE_PASS", "GUEST_EXIT_ZERO",
                                    "BREAK_EXCEPTION_PROBE_PASS",
                                    "BREAK_EXCEPTION_KERNEL_PASS") else 1


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
        command = [str(simulator), "-forcesystem", str(snapshot)]
        return run_simulator(command, args.timeout, capture_dir, "system-mode-smoke",
                             SMOKE_MARKER, args.expected_sha256)


def validate_probe_manifest_fields(manifest: dict[str, Any]) -> None:
    expected = {
        "schema": "guidexos-ia64-break-exception-probe-v1",
        "profile": "break-probe",
        "guest_execution_authorized": "probe-only",
        "system_mode": "-forcesystem",
        "cpu_count": 1,
        "ivt_vector_offset": 0x2c00,
        "break_immediates": [0x6a17, 0x6a18],
        "expected_break_faults": 2,
        "expected_continuations": [1, 1],
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
                              "guidexos-ia64-break-exception-probe-v1")
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
            "rse_base": symbols["rse_base"], "rse_top": symbols["rse_top"],
        },
    })
    result_path.write_text(json.dumps(run_record, indent=2) + "\n", encoding="utf-8")
    return status


def verify_break_probe_evidence(manifest: dict[str, Any]) -> dict[str, Any]:
    evidence = manifest.get("break_exception_probe")
    if not isinstance(evidence, dict) or \
            evidence.get("schema") != "guidexos-ia64-break-probe-evidence-v1" or \
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
                                    "guidexos-ia64-break-exception-probe-v1")
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
    probe_load_ranges = validate_direct_system_elf(probe_elf)
    if run_record.get("load_ranges") != probe_load_ranges:
        raise GateError("Break probe result does not bind the current system-mode PT_LOAD ranges")
    if run_record.get("break_record_symbols") != {
            "slot1": probe_symbols["break_slot1"],
            "slot2": probe_symbols["break_slot2"],
            "rse_base": probe_symbols["rse_base"],
            "rse_top": probe_symbols["rse_top"]}:
        raise GateError("Break probe result does not bind its exception-record/RSE symbols")
    return {"artifact": probe_elf, "artifact_sha256": evidence["artifact_sha256"],
            "manifest": probe_manifest_path,
            "run_result": run_result_path, "symbols": probe_symbols,
            "simulator_sha256": run_record.get("simulator_sha256")}


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
                manifest.get("guest_execution_authorized") is not False:
            raise GateError("early-exec manifest or static gates are incomplete")
        probe_evidence = verify_break_probe_evidence(manifest)
        if probe_evidence["simulator_sha256"] != simulator_hash:
            raise GateError("kernel and independent Break probe must use the same qualified Ski binary")
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
    if args.profile == "early-exec":
        candidate_elf = ELF_VALIDATOR.Elf64(args.artifact)
        required_symbols = ("ia64_ivt", "rse_backing_store_base", "rse_backing_store_top")
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
             "rse_base": candidate_elf.symbols["rse_backing_store_base"]["value"],
             "rse_top": candidate_elf.symbols["rse_backing_store_top"]["value"]},
            "BREAK_EXCEPTION_KERNEL_PASS", "IA64_BREAK_RECORD",
            "IA64_BREAK_EXCEPTION_RESULT=PASS", "IA64_BREAK_EXCEPTION_RESULT=FAIL",
            "IA64_BREAK_EXCEPTION_VALIDATION")
    with verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
        validate_direct_system_elf(snapshot)
        command = [str(simulator), "-forcesystem", str(snapshot)]
        status = run_simulator(command, args.timeout, capture_dir,
                               "guideXOS-kernel", artifact_hash=args.expected_sha256,
                               checkpoint_expectations=checkpoint_expectations,
                               break_exception_contract=break_exception_contract)

    result_path = capture_dir / "run-result.json"
    run_record = read_json(result_path, "kernel run result")
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
        return run_kernel(args)
    except GateError as exc:
        print(f"[RUN_BLOCKED] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

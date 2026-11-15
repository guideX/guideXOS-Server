#!/usr/bin/env python3
"""Bounded, one-run Ski capture of the first Context B exception."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import time
from typing import Any

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = Path("/home/guidex/src/ia64-5ar7-context-b-exception-20261114-r1")
VECTOR_OFFSET = 0x5400
PAUSE_OFFSET = 0x5410
MAX_BREAKPOINTS = 6
INITIAL_STEPS = (64, 1)
POST_START_STEPS = (135076, 512, 512, 512)
MAX_GUEST_INSTRUCTIONS = sum(INITIAL_STEPS + POST_START_STEPS)
MAX_WALL_SECONDS = 30
CPU_SOFT_SECONDS = 20
CPU_HARD_SECONDS = 22
ADDRESS_SPACE_BYTES = 2 * 1024 * 1024 * 1024
RNAT_CONTEXT_ID_OFFSET = 568
SLOT_OBJ_DUMP_OFFSETS = (0, 6, 12)
SNAPSHOT_NAMES = (
    "initial", "after-startup-64", "after-startup-1", "context-b-first-entry",
    "context-b-bootstrap-call-boundary", "context-b-test-entry", "first-post-b-stop",
)
EVAL_REGISTERS = (
    "iva", "psr", "rsc", "bsp", "bspst", "rnat", "unat", "pfs", "sp", "gp",
    "b0", "r32", "r33", "r42", "r43", "iip", "ipsr", "isr", "ifs", "iipa",
)
ISR_CLASSIFICATIONS = {
    0x00: "illegal-operation",
    0x10: "privileged-operation",
    0x20: "privileged-register",
    0x30: "reserved-register-field",
    0x40: "disabled-instruction-set-transition",
}


class DiagnosticError(ValueError):
    pass


def load_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise DiagnosticError(f"cannot load diagnostic helper: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_base_diagnostic() -> Any:
    return load_module("ia64_coop_diag_for_context_b_exception",
                       ROOT / "scripts" / "diagnose-ia64-coop-switch.py")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json_new(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def exception_targets(elf: Any, symbols: dict[str, Any]) -> tuple[dict[str, int], list[dict[str, Any]]]:
    required = (
        "ia64_ivt", "ia64_coop_b_first_entry", "ia64_coop_b_first_entry_call",
        "ia64_coop_b_test", "ia64_coop_test_fail", "ia64_coop_context_a",
        "ia64_coop_context_b", "ia64_coop_probe_record",
    )
    missing = [name for name in required if name not in elf.symbols]
    if missing:
        raise DiagnosticError("probe ELF is missing required symbols: " + ", ".join(missing))

    ivt_section = elf.section(".ivt")
    ivt = elf.symbols["ia64_ivt"]["value"]
    if ivt_section is None or ivt_section["addr"] != ivt or \
            ivt_section["size"] != 0x8000 or ivt & 0x7fff:
        raise DiagnosticError("probe ELF does not have the expected aligned 32 KiB IVT")

    addresses = {
        "ivt": ivt,
        "general_exception": ivt + VECTOR_OFFSET,
        "pause_loop": ivt + PAUSE_OFFSET,
        "context_b_first": elf.symbols["ia64_coop_b_first_entry"]["value"],
        "context_b_call_boundary": elf.symbols["ia64_coop_b_first_entry_call"]["value"],
        "context_b_test": elf.symbols["ia64_coop_b_test"]["value"],
        "test_fail": elf.symbols["ia64_coop_test_fail"]["value"],
        "context_a": elf.symbols["ia64_coop_context_a"]["value"],
        "context_b": elf.symbols["ia64_coop_context_b"]["value"],
        "probe_record": elf.symbols["ia64_coop_probe_record"]["value"],
    }
    if addresses["ivt"] != symbols["ivt"]:
        raise DiagnosticError("base diagnostic and selected ELF disagree on the IVT base")
    text_start, text_end = symbols["text"]
    for key in ("context_b_first", "context_b_call_boundary", "context_b_test", "test_fail"):
        if not text_start <= addresses[key] < text_end:
            raise DiagnosticError(f"{key} is outside the probe's executable text")
    if not (ivt <= addresses["general_exception"] < ivt + 0x8000 and
            ivt <= addresses["pause_loop"] < ivt + 0x8000):
        raise DiagnosticError("General Exception or pause checkpoint is outside the IVT")

    targets = [
        {"address": addresses["context_b_first"], "label": "Context B first instruction"},
        {"address": addresses["context_b_call_boundary"],
         "label": "Context B bootstrap call boundary"},
        {"address": addresses["context_b_test"], "label": "Context B test entry"},
        {"address": addresses["general_exception"],
         "label": "IVA plus General Exception vector offset"},
        {"address": addresses["pause_loop"], "label": "observed IVT pause loop"},
        {"address": addresses["test_fail"], "label": "probe assertion fail-stop fallback"},
    ]
    if len({item["address"] & ~0x3 for item in targets}) != len(targets):
        raise DiagnosticError("exception diagnostic has duplicate/equivalent breakpoints")
    if len(targets) > MAX_BREAKPOINTS:
        raise DiagnosticError("exception diagnostic exceeds the six-breakpoint limit")
    return addresses, targets


def _append_checkpoint(lines: list[str], root: Path, index: int, name: str,
                       addresses: dict[str, int], include_contexts: bool) -> None:
    lines.extend((
        "eval ip+0 $insts$",
        "eval " + " ".join(EVAL_REGISTERS),
        f"rd {root / 'registers' / f'{index:02d}-{name}.txt'}",
    ))
    if include_contexts:
        for which in ("a", "b"):
            base = addresses[f"context_{which}"]
            lines.append(
                f"dd 0x{base:x} 0x{base + 1023:x} "
                f"{root / 'contexts' / f'{index:02d}-{name}-{which}.txt'}")
        record = addresses["probe_record"]
        lines.append(
            f"dd 0x{record:x} 0x{record + 343:x} "
            f"{root / 'records' / f'{index:02d}-{name}.txt'}")
    lines.append("if $exited$ quit")


def make_command_file(root: Path, addresses: dict[str, int],
                      targets: list[dict[str, Any]], symbols: dict[str, Any]) -> str:
    root = root.resolve()
    paths = (root, root / "registers", root / "contexts", root / "records")
    if any(any(ch.isspace() for ch in str(path)) for path in paths):
        raise DiagnosticError("Ski debugger command-file paths cannot contain whitespace")

    lines = [
        "eval ip+0 $insts$",
        "eval " + " ".join(EVAL_REGISTERS),
        f"rd {root / 'registers' / '00-initial.txt'}",
    ]
    lines.extend(f"bs 0x{item['address']:x}" for item in targets)
    lines.append("bl")
    for index, (step, name, include_contexts) in enumerate((
        (64, "after-startup-64", False),
        (1, "after-startup-1", False),
        (135076, "context-b-first-entry", True),
        (512, "context-b-bootstrap-call-boundary", True),
        (512, "context-b-test-entry", True),
        (512, "first-post-b-stop", True),
    ), start=1):
        lines.append(f"step {step}")
        _append_checkpoint(lines, root, index, name, addresses, include_contexts)
    lines.append("quit")

    valid_ranges = ((addresses["ivt"], addresses["ivt"] + 0x8000),
                    tuple(symbols["text"]))
    validate_command_file("\n".join(lines) + "\n", root, targets,
                          valid_ranges, tuple(symbols["load_ranges"]))
    return "\n".join(lines) + "\n"


def validate_command_file(command_text: str, root: Path,
                          expected_targets: list[dict[str, Any]],
                          valid_ranges: tuple[tuple[int, int], ...],
                          load_ranges: tuple[tuple[int, int], ...]) -> dict[str, Any]:
    try:
        command_text.encode("ascii")
    except UnicodeEncodeError as exc:
        raise DiagnosticError("Ski debugger command file must be ASCII") from exc
    if "\r" in command_text or "\0" in command_text:
        raise DiagnosticError("Ski debugger command file contains an invalid control character")
    commands = [line.strip() for line in command_text.splitlines() if line.strip()]
    if not commands:
        raise DiagnosticError("Ski debugger command file is empty")

    base = load_base_diagnostic()
    found_targets: list[int] = []
    breakpoint_line_indices: list[int] = []
    bl_indices: list[int] = []
    step_counts: list[int] = []
    register_files: list[str] = []
    memory_files: list[str] = []
    for index, line in enumerate(commands):
        if line in ("eval ip+0 $insts$", "eval " + " ".join(EVAL_REGISTERS)):
            continue
        if line.startswith("bs "):
            match = re.fullmatch(r"bs (0[xX][0-9a-fA-F]+)", line)
            if match is None:
                raise DiagnosticError(f"breakpoints must use numeric addresses: {line}")
            found_targets.append(base._validate_breakpoint_address(
                int(match.group(1), 16), valid_ranges))
            breakpoint_line_indices.append(index)
        elif line == "bl":
            bl_indices.append(index)
        elif line.startswith("rd "):
            match = re.fullmatch(r"rd (\S+)", line)
            if match is None:
                raise DiagnosticError(f"malformed Ski register-dump command: {line}")
            register_files.append(match.group(1))
        elif line.startswith("dd "):
            match = re.fullmatch(r"dd (0[xX][0-9a-fA-F]+) (0[xX][0-9a-fA-F]+) (\S+)", line)
            if match is None:
                raise DiagnosticError(f"malformed Ski memory-dump command: {line}")
            start, end = int(match.group(1), 16), int(match.group(2), 16)
            base._validate_memory_dump(start, end, load_ranges, match.group(3))
            memory_files.append(match.group(3))
        elif line.startswith("step "):
            match = re.fullmatch(r"step ([1-9][0-9]*)", line)
            if match is None:
                raise DiagnosticError(f"command file contains an invalid/non-finite step: {line}")
            step_counts.append(int(match.group(1)))
        elif line == "if $exited$ quit" or line == "quit":
            continue
        else:
            raise DiagnosticError(
                "unsupported or malformed Ski debugger command: " + line.split(None, 1)[0])

    expected_steps = list(INITIAL_STEPS + POST_START_STEPS)
    if step_counts != expected_steps or sum(step_counts) != MAX_GUEST_INSTRUCTIONS:
        raise DiagnosticError("command file step sequence or instruction ceiling changed")
    if sum(step_counts) > MAX_GUEST_INSTRUCTIONS:
        raise DiagnosticError("command file exceeds its absolute instruction ceiling")
    if len(found_targets) > MAX_BREAKPOINTS:
        raise DiagnosticError("Ski breakpoint plan exceeds the six-target diagnostic cap")
    expected_addresses = sorted({int(item["address"]) & ~0x3 for item in expected_targets})
    if sorted(found_targets) != expected_addresses:
        raise DiagnosticError("debugger targets do not match the verified ELF frontiers")
    first_step = next((i for i, line in enumerate(commands) if line.startswith("step ")), -1)
    if len(bl_indices) != 1 or not breakpoint_line_indices or \
            bl_indices[0] <= breakpoint_line_indices[-1] or bl_indices[0] >= first_step:
        raise DiagnosticError("bl must list every installed breakpoint before the first step")
    if commands[-1] != "quit" or commands.count("quit") != 1:
        raise DiagnosticError("command file must end with one clean quit")
    if len(register_files) != len(SNAPSHOT_NAMES) or len(set(register_files)) != len(register_files):
        raise DiagnosticError("command file has missing or duplicated register snapshots")
    if len(memory_files) != 12 or len(set(memory_files)) != len(memory_files):
        raise DiagnosticError("command file has missing or duplicated context/record snapshots")
    if any(not Path(path).resolve().is_relative_to(root.resolve())
           for path in register_files + memory_files):
        raise DiagnosticError("command output path escapes the unique capture root")
    return {
        "breakpoint_set_commands": len(found_targets),
        "unique_breakpoint_targets": len(set(found_targets)),
        "breakpoint_addresses": sorted(found_targets),
        "maximum_simultaneous_programmed": len(set(found_targets)),
        "ski_source_capacity": 10,
        "unused_slots_at_peak": 10 - len(set(found_targets)),
        "finite_step_commands": len(step_counts),
        "step_counts": step_counts,
        "maximum_guest_instructions": sum(step_counts),
        "bounded_execution": True,
        "register_snapshot_paths": register_files,
        "memory_snapshot_paths": memory_files,
    }


def parse_eval_register_samples(stderr_text: str) -> list[dict[str, Any]]:
    count_matches = list(re.finditer(r"(?m)^\s*\$insts\$\s*=\s*(\d+)\s*$", stderr_text))
    observations: list[dict[str, Any]] = []
    alias_pattern = re.compile(
        r"(?mi)^\s*(iva|psr|rsc|bsp|bspst|rnat|unat|pfs|sp|gp|b0|r32|r33|r42|r43|"
        r"iip|ipsr|isr|ifs|iipa)\s*=\s*(?:0x)?([0-9a-f]+)\s*$")
    for index, match in enumerate(count_matches):
        end = count_matches[index + 1].start() if index + 1 < len(count_matches) else len(stderr_text)
        segment = re.split(r"(?m)^\s*Hex:", stderr_text[match.end():end], maxsplit=1)[0]
        registers: dict[str, int] = {}
        for alias, raw in alias_pattern.findall(segment):
            key = alias.lower()
            if key in registers:
                raise DiagnosticError(f"debugger repeated register evaluation {key}")
            registers[key] = int(raw, 16)
        observations.append({"instructions": int(match.group(1)), "registers": registers})
    return observations


def parse_step_statistics(stderr_text: str) -> list[dict[str, int]]:
    result = []
    for match in re.finditer(r"(?mi)^\s*(\d+)\s+insts\s+\((\d+)\s+faults?\)", stderr_text):
        result.append({"instructions": int(match.group(1)), "faults": int(match.group(2))})
    return result


def interpret_isr(isr: int, claimed_classification: str | None = None) -> dict[str, Any]:
    if not isinstance(isr, int) or isr < 0 or isr >= (1 << 64):
        raise DiagnosticError("ISR is missing or outside the 64-bit register width")
    code = isr & 0xFFFF
    classification = ISR_CLASSIFICATIONS.get(code)
    if classification is None:
        raise DiagnosticError(f"unsupported General Exception ISR.code {code:#x}")
    if claimed_classification is not None and claimed_classification != classification:
        raise DiagnosticError(
            f"ISR.code {code:#x} means {classification}, not {claimed_classification}")
    return {
        "code": code,
        "classification": classification,
        "slot": (isr >> 41) & 0x3,
        "ni": (isr >> 39) & 1,
        "ir": (isr >> 38) & 1,
        "rs": (isr >> 37) & 1,
        "sp": (isr >> 36) & 1,
        "na": (isr >> 35) & 1,
        "r": (isr >> 34) & 1,
        "w": (isr >> 33) & 1,
        "x": (isr >> 32) & 1,
    }


def validate_interruption_snapshot(snapshot: dict[str, Any], *, expected_iva: int,
                                   text_range: tuple[int, int],
                                   claimed_classification: str | None = None) -> dict[str, Any]:
    required = (
        "current_ip", "iva", "psr", "rsc", "bsp", "sp", "pfs", "b0",
        "iip", "ipsr", "isr", "ifs", "iipa",
    )
    missing = [key for key in required if key not in snapshot or snapshot[key] is None]
    if missing:
        raise DiagnosticError("saved interruption snapshot is missing: " + ", ".join(missing))
    if snapshot["iva"] != expected_iva:
        raise DiagnosticError("captured CR.IVA does not match the selected probe ELF")
    vector_entry = expected_iva + VECTOR_OFFSET
    if snapshot["current_ip"] != vector_entry:
        raise DiagnosticError(
            f"snapshot is not at the General Exception vector entry: {snapshot['current_ip']:#x}")
    if (snapshot["psr"] >> 13) & 1:
        raise DiagnosticError("current PSR.ic is not clear at the vector entry")
    if ((snapshot["ipsr"] >> 13) & 1) != 1:
        raise DiagnosticError("saved IPSR.ic does not establish a valid interruption frame")
    if (snapshot["ifs"] >> 63) & 1:
        raise DiagnosticError("IFS.v should be cleared on this interruption")
    fault_bundle = snapshot["iip"]
    if fault_bundle & 0xF:
        raise DiagnosticError("saved CR.IIP is not an IA-64 instruction-bundle address")
    if not text_range[0] <= fault_bundle < text_range[1]:
        raise DiagnosticError("saved CR.IIP is outside the probe executable text")
    isr = interpret_isr(snapshot["isr"], claimed_classification)
    ipsr_slot = (snapshot["ipsr"] >> 41) & 0x3
    if ipsr_slot not in (0, 1, 2) or isr["slot"] not in (0, 1, 2) or ipsr_slot != isr["slot"]:
        raise DiagnosticError("saved IPSR.ri and ISR.ei do not identify the same valid slot")
    instruction_address = fault_bundle + SLOT_OBJ_DUMP_OFFSETS[isr["slot"]]
    if not text_range[0] <= instruction_address < text_range[1]:
        raise DiagnosticError("saved instruction slot is outside the probe executable text")
    return {
        "vector_entry": vector_entry,
        "vector_offset": VECTOR_OFFSET,
        "iva": expected_iva,
        "fault_bundle": fault_bundle,
        "fault_slot": isr["slot"],
        "fault_instruction_address": instruction_address,
        "classification": isr["classification"],
        "isr": isr,
        "ifs_v": 0,
        "ifs_ifm_interpreted": False,
        "iipa": snapshot["iipa"],
        "iipa_architecturally_defined_for_vector": True,
        "rsc_mode": snapshot["rsc"] & 0x3,
        "bsp": snapshot["bsp"],
        "bspstore_debugger_value": snapshot.get("bspst", snapshot.get("bspstore")),
        "bspstore_read_method": "Ski read-only debugger inspection",
        "memory_sp": snapshot["sp"],
        "pfs": snapshot["pfs"],
        "b0": snapshot["b0"],
    }


def analyze_first_vector_checkpoint(checkpoints: list[dict[str, Any]], *,
                                    vector_entry: int, expected_iva: int,
                                    text_range: tuple[int, int]
                                    ) -> tuple[dict[str, Any] | None, str | None, str | None]:
    entry = next((item for item in checkpoints if item.get("current_ip") == vector_entry), None)
    if entry is None:
        return None, None, "no captured checkpoint is exactly at the General Exception vector entry"
    try:
        analysis = validate_interruption_snapshot(
            {"current_ip": entry["current_ip"], **entry.get("registers", {})},
            expected_iva=expected_iva, text_range=text_range)
    except DiagnosticError as exc:
        return None, entry.get("snapshot_id"), str(exc)
    return analysis, entry.get("snapshot_id"), None


def validate_snapshot_provenance(snapshots: list[dict[str, Any]], *, run_started_ns: int,
                                 artifact_sha256: str, simulator_sha256: str) -> None:
    if not snapshots:
        raise DiagnosticError("no exception snapshots were captured")
    ids: set[str] = set()
    paths: set[str] = set()
    for item in snapshots:
        for required in ("snapshot_id", "path", "mtime_ns", "sha256", "artifact_sha256",
                         "simulator_sha256", "context_a_id", "context_b_id"):
            if required not in item:
                raise DiagnosticError(f"snapshot provenance is missing {required}")
        if item["snapshot_id"] in ids or item["path"] in paths:
            raise DiagnosticError("snapshot provenance contains duplicate IDs or paths")
        ids.add(item["snapshot_id"])
        paths.add(item["path"])
        if item["mtime_ns"] < run_started_ns:
            raise DiagnosticError("snapshot provenance contains a stale pre-run file")
        if not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]):
            raise DiagnosticError("snapshot file SHA-256 is malformed")
        if item["artifact_sha256"] != artifact_sha256:
            raise DiagnosticError("snapshot artifact identity does not match the selected probe")
        if item["simulator_sha256"] != simulator_sha256:
            raise DiagnosticError("snapshot simulator identity does not match pinned Ski")
        context_a_id = item.get("context_a_id")
        context_b_id = item.get("context_b_id")
        if (context_a_id is None) != (context_b_id is None) or \
                (context_a_id is not None and
                 (context_a_id != 0xA or context_b_id != 0xB)):
            raise DiagnosticError("snapshot context identity does not match Context A/B records")


def parse_context_id(base_diagnostic: Any, path: Path, base: int) -> int:
    words = base_diagnostic.parse_dd_words(path.read_text(encoding="ascii"), base, 1024)
    if len(words) <= RNAT_CONTEXT_ID_OFFSET // 8:
        raise DiagnosticError(f"context memory snapshot is truncated: {path}")
    return words[RNAT_CONTEXT_ID_OFFSET // 8]


def collect_snapshot_records(base: Any, capture_root: Path, run_started_ns: int,
                             addresses: dict[str, int], artifact_hash: str,
                             simulator_hash: str) -> list[dict[str, Any]]:
    records = []
    for index, name in enumerate(SNAPSHOT_NAMES):
        reg_path = capture_root / "registers" / f"{index:02d}-{name}.txt"
        if not reg_path.is_file():
            continue
        stat = reg_path.stat()
        context_a_path = capture_root / "contexts" / f"{index:02d}-{name}-a.txt"
        context_b_path = capture_root / "contexts" / f"{index:02d}-{name}-b.txt"
        context_ids = (None, None)
        if context_a_path.is_file() and context_b_path.is_file():
            context_ids = (
                parse_context_id(base, context_a_path, addresses["context_a"]),
                parse_context_id(base, context_b_path, addresses["context_b"]),
            )
        records.append({
            "snapshot_id": name,
            "path": str(reg_path.resolve()),
            "mtime_ns": stat.st_mtime_ns,
            "sha256": sha256(reg_path),
            "artifact_sha256": artifact_hash,
            "simulator_sha256": simulator_hash,
            "context_a_id": context_ids[0],
            "context_b_id": context_ids[1],
        })
    if records:
        validate_snapshot_provenance(records, run_started_ns=run_started_ns,
                                     artifact_sha256=artifact_hash,
                                     simulator_sha256=simulator_hash)
    return records


def run_no_guest_register_smoke(runner: Any, simulator: Path,
                                capture_root: Path) -> dict[str, Any]:
    smoke_root = capture_root / "no-guest-parser-smoke"
    smoke_root.mkdir()
    sim_capture = smoke_root / "simulator"
    command_file = smoke_root / "commands.txt"
    register_file = smoke_root / "registers.txt"
    command_text = "\n".join((
        "eval ip+0 $insts$",
        "eval " + " ".join(EVAL_REGISTERS),
        f"rd {register_file}",
        "quit",
        "",
    ))
    if any(ch.isspace() for ch in str(register_file)):
        raise DiagnosticError("no-guest Ski smoke path cannot contain whitespace")
    command_file.write_text(command_text, encoding="ascii", newline="\n")
    command = [str(simulator), "-noconsole", "-forcesystem", "-i", str(command_file)]
    status = runner.run_simulator(
        command, 8, sim_capture, "ia64-context-b-register-eval-no-guest-smoke",
        resource_limits={
            "address_space_bytes": 1024 * 1024 * 1024,
            "cpu_soft_seconds": 5,
            "cpu_hard_seconds": 6,
        }, stdin_pty=True)
    run_record = runner.read_json(sim_capture / "run-result.json", "no-guest Ski smoke record")
    if run_record.get("exit_code") != 0 or run_record.get("timed_out") is not False or \
            run_record.get("cleanup_complete") is not True or \
            run_record.get("process_group_cleanup_complete") is not True or \
            run_record.get("pty_stdin_verification_error") is not None:
        raise DiagnosticError("pinned Ski no-guest register smoke did not exit cleanly")
    if not register_file.is_file():
        raise DiagnosticError("pinned Ski no-guest register dump command was not accepted")
    dump = register_file.read_text(encoding="utf-8", errors="replace")
    if "System Registers" not in dump or "General Registers" not in dump:
        raise DiagnosticError("pinned Ski no-guest register dump is incomplete")
    stderr_text = Path(run_record["stderr"]).read_text(encoding="utf-8", errors="replace")
    eval_observations = parse_eval_register_samples(stderr_text)
    if not eval_observations or set(EVAL_REGISTERS) - eval_observations[0]["registers"].keys():
        raise DiagnosticError("Ski eval does not expose all required read-only register aliases")
    if Path(run_record["command"][-1]).suffix.lower() in (".elf", ".o"):
        raise DiagnosticError("no-guest smoke unexpectedly passed a guest artifact to Ski")
    record = {
        "status": "PASS",
        "guest_elf_loaded": False,
        "guest_instructions_executed": 0,
        "command_file_sha256": sha256(command_file),
        "register_dump_sha256": sha256(register_file),
        "run_result": run_record,
        "runner_status": status,
    }
    write_json_new(smoke_root / "summary.json", record)
    return record


def analyze_capture(base: Any, runner: Any, capture_root: Path, *,
                    simulator: Path, addresses: dict[str, int],
                    targets: list[dict[str, Any]], symbols: dict[str, Any],
                    plan: dict[str, Any], command_hash: str,
                    artifact_hash: str, manifest_hash: str,
                    simulator_hash: str, run_started_ns: int,
                    runner_status: int | None) -> dict[str, Any]:
    command_file = capture_root / "debugger-commands.txt"
    if not command_file.is_file() or sha256(command_file) != command_hash:
        raise DiagnosticError("saved debugger command file does not match the verified plan")
    preflight = runner.read_json(capture_root / "preflight.json", "saved preflight record")
    authorization = runner.read_json(
        capture_root / "authorization-consumed.json", "saved one-run authorization")
    identity_fields = (
        "probe_sha256", "manifest_sha256", "kernel_sha256", "simulator_sha256",
        "command_file_sha256", "breakpoints", "addresses", "breakpoint_plan",
        "maximum_guest_instructions", "maximum_wall_seconds", "address_space_bytes",
        "cpu_count", "kernel_execution",
    )
    expected_identities = {
        "probe_sha256": artifact_hash,
        "manifest_sha256": manifest_hash,
        "kernel_sha256": base.EXPECTED_KERNEL_SHA256,
        "simulator_sha256": simulator_hash,
        "command_file_sha256": command_hash,
        "breakpoints": targets,
        "addresses": addresses,
        "breakpoint_plan": plan,
        "maximum_guest_instructions": MAX_GUEST_INSTRUCTIONS,
        "maximum_wall_seconds": MAX_WALL_SECONDS,
        "address_space_bytes": ADDRESS_SPACE_BYTES,
        "cpu_count": 1,
        "kernel_execution": False,
    }
    for record_name, record in (("preflight", preflight), ("authorization", authorization)):
        for field in identity_fields:
            if record.get(field) != expected_identities[field]:
                raise DiagnosticError(f"saved {record_name} identity mismatch in {field}")
    if authorization.get("authorized_launch_count") != 1 or \
            authorization.get("status") != "CONSUMED_BEFORE_SINGLE_GUEST_LAUNCH" or \
            authorization.get("no_guidexos_kernel_execution") is not True:
        raise DiagnosticError("saved authorization does not attest one probe-only launch")
    smoke = authorization.get("no_guest_register_smoke", {})
    if smoke.get("status") != "PASS" or smoke.get("guest_elf_loaded") is not False or \
            smoke.get("guest_instructions_executed") != 0:
        raise DiagnosticError("saved no-guest register-alias smoke did not pass")

    run_result = runner.read_json(capture_root / "simulator" / "run-result.json",
                                  "Ski run record")
    if run_result.get("artifact_sha256") != artifact_hash or \
            run_result.get("simulator_sha256") != simulator_hash or \
            run_result.get("timed_out") is not False or run_result.get("exit_code") != 0 or \
            run_result.get("cleanup_complete") is not True or \
            run_result.get("process_group_cleanup_complete") is not True or \
            run_result.get("pty_stdin_verification_error") is not None:
        raise DiagnosticError("saved Ski run record does not match the authorized clean launch")
    run_command = run_result.get("command", [])
    if not run_command or run_command[0] != str(simulator) or \
            "-forcesystem" not in run_command or "-i" not in run_command:
        raise DiagnosticError("saved Ski invocation is missing the verified system-mode command")
    command_index = run_command.index("-i")
    if command_index + 1 >= len(run_command) or run_command[command_index + 1] != str(command_file):
        raise DiagnosticError("saved Ski invocation does not use the verified command file")

    stderr_text = Path(run_result["stderr"]).read_text(encoding="utf-8", errors="replace")
    progress = base.parse_progress_samples(
        stderr_text, ((addresses["ivt"], addresses["ivt"] + 0x8000),
                      tuple(symbols["text"])))
    register_samples = parse_eval_register_samples(stderr_text)
    step_stats = parse_step_statistics(stderr_text)
    snapshot_records = collect_snapshot_records(
        base, capture_root, run_started_ns, addresses, artifact_hash, simulator_hash)

    checkpoint_observations = []
    for index, name in enumerate(SNAPSHOT_NAMES):
        if index >= len(progress):
            break
        progress_item = progress[index]
        register_item = register_samples[index] if index < len(register_samples) else None
        checkpoint_observations.append({
            "snapshot_id": name,
            "instruction_count": progress_item["instructions"],
            "current_ip": progress_item["ip"],
            "instruction_slot": progress_item["slot"],
            "registers": register_item["registers"] if register_item else {},
        })

    exception_analysis, exception_checkpoint, capture_error = analyze_first_vector_checkpoint(
        checkpoint_observations, vector_entry=addresses["general_exception"],
        expected_iva=addresses["ivt"], text_range=tuple(symbols["text"]))
    capture_errors = [capture_error] if capture_error else []
    analysis = {
        "schema": "guidexos-ia64-context-b-exception-capture-v1",
        "outcome": ("IA64_CONTEXT_B_EXCEPTION_CAPTURED" if exception_analysis
                    else "IA64_CONTEXT_B_DIAGNOSTICS_PARTIAL"),
        "guest_execution_count": 1,
        "kernel_execution_count": 0,
        "probe_sha256": artifact_hash,
        "manifest_sha256": manifest_hash,
        "simulator_sha256": simulator_hash,
        "command_file_sha256": command_hash,
        "breakpoint_plan": plan,
        "actual_iva": addresses["ivt"],
        "general_exception_vector": addresses["general_exception"],
        "observed_pause_loop": addresses["pause_loop"],
        "checkpoint_observations": checkpoint_observations,
        "step_statistics": step_stats,
        "snapshot_provenance": snapshot_records,
        "exception_capture_checkpoint": exception_checkpoint,
        "exception_analysis": exception_analysis,
        "capture_errors": capture_errors,
        "ski_run_result": run_result,
        "runner_status": runner_status,
        "offline_analysis": runner_status is None,
    }
    analysis_path = capture_root / "analysis.json"
    if analysis_path.exists():
        previous = runner.read_json(analysis_path, "previous offline analysis")
        if not previous.get("offline_analysis"):
            raise DiagnosticError("refusing to replace a non-offline or historical analysis")
        with analysis_path.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(analysis, stream, indent=2, sort_keys=True)
            stream.write("\n")
    else:
        write_json_new(analysis_path, analysis)
    return analysis


def make_preflight_namespace(base: Any, args: argparse.Namespace) -> argparse.Namespace:
    return argparse.Namespace(
        artifact=args.artifact,
        manifest=args.manifest,
        kernel_artifact=args.kernel_artifact,
        ski_prefix=args.ski_prefix,
        objdump=args.objdump,
        capture_dir=args.capture_dir,
        expected_sha256=args.expected_sha256,
        simulator_sha256=args.simulator_sha256,
        instruction_budget=base.SUPPORTED_INSTRUCTION_BUDGET,
    )


def main(argv: list[str] | None = None) -> int:
    base = load_base_diagnostic()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, default=base.PROBE)
    parser.add_argument("--manifest", type=Path, default=base.MANIFEST)
    parser.add_argument("--kernel-artifact", type=Path, default=base.KERNEL)
    parser.add_argument("--ski-prefix", type=Path, default=base.SKI_PREFIX)
    parser.add_argument("--objdump", type=Path, default=base.OBJDUMP)
    parser.add_argument("--capture-dir", type=Path, default=CAPTURE)
    parser.add_argument("--expected-sha256", default=base.EXPECTED_PROBE_SHA256)
    parser.add_argument("--simulator-sha256", default=base.EXPECTED_SKI_SHA256)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--preflight-only", action="store_true")
    mode.add_argument("--print-command-file", action="store_true")
    mode.add_argument("--analyze-existing-capture", action="store_true")
    mode.add_argument("--authorize-one-diagnostic-guest-execution", action="store_true")
    args = parser.parse_args(argv)
    try:
        preflight_args = make_preflight_namespace(base, args)
        if args.analyze_existing_capture:
            preflight_args.capture_dir = args.capture_dir.resolve().parent / (
                f".{args.capture_dir.name}-offline-preflight-{time.time_ns()}")
        runner, simulator, symbols, elf_facts = base.preflight(preflight_args)
        elf = runner.ELF_VALIDATOR.Elf64(args.artifact.resolve())
        addresses, targets = exception_targets(elf, symbols)
        command_text = make_command_file(args.capture_dir, addresses, targets, symbols)
        plan = validate_command_file(
            command_text, args.capture_dir.resolve(), targets,
            ((addresses["ivt"], addresses["ivt"] + 0x8000), tuple(symbols["text"])),
            tuple(symbols["load_ranges"]))
        command_hash = hashlib.sha256(command_text.encode("ascii")).hexdigest()
        preflight_record = {
            "preflight": "PASS",
            "probe_sha256": args.expected_sha256,
            "manifest_sha256": base.EXPECTED_MANIFEST_SHA256,
            "kernel_sha256": base.EXPECTED_KERNEL_SHA256,
            "simulator": str(simulator),
            "simulator_sha256": args.simulator_sha256,
            "elf_entry": elf_facts["elf_entry"],
            "addresses": addresses,
            "breakpoints": targets,
            "breakpoint_plan": plan,
            "command_file_sha256": command_hash,
            "maximum_guest_instructions": MAX_GUEST_INSTRUCTIONS,
            "maximum_wall_seconds": MAX_WALL_SECONDS,
            "cpu_soft_seconds": CPU_SOFT_SECONDS,
            "cpu_hard_seconds": CPU_HARD_SECONDS,
            "address_space_bytes": ADDRESS_SPACE_BYTES,
            "cpu_count": 1,
            "kernel_execution": False,
        }
        if args.print_command_file:
            print(command_text, end="")
            return 0
        if args.preflight_only:
            print(json.dumps(preflight_record, sort_keys=True))
            return 0
        if args.analyze_existing_capture:
            capture_root = args.capture_dir.resolve()
            auth_path = capture_root / "authorization-consumed.json"
            if not auth_path.is_file():
                raise DiagnosticError("existing capture has no consumed authorization record")
            analysis = analyze_capture(
                base, runner, capture_root, simulator=simulator,
                addresses=addresses, targets=targets, symbols=symbols, plan=plan,
                command_hash=command_hash, artifact_hash=args.expected_sha256,
                manifest_hash=base.EXPECTED_MANIFEST_SHA256,
                simulator_hash=args.simulator_sha256,
                run_started_ns=auth_path.stat().st_mtime_ns, runner_status=None)
            print(json.dumps({"outcome": analysis["outcome"],
                              "capture_dir": str(capture_root),
                              "exception_capture_checkpoint": analysis["exception_capture_checkpoint"],
                              "exception_analysis": analysis["exception_analysis"],
                              "capture_errors": analysis["capture_errors"]}, sort_keys=True))
            return 0 if analysis["exception_analysis"] is not None else 1
        if not args.authorize_one_diagnostic_guest_execution:
            raise DiagnosticError("guest execution requires --authorize-one-diagnostic-guest-execution")

        capture_root = args.capture_dir.resolve()
        capture_root.mkdir(parents=True, exist_ok=False)
        for subdir in ("registers", "contexts", "records", "simulator"):
            (capture_root / subdir).mkdir()
        command_file = capture_root / "debugger-commands.txt"
        command_file.write_text(command_text, encoding="ascii", newline="\n")
        if sha256(command_file) != command_hash:
            raise DiagnosticError("generated debugger script hash changed before execution")
        write_json_new(capture_root / "preflight.json", preflight_record)

        no_guest_smoke = run_no_guest_register_smoke(runner, simulator, capture_root)
        auth_record = {
            **preflight_record,
            "authorized_launch_count": 1,
            "status": "CONSUMED_BEFORE_SINGLE_GUEST_LAUNCH",
            "command_file": str(command_file),
            "command_file_sha256": sha256(command_file),
            "no_guest_register_smoke": no_guest_smoke,
            "no_guidexos_kernel_execution": True,
        }
        write_json_new(capture_root / "authorization-consumed.json", auth_record)

        started_ns = time.time_ns()
        with runner.verified_snapshot(args.artifact.resolve(), args.expected_sha256) as snapshot:
            runner.validate_direct_system_elf(snapshot)
            sim_capture = capture_root / "simulator"
            command = [str(simulator), "-noconsole", "-forcesystem", "-i",
                       str(command_file), str(snapshot)]
            status = runner.run_simulator(
                command, MAX_WALL_SECONDS, sim_capture,
                "ia64-context-b-exception-diagnostic",
                artifact_hash=args.expected_sha256,
                resource_limits={
                    "address_space_bytes": ADDRESS_SPACE_BYTES,
                    "cpu_soft_seconds": CPU_SOFT_SECONDS,
                    "cpu_hard_seconds": CPU_HARD_SECONDS,
                },
                stdin_pty=True)

        analysis = analyze_capture(
            base, runner, capture_root, simulator=simulator,
            addresses=addresses, targets=targets, symbols=symbols, plan=plan,
            command_hash=command_hash, artifact_hash=args.expected_sha256,
            manifest_hash=base.EXPECTED_MANIFEST_SHA256,
            simulator_hash=args.simulator_sha256,
            run_started_ns=started_ns, runner_status=status)
        print(json.dumps({"outcome": analysis["outcome"],
                          "capture_dir": str(capture_root),
                          "exception_capture_checkpoint": analysis["exception_capture_checkpoint"],
                          "exception_analysis": analysis["exception_analysis"],
                          "capture_errors": analysis["capture_errors"]}, sort_keys=True))
        return 0 if analysis["exception_analysis"] is not None else 1
    except (DiagnosticError, base.DiagnosticError, base.load_runner().GateError,
            OSError, ValueError, KeyError, TypeError) as exc:
        print(f"[DIAGNOSTICS_BLOCKED] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""One-run, instruction-bounded Ski diagnostics for the cooperative probe."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import time
from typing import Any

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
PROBE = Path("/home/guidex/src/ia64-5ar5-coop-probe-20261114-r1/ia64-cooperative-context-probe.elf")
MANIFEST = PROBE.with_name("ia64-cooperative-context-probe.manifest.json")
KERNEL = Path("/home/guidex/src/ia64-4g-stacked-nat-rse-kernel-20261111-r3/bin/kernel.elf")
SKI_PREFIX = Path("/home/guidex/opt/ski-dfc2902")
OBJDUMP = Path("/home/guidex/src/ia64-2-toolchain-shim/ia64-linux-objdump")
CAPTURE = Path("/home/guidex/src/ia64-5ar5-coop-instruction-diagnostic-20261114-r1")

EXPECTED_PROBE_SHA256 = "ae0fc8fc49e576f37d8d6bf7d9846d0b293603ca7b9d15467afc81730992c74f"
EXPECTED_MANIFEST_SHA256 = "1dbebf04c3be50909316024fec69e67b1410d55c33a0cf9f17dac3b1c8475585"
EXPECTED_KERNEL_SHA256 = "ab13fabb6a92ebe039afa2afd9729f6f6ceb2529b63be2b47b0c87cf264d960c"
EXPECTED_SKI_SHA256 = "ff6b19cb87dbf2db39cda4478d0e49995c9a5ec9ddd05aac83191f4ba1e40b45"

PRE_SSC_BUDGET = 64
SSC_INSTRUCTION_BUDGET = 1
STEP_QUANTUM = 512
STEP_QUANTA = 469
SUPPORTED_INSTRUCTION_BUDGET = STEP_QUANTUM * STEP_QUANTA
MAX_STEPPED_INSTRUCTIONS = PRE_SSC_BUDGET + SSC_INSTRUCTION_BUDGET + SUPPORTED_INSTRUCTION_BUDGET
MAX_WALL_SECONDS = 30
CPU_SOFT_SECONDS = 20
CPU_HARD_SECONDS = 22
ADDRESS_SPACE_BYTES = 2 * 1024 * 1024 * 1024

LOCALIZATION_MIN_SAMPLES = 32
AR4_SETUP_RANGE = (0x10A2D0, 0x10AB50)
AR4_SETUP_CALL_CONTINUATION = 0x10A2E0
# Kept for the IA64-5AR4 localized-capture contract tests and offline reports.
SETUP_RANGE = AR4_SETUP_RANGE
SETUP_CALL_CONTINUATION = AR4_SETUP_CALL_CONTINUATION

CONTEXT_ID_OFFSET = 568
EVENT_NAMES = (
    (1, 0xA, "A_TO_B"),
    (2, 0xB, "B_FIRST_ENTRY"),
    (3, 0xB, "B_TO_A_FIRST"),
    (4, 0xA, "A_RESUMED_FIRST"),
    (5, 0xA, "A_TO_B_SECOND"),
    (6, 0xB, "B_RESUMED"),
    (7, 0xB, "B_TO_A_SECOND"),
    (8, 0xA, "A_RESUMED_FINAL"),
)


class DiagnosticError(ValueError):
    pass


def load_runner() -> Any:
    path = ROOT / "scripts" / "run-ia64-ski.py"
    spec = importlib.util.spec_from_file_location("ia64_ski_runner_for_coop_diag", path)
    if spec is None or spec.loader is None:
        raise DiagnosticError(f"cannot load qualified Ski runner: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_instruction_budget(value: int) -> int:
    if value != SUPPORTED_INSTRUCTION_BUDGET:
        raise DiagnosticError(
            f"unsupported instruction budget {value}; this command file is fixed at "
            f"{SUPPORTED_INSTRUCTION_BUDGET} post-SSC instructions in "
            f"{STEP_QUANTA} quanta of {STEP_QUANTUM}"
        )
    return value


def ski_breakpoint_ip(address: int) -> int:
    """Map objdump's slot byte address to Ski's bundle/slot instruction IP."""
    bundle = address & ~0xF
    offset = address - bundle
    slots = {0: 0, 6: 4, 12: 8}
    if offset not in slots:
        raise DiagnosticError(f"unsupported IA-64 disassembly slot address: {address:#x}")
    return bundle + slots[offset]


def validate_debugger_ip(raw_ip: int, valid_ranges: tuple[tuple[int, int], ...]) -> dict[str, int]:
    if raw_ip < 0 or raw_ip >= (1 << 64):
        raise DiagnosticError(f"debugger IP is outside the 64-bit range: {raw_ip}")
    low = raw_ip & 0xF
    if low not in (0, 4, 8):
        raise DiagnosticError(f"debugger IP has an invalid IA-64 slot encoding: {raw_ip:#x}")
    iip = raw_ip & ~0xF
    if not any(start <= iip < end for start, end in valid_ranges):
        raise DiagnosticError(f"debugger IIP is outside the ELF IVT/text ranges: {iip:#x}")
    return {"ip": raw_ip, "iip": iip, "slot": low // 4}


def parse_progress_samples(stderr_text: str,
                           valid_ranges: tuple[tuple[int, int], ...],
                           max_instructions: int = MAX_STEPPED_INSTRUCTIONS) -> list[dict[str, int]]:
    ip_values = [int(value, 16) for value in re.findall(
        r"(?m)^\s*Hex:\s*(?:0x)?([0-9a-fA-F]+)\s+Decimal:\s*-?\d+\s*$", stderr_text)]
    count_values = [int(value) for value in re.findall(
        r"(?m)^\s*\$insts\$\s*=\s*(\d+)\s*$", stderr_text)]
    if not ip_values or not count_values:
        raise DiagnosticError("debugger output contains no complete IIP/instruction-count progress")
    if len(ip_values) != len(count_values):
        raise DiagnosticError(
            f"debugger progress is incomplete: {len(ip_values)} IIP values, "
            f"{len(count_values)} instruction counts"
        )
    samples: list[dict[str, int]] = []
    previous_count = -1
    for index, (raw_ip, count) in enumerate(zip(ip_values, count_values, strict=True)):
        observed = validate_debugger_ip(raw_ip, valid_ranges)
        if count < 0 or count > max_instructions:
            raise DiagnosticError(f"instruction count {count} exceeds the fixed budget")
        if count <= previous_count:
            raise DiagnosticError("instruction counts must increase at every debugger checkpoint")
        previous_count = count
        samples.append({"sample": index, "instructions": count, **observed})
    return samples


def parse_eval_register_samples(stderr_text: str) -> list[dict[str, Any]]:
    requested = ("psr", "rsc", "bsp", "bspst", "rnat", "unat", "pfs", "sp",
                 "gp", "b0", "r32", "r33", "r42", "r43")
    count_matches = list(re.finditer(r"(?m)^\s*\$insts\$\s*=\s*(\d+)\s*$", stderr_text))
    observations: list[dict[str, Any]] = []
    for index, match in enumerate(count_matches):
        end = count_matches[index + 1].start() if index + 1 < len(count_matches) else len(stderr_text)
        segment = stderr_text[match.end():end]
        # The next sample begins with its `Hex:` result; do not consume its registers.
        segment = re.split(r"(?m)^\s*Hex:", segment, maxsplit=1)[0]
        state: dict[str, int] = {}
        for alias, raw in re.findall(
                r"(?mi)^\s*(psr|rsc|bsp|bspst|rnat|unat|pfs|sp|gp|b0|r32|r33|r42|r43)"
                r"\s*=\s*(?:0x)?([0-9a-f]+)\s*$", segment):
            key = alias.lower()
            if key in state:
                raise DiagnosticError(f"debugger repeated register evaluation {key}")
            state[key] = int(raw, 16)
        observations.append({"instructions": int(match.group(1)), "registers": state})
    return observations


def parse_dd_words(text: str, expected_base: int, expected_size: int) -> list[int]:
    """Parse pinned Ski's address-prefixed, two-qword-per-line `dd` output."""
    if expected_base < 0 or expected_size <= 0 or expected_base % 8 or expected_size % 8:
        raise DiagnosticError("memory dump geometry must be positive and qword aligned")
    words: list[int] = []
    expected_address = expected_base
    for line in text.splitlines():
        fields = line.strip().split()
        if not fields:
            continue
        address_token = fields[0].rstrip(":")
        if address_token.lower().startswith("0x"):
            address_token = address_token[2:]
        address: int | None = None
        if re.fullmatch(r"[0-9a-fA-F]+", address_token):
            candidate = int(address_token, 16)
            if expected_base <= candidate < expected_base + expected_size:
                address = candidate
        qwords: list[int] = []
        for token in fields[1:]:
            token = token.strip("|,;")
            if token.lower().startswith("0x"):
                token = token[2:]
            if re.fullmatch(r"[0-9a-fA-F]{16}", token):
                qwords.append(int(token, 16))
            elif token.lower() == "x" * 16:
                raise DiagnosticError(f"memory dump contains inaccessible data at {expected_address:#x}")
            else:
                break
        if not qwords:
            continue
        if address is not None and address != expected_address:
            raise DiagnosticError(
                f"memory dump address discontinuity: {address:#x}, expected {expected_address:#x}"
            )
        expected_here = min(2, (expected_base + expected_size - expected_address) // 8)
        if len(qwords) != expected_here:
            raise DiagnosticError(
                f"memory dump at {address:#x} has {len(qwords)} qwords, expected {expected_here}"
            )
        words.extend(qwords)
        expected_address += len(qwords) * 8
    if expected_address != expected_base + expected_size:
        raise DiagnosticError(
            f"memory dump is incomplete: ended at {expected_address:#x}, "
            f"expected {expected_base + expected_size:#x}"
        )
    return words


def validate_context_words(words: list[int], expected_id: int) -> dict[str, int] | None:
    if len(words) != 128:
        raise DiagnosticError(f"context record has {len(words)} qwords; expected 128")
    identity = words[CONTEXT_ID_OFFSET // 8]
    if identity == 0:
        return None
    if identity != expected_id:
        raise DiagnosticError(f"context identity {identity:#x} does not match {expected_id:#x}")
    return {
        "context_id": identity,
        "rp": words[64 // 8],
        "pfs": words[72 // 8],
        "rsc": words[80 // 8],
        "bsp": words[88 // 8],
        "bspstore": words[96 // 8],
        "rnat": words[104 // 8],
        "first_entry": words[560 // 8],
        "first_entries": words[576 // 8],
        "resume_entries": words[584 // 8],
        "switch_count": words[592 // 8],
        "rnat_valid_mask": words[640 // 8],
        "rnat_defined_bits": words[648 // 8],
    }


def validate_probe_record_words(words: list[int]) -> dict[str, Any]:
    if len(words) not in (43, 44):
        raise DiagnosticError(f"probe record has {len(words)} qwords; expected 43 plus at most one dump pad")
    words = words[:43]
    count = words[0]
    if count > len(EVENT_NAMES):
        raise DiagnosticError(f"probe event count is outside the fixed record: {count}")
    events: list[dict[str, int | str]] = []
    for index in range(count):
        sequence, context_id, event_code = words[1 + index * 3:4 + index * 3]
        expected_code, expected_context, name = EVENT_NAMES[index]
        if (sequence, context_id, event_code) != (index + 1, expected_context, expected_code):
            raise DiagnosticError(f"probe event {index + 1} has an invalid sequence, identity, or code")
        events.append({"sequence": sequence, "context_id": context_id,
                       "event_code": event_code, "event": name})
    return {"event_count": count, "events": events, "errors": words[25]}


def validate_fresh_records(paths: list[Path], run_started_ns: int,
                           tolerance_ns: int = 2_000_000_000) -> None:
    if not paths:
        raise DiagnosticError("no diagnostic record files were selected for freshness validation")
    for path in paths:
        try:
            stat_result = path.stat()
        except OSError as exc:
            raise DiagnosticError(f"diagnostic record is missing: {path}") from exc
        if not path.is_file() or stat_result.st_size == 0:
            raise DiagnosticError(f"diagnostic record is empty or not a file: {path}")
        if stat_result.st_mtime_ns + tolerance_ns < run_started_ns:
            raise DiagnosticError(f"diagnostic record predates this run: {path}")


def analyze_snapshot_files(root: Path, samples: list[dict[str, int]], run_started_ns: int,
                           context_a: int, context_b: int,
                           probe_record: int) -> dict[str, Any]:
    states: list[dict[str, Any]] = []
    freshness_errors: list[str] = []
    for index, sample in enumerate(samples):
        if index == 0:
            continue
        prefix = f"{index:04d}-"
        context_a_files = list((root / "contexts").glob(prefix + "*-a.txt"))
        context_b_files = list((root / "contexts").glob(prefix + "*-b.txt"))
        record_files = list((root / "records").glob(prefix + "*.txt"))
        state: dict[str, Any] = {"sample": index, "instructions": sample["instructions"],
                                 "ip": sample["ip"], "iip": sample["iip"],
                                 "slot": sample["slot"]}
        try:
            if len(context_a_files) != 1 or len(context_b_files) != 1 or len(record_files) != 1:
                raise DiagnosticError("checkpoint is missing one or more memory snapshots")
            selected = context_a_files + context_b_files + record_files
            validate_fresh_records(selected, run_started_ns)
            state["context_a"] = validate_context_words(
                parse_dd_words(context_a_files[0].read_text(encoding="utf-8", errors="replace"),
                               context_a, 1024), 0xA)
            state["context_b"] = validate_context_words(
                parse_dd_words(context_b_files[0].read_text(encoding="utf-8", errors="replace"),
                               context_b, 1024), 0xB)
            state["probe_record"] = validate_probe_record_words(
                parse_dd_words(record_files[0].read_text(encoding="utf-8", errors="replace"),
                               probe_record, 352))
        except (DiagnosticError, OSError) as exc:
            freshness_errors.append(f"sample {index}: {exc}")
            state["snapshot_error"] = str(exc)
        states.append(state)
    return {"snapshots": states, "snapshot_errors": freshness_errors}


def classify_setup_return_cycle(samples: list[dict[str, int]],
                                register_states: list[dict[str, Any]],
                                context_snapshots: list[dict[str, Any]],
                                breakpoint_observations: dict[str, list[dict[str, Any]]],
                                stdout_bytes: int) -> dict[str, Any] | None:
    """Recognize the pinned probe's setup re-entry with corroborating state.

    The returned localization is deliberately tied to the immutable probe and
    requires many advancing checkpoints, fresh matching context/event reads,
    and a stable b0 value. A single repeated IIP is never enough.
    """
    if stdout_bytes != 0 or not samples or not register_states or not context_snapshots:
        return None
    if len(samples) != len(register_states):
        return None
    snapshots_by_sample = {item.get("sample"): item for item in context_snapshots}
    registers_by_count = {item.get("instructions"): item.get("registers", {})
                          for item in register_states}
    ready_index: int | None = None
    for sample in samples:
        snapshot = snapshots_by_sample.get(sample.get("sample"))
        if snapshot and snapshot.get("context_a") and snapshot.get("context_b"):
            ready_index = sample["sample"]
            break
    if ready_index is None:
        return None
    observed = [sample for sample in samples if sample["sample"] >= ready_index]
    if len(observed) < LOCALIZATION_MIN_SAMPLES:
        return None
    if len({sample["iip"] for sample in observed}) < 3:
        return None
    if any(not AR4_SETUP_RANGE[0] <= sample["iip"] < AR4_SETUP_RANGE[1]
           for sample in observed):
        return None
    for sample in observed:
        registers = registers_by_count.get(sample["instructions"])
        snapshot = snapshots_by_sample.get(sample["sample"])
        if registers is None or registers.get("b0") != AR4_SETUP_CALL_CONTINUATION:
            return None
        if not snapshot or snapshot.get("snapshot_error"):
            return None
        context_a = snapshot.get("context_a") or {}
        context_b = snapshot.get("context_b") or {}
        record = snapshot.get("probe_record") or {}
        if (context_a.get("context_id") != 0xA or context_b.get("context_id") != 0xB or
                context_a.get("switch_count") != 0 or context_b.get("switch_count") != 0 or
                record.get("event_count") != 0 or record.get("errors") != 0):
            return None
    forbidden_hits = (
        "ia64_coop_context_switch entry", "flushrs slot 1",
        "incoming ar.bspstore write slot 1", "Context B first-entry bootstrap",
        "Context A first return continuation", "Context B resume continuation",
        "Context A final return continuation", "switch br.ret slot 2",
    )
    if any(label not in breakpoint_observations for label in forbidden_hits) or \
            any(breakpoint_observations[label] for label in forbidden_hits):
        return None
    return {
        "classification": "IA64_COOPERATIVE_STALL_LOCALIZED",
        "frontier": "ia64_coop_probe_setup re-entry after nested helper call",
        "first_context_ready_sample": ready_index,
        "corroborating_samples": len(observed),
        "observed_iip_range": [min(item["iip"] for item in observed),
                               max(item["iip"] for item in observed)],
        "setup_range": list(AR4_SETUP_RANGE),
        "stable_b0": AR4_SETUP_CALL_CONTINUATION,
        "context_ids": [0xA, 0xB],
        "switch_events": 0,
        "switch_entry_or_rse_handoff_breakpoints": 0,
        "source_disassembly_finding": (
            "The pinned setup routine calls ia64_coop_set_nat8 at 0x10a2dc, "
            "which overwrites b0 with its 0x10a2e0 continuation; setup later "
            "executes br.ret b0 at 0x10ab4c without restoring its caller's b0."
        ),
    }


def analyze_runtime_frontiers(samples: list[dict[str, int]],
                              register_states: list[dict[str, Any]],
                              breakpoints: tuple[tuple[int, str], ...],
                              expected_setup_return: int) -> dict[str, Any]:
    observations = {
        label: [sample for sample in samples if sample["ip"] == address]
        for address, label in breakpoints
    }
    registers_by_count = {item["instructions"]: item.get("registers", {})
                          for item in register_states}
    setup_exit = observations.get("setup final br.ret", [])
    setup_b0 = None
    if setup_exit:
        setup_b0 = registers_by_count.get(setup_exit[0]["instructions"], {}).get("b0")
    caller_hits = observations.get("setup caller continuation", [])
    returned_at_caller = bool(setup_exit and any(
        hit["instructions"] > setup_exit[0]["instructions"] for hit in caller_hits))
    setup_return_verified = setup_b0 == expected_setup_return and returned_at_caller
    observed_breakpoints = [
        {"label": label, "instructions": max(item["instructions"] for item in hits)}
        for label, hits in observations.items() if hits
    ]
    furthest = max(observed_breakpoints, key=lambda item: item["instructions"]
                   ) if observed_breakpoints else None
    return {
        "breakpoint_observations": observations,
        "setup_return": {
            "setup_exit_hit": bool(setup_exit),
            "b0_at_setup_br_ret": setup_b0,
            "expected_caller_continuation": expected_setup_return,
            "caller_continuation_hit_after_br_ret": returned_at_caller,
            "verified": setup_return_verified,
        },
        "furthest_proven_frontier": furthest["label"] if furthest else None,
        "furthest_proven_instruction_count": furthest["instructions"] if furthest else None,
        "switch_entry_hits": len(observations.get("first switch entry", [])),
        "context_save_flushrs_hits": len(observations.get("first context save flushrs", [])),
        "incoming_bspstore_handoff_hits": len(observations.get(
            "incoming ar.bspstore handoff", [])),
        "context_b_first_entry_hits": len(observations.get(
            "Context B first-entry bootstrap", [])),
        "context_a_first_resume_hits": len(observations.get("return to Context A", [])),
        "context_b_second_resume_hits": len(observations.get(
            "second Context B resumption", [])),
        "context_a_final_resume_hits": len(observations.get(
            "final Context A continuation", [])),
        "context_a_final_exit_hits": len(observations.get("final Context A exit", [])),
        "unexpected_context_b_resume_hits": len(observations.get(
            "unexpected Context B resumption", [])),
    }


def analyze_debugger_initialization(stderr_text: str, command_text: str,
                                   samples: list[dict[str, int]],
                                   elf_entry: int) -> dict[str, Any] | None:
    if "All breakpoints in use" not in stderr_text:
        return None
    requested = sum(line.strip().startswith("bs ")
                    for line in command_text.splitlines())
    initial_sample = samples[0] if samples else None
    return {
        "failure_class": "SIMULATOR_OBSERVATION_LIMITATION",
        "reason": "Ski reported 'All breakpoints in use' while installing the debugger plan",
        "breakpoints_requested": requested,
        "elf_entry": elf_entry,
        "guest_instructions_executed": (
            initial_sample["instructions"] if initial_sample is not None else None),
        "failure_frontier": (
            "ELF entry before first guest instruction"
            if initial_sample is not None and initial_sample["instructions"] == 0
            and initial_sample["iip"] == elf_entry else "debugger initialization"),
        "setup_return_observed": False,
        "context_switch_observed": False,
        "fault_count_available": False,
    }


def _format_eval_and_snapshot(lines: list[str], index: int, label: str,
                              root: Path, context_a: int, context_b: int,
                              probe_record: int) -> None:
    lines.append("eval ip+0 $insts$")
    lines.append("eval psr rsc bsp bspst rnat unat pfs sp gp b0 r32 r33 r42 r43")
    register_path = root / "registers" / f"{index:04d}-{label}.txt"
    context_a_path = root / "contexts" / f"{index:04d}-{label}-a.txt"
    context_b_path = root / "contexts" / f"{index:04d}-{label}-b.txt"
    record_path = root / "records" / f"{index:04d}-{label}.txt"
    lines.append(f"rd {register_path}")
    lines.append(f"dd 0x{context_a:x} 0x{context_a + 1023:x} {context_a_path}")
    lines.append(f"dd 0x{context_b:x} 0x{context_b + 1023:x} {context_b_path}")
    lines.append(f"dd 0x{probe_record:x} 0x{probe_record + 343:x} {record_path}")
    lines.append("if $exited$ quit")


def cooperative_breakpoints(frontiers: dict[str, int]) -> tuple[tuple[int, str], ...]:
    return (
        (0x1080C8, "SSC_CONSOLE_INIT break.i slot 2"),
        (frontiers["setup_entry"], "ia64_coop_probe_setup entry"),
        (frontiers["setup_nested_return"], "return from ia64_coop_set_nat8"),
        (frontiers["setup_exit"], "setup final br.ret"),
        (frontiers["setup_caller_return"], "setup caller continuation"),
        (frontiers["switch_entry"], "first switch entry"),
        (frontiers["context_a_save"], "first context save flushrs"),
        (frontiers["rbs_handoff"], "incoming ar.bspstore handoff"),
        (frontiers["context_b_first"], "Context B first-entry bootstrap"),
    )


def make_command_file(root: Path, symbols: dict[str, Any],
                      instruction_budget: int = SUPPORTED_INSTRUCTION_BUDGET) -> str:
    validate_instruction_budget(instruction_budget)
    for path in (root, root / "registers", root / "contexts", root / "records"):
        if any(ch.isspace() for ch in str(path)):
            raise DiagnosticError("Ski command-file paths cannot contain whitespace")
    text_start, text_end = symbols["text"]
    ivt = symbols["ivt"]
    breakpoints = cooperative_breakpoints(symbols["frontiers"])
    for address, label in breakpoints:
        if address & 3 or not (ivt <= address < 0x108000 or text_start <= address < text_end):
            raise DiagnosticError(f"breakpoint {label} is outside aligned IVT/text: {address:#x}")
    lines = ["eval ip+0 $insts$",
             f"rd {root / 'registers' / '0000-initial.txt'}"]
    lines.extend(f"bs 0x{address:x}" for address, _ in breakpoints)
    lines.append(f"step {PRE_SSC_BUDGET}")
    _format_eval_and_snapshot(lines, 1, "ssc-frontier", root,
                              symbols["context_a"], symbols["context_b"],
                              symbols["probe_record"])
    lines.append(f"step {SSC_INSTRUCTION_BUDGET}")
    _format_eval_and_snapshot(lines, 2, "ssc-returned", root,
                              symbols["context_a"], symbols["context_b"],
                              symbols["probe_record"])
    for quantum in range(1, STEP_QUANTA + 1):
        lines.append(f"step {STEP_QUANTUM}")
        _format_eval_and_snapshot(lines, quantum + 2, f"q{quantum:03d}", root,
                                  symbols["context_a"], symbols["context_b"],
                                  symbols["probe_record"])
    lines.append("quit")
    validate_command_file("\n".join(lines) + "\n")
    return "\n".join(lines) + "\n"


def validate_command_file(command_text: str) -> None:
    commands = [line.strip() for line in command_text.splitlines() if line.strip()]
    steps = [line for line in commands if line.startswith("step ")]
    if len(steps) != STEP_QUANTA + 2:
        raise DiagnosticError("command file does not contain the fixed staged step count")
    step_counts: list[int] = []
    for line in steps:
        match = re.fullmatch(r"step ([1-9][0-9]*)", line)
        if match is None:
            raise DiagnosticError(f"command file contains a non-finite step command: {line}")
        step_counts.append(int(match.group(1)))
    if step_counts[:2] != [PRE_SSC_BUDGET, SSC_INSTRUCTION_BUDGET] or \
            step_counts[2:] != [STEP_QUANTUM] * STEP_QUANTA or \
            sum(step_counts) != MAX_STEPPED_INSTRUCTIONS:
        raise DiagnosticError("command file step totals do not match the fixed instruction budget")
    lowered = [line.lower() for line in commands]
    if any(re.match(r"^(run|cont(?:inue)?|go)(?:\s|$)", line) for line in lowered):
        raise DiagnosticError("unbounded run/continue command is prohibited")
    breakpoint_count = sum(line.startswith("bs ") for line in commands)
    if breakpoint_count > 9:
        raise DiagnosticError(
            f"Ski breakpoint plan exceeds the demonstrated capacity of 9: {breakpoint_count}")
    if commands[-1] != "quit":
        raise DiagnosticError("command file must end with clean quit")
    if commands.count("if $exited$ quit") != STEP_QUANTA + 2:
        raise DiagnosticError("each stepped checkpoint must guard guest exit")


def preflight(args: argparse.Namespace) -> tuple[Any, Path, dict[str, Any], dict[str, Any]]:
    runner = load_runner()
    args.artifact = args.artifact.resolve()
    args.manifest = args.manifest.resolve()
    args.kernel_artifact = args.kernel_artifact.resolve()
    args.ski_prefix = args.ski_prefix.resolve()
    args.objdump = args.objdump.resolve()
    args.capture_dir = args.capture_dir.resolve()
    for path, expected, label in (
        (args.artifact, args.expected_sha256, "cooperative probe ELF"),
        (args.manifest, EXPECTED_MANIFEST_SHA256, "cooperative probe manifest"),
        (args.kernel_artifact, EXPECTED_KERNEL_SHA256, "accepted kernel ELF"),
    ):
        if not path.is_file():
            raise DiagnosticError(f"{label} is missing: {path}")
        actual = sha256(path)
        if actual != expected:
            raise DiagnosticError(f"{label} identity mismatch: {actual}")
    if args.expected_sha256 != EXPECTED_PROBE_SHA256:
        raise DiagnosticError("this diagnostic is pinned to the unmodified IA64-5AR3 probe")
    if args.simulator_sha256 != EXPECTED_SKI_SHA256:
        raise DiagnosticError("this diagnostic is pinned to the qualified Ski 1.5.1 executable")
    if sha256(args.manifest) != EXPECTED_MANIFEST_SHA256:
        raise DiagnosticError("matching manifest identity changed")
    symbols = runner.validate_cooperative_context_probe_static(
        args.artifact.resolve(), args.manifest.resolve(), args.expected_sha256, str(args.objdump))
    load_ranges = runner.validate_direct_system_elf(args.artifact)
    simulator = runner.verify_ski(args.ski_prefix, args.simulator_sha256)
    elf = runner.ELF_VALIDATOR.Elf64(args.artifact)
    symbols = dict(symbols)
    symbols["switch_entry"] = elf.symbols["ia64_coop_context_switch"]["value"]
    symbols["context_b_first_entry"] = elf.symbols["ia64_coop_b_first_entry"]["value"]
    symbols["probe_record"] = elf.symbols["ia64_coop_probe_record"]["value"]
    if not load_ranges or not (symbols["text"][0] <= elf.entry < symbols["text"][1]):
        raise DiagnosticError("ELF entry does not match the probe text range")
    symbols["frontiers"] = {
        "setup_entry": ski_breakpoint_ip(symbols["setup"]),
        "setup_call": ski_breakpoint_ip(symbols["setup_call_instruction"]),
        "setup_nested_return": ski_breakpoint_ip(symbols["setup_nested_return"]),
        "setup_exit": ski_breakpoint_ip(symbols["setup_return_instruction"]),
        "setup_caller_return": ski_breakpoint_ip(symbols["probe_after_setup"]),
        "switch_entry": ski_breakpoint_ip(symbols["switch_entry"]),
        "context_a_save": ski_breakpoint_ip(symbols["switch_flushrs"]),
        "rbs_handoff": ski_breakpoint_ip(symbols["switch_bspstore"]),
        "context_b_first": ski_breakpoint_ip(symbols["context_b_first_entry"]),
        "context_a_resume_first": ski_breakpoint_ip(symbols["a_resume_first"]),
        "context_b_resume_second": ski_breakpoint_ip(symbols["b_resume_second"]),
        "context_a_resume_final": ski_breakpoint_ip(symbols["a_resume_final"]),
        "context_a_final_exit": ski_breakpoint_ip(symbols["a_final_exit"]),
        "context_b_unexpected_resume": ski_breakpoint_ip(symbols["b_unexpected_resume"]),
        "switch_return": ski_breakpoint_ip(symbols["switch_return"]),
    }
    validate_instruction_budget(args.instruction_budget)
    if args.capture_dir.exists():
        raise DiagnosticError(f"capture root already exists; refusing to overwrite: {args.capture_dir}")
    return runner, simulator, symbols, {"elf_entry": elf.entry, "load_ranges": load_ranges,
                                        "frontiers": symbols["frontiers"]}


def write_json_new(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def run_no_guest_parser_smoke(runner: Any, simulator: Path, root: Path) -> dict[str, Any]:
    smoke_root = root / "no-guest-parser-smoke"
    smoke_root.mkdir(parents=True, exist_ok=False)
    command_file = smoke_root / "commands.txt"
    memory_file = smoke_root / "memory.txt"
    register_file = smoke_root / "registers.txt"
    commands = "\n".join((
        "eval ip+0 $insts$",
        "eval psr rsc bsp bspst rnat unat pfs sp gp b0 r32 r33 r42 r43",
        "bs 0x108000",
        f"rd {register_file}",
        f"dd 0x0 0xf {memory_file}",
        "if 1 quit",
        "quit",
        "",
    ))
    command_file.write_text(commands, encoding="ascii", newline="\n")
    validate_smoke_command_file(commands)
    sim_capture = smoke_root / "simulator"
    status = runner.run_simulator(
        [str(simulator), "-noconsole", "-forcesystem", "-i", str(command_file)],
        8, sim_capture, "ia64-coop-debugger-no-guest-command-smoke",
        resource_limits={"address_space_bytes": ADDRESS_SPACE_BYTES,
                         "cpu_soft_seconds": 5, "cpu_hard_seconds": 6},
        stdin_pty=True,
    )
    result = runner.read_json(sim_capture / "run-result.json", "no-guest parser smoke result")
    if result.get("exit_code") != 0 or not result.get("cleanup_complete") or \
            not result.get("process_group_cleanup_complete") or \
            not result.get("pty_stdin_observation", {}).get("passed") or \
            not memory_file.is_file() or not register_file.is_file():
        raise DiagnosticError("pinned bskinc no-guest command parser smoke did not pass")
    stderr_text = (sim_capture / "stderr.log").read_text(encoding="utf-8", errors="replace")
    # With no ELF loaded, pinned bskinc reports its initial IIP as 0; the
    # separate smoke validates command syntax and output formatting only.
    progress = parse_progress_samples(stderr_text, ((0, 1),), 0)
    register_states = parse_eval_register_samples(stderr_text)
    required_registers = {"psr", "rsc", "bsp", "bspst", "rnat", "unat", "pfs",
                          "sp", "gp", "b0", "r32", "r33", "r42", "r43"}
    if len(progress) != 1 or len(register_states) != 1 or \
            not required_registers <= register_states[0]["registers"].keys():
        raise DiagnosticError("pinned bskinc eval syntax did not return the required raw registers")
    return {"runner_status": status, "simulator_exit_code": result.get("exit_code"),
            "classification": result.get("classification"),
            "command_sha256": sha256(command_file),
            "capture": str(smoke_root), "initial_progress": progress[0],
            "register_evaluation": register_states[0]}


def validate_smoke_command_file(command_text: str) -> None:
    required = ("eval ip+0 $insts$", "eval psr rsc bsp bspst rnat unat pfs sp gp b0 "
                "r32 r33 r42 r43", "bs 0x108000", "rd ",
                "dd 0x0 0xf ", "if 1 quit", "quit")
    if any(command not in command_text for command in required):
        raise DiagnosticError("no-guest parser smoke is missing a required pinned command")
    if re.search(r"(?im)^\s*(run|cont(?:inue)?|go)(?:\s|$)", command_text):
        raise DiagnosticError("no-guest parser smoke must not run a guest")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, default=PROBE)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--kernel-artifact", type=Path, default=KERNEL)
    parser.add_argument("--ski-prefix", type=Path, default=SKI_PREFIX)
    parser.add_argument("--objdump", type=Path, default=OBJDUMP)
    parser.add_argument("--capture-dir", type=Path, default=CAPTURE)
    parser.add_argument("--expected-sha256", default=EXPECTED_PROBE_SHA256)
    parser.add_argument("--simulator-sha256", default=EXPECTED_SKI_SHA256)
    parser.add_argument("--instruction-budget", type=int, default=SUPPORTED_INSTRUCTION_BUDGET)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--preflight-only", action="store_true",
                       help="verify identities and static contracts without guest execution")
    modes.add_argument("--no-guest-parser-smoke-only", action="store_true",
                       help="exercise pinned bskinc commands without loading any guest ELF")
    parser.add_argument("--authorize-one-diagnostic-guest-execution", action="store_true",
                        help="consume the request's one allowed cooperative diagnostic launch")
    args = parser.parse_args(argv)
    try:
        runner, simulator, symbols, elf_facts = preflight(args)
        command_text = make_command_file(args.capture_dir, symbols, args.instruction_budget)
        if args.preflight_only:
            print(json.dumps({"preflight": "PASS", "simulator": str(simulator),
                              "simulator_sha256": args.simulator_sha256,
                              "probe_sha256": args.expected_sha256,
                              "manifest_sha256": EXPECTED_MANIFEST_SHA256,
                              "kernel_sha256": EXPECTED_KERNEL_SHA256,
                              "elf_entry": elf_facts["elf_entry"],
                              "text": symbols["text"],
                              "frontiers": elf_facts["frontiers"],
                              "setup_call_instruction": symbols["setup_call_instruction"],
                              "setup_return_instruction": symbols["setup_return_instruction"],
                              "context_a_save_instruction": symbols["switch_flushrs"],
                              "incoming_bspstore_instruction": symbols["switch_bspstore"],
                              "switch_return_instruction": symbols["switch_return"],
                              "step_quanta": STEP_QUANTA,
                              "step_quantum": STEP_QUANTUM,
                              "post_ssc_instruction_budget": args.instruction_budget,
                              "maximum_guest_instructions": MAX_STEPPED_INSTRUCTIONS},
                             sort_keys=True))
            return 0
        if args.no_guest_parser_smoke_only:
            args.capture_dir.mkdir(parents=True, exist_ok=False)
            smoke = run_no_guest_parser_smoke(runner, simulator, args.capture_dir)
            print(json.dumps({"no_guest_parser_smoke": "PASS", **smoke}, sort_keys=True))
            return 0
        if not args.authorize_one_diagnostic_guest_execution:
            raise DiagnosticError("guest execution requires --authorize-one-diagnostic-guest-execution")
        args.capture_dir.mkdir(parents=True, exist_ok=False)
        smoke = run_no_guest_parser_smoke(runner, simulator, args.capture_dir)
        for subdir in ("registers", "contexts", "records"):
            (args.capture_dir / subdir).mkdir()
        command_file = args.capture_dir / "debugger-commands.txt"
        command_file.write_text(command_text, encoding="ascii", newline="\n")
        auth_path = args.capture_dir / "authorization-consumed.json"
        write_json_new(auth_path, {
            "authorized_launch_count": 1,
            "status": "CONSUMED_BEFORE_SINGLE_GUEST_LAUNCH",
            "probe_sha256": args.expected_sha256,
            "manifest_sha256": EXPECTED_MANIFEST_SHA256,
            "simulator_sha256": args.simulator_sha256,
            "command_file_sha256": sha256(command_file),
            "no_guest_parser_smoke": smoke,
            "pre_ssc_steps": PRE_SSC_BUDGET,
            "ssc_step": SSC_INSTRUCTION_BUDGET,
            "quantum": STEP_QUANTUM,
            "quanta": STEP_QUANTA,
            "post_ssc_budget": args.instruction_budget,
            "maximum_guest_instructions": MAX_STEPPED_INSTRUCTIONS,
            "wall_limit_seconds": MAX_WALL_SECONDS,
            "cpu_soft_seconds": CPU_SOFT_SECONDS,
            "cpu_hard_seconds": CPU_HARD_SECONDS,
            "address_space_bytes": ADDRESS_SPACE_BYTES,
            "cpu_count": 1,
            "kernel_execution": False,
        })
        with runner.verified_snapshot(args.artifact, args.expected_sha256) as snapshot:
            runner.validate_direct_system_elf(snapshot)
            command = [str(simulator), "-noconsole", "-forcesystem", "-i",
                       str(command_file), str(snapshot)]
            run_started_ns = time.time_ns()
            simulation_capture = args.capture_dir / "simulator"
            status = runner.run_simulator(
                command, MAX_WALL_SECONDS, simulation_capture,
                "ia64-coop-instruction-level-diagnostic",
                artifact_hash=args.expected_sha256,
                resource_limits={"address_space_bytes": ADDRESS_SPACE_BYTES,
                                 "cpu_soft_seconds": CPU_SOFT_SECONDS,
                                 "cpu_hard_seconds": CPU_HARD_SECONDS},
                stdin_pty=True,
            )
        result = runner.read_json(simulation_capture / "run-result.json",
                                  "bounded cooperative diagnostic result")
        stderr_path = simulation_capture / "stderr.log"
        stdout_path = simulation_capture / "stdout.log"
        stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace")
        stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace")
        ranges = ((symbols["ivt"], 0x108000), tuple(symbols["text"]))
        progress_error: str | None = None
        try:
            samples = parse_progress_samples(stderr_text, ranges)
        except DiagnosticError as exc:
            samples = []
            progress_error = str(exc)
        register_state_error: str | None = None
        try:
            register_states = parse_eval_register_samples(stderr_text)
        except DiagnosticError as exc:
            register_states = []
            register_state_error = str(exc)
        sample_files = sorted((args.capture_dir / "registers").glob("*.txt"))
        freshness_error: str | None = None
        try:
            validate_fresh_records(sample_files, run_started_ns)
        except DiagnosticError as exc:
            freshness_error = str(exc)
        memory_analysis = analyze_snapshot_files(
            args.capture_dir, samples, run_started_ns,
            symbols["context_a"], symbols["context_b"], symbols["probe_record"])
        breakpoints = cooperative_breakpoints(symbols["frontiers"])
        runtime_frontiers = analyze_runtime_frontiers(
            samples, register_states, breakpoints,
            symbols["probe_after_setup"])
        debugger_initialization = analyze_debugger_initialization(
            stderr_text, command_text, samples, elf_facts["elf_entry"])
        analysis = {
            "outcome": "IA64_COOPERATIVE_REQUALIFICATION_BLOCKED",
            "probe_sha256": args.expected_sha256,
            "manifest_sha256": EXPECTED_MANIFEST_SHA256,
            "simulator_sha256": args.simulator_sha256,
            "kernel_sha256": EXPECTED_KERNEL_SHA256,
            "run_status": status,
            "run_result": result,
            "guest_execution_count": 1,
            "kernel_execution_count": 0,
            "instruction_budget": {
                "pre_ssc_step": PRE_SSC_BUDGET,
                "ssc_instruction": SSC_INSTRUCTION_BUDGET,
                "quantum": STEP_QUANTUM,
                "quanta": STEP_QUANTA,
                "post_ssc_maximum": args.instruction_budget,
                "absolute_maximum": MAX_STEPPED_INSTRUCTIONS,
            },
            "debugger_frontiers": {
                key: {"ski_ip": value, "objdump_address": symbols.get(raw_key)}
                for key, value, raw_key in (
                    ("elf_entry", elf_facts["elf_entry"], None),
                    ("setup_entry", symbols["frontiers"]["setup_entry"], "setup"),
                    ("nested_call", symbols["frontiers"]["setup_call"],
                     "setup_call_instruction"),
                    ("nested_return", symbols["frontiers"]["setup_nested_return"],
                     "setup_nested_return"),
                    ("setup_exit", symbols["frontiers"]["setup_exit"],
                     "setup_return_instruction"),
                    ("setup_caller_continuation",
                     symbols["frontiers"]["setup_caller_return"], "probe_after_setup"),
                    ("first_switch_entry", symbols["frontiers"]["switch_entry"],
                     "switch_entry"),
                    ("context_a_save", symbols["frontiers"]["context_a_save"],
                     "switch_flushrs"),
                    ("context_b_first_entry", symbols["frontiers"]["context_b_first"],
                     "context_b_first_entry"),
                    ("return_to_context_a", symbols["frontiers"]["context_a_resume_first"],
                     "a_resume_first"),
                    ("second_context_b_resumption",
                     symbols["frontiers"]["context_b_resume_second"], "b_resume_second"),
                    ("final_context_a_exit", symbols["frontiers"]["context_a_final_exit"],
                     "a_final_exit"),
                )
            },
            "breakpoint_contract": [
                {"address": address, "label": label}
                for address, label in breakpoints
            ],
            "probe_stdout_bytes": len(stdout_text.encode("utf-8", errors="replace")),
            "debugger_progress_samples": samples,
            "debugger_progress_error": progress_error,
            "debugger_register_evaluations": register_states,
            "debugger_register_evaluation_error": register_state_error,
            "runtime_frontier_analysis": runtime_frontiers,
            "debugger_initialization_failure": debugger_initialization,
            "register_snapshot_count": len(sample_files),
            "register_snapshot_freshness_error": freshness_error,
            "context_and_event_snapshots": memory_analysis["snapshots"],
            "context_and_event_snapshot_errors": memory_analysis["snapshot_errors"],
            "last_observation": samples[-1] if samples else None,
            "captured_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        try:
            output = runner.validate_cooperative_context_probe_output(stdout_text)
            analysis["probe_output_validation"] = output
        except runner.GateError as exc:
            analysis["probe_output_validation_error"] = str(exc)
        output_valid = "probe_output_validation" in analysis
        intentional_exit = (
            result.get("classification") == "GUEST_EXIT_ZERO" and
            result.get("ski_fault_count") == 0 and
            result.get("ski_instruction_count") is not None and
            result.get("timed_out") is False and
            result.get("cleanup_complete") is True and
            result.get("process_group_cleanup_complete") is True and
            (result.get("pty_stdin_observation") or {}).get("passed") is True
        )
        analysis["setup_return_evidence"] = runtime_frontiers["setup_return"]
        analysis["intentional_exit_verified"] = intentional_exit
        analysis["furthest_proven_frontier"] = (
            debugger_initialization["failure_frontier"]
            if debugger_initialization is not None else
            runtime_frontiers["furthest_proven_frontier"])
        if output_valid and intentional_exit:
            analysis["outcome"] = "IA64_COOPERATIVE_PROBE_QUALIFIED"
        elif runtime_frontiers["setup_return"]["verified"]:
            analysis["outcome"] = "IA64_COOPERATIVE_FRONTIER_ADVANCED"
        else:
            analysis["outcome"] = "IA64_COOPERATIVE_REQUALIFICATION_BLOCKED"
        write_json_new(args.capture_dir / "analysis.json", analysis)
        print(json.dumps({"analysis": str(args.capture_dir / "analysis.json"),
                          "outcome": analysis["outcome"],
                          "guest_execution_count": 1,
                          "last_observation": samples[-1] if samples else None,
                          "register_snapshot_count": len(sample_files),
                          "simulator_exit_code": result.get("exit_code"),
                          "timed_out": result.get("timed_out")}, sort_keys=True))
        return 0 if status == 0 else 1
    except (DiagnosticError, OSError, ValueError) as exc:
        print(f"DIAGNOSTIC_BLOCKED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Offline R26 reconciliation for frozen R25 first-failure evidence."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
from typing import Any


_R25_PATH = Path(__file__).with_name("analyze-c188r25-failfast-origin.py")
_SPEC = importlib.util.spec_from_file_location("c188r25_failfast_analyzer", _R25_PATH)
assert _SPEC is not None and _SPEC.loader is not None
_R25 = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_R25)

FAST_FAIL_NAMES = {7: "FAST_FAIL_FATAL_APP_EXIT"}
STAGE_NAMES = {
    0: "unknown",
    1: "native_module_preparation",
    2: "before_or_inside_InitializeModules",
    3: "InitializeModules_returned",
}
SERIAL_MARKERS = {
    "M1": r"(?m)^\[C102-STARTUP\] stage=C188B006\s*$",
    "M2": r"(?m)^\[C102-STARTUP\] stage=C188B007\s*$",
    "M7": r"(?m)^\[C102-STARTUP\] stage=C188B005\s*$",
    "M8": r"(?m)^\[C102-STARTUP\] stage=C188B002\s*$",
    "M9": r"C188R21-FACTORY .*stage=factory-invoke",
}


def read_json(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    return json.loads(path.read_text(encoding="utf-8-sig"))


def _candidate(candidate_root: Path) -> dict[str, Any]:
    manifest_path = candidate_root / "candidate-manifest.json"
    manifest = read_json(manifest_path)
    if manifest is None:
        raise ValueError("candidate manifest is unavailable")
    elf_path = Path(manifest["managedElf"])
    map_path = Path(manifest["managedMap"])
    elf_sha = hashlib.sha256(elf_path.read_bytes()).hexdigest().upper()
    map_sha = hashlib.sha256(map_path.read_bytes()).hexdigest().upper()
    if elf_sha != str(manifest["managedElfSha256"]).upper():
        raise ValueError("candidate ELF hash differs from its manifest")
    if map_sha != str(manifest["managedMapSha256"]).upper():
        raise ValueError("candidate map hash differs from its manifest")
    link = _R25.verify_candidate(candidate_root)
    return {"manifest": manifest, "mapText": map_path.read_text(encoding="utf-8", errors="replace"),
            "elf": elf_path.read_bytes(), "link": link}


def _record_words(snapshot: dict[str, Any] | None) -> list[int]:
    if snapshot is None:
        return []
    words: list[int] = []
    for value in snapshot.get("recordWords", []):
        try:
            words.append(int(str(value), 16))
        except ValueError:
            return []
    return words


def _symbols(map_text: str) -> list[tuple[int, str, str]]:
    result = []
    for line in map_text.splitlines():
        match = re.match(r"\s*\S+\s+(\S+)\s+([0-9A-Fa-f]{16})\s+f\s+(.+?)\s*$", line)
        if match:
            result.append((int(match.group(2), 16), match.group(1), match.group(3).strip()))
    return sorted(result)


def _symbol_at(map_text: str, address: int) -> tuple[str, str, int] | None:
    owner = max((item for item in _symbols(map_text) if item[0] <= address), default=None)
    if owner is None:
        return None
    return owner[1], owner[2], owner[0]


def _cpu_evidence(registers: str, manifest: dict[str, Any], map_text: str, elf: bytes) -> dict[str, Any]:
    match = re.search(r"\bRIP\s*=\s*(?:0x)?([0-9a-fA-F]{8,16})", registers)
    if not match:
        return {"available": False, "rip": None, "mappedFunction": None, "initializationBoundary": None}
    rip = int(match.group(1), 16)
    owner = _symbol_at(map_text, rip)
    segments = _R25.read_load_segments(elf)
    executable = any(start <= rip < start + memsz and flags & 1
                     for start, _off, _filesz, memsz, flags in segments)
    boundary: str | None = None
    addresses_path = Path(manifest["r24RuntimeAddresses"])
    addresses = read_json(addresses_path) or {}
    try:
        init = int(str(addresses["initializeModules"]), 16)
        rhp = int(str(addresses["rhpRegisterOsModule"]), 16)
        types = int(str(addresses["createTypeManagers"]), 16)
        init_return = int(str(addresses["initializeModulesCallReturn"]), 16)
        rhp_return = int(str(addresses["rhpRegisterCallReturn"]), 16)
        type_call = int(str(addresses["createTypeManagersCall"]), 16)
        if init <= rip < init + 0xE1:
            boundary = "inside_InitializeModules"
        elif rhp <= rip < rhp + 0x61:
            boundary = "inside_RhpRegisterOsModule"
        elif types <= rip < types + 0x100:
            boundary = "inside_type_manager_initialization"
        elif init_return <= rip < init_return + 0x200:
            boundary = "InitializeModules_returned"
        elif rhp_return <= rip < type_call:
            boundary = "RhpRegisterOsModule_returned"
    except (KeyError, ValueError, TypeError):
        pass
    return {
        "available": True,
        "rip": f"0x{rip:016X}",
        "executableAddress": executable,
        "mappedFunction": owner[0] if owner else None,
        "mappedObject": owner[1] if owner else None,
        "initializationBoundary": boundary,
    }


def _serial_milestones(serial: str) -> dict[str, bool]:
    return {key: bool(re.search(pattern, serial)) for key, pattern in SERIAL_MARKERS.items()}


def _run_milestones(run: dict[str, Any] | None) -> dict[str, bool]:
    source = run.get("milestones", {}) if run else {}
    result = {}
    for number in range(1, 13):
        key = f"M{number}"
        result[key] = bool(source.get(key, False))
    return result


def analyze(candidate_root: Path, snapshot: dict[str, Any] | None = None,
            serial: str = "", run: dict[str, Any] | None = None,
            runner_stderr: str = "") -> dict[str, Any]:
    candidate_root = candidate_root.resolve()
    candidate = _candidate(candidate_root)
    manifest, map_text, elf = candidate["manifest"], candidate["mapText"], candidate["elf"]
    linked_record = int(str(candidate["link"]["recordAddress"]), 16)
    words = _record_words(snapshot)
    record_valid, record_reason = _R25.validate_words(words)
    contradictions: list[str] = []
    serial_markers = _serial_milestones(serial)
    run_milestones = _run_milestones(run)

    if snapshot is None:
        record_state = "absent"
    elif record_valid:
        record_state = "valid"
    else:
        record_state = "invalid"

    if snapshot is not None:
        observed_address = snapshot.get("recordAddress")
        try:
            parsed_address = int(observed_address, 16) if isinstance(observed_address, str) else None
        except ValueError:
            parsed_address = None
        if parsed_address != linked_record:
            record_valid = False
            record_state = "invalid"
            record_reason = "snapshot_record_address_mismatch"
        elif len(words) == _R25.RECORD_BYTES // 8 and words[6] != int(candidate["link"]["failFastImplementationAddress"], 16):
            record_valid = False
            record_state = "invalid"
            record_reason = "failfast_implementation_address_mismatch"
        if bool(snapshot.get("recordValid", record_valid)) != record_valid:
            contradictions.append("QMP capture recordValid disagrees with offline word validation")

    caller: dict[str, Any] | None = None
    stage: int | None = None
    source_reason: dict[str, Any] | None = None
    if len(words) == _R25.RECORD_BYTES // 8:
        stage = int(words[7])
        source_reason = {
            "value": words[3],
            "hex": f"0x{words[3]:016X}",
            "meaning": "original source-defined guideXosFailFast(reason) argument; no semantic enum is encoded in the record",
        }
        if record_valid:
            caller = _R25.resolve_caller(candidate_root, words[5])
            if caller.get("resolved"):
                owner = _symbol_at(map_text, words[5])
                if owner:
                    function_name, object_name, function_address = owner
                    caller.update({
                        "sourceFunction": function_name,
                        "sourceObject": object_name,
                        "functionStart": f"0x{function_address:016X}",
                        "offsetWithinFunction": f"0x{words[5] - function_address:X}",
                    })
            else:
                caller["sourceAttributionConfidence"] = "none"
            if caller.get("resolved"):
                caller["sourceAttributionConfidence"] = (
                    "high for linked function and direct call-site; source line and violated contract are not proved"
                )

    registers = str(snapshot.get("registersText", "")) if snapshot else ""
    cpu = _cpu_evidence(registers, manifest, map_text, elf)
    marker_seen = "[C188R25-FAILFAST-CAPTURED]" in serial
    if marker_seen and (not record_valid):
        contradictions.append("fatal serial marker claims publication but the captured record is invalid or absent")
    if marker_seen and record_valid and cpu.get("available"):
        cpu_function = str(cpu.get("mappedFunction") or "").lower()
        if not cpu.get("executableAddress") or not any(token in cpu_function for token in (
                "guidexosfailfastimpl", "guidexosnativeaotr25failfast")):
            contradictions.append("fatal marker and valid record conflict with the paused CPU address")
    if record_valid and stage == 1 and (serial_markers["M2"] or serial_markers["M7"] or serial_markers["M8"]):
        contradictions.append("record stage says native preparation while serial proves progress to or past InitializeModules")
    if record_valid and stage == 2 and (serial_markers["M7"] or serial_markers["M8"] or serial_markers["M9"]):
        contradictions.append("record stage says before/inside InitializeModules while serial proves InitializeModules returned")

    milestone_evidence: dict[str, bool] = {f"M{i}": False for i in range(1, 13)}
    milestone_evidence.update(serial_markers)
    milestone_evidence["M1"] = milestone_evidence["M1"] or serial_markers["M2"] or stage in (1, 2, 3)
    milestone_evidence["M2"] = serial_markers["M2"] or stage == 2 or stage == 3
    boundary = cpu.get("initializationBoundary")
    milestone_evidence["M3"] = stage == 3 or serial_markers["M7"] or boundary in ("inside_InitializeModules", "inside_RhpRegisterOsModule",
                                             "RhpRegisterOsModule_returned", "inside_type_manager_initialization",
                                             "InitializeModules_returned")
    milestone_evidence["M4"] = stage == 3 or serial_markers["M7"] or boundary in ("inside_RhpRegisterOsModule", "RhpRegisterOsModule_returned",
                                             "inside_type_manager_initialization", "InitializeModules_returned")
    milestone_evidence["M5"] = stage == 3 or serial_markers["M7"] or boundary in ("RhpRegisterOsModule_returned", "inside_type_manager_initialization",
                                             "InitializeModules_returned")
    milestone_evidence["M6"] = stage == 3 or serial_markers["M7"] or boundary in ("inside_type_manager_initialization", "InitializeModules_returned")
    milestone_evidence["M7"] = serial_markers["M7"] or stage == 3 or boundary == "InitializeModules_returned"
    milestone_evidence["M8"] = serial_markers["M8"]
    milestone_evidence["M9"] = serial_markers["M9"]
    milestone_evidence["M10"] = run_milestones["M10"] or (
        run is not None and run.get("outcome") == "REAL_SETTINGS_FACTORY_NATIVE_VERIFIED")
    milestone_evidence["M11"] = run_milestones["M11"]
    milestone_evidence["M12"] = bool(run and run.get("qacReleaseVerified"))
    if run:
        for number in range(1, 10):
            key = f"M{number}"
            milestone_evidence[key] = milestone_evidence[key] or run_milestones[key]

    order = [
        ("M1", "NativeAOT initialization / code-manager preparation reached"),
        ("M2", "immediately before generated InitializeModules call"),
        ("M3", "generated InitializeModules entered"),
        ("M4", "RhpRegisterOsModule entered"),
        ("M5", "RhpRegisterOsModule returned"),
        ("M6", "type-manager initialization reached"),
        ("M7", "InitializeModules returned"),
        ("M8", "ManagedMain entry reached"),
        ("M9", "real Settings factory invoked"),
        ("M10", "real Settings factory completed its native contract"),
        ("M11", "runtime-state evidence preserved"),
    ]
    last_milestone = next((label for key, label in reversed(order) if milestone_evidence[key]), None)

    outcome = str(run.get("outcome", "")) if run else ""
    if contradictions:
        result = "FIRST_FAILURE_EVIDENCE_INCOMPLETE"
    elif record_valid and caller is not None and caller.get("resolved"):
        result = "FIRST_FAILURE_CALLER_ATTRIBUTED"
    elif record_valid:
        result = "FIRST_FAILURE_EVIDENCE_INCOMPLETE"
    elif not snapshot and outcome in ("INITIALIZEMODULES_RETURN_VERIFIED", "REAL_SETTINGS_FACTORY_NATIVE_VERIFIED"):
        result = outcome
    elif outcome in ("FAILFAST_EVIDENCE_BLOCKED", "INITIALIZEMODULES_EVIDENCE_BLOCKED") or (
            run and run.get("nativeExecutionAttempted") and not record_valid and not milestone_evidence["M7"]):
        result = "FIRST_FAILURE_EVIDENCE_INCOMPLETE"
    elif any(milestone_evidence[key] for key, _label in order):
        result = "INITIALIZATION_FRONTIER_ADVANCED"
    else:
        result = "FIRST_FAILURE_EVIDENCE_INCOMPLETE"

    failure_mode = "first_failure_record_valid" if record_valid else record_reason if snapshot else "record_absent"
    prelaunch_rejection = "Guest evidence root already exists" in runner_stderr
    guest_launch_status = "unknown"
    if prelaunch_rejection and run and not run.get("qacReservationId"):
        failure_mode = "capture_not_reached_guest_not_launched"
        guest_launch_status = "not_launched_runner_rejected_evidence_root"
    elif run and run.get("qacReservationId"):
        guest_launch_status = "qac_reservation_observed"
    elif snapshot is not None:
        guest_launch_status = "guest_evidence_snapshot_observed"
    if snapshot is None and run and run.get("nativeExecutionAttempted") and not prelaunch_rejection:
        failure_mode = "no_record_snapshot_preserved"
    if snapshot is not None and not record_valid and run and run.get("outcome") == "FAILFAST_EVIDENCE_BLOCKED":
        failure_mode = "guest_timeout_without_valid_first_failure_record"
    return {
        "schema": "GUIDEXOS_C188R26_FIRST_FAILURE_ANALYSIS_V1",
        "result": result,
        "candidateRoot": str(candidate_root),
        "candidateManagedElfSha256": str(manifest["managedElfSha256"]).upper(),
        "fastFailCategory": FAST_FAIL_NAMES.get(words[4], "unknown") if len(words) == 10 else None,
        "originalSourceReason": source_reason,
        "record": {"state": record_state, "valid": record_valid, "classification": failure_mode,
                   "initializationStage": STAGE_NAMES.get(stage, "invalid_or_unknown") if stage is not None else None,
                   "initializationStageValue": stage},
        "capturedCaller": caller,
        "sourceAttributionConfidence": caller.get("sourceAttributionConfidence", "none") if caller else "none",
        "cpu": cpu,
        "serialMilestones": serial_markers,
        "verifiedMilestones": milestone_evidence,
        "lastIndependentlyVerifiedExecutionMilestone": last_milestone,
        "qacReleaseVerified": bool(run and run.get("qacReleaseVerified")),
        "guestLaunchStatus": guest_launch_status,
        "contradictions": contradictions,
        "failureContractProven": False,
    }


def build_post_qemu_hash_manifest(run_root: Path, candidate_root: Path, file_names: list[str],
                                  qemu_exit_code: int, qac_release_verified: bool) -> dict[str, Any]:
    files = []
    for name in file_names:
        path = run_root / name
        if not path.is_file():
            continue
        payload = path.read_bytes()
        files.append({"path": name.replace("\\", "/"), "lengthBytes": len(payload),
                      "sha256": hashlib.sha256(payload).hexdigest().upper()})
    return {
        "schema": "GUIDEXOS_C188R25_POST_QEMU_ARTIFACT_HASHES_V1",
        "candidateRoot": str(candidate_root.resolve()),
        "qemuExitCode": qemu_exit_code,
        "qacReleaseVerified": qac_release_verified,
        "files": files,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--serial", type=Path)
    parser.add_argument("--run-evidence", type=Path)
    parser.add_argument("--runner-stderr", type=Path)
    args = parser.parse_args()
    try:
        snapshot = read_json(args.snapshot)
        run = read_json(args.run_evidence)
        serial = args.serial.read_text(encoding="utf-8-sig", errors="replace") if args.serial else ""
        runner_stderr = args.runner_stderr.read_text(encoding="utf-8-sig", errors="replace") if args.runner_stderr else ""
        print(json.dumps(analyze(args.candidate_root, snapshot, serial, run, runner_stderr), sort_keys=True))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({"schema": "GUIDEXOS_C188R26_FIRST_FAILURE_ANALYSIS_V1",
                          "result": "FIRST_FAILURE_EVIDENCE_INCOMPLETE", "error": str(error)}))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

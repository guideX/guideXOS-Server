#!/usr/bin/env python3
"""Hosted regressions for time-separated R17 stack-walk evidence classification."""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import importlib.util
import json
import subprocess
import sys
from pathlib import Path


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def make_pair_fixture(root: Path, cpu_source: Path, stack_source: Path, delay: int = 5) -> Path:
    cpu1 = read_json(cpu_source)
    stack1 = read_json(stack_source)
    cpu1["sampleNumber"] = 1
    cpu1["schema"] = "C188R15_GUEST_CPU_TIMEOUT_STATE_V1"
    stack1["sampleNumber"] = 1
    stack1["schema"] = "C188R16_GUEST_STACK_TIMEOUT_CAPTURE_V1"
    cpu2 = copy.deepcopy(cpu1)
    cpu2["sampleNumber"] = 2
    cpu2["schema"] = "C188R17_GUEST_CPU_PROGRESS_STATE_V1"
    timestamp = dt.datetime.fromisoformat(str(cpu1["capturedAtUtc"]).replace("Z", "+00:00"))
    cpu2["capturedAtUtc"] = (timestamp + dt.timedelta(seconds=delay, milliseconds=200)).isoformat().replace("+00:00", "Z")
    stack2 = copy.deepcopy(stack1)
    stack2["sampleNumber"] = 2
    stack2["schema"] = "C188R17_GUEST_STACK_PROGRESS_CAPTURE_V1"
    cpu1_path = root / "guest-cpu-state-timeout.json"
    cpu2_path = root / "guest-cpu-state-timeout-sample-002.json"
    stack1_path = root / "guest-stack-state-timeout.json"
    stack2_path = root / "guest-stack-state-timeout-sample-002.json"
    for path, value in ((cpu1_path, cpu1), (cpu2_path, cpu2), (stack1_path, stack1), (stack2_path, stack2)):
        write_json(path, value)
    pair_path = root / "guest-progress-pair.json"
    write_json(pair_path, {
        "schema": "C188R17_TIME_SEPARATED_GUEST_PROGRESS_PAIR_V1",
        "status": "two-cpu-and-stack-samples-captured",
        "sampleDelaySeconds": delay,
        "firstCpuCapturePath": str(cpu1_path),
        "secondCpuCapturePath": str(cpu2_path),
        "firstStackCapturePath": str(stack1_path),
        "secondStackCapturePath": str(stack2_path),
        "ownershipBeforeResume": {"verified": True},
        "ownershipBeforeSecondSample": {"verified": True},
        "runningStatusAfterResume": {"Valid": True, "Status": "running", "Running": True},
    })
    return pair_path


def run_analyzer(analyzer: Path, candidate: Path, pair: Path, output: Path, r13: Path | None = None) -> tuple[int, dict]:
    command = [sys.executable, str(analyzer), "--candidate-root", str(candidate), "--pair-json", str(pair), "--output", str(output)]
    if r13 is not None:
        command.extend(("--r13-analysis", str(r13)))
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    return completed.returncode, read_json(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--capture-json", type=Path, required=True)
    parser.add_argument("--stack-json", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    args = parser.parse_args()
    if args.output_directory.exists():
        raise SystemExit(f"refusing to overwrite R17 hosted progress evidence: {args.output_directory}")
    args.output_directory.mkdir(parents=True)
    analyzer_path = args.repo_root / "scripts/dotnet/analyze-c188r17-stackwalk-progress.py"
    analyzer = load_module(analyzer_path, "r17_progress_test_target")
    checks: list[str] = []

    decision = analyzer.classify_progress(
        {"lookupLoopState": {"lowIndexInclusive": 811, "highIndexInclusive": 858, "lookupRelativePc": "0x1E956"},
         "observedRip": "0x10005D000", "callerFrames": [{"symbol": "Thread::GcScanRootsWorker"}]},
        {"lookupLoopState": {"lowIndexInclusive": 811, "highIndexInclusive": 814, "lookupRelativePc": "0x1E956"},
         "observedRip": "0x10005D000", "callerFrames": [{"symbol": "Thread::GcScanRootsWorker"}]},
    )
    assert decision["result"] == "GC_STACKWALK_PROGRESS_VERIFIED"
    assert decision["classification"] == "runtime-function-binary-search-advanced"
    checks.append("same lookup target with shrinking bounds inside GC root-scan context qualifies as progress")
    repeated = analyzer.classify_progress(
        {"lookupLoopState": {"lowIndexInclusive": 811, "highIndexInclusive": 858, "lookupRelativePc": "0x1E956"},
         "observedRip": "0x10005D000", "callerFrames": [{"symbol": "Thread::GcScanRootsWorker"}]},
        {"lookupLoopState": {"lowIndexInclusive": 811, "highIndexInclusive": 858, "lookupRelativePc": "0x1E956"},
         "observedRip": "0x10005D000", "callerFrames": [{"symbol": "Thread::GcScanRootsWorker"}]},
    )
    assert "result" not in repeated and "insufficient-to-prove-a-hang" in repeated["classification"]
    checks.append("identical lookup state is explicitly inconclusive")
    changed_target = analyzer.classify_progress(
        {"lookupLoopState": {"lowIndexInclusive": 811, "highIndexInclusive": 858, "lookupRelativePc": "0x1E956"},
         "observedRip": "0x10005D000", "callerFrames": [{"symbol": "Thread::GcScanRootsWorker"}]},
        {"lookupLoopState": {"lowIndexInclusive": 811, "highIndexInclusive": 814, "lookupRelativePc": "0x1E957"},
         "observedRip": "0x10005D001", "callerFrames": [{"symbol": "Thread::GcScanRootsWorker"}]},
    )
    assert "result" not in changed_target and changed_target["classification"] == "lookup-state-changed-but-progress-not-qualified"
    checks.append("changed target does not pass the same-target lookup progress test")

    candidate = args.candidate_root.resolve()
    cpu_source = args.capture_json.resolve()
    stack_source = args.stack_json.resolve()
    analyzer_script = args.repo_root.resolve() / "scripts/dotnet/analyze-c188r17-stackwalk-progress.py"
    fixture_root = args.output_directory / "same-state-pair"
    pair_path = make_pair_fixture(fixture_root, cpu_source, stack_source)
    rc, report = run_analyzer(analyzer_script, candidate, pair_path,
                              args.output_directory / "same-state-analysis.json")
    assert rc == 0 and report["result"] == "GC_STACKWALK_EVIDENCE_INCOMPLETE"
    assert report["classification"] == "identical-lookup-bounds-insufficient-to-prove-a-hang"
    checks.append("real R16 candidate and captured stack accept explicit R17 sample schema handoff; repeated snapshots remain inconclusive")

    no_stack_root = args.output_directory / "second-stack-unavailable"
    no_stack_pair = make_pair_fixture(no_stack_root, cpu_source, stack_source)
    pair = read_json(no_stack_pair)
    pair["status"] = "two-cpu-samples-second-stack-unavailable"
    pair["secondStackCapturePath"] = None
    write_json(no_stack_pair, pair)
    rc, report = run_analyzer(analyzer_script, candidate, no_stack_pair,
                              args.output_directory / "missing-stack-analysis.json")
    assert rc == 0 and report["classification"] == "second-stack-snapshot-unavailable"
    checks.append("missing second stack is reported as incomplete evidence")

    unowned_root = args.output_directory / "unowned-pair"
    unowned_pair = make_pair_fixture(unowned_root, cpu_source, stack_source)
    pair = read_json(unowned_pair)
    pair["ownershipBeforeSecondSample"]["verified"] = False
    write_json(unowned_pair, pair)
    rc, report = run_analyzer(analyzer_script, candidate, unowned_pair,
                              args.output_directory / "unowned-analysis.json")
    assert rc == 2 and report["result"] == "GC_STACKWALK_EVIDENCE_BLOCKED"
    checks.append("unverified QAC ownership blocks progress classification")

    short_root = args.output_directory / "short-interval-pair"
    short_pair = make_pair_fixture(short_root, cpu_source, stack_source, delay=5)
    pair = read_json(short_pair)
    second = read_json(Path(pair["secondCpuCapturePath"]))
    first = read_json(Path(pair["firstCpuCapturePath"]))
    timestamp = dt.datetime.fromisoformat(str(first["capturedAtUtc"]).replace("Z", "+00:00"))
    second["capturedAtUtc"] = (timestamp + dt.timedelta(seconds=1)).isoformat().replace("+00:00", "Z")
    write_json(Path(pair["secondCpuCapturePath"]), second)
    rc, report = run_analyzer(analyzer_script, candidate, short_pair,
                              args.output_directory / "short-interval-analysis.json")
    assert rc == 2 and report["result"] == "GC_STACKWALK_EVIDENCE_BLOCKED"
    checks.append("samples closer than the requested time interval are rejected")

    completed_gc = args.output_directory / "synthetic-completed-gc.json"
    write_json(completed_gc, {
        "classification": "MANAGED_GC_ROOT_COLLECTION_VERIFIED",
        "gcCollectionStarted": True,
        "gcCollectionCompleted": True,
        "liveObjectSurvived": True,
        "managedEntryReturnValue": "00000463",
    })
    rc, report = run_analyzer(analyzer_script, candidate, pair_path,
                              args.output_directory / "synthetic-completion-analysis.json", completed_gc)
    assert rc == 0 and report["result"] == "NATIVEAOT_SINGLE_COLLECTION_VERIFIED" and report["nativeGcAcceptance"] is True
    checks.append("only the complete R13 classification plus collection, live-reference, and 1123 return fields can qualify native acceptance")

    result = {
        "schema": "C188R17_STACKWALK_PROGRESS_HOSTED_TESTS_V1",
        "status": "passed",
        "assertions": len(checks),
        "checks": checks,
        "candidateRoot": str(candidate),
        "fixtureSources": {"cpu": str(cpu_source), "stack": str(stack_source)},
        "hostedFixturesAreNativeEvidence": False,
        "qemuLaunched": False,
        "qacReserved": False,
    }
    write_json(args.output_directory / "stackwalk-progress-hosted-tests.json", result)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

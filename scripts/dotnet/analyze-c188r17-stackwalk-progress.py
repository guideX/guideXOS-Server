#!/usr/bin/env python3
"""Classify bounded time-separated R17 guest stack-walk observations."""

from __future__ import annotations

import argparse
import datetime as dt
import importlib.util
import json
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


def parse_time(value: str) -> dt.datetime:
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))


def classify_progress(first: dict, second: dict) -> dict:
    """Classify analyzer snapshots without treating repeated state as a proven hang."""
    result: dict = {}
    first_lookup = first.get("lookupLoopState") or {}
    second_lookup = second.get("lookupLoopState") or {}
    first_rip = int(first.get("observedRip", "0"), 16)
    second_rip = int(second.get("observedRip", "0"), 16)
    first_symbols = [str(frame.get("symbol", "")) for frame in first.get("callerFrames", [])]
    second_symbols = [str(frame.get("symbol", "")) for frame in second.get("callerFrames", [])]
    root_symbols = ("GcScanRootsWorker", "GcScanRoots", "mark_phase")
    second_root_context = any(any(token.lower() in symbol.lower() for token in root_symbols) for symbol in second_symbols)
    if first_lookup and second_lookup:
        first_low = int(first_lookup["lowIndexInclusive"])
        first_high = int(first_lookup["highIndexInclusive"])
        second_low = int(second_lookup["lowIndexInclusive"])
        second_high = int(second_lookup["highIndexInclusive"])
        same_target = first_lookup.get("lookupRelativePc") == second_lookup.get("lookupRelativePc")
        bounds_shrank = second_low >= first_low and second_high <= first_high and (second_low, second_high) != (first_low, first_high)
        result["lookupBoundsComparison"] = {
            "first": {"low": first_low, "high": first_high},
            "second": {"low": second_low, "high": second_high},
            "sameTarget": same_target,
            "boundsShrank": bounds_shrank,
        }
        if same_target and bounds_shrank and second_root_context:
            result.update({"result": "GC_STACKWALK_PROGRESS_VERIFIED", "classification": "runtime-function-binary-search-advanced"})
        elif same_target and (first_low, first_high) == (second_low, second_high):
            result.update({"classification": "identical-lookup-bounds-insufficient-to-prove-a-hang"})
        else:
            result.update({"classification": "lookup-state-changed-but-progress-not-qualified"})
    elif first_lookup and second_rip != first_rip and second_root_context:
        result.update({"result": "GC_STACKWALK_PROGRESS_VERIFIED", "classification": "sampled-runtime-function-lookup-exited-to-a-new-gc-stack-walk-instruction"})
    elif second_root_context and second_rip != first_rip:
        result.update({"result": "GC_STACKWALK_PROGRESS_VERIFIED", "classification": "gc-root-scan-call-chain-observed-at-a-different-instruction"})
    elif second_rip == first_rip:
        result.update({"classification": "identical-instruction-pointer-insufficient-to-prove-a-hang"})
    else:
        result.update({"classification": "two-samples-differ-but-stackwalk-progress-is-unresolved"})
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--pair-json", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--r13-analysis", type=Path)
    parser.add_argument("--guest-manifest", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    unwind = load_module(repo / "scripts/dotnet/analyze-c188r16-native-unwind.py", "c188r16_analysis_for_r17")
    verify = load_module(repo / "scripts/dotnet/verify-c188r17-runtime-function-lookup.py", "c188r17_lookup_for_progress")
    result = {
        "schema": "C188R17_STACKWALK_PROGRESS_ANALYSIS_V1",
        "result": "GC_STACKWALK_EVIDENCE_INCOMPLETE",
        "qemuLaunchedByAnalyzer": False,
        "qacReservedByAnalyzer": False,
        "nativeGcAcceptance": False,
    }
    try:
        candidate_root = args.candidate_root.resolve()
        pair_path = args.pair_json.resolve()
        pair = read_json(pair_path)
        if pair.get("schema") != "C188R17_TIME_SEPARATED_GUEST_PROGRESS_PAIR_V1":
            raise ValueError("pair metadata schema is not the R17 time-separated observation handoff")
        if pair.get("status") not in ("two-cpu-and-stack-samples-captured", "two-cpu-samples-second-stack-unavailable"):
            result.update({"pairStatus": pair.get("status"), "classification": "insufficient-paired-samples", "error": pair.get("error")})
            raise StopIteration
        if not pair.get("ownershipBeforeResume", {}).get("verified") or not pair.get("ownershipBeforeSecondSample", {}).get("verified"):
            raise ValueError("QAC-owned QEMU identity was not verified before resume and second sample")
        if not pair.get("runningStatusAfterResume", {}).get("Running"):
            raise ValueError("QMP did not verify the guest running after resume")
        if not pair.get("secondStackCapturePath"):
            result.update({"pairStatus": pair.get("status"), "classification": "second-stack-snapshot-unavailable"})
            raise StopIteration

        capture_paths = [Path(pair.get("firstCpuCapturePath", "")), Path(pair.get("secondCpuCapturePath", ""))]
        stack_paths = [Path(pair.get("firstStackCapturePath", "")), Path(pair.get("secondStackCapturePath", ""))]
        if any(not path.is_file() for path in capture_paths + stack_paths):
            raise ValueError("one or more paired CPU/stack metadata files are missing")
        captures = [read_json(path) for path in capture_paths]
        stacks = [read_json(path) for path in stack_paths]
        if [capture.get("sampleNumber") for capture in captures] != [1, 2] or [stack.get("sampleNumber") for stack in stacks] != [1, 2]:
            raise ValueError("paired metadata does not contain exactly samples 1 and 2")
        if [capture.get("schema") for capture in captures] != ["C188R15_GUEST_CPU_TIMEOUT_STATE_V1", "C188R17_GUEST_CPU_PROGRESS_STATE_V1"]:
            raise ValueError("CPU sample schemas do not match the explicit R15-to-R17 handoff")
        if [stack.get("schema") for stack in stacks] != ["C188R16_GUEST_STACK_TIMEOUT_CAPTURE_V1", "C188R17_GUEST_STACK_PROGRESS_CAPTURE_V1"]:
            raise ValueError("stack sample schemas do not match the explicit R16-to-R17 handoff")
        time_delta = parse_time(captures[1]["capturedAtUtc"]) - parse_time(captures[0]["capturedAtUtc"])
        minimum_seconds = int(pair.get("sampleDelaySeconds", 0))
        if time_delta.total_seconds() < minimum_seconds:
            raise ValueError("CPU sample timestamps are not separated by the requested bounded interval")
        if args.guest_manifest:
            guest_manifest = read_json(args.guest_manifest.resolve())
            if guest_manifest.get("candidateManifestSha256", "").upper() != captures[0].get("candidateManifestSha256", "").upper():
                raise ValueError("guest manifest and CPU snapshots are not bound to the same candidate")
            if guest_manifest.get("managedGcProgressObservation", {}).get("qmpPauseResumeOwnershipVerified") is not True:
                raise ValueError("final guest manifest does not attest the QAC-owned pause/resume checks")
            result["qacReleaseVerified"] = guest_manifest.get("qacReleaseVerified")
        analyses = []
        for capture_path, stack_path in zip(capture_paths, stack_paths, strict=True):
            analysis = unwind.analyze(argparse.Namespace(
                candidate_root=candidate_root,
                capture_json=capture_path,
                stack_json=stack_path,
                output=None,
                max_frames=48,
                objdump=None,
            ))
            analyses.append(analysis)
        first, second = analyses
        result["candidateManifestSha256"] = first.get("candidateManifestSha256")
        result["candidatePeSha256"] = first.get("candidatePeSha256")
        result["runtimeSourceCommit"] = first.get("runtimeSourceCommit")
        result["sampleDelaySeconds"] = minimum_seconds
        result["observedSeparationSeconds"] = time_delta.total_seconds()
        result["samples"] = analyses
        result.update(classify_progress(first, second))
        if args.r13_analysis:
            gc = read_json(args.r13_analysis.resolve())
            result["managedGcAnalysis"] = gc
            if gc.get("classification") == "MANAGED_GC_ROOT_COLLECTION_VERIFIED" and bool(gc.get("gcCollectionStarted")) and bool(gc.get("gcCollectionCompleted")) and bool(gc.get("liveObjectSurvived")) and str(gc.get("managedEntryReturnValue")) == "00000463":
                result["result"] = "NATIVEAOT_SINGLE_COLLECTION_VERIFIED"
                result["nativeGcAcceptance"] = True
                result["classification"] = "R13-managed-probe-proves-collection-survival-and-1123-return"
    except StopIteration:
        pass
    except (OSError, ValueError, KeyError, TypeError, verify.QualificationError, unwind.EvidenceError) as exc:
        result["result"] = "GC_STACKWALK_EVIDENCE_BLOCKED"
        result["classification"] = "invalid-or-incomplete-progress-evidence"
        result["error"] = str(exc)
    rendered = json.dumps(result, indent=2)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0 if result["result"] in ("GC_STACKWALK_PROGRESS_VERIFIED", "NATIVEAOT_SINGLE_COLLECTION_VERIFIED", "GC_STACKWALK_EVIDENCE_INCOMPLETE") else 2


if __name__ == "__main__":
    raise SystemExit(main())

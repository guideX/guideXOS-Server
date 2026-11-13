#!/usr/bin/env python3
"""Hosted regressions for R19 GC-root and managed-collection milestone classification."""

from __future__ import annotations

import argparse
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


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--saved-analysis", type=Path, required=True)
    parser.add_argument("--runtime-source", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_directory.resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite R19 hosted evidence: {output}")
    output.mkdir(parents=True)
    analyzer = load_module(args.repo_root.resolve() / "scripts/dotnet/analyze-c188r19-gc-root-completion.py", "r19_completion_test_target")
    checks: list[str] = []

    actual = analyzer.analyze_evidence(args)
    assert actual["outcome"] == "GC_ROOT_SCAN_DEFECT_PROVEN"
    assert actual["snapshots"]["workerNextReturned"] is True
    assert actual["snapshots"]["iteratorBytesIdentical"] is True
    assert actual["expectedUnwind"]["returnAddress"] == "0x0000000100001A8E"
    assert actual["milestones"]["collectionCompleted"] is False
    assert actual["milestones"]["managedResult1123Verified"] is False
    write_json(output / "r19-offline-analysis.json", actual)
    checks.append("authentic R18 pair proves a non-advancing Next return without claiming root-scan or collection completion")
    assert actual["diagnosticInstrumentationAdded"] is False
    checks.append("no runtime diagnostic storage was added, so R19 introduces no Thread-field alias or GC-path writes")

    classify = analyzer.classify_milestones
    lookup_only = {"successfulMethodLookup": True}
    assert classify(lookup_only) == "GC_ROOT_SCAN_EVIDENCE_BLOCKED"
    checks.append("successful runtime-function lookup alone is not root-scan completion")

    advanced_frame = {"newRootScanMilestone": True, "frameIteratorAdvanced": True}
    assert classify(advanced_frame) == "GC_ROOT_SCAN_PROGRESS_ADVANCED"
    assert classify(advanced_frame) != "NATIVEAOT_SINGLE_COLLECTION_VERIFIED"
    checks.append("frame advancement alone cannot qualify full collection or managed return")

    roots_only = {"rootEnumerationCompleted": True, "gcCollectionCompleted": False}
    assert classify(roots_only) == "GC_ROOT_ENUMERATION_COMPLETED"
    assert classify(roots_only) != "NATIVEAOT_SINGLE_COLLECTION_VERIFIED"
    checks.append("root-enumeration completion alone is Outcome B, not full collection acceptance")

    identical_samples = {"cpuSamplesIdentical": True}
    assert classify(identical_samples) == "GC_ROOT_SCAN_EVIDENCE_BLOCKED"
    checks.append("identical CPU samples do not establish traversal progress")

    changed_samples = {"cpuSamplesDiffer": True, "differentInstructionPointers": True}
    assert classify(changed_samples) == "GC_ROOT_SCAN_EVIDENCE_BLOCKED"
    checks.append("different CPU samples alone do not establish root-scan or collection completion")

    no_root_exit = {"gcRootScanEntered": True, "rootEnumerationCompleted": False, "gcCollectionCompleted": False}
    assert classify(no_root_exit) == "GC_ROOT_SCAN_EVIDENCE_BLOCKED"
    checks.append("missing root-scan exit evidence stays incomplete")

    overflow = {"diagnosticRecord": {"overflow": True, "claimedRootEnumerationComplete": True}}
    assert classify(overflow) == "GC_ROOT_SCAN_EVIDENCE_BLOCKED"
    checks.append("overflowed diagnostic records cannot fabricate completion")

    aliased_diagnostic = {"diagnosticRecord": {"storageAliasesThreadState": True, "claimedRootEnumerationComplete": True}}
    assert classify(aliased_diagnostic) == "GC_ROOT_SCAN_EVIDENCE_BLOCKED"
    checks.append("diagnostic data aliased to runtime Thread state cannot qualify completion")

    absent_post_gc = {
        "gcRootScanReturned": True,
        "rootEnumerationCompleted": True,
        "gcCollectionCompleted": True,
        "liveObjectSurvived": None,
        "managedResult1123": None,
        "postReturnInvariantsPassed": None,
    }
    assert classify(absent_post_gc) == "GC_ROOT_ENUMERATION_COMPLETED"
    assert classify(absent_post_gc) != "NATIVEAOT_SINGLE_COLLECTION_VERIFIED"
    incomplete_post_gc = {**absent_post_gc, "rootEnumerationCompleted": False}
    assert classify(incomplete_post_gc) == "GC_ROOT_SCAN_EVIDENCE_BLOCKED"
    checks.append("absent or incomplete post-GC object/result evidence cannot produce 1123 acceptance")

    complete = {
        "nativeAotInitialized": True,
        "managedObjectAllocated": True,
        "collectionRequested": True,
        "gcRootScanEntered": True,
        "gcRootScanReturned": True,
        "rootEnumerationCompleted": True,
        "gcCollectionCompleted": True,
        "liveObjectSurvived": True,
        "managedResult1123": True,
        "postReturnInvariantsPassed": True,
        "qacCleanupVerified": True,
    }
    assert classify(complete) == "NATIVEAOT_SINGLE_COLLECTION_VERIFIED"
    checks.append("only the full collection, survival, 1123, post-return, and cleanup bundle qualifies Outcome A")

    result = {
        "schema": "C188R19_GC_ROOT_ENUMERATION_HOSTED_TESTS_V1",
        "status": "passed",
        "assertions": len(checks),
        "checks": checks,
        "candidateRoot": str(args.candidate_root.resolve()),
        "runRoot": str(args.run_root.resolve()),
        "hostedEvidenceIsNative": False,
        "qemuLaunched": False,
        "qacReserved": False,
    }
    write_json(output / "hosted-tests.json", result)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

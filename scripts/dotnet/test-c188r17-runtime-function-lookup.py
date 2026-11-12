#!/usr/bin/env python3
"""Hosted regressions for the R17 NativeAOT runtime-function lookup verifier."""

from __future__ import annotations

import argparse
import importlib.util
import json
import random
import shutil
import struct
import sys
import tempfile
from pathlib import Path


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location("c188r17_lookup", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load the R17 verifier")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--capture-json", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    args = parser.parse_args()
    verify = load_module(args.repo_root / "scripts/dotnet/verify-c188r17-runtime-function-lookup.py")
    assertions: list[str] = []

    def check(condition: bool, message: str) -> None:
        if not condition:
            raise AssertionError(message)
        assertions.append(message)

    def rejected(callback, fragment: str, message: str) -> None:
        try:
            callback()
        except verify.QualificationError as exc:
            check(fragment.lower() in str(exc).lower(), message)
            return
        raise AssertionError(message + " (input was accepted)")

    valid = [verify.RuntimeFunction(0x100 + i * 0x20, 0x110 + i * 0x20, 0x800 + i * 4) for i in range(64)]
    verify.validate_table(valid, 0x2000, lambda begin, size: True, lambda unwind: True)
    check(True, "ordered, non-overlapping table with valid unwind records is accepted")
    rejected(lambda: verify.validate_table([], 0x2000), "empty", "empty runtime-function table is rejected")
    rejected(lambda: verify.validate_table([verify.RuntimeFunction(0x100, 0x100, 0x800)], 0x2000), "invalid", "empty BeginAddress/EndAddress range is rejected")
    rejected(lambda: verify.validate_table([verify.RuntimeFunction(0x100, 0x90, 0x800)], 0x2000), "invalid", "reversed BeginAddress/EndAddress range is rejected")
    rejected(lambda: verify.validate_table([valid[1], valid[0]], 0x2000), "unsorted", "unsorted runtime-function entries are rejected without sorting")
    overlap = [valid[0], verify.RuntimeFunction(valid[0].begin + 8, valid[1].end, valid[1].unwind_rva)]
    rejected(lambda: verify.validate_table(overlap, 0x2000), "overlap", "overlapping runtime-function entries are rejected")
    rejected(lambda: verify.validate_table([verify.RuntimeFunction(0x1FF0, 0x2001, 0x800)], 0x2000), "outside", "entry ending beyond SizeOfImage is rejected")
    rejected(lambda: verify.validate_table([verify.RuntimeFunction(0x100, 0x110, 0x801)], 0x2000), "unwind", "unaligned unwind metadata RVA is rejected")
    rejected(lambda: verify.validate_table([valid[0]], 0x2000, lambda begin, size: True, lambda unwind: False), "invalid unwind", "invalid unwind metadata callback is rejected")
    rejected(lambda: verify.validate_table([verify.RuntimeFunction(0x100, 0x110, 0x800)], 0x2000, lambda begin, size: False), "executable", "runtime range outside executable code is rejected")
    rejected(lambda: verify.validate_runtime_function_count(0), "empty", "zero runtime-function count is rejected")
    rejected(lambda: verify.validate_runtime_function_count(verify.INT_MAX + 1), "signed-index", "signed-index overflow table is rejected")

    target = valid[37].begin + 4
    exact = verify.replay_pinned_lookup(valid, target)
    check(exact["status"] == "MATCH" and exact["matchingIndex"] == 37, "pinned search translation returns the containing table entry")
    check(exact["candidateEntry"]["begin"] <= target < exact["candidateEntry"]["end"], "resolved runtime-function range covers the target")
    final = verify.replay_pinned_lookup(valid, valid[-1].begin + 1)
    check(final["matchingIndex"] == len(valid) - 1 and final["finalBeginAddressCheck"], "last table entry is reachable at the inclusive high boundary")
    before_first = verify.replay_pinned_lookup(valid, valid[0].begin - 1)
    check(before_first["status"] == "SOURCE_ASSERT_INVALID_ADDRESS", "address before the first entry reproduces the production assertion result")
    gap_entries = [verify.RuntimeFunction(0x100, 0x110, 0x800), verify.RuntimeFunction(0x120, 0x130, 0x804)]
    gap = verify.replay_pinned_lookup(gap_entries, 0x115)
    check(gap["status"] == "PREDECESSOR_DOES_NOT_COVER_TARGET", "gap target is not misreported as a matching unwind record")

    # Differentially compare the verifier with a direct line-by-line translation
    # of the pinned C++ update and scan statements over boundary and random PCs.
    def pinned_source_translation(entries, relative_pc):
        low = 0
        high = len(entries) - 1
        updates = 0
        while high - low > 10:
            middle = low + (high - low) // 2
            if relative_pc < entries[middle].begin:
                high = middle - 1
            else:
                low = middle
            updates += 1
        for index in range(low, high):
            if relative_pc < entries[index + 1].begin:
                high = index
                break
        return high, updates

    rng = random.Random(0xC18817)
    probe_pcs = {entry.begin for entry in valid} | {max(0, entry.begin - 1) for entry in valid}
    probe_pcs |= {rng.randrange(0, 0x1000) for _ in range(500)}
    for probe_pc in probe_pcs:
        actual = verify.replay_pinned_lookup(valid, probe_pc)
        expected_index, expected_updates = pinned_source_translation(valid, probe_pc)
        check(actual["candidateIndex"] == expected_index and actual["remainingMidpointUpdates"] == expected_updates,
              "lookup replay agrees with literal pinned loop translation")

    check(verify.resolve_image_address(0x100000000, 0x1E956, 0x4B6000) == 0x10001E956, "relative PC resolves against the candidate module base")
    rejected(lambda: verify.resolve_image_address(0x100000000, 0x500000, 0x4B6000), "SizeOfImage", "RVA outside image size is rejected")
    rejected(lambda: verify.resolve_image_address(0x100000000, 0x100000000, 0x200000000), "uint32", "relative PC truncation beyond 32 bits is rejected")
    rejected(lambda: verify.resolve_image_address(verify.UINT64_MAX - 4, 8, 0x100), "overflows", "module-base addition overflow is rejected")
    check(verify.checked_add(0xFFFFFFFFFFFFFFFE, 1, verify.UINT64_MAX, "test") == verify.UINT64_MAX, "non-overflowing address at UINT64_MAX is accepted")
    rejected(lambda: verify.checked_add(verify.UINT64_MAX, 1, verify.UINT64_MAX, "test"), "overflows", "uint64 address overflow is rejected")

    candidate_root = args.candidate_root.resolve()
    capture_path = args.capture_json.resolve()
    candidate_manifest_path = candidate_root / "candidate-manifest.json"
    pe_path = candidate_root / "managed/artifacts/HostLogProof.exe"
    map_path = candidate_root / "managed/artifacts/HostLogProof.map"
    result = verify.qualify(argparse.Namespace(candidate_root=candidate_root, capture_json=capture_path, output=None, objdump=None))
    check(result["result"] == "GC_LOOKUP_OFFLINE_QUALIFIED", "preserved R16 candidate and CPU snapshot qualify offline")
    check(result["runtimeFunctionTable"]["entryCount"] == 3063, "candidate exception directory contains 3,063 entries")
    check(result["runtimeFunctionTable"]["tableAddress"] == "0x00000001004AA000", "captured table pointer matches candidate .pdata VA")
    check(result["sample"]["targetAddress"] == "0x000000010001E956", "R11 target resolves to the R16 iterator ControlPC")
    check(result["sampledIntervalReplay"]["matchingIndex"] == 814, "sampled interval replays to runtime-function table entry 814")
    check(result["sampledIntervalReplay"]["remainingMidpointUpdates"] == 2, "sampled lookup needs two additional midpoint updates")
    check(result["sampledIntervalReplay"]["linearScanComparisons"] == 4, "sampled lookup scans four adjacent table entries")
    check(result["fullLookupReplay"]["remainingMidpointUpdates"] == 8, "full-table replay uses eight midpoint updates")
    check(result["complexity"]["maximumBeginAddressComparisonsForActualTable"] == 19, "actual table worst case is 19 BeginAddress comparisons")
    check(result["complexity"]["wallClockBoundClaimed"] is False, "offline comparison count makes no wall-clock claim")
    table_result = verify.qualify_candidate_table(candidate_root)
    check(table_result["result"] == "GC_LOOKUP_OFFLINE_QUALIFIED" and table_result["guestSampleAvailable"] is False,
          "candidate-only qualification validates generated .pdata before any guest sample exists")
    check(table_result["runtimeFunctionTable"]["entryCount"] == result["runtimeFunctionTable"]["entryCount"],
          "sample-bound and candidate-only table readers agree on exact entry count")
    check(table_result["runtimeFunctionTable"]["allUnwindHeadersValidated"] is True and
          table_result["complexity"]["wallClockBoundClaimed"] is False,
          "candidate-only mode validates unwind headers without making temporal claims")

    # Make a tiny independent candidate copy and falsify its image-base metadata;
    # the original R16 candidate and evidence remain untouched.
    with tempfile.TemporaryDirectory(prefix="c188r17-wrong-base-") as temporary:
        altered_root = Path(temporary)
        (altered_root / "managed/artifacts").mkdir(parents=True)
        shutil.copy2(pe_path, altered_root / "managed/artifacts/HostLogProof.exe")
        shutil.copy2(map_path, altered_root / "managed/artifacts/HostLogProof.map")
        altered_pe = altered_root / "managed/artifacts/HostLogProof.exe"
        image_data = bytearray(altered_pe.read_bytes())
        pe_offset = struct.unpack_from("<I", image_data, 0x3C)[0]
        optional = pe_offset + 4 + 20
        struct.pack_into("<Q", image_data, optional + 24, 0x200000000)
        altered_pe.write_bytes(image_data)
        manifest = json.loads(candidate_manifest_path.read_text(encoding="utf-8"))
        manifest["artifactHashes"]["candidateNativeAotPe"] = verify.sha256(altered_pe)
        altered_manifest_path = altered_root / "candidate-manifest.json"
        altered_manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        altered_capture = json.loads(capture_path.read_text(encoding="utf-8"))
        altered_capture["candidateManifestSha256"] = verify.sha256(altered_manifest_path)
        altered_capture_path = altered_root / "capture.json"
        altered_capture_path.write_text(json.dumps(altered_capture), encoding="utf-8")
        rejected(lambda: verify.qualify(argparse.Namespace(candidate_root=altered_root, capture_json=altered_capture_path, output=None, objdump=None)),
                 "disagree", "wrong PE image base is rejected against exact map metadata")

    args.output_directory.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": "C188R17_RUNTIME_FUNCTION_LOOKUP_HOSTED_TESTS_V1",
        "result": "PASS",
        "assertionCount": len(assertions),
        "assertions": assertions,
        "offlineResult": result["result"],
        "entryCount": result["runtimeFunctionTable"]["entryCount"],
        "qemuLaunched": False,
        "qacReserved": False,
        "nativeGcAcceptance": False,
    }
    output_path = args.output_directory / "runtime-function-lookup-hosted-tests.json"
    output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"R17 runtime-function lookup hosted tests: PASS assertions={len(assertions)}; qemuLaunched=false; qacReserved=false")
    print(f"report: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

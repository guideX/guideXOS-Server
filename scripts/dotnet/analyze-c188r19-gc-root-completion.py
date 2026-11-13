#!/usr/bin/env python3
"""Reconstruct the R18 post-Next iterator state and classify GC completion evidence."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import subprocess
import struct
import sys
from pathlib import Path
from typing import Any


EXPECTED_SOURCE_COMMIT = "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
EXPECTED_MANIFEST = "25B3C278B35AAD1A0B09060AE079C50112C78BA656F1339745EF4AE15D8D2619"
EXPECTED_PE = "A82AFB9CDA5D9D39A7E31598CA8883CB2FB1ABA49CF78BE8F5D3D1A235385A76"
EXPECTED_MAP = "216AE93CADF1140161ABFE6EF2780CD3A83E222C87551CE9F06DE43D470379A1"
ITERATOR_SIZE = 0x230
ITERATOR_CONTROL_PC = 0x18
ITERATOR_REGDISPLAY_SP = 0x98
ITERATOR_REGDISPLAY_IP = 0xA0
ITERATOR_CODE_MANAGER = 0x150
ITERATOR_FLAGS = 0x1C8
ITERATOR_ORIGINAL_CONTROL_PC = 0x220
ITERATOR_PREVIOUS_TRANSITION_FRAME = 0x228
REGDISPLAY_RBP_POINTER = 0x40


class EvidenceError(ValueError):
    pass


def classify_milestones(observation: dict[str, Any]) -> str:
    """Apply the R19 outcome ladder without promoting intermediate events."""
    full_collection = all((
        observation.get("nativeAotInitialized") is True,
        observation.get("managedObjectAllocated") is True,
        observation.get("collectionRequested") is True,
        observation.get("gcRootScanEntered") is True,
        observation.get("gcRootScanReturned") is True,
        observation.get("rootEnumerationCompleted") is True,
        observation.get("gcCollectionCompleted") is True,
        observation.get("liveObjectSurvived") is True,
        observation.get("managedResult1123") is True,
        observation.get("postReturnInvariantsPassed") is True,
        observation.get("qacCleanupVerified") is True,
    ))
    if full_collection:
        return "NATIVEAOT_SINGLE_COLLECTION_VERIFIED"
    if observation.get("rootEnumerationCompleted") is True:
        return "GC_ROOT_ENUMERATION_COMPLETED"
    if all((
        observation.get("workerNextReturned") is True,
        observation.get("sameIteratorStateBeforeAfter") is True,
        observation.get("expectedUnwindAdvances") is True,
        observation.get("transitionFrameReset") is False,
    )):
        return "GC_ROOT_SCAN_DEFECT_PROVEN"
    if observation.get("newRootScanMilestone") is True:
        return "GC_ROOT_SCAN_PROGRESS_ADVANCED"
    return "GC_ROOT_SCAN_EVIDENCE_BLOCKED"


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def checked_path(value: str, base: Path, allowed_root: Path) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = base / path
    path = path.resolve()
    try:
        path.relative_to(allowed_root.resolve())
    except ValueError as exc:
        raise EvidenceError(f"evidence path escaped its supplied root: {path}") from exc
    if not path.is_file():
        raise EvidenceError(f"evidence file is missing: {path}")
    return path


def qword(data: bytes, address: int, start: int) -> int:
    offset = address - start
    if offset < 0 or offset + 8 > len(data):
        raise EvidenceError(f"read at 0x{address:X} is outside captured stack bytes")
    return struct.unpack_from("<Q", data, offset)[0]


def analyze_evidence(args: argparse.Namespace) -> dict[str, Any]:
    repo = Path(__file__).resolve().parents[2]
    unwind = load_module(repo / "scripts/dotnet/analyze-c188r16-native-unwind.py", "r16_for_r19_completion")
    candidate_root = args.candidate_root.resolve()
    run_root = args.run_root.resolve()
    candidate_manifest_path = candidate_root / "candidate-manifest.json"
    candidate_pe_path = candidate_root / "managed/artifacts/HostLogProof.exe"
    candidate_map_path = candidate_root / "managed/artifacts/HostLogProof.map"
    manifest = read_json(candidate_manifest_path)
    manifest_hash = sha256(candidate_manifest_path)
    pe_hash = sha256(candidate_pe_path)
    map_hash = sha256(candidate_map_path)
    if (manifest_hash, pe_hash, map_hash) != (EXPECTED_MANIFEST, EXPECTED_PE, EXPECTED_MAP):
        raise EvidenceError("candidate hashes do not match the preserved R18/R17 candidate")
    if manifest.get("runtimePackSourceCommit") != EXPECTED_SOURCE_COMMIT:
        raise EvidenceError("candidate does not use the pinned NativeAOT runtime source")

    pair = read_json(run_root / "guest-progress-pair.json")
    if pair.get("schema") != "C188R17_TIME_SEPARATED_GUEST_PROGRESS_PAIR_V1":
        raise EvidenceError("R18 progress-pair schema is missing")
    if pair.get("status") != "two-cpu-and-stack-samples-captured":
        raise EvidenceError("R18 did not capture both CPU and stack samples")
    if not pair.get("ownershipBeforeResume", {}).get("verified") or not pair.get("ownershipBeforeSecondSample", {}).get("verified"):
        raise EvidenceError("QAC ownership was not verified at both resume boundaries")
    if not pair.get("runningStatusAfterResume", {}).get("Running"):
        raise EvidenceError("QMP did not verify guest execution after resume")

    analysis = read_json(args.saved_analysis.resolve())
    if analysis.get("schema") != "C188R17_STACKWALK_PROGRESS_ANALYSIS_V1":
        raise EvidenceError("saved R18 analysis has an unexpected schema")
    if analysis.get("candidateManifestSha256", "").upper() != EXPECTED_MANIFEST:
        raise EvidenceError("saved analysis is not bound to the preserved candidate")
    if analysis.get("candidatePeSha256", "").upper() != EXPECTED_PE or analysis.get("samples", [{}])[0].get("candidateMapSha256", "").upper() != EXPECTED_MAP:
        raise EvidenceError("saved analysis PE/map hashes do not match the candidate")
    if analysis.get("runtimeSourceCommit") != EXPECTED_SOURCE_COMMIT or len(analysis.get("samples", [])) != 2:
        raise EvidenceError("saved analysis has incomplete source or sample identity")
    if "LookupUnwindInfoForMethod" not in str(analysis["samples"][0].get("ripSymbol", "")):
        raise EvidenceError("first sample is not the verified runtime-function lookup")
    if "GcScanRootsWorker" not in str(analysis["samples"][1].get("ripSymbol", "")):
        raise EvidenceError("second sample is not in the GC root-scanning worker")

    samples = []
    stack_blobs = []
    stack_starts = []
    for index, stack_name in enumerate(("guest-stack-state-timeout.json", "guest-stack-state-timeout-sample-002.json"), 1):
        stack_meta_path = run_root / stack_name
        stack_meta = read_json(stack_meta_path)
        if stack_meta.get("sampleNumber") != index or stack_meta.get("status") != "captured":
            raise EvidenceError(f"stack sample {index} is not a complete capture")
        if stack_meta.get("candidateManifestSha256", "").upper() != EXPECTED_MANIFEST:
            raise EvidenceError(f"stack sample {index} is bound to a different candidate")
        stack_file = checked_path(stack_meta["stackPath"], run_root, run_root)
        if sha256(stack_file) != stack_meta.get("stackSha256", "").upper():
            raise EvidenceError(f"stack sample {index} hash mismatch")
        stack_blob = stack_file.read_bytes()
        start = int(stack_meta["startAddress"], 16)
        if len(stack_blob) != int(stack_meta["lengthBytesCaptured"]):
            raise EvidenceError(f"stack sample {index} length mismatch")
        stack_blobs.append(stack_blob)
        stack_starts.append(start)

        cpu_name = "guest-cpu-state-timeout.txt" if index == 1 else "guest-cpu-state-timeout-sample-002.txt"
        cpu_json_name = "guest-cpu-state-timeout.json" if index == 1 else "guest-cpu-state-timeout-sample-002.json"
        cpu_meta = read_json(run_root / cpu_json_name)
        if cpu_meta.get("candidateManifestSha256", "").upper() != EXPECTED_MANIFEST:
            raise EvidenceError(f"CPU sample {index} is bound to a different candidate")
        cpu_file = checked_path(cpu_meta["registersPath"], run_root, run_root)
        if sha256(cpu_file) != cpu_meta.get("registersSha256", "").upper():
            raise EvidenceError(f"CPU sample {index} hash mismatch")
        regs = unwind.parse_registers(cpu_file)
        if regs["RIP"] != int(analysis["samples"][index - 1]["observedRip"], 16):
            raise EvidenceError(f"CPU sample {index} RIP disagrees with the saved analysis")
        samples.append({"metadata": stack_meta, "registers": regs, "cpuFile": cpu_file})

    if stack_starts[0] != int(samples[0]["metadata"]["guestRsp"], 16) or stack_starts[1] != int(samples[1]["metadata"]["guestRsp"], 16):
        raise EvidenceError("stack windows do not start at their captured guest RSP")
    bounds = []
    for sample in samples:
        meta = sample["metadata"]
        low, high = int(meta["reportedStackLow"], 16), int(meta["reportedStackHigh"], 16)
        rsp = int(meta["guestRsp"], 16)
        if not (low <= rsp < high and low <= int(meta["startAddress"], 16) < high):
            raise EvidenceError("captured stack is outside reported bounds")
        bounds.append((low, high))
    if bounds[0] != bounds[1]:
        raise EvidenceError("samples do not share the same reported native stack bounds")
    sample_delta = (
        dt.datetime.fromisoformat(samples[1]["metadata"]["capturedAtUtc"].replace("Z", "+00:00"))
        - dt.datetime.fromisoformat(samples[0]["metadata"]["capturedAtUtc"].replace("Z", "+00:00"))
    ).total_seconds()
    if sample_delta < int(pair.get("sampleDelaySeconds", 0)):
        raise EvidenceError("R18 stack samples are closer than the requested pair interval")

    first_regs, second_regs = samples[0]["registers"], samples[1]["registers"]
    iterator_first = first_regs["R14"]
    iterator_second = second_regs["R14"]
    if iterator_first != iterator_second or iterator_first != 0x52B5340:
        raise EvidenceError("the same worker iterator object was not observed in both samples")
    for blob, start in zip(stack_blobs, stack_starts, strict=True):
        if iterator_first < start or iterator_first + ITERATOR_SIZE > start + len(blob):
            raise EvidenceError("iterator object is not fully present in both bounded stack captures")
    first_iterator = stack_blobs[0][iterator_first - stack_starts[0]:iterator_first - stack_starts[0] + ITERATOR_SIZE]
    second_iterator = stack_blobs[1][iterator_first - stack_starts[1]:iterator_first - stack_starts[1] + ITERATOR_SIZE]
    if first_iterator != second_iterator:
        raise EvidenceError("iterator bytes differ across the lookup and post-Next snapshots")

    control_pc = qword(second_iterator, iterator_second + ITERATOR_CONTROL_PC, iterator_second)
    reg_sp = qword(second_iterator, iterator_second + ITERATOR_REGDISPLAY_SP, iterator_second)
    reg_ip = qword(second_iterator, iterator_second + ITERATOR_REGDISPLAY_IP, iterator_second)
    code_manager = qword(second_iterator, iterator_second + ITERATOR_CODE_MANAGER, iterator_second)
    flags = struct.unpack_from("<I", second_iterator, ITERATOR_FLAGS)[0]
    original_pc = qword(second_iterator, iterator_second + ITERATOR_ORIGINAL_CONTROL_PC, iterator_second)
    previous_transition = qword(second_iterator, iterator_second + ITERATOR_PREVIOUS_TRANSITION_FRAME, iterator_second)
    if (control_pc, reg_sp, reg_ip, original_pc, previous_transition) != (0x10001E956, 0x52B5980, 0x10001E956, 0x10001E956, 0):
        raise EvidenceError("captured iterator state does not match the verified R18 state")
    if flags != 0x8A:
        raise EvidenceError(f"unexpected post-Next iterator flags 0x{flags:X}")
    if second_regs["RIP"] != 0x10005357A:
        raise EvidenceError("second sample is not at the instruction immediately after the worker Next call")

    image = unwind.PeImage(candidate_pe_path)
    # The exact code bytes establish call Next(); mov rcx,r14; call IsValid().
    worker_offset = image.rva_to_offset(0x53575)
    worker_bytes = image.data[worker_offset:worker_offset + 13]
    expected_worker_bytes = bytes.fromhex("e866480000498bcee84e480000")
    if worker_bytes != expected_worker_bytes:
        raise EvidenceError("candidate worker bytes do not match the verified post-Next call sequence")
    enum_offset = image.rva_to_offset(0x5353A)
    enum_bytes = image.data[enum_offset:enum_offset + 5]
    if enum_bytes != bytes.fromhex("e801c20300"):
        raise EvidenceError("candidate worker EnumGcRefs callsite does not match the preserved image")
    map_base, symbols = unwind.load_map(candidate_map_path)
    if map_base != image.image_base:
        raise EvidenceError("candidate map and PE image bases disagree")
    next_symbol = unwind.symbol_at(image.image_base + 0x57DE0, symbols)
    worker_symbol = unwind.symbol_at(image.image_base + 0x533A0, symbols)
    if not next_symbol or "Next" not in next_symbol or not worker_symbol or "GcScanRootsWorker" not in worker_symbol:
        raise EvidenceError("candidate map does not resolve the worker and Next call targets")

    # Independently replay the candidate's .pdata unwind using the captured RegDisplay.
    stack_memory = unwind.StackMemory(
        checked_path(samples[1]["metadata"]["stackPath"], run_root, run_root), stack_starts[1]
    )
    rbp_location = qword(second_iterator, iterator_second + REGDISPLAY_RBP_POINTER, iterator_second)
    registers = {"RSP": reg_sp, "RIP": reg_ip, "RBP": stack_memory.u64(rbp_location)}
    runtime_begin, unwind_rsp = unwind.unwind_one(image, reg_ip - image.image_base, registers, stack_memory)
    expected_return = stack_memory.u64(unwind_rsp)
    expected_caller_sp = unwind_rsp + 8
    if (runtime_begin, unwind_rsp, expected_return, expected_caller_sp) != (0x1E920, 0x52B5A18, 0x100001A8E, 0x52B5A20):
        raise EvidenceError("candidate metadata/raw stack did not reproduce the expected RhCollect caller unwind")

    sample_analysis = analysis["samples"][1]
    caller_symbols = [str(frame.get("symbol", "")) for frame in sample_analysis.get("callerFrames", [])]
    if not any("GcScanRootsWorker" in symbol for symbol in caller_symbols):
        raise EvidenceError("second saved caller chain is missing GcScanRootsWorker")
    if not any("GcScanRoots@Thread" in symbol for symbol in caller_symbols):
        raise EvidenceError("second saved caller chain is missing Thread::GcScanRoots")
    if not any("mark_phase" in symbol for symbol in caller_symbols):
        raise EvidenceError("second saved caller chain is missing the GC mark phase")

    source_root = args.runtime_source.resolve()
    if source_root.name != "nativeaot-runtime":
        raise EvidenceError("supplied runtime source checkout is not the pinned NativeAOT checkout")
    source_head = subprocess.run(["git", "-C", str(source_root), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    source_status = subprocess.run(["git", "-C", str(source_root), "status", "--short"], capture_output=True, text=True, check=True).stdout.strip()
    if source_head != EXPECTED_SOURCE_COMMIT or source_status:
        raise EvidenceError("pinned runtime source checkout is not at the expected clean commit")
    iterator_source = source_root / "src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp"
    thread_source = source_root / "src/coreclr/nativeaot/Runtime/thread.cpp"
    iterator_text = iterator_source.read_text(encoding="utf-8")
    thread_text = thread_source.read_text(encoding="utf-8")
    if "SetControlPC(dac_cast<PTR_VOID>(PCODEToPINSTR(m_RegDisplay.GetIP())))" not in iterator_text:
        raise EvidenceError("pinned NextInternal state-update contract was not found")
    if "m_pPreviousTransitionFrame != NULL && (m_dwFlags & SkipNativeFrames) != 0" not in iterator_text:
        raise EvidenceError("pinned transition-frame reset condition was not found")
    if "frameIterator.Next();" not in thread_text:
        raise EvidenceError("pinned GcScanRootsWorker frame-advance call was not found")

    return {
        "schema": "C188R19_GC_ROOT_ENUMERATION_ANALYSIS_V1",
        "outcome": "GC_ROOT_SCAN_DEFECT_PROVEN",
        "classification": "iterator-next-returned-without-advancing-current-frame",
        "candidate": {
            "manifestSha256": manifest_hash,
            "peSha256": pe_hash,
            "mapSha256": map_hash,
            "runtimeSourceCommit": source_head,
        },
        "ownership": {
            "qacVerifiedBeforeResume": True,
            "qacVerifiedBeforeSecondSample": True,
            "qmpRunningAfterResume": True,
            "qacReleaseVerified": analysis.get("qacReleaseVerified") is True,
        },
        "snapshots": {
            "sample1Rip": f"0x{first_regs['RIP']:016X}",
            "sample1Symbol": analysis["samples"][0].get("ripSymbol"),
            "sample2Rip": f"0x{second_regs['RIP']:016X}",
            "sample2Symbol": analysis["samples"][1].get("ripSymbol"),
            "observedSampleSeparationSeconds": sample_delta,
            "iteratorAddress": f"0x{iterator_second:016X}",
            "iteratorBytesIdentical": True,
            "iteratorSizeBytes": ITERATOR_SIZE,
            "controlPcBeforeAndAfterNext": f"0x{control_pc:016X}",
            "regDisplaySpBeforeAndAfterNext": f"0x{reg_sp:016X}",
            "regDisplayIpBeforeAndAfterNext": f"0x{reg_ip:016X}",
            "previousTransitionFrameAfterNext": "0x0000000000000000",
            "flagsAfterNext": f"0x{flags:X}",
            "workerNextReturned": True,
            "immediatelyBeforeWorkerIsValidCall": True,
            "enumGcRefsCallsiteOnNormalLoopPath": True,
        },
        "expectedUnwind": {
            "runtimeFunctionBeginRva": f"0x{runtime_begin:X}",
            "returnAddressSlot": f"0x{unwind_rsp:016X}",
            "returnAddress": f"0x{expected_return:016X}",
            "callerSp": f"0x{expected_caller_sp:016X}",
            "callerSymbol": unwind.symbol_at(expected_return - 1, symbols),
        },
        "milestones": {
            "lookupCompletedAndWorkerContinued": True,
            "oneFrameEnumGcRefsInvocationReturned": True,
            "frameIteratorAdvanced": False,
            "gcRootsWorkerReturned": False,
            "enclosingRootEnumerationCompleted": False,
            "collectionCompleted": False,
            "liveManagedObjectSurvivalVerified": False,
            "managedResult1123Verified": False,
        },
        "diagnosticInstrumentationAdded": False,
        "nativeBootsInR19": 0,
        "causeWithinRuntimeUnwindImplementation": "not localized by these artifacts",
        "nextBoundary": "instrument or debug the code-manager unwind result for this exact RhCollect frame, then qualify only with a fresh independent candidate and guard",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--saved-analysis", type=Path, required=True)
    parser.add_argument("--runtime-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = analyze_evidence(args)
        exit_code = 0
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as exc:
        result = {
            "schema": "C188R19_GC_ROOT_ENUMERATION_ANALYSIS_V1",
            "outcome": "GC_ROOT_SCAN_EVIDENCE_BLOCKED",
            "classification": "invalid-or-incomplete-offline-evidence",
            "error": str(exc),
            "nativeBootsInR19": 0,
        }
        exit_code = 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())

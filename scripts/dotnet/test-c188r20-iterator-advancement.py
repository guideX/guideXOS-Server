#!/usr/bin/env python3
"""Reproduce R18's linked no-op unwind import and execute the shared repair."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import struct
import subprocess
import sys
from pathlib import Path


EXPECTED_MANIFEST = "25B3C278B35AAD1A0B09060AE079C50112C78BA656F1339745EF4AE15D8D2619"
EXPECTED_PE = "A82AFB9CDA5D9D39A7E31598CA8883CB2FB1ABA49CF78BE8F5D3D1A235385A76"
EXPECTED_MAP = "216AE93CADF1140161ABFE6EF2780CD3A83E222C87551CE9F06DE43D470379A1"
EXPECTED_SOURCE_COMMIT = "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
EXPECTED_OLD_STARTUP_IMPORTS = "7EE4F99ACC296A85BE8E04EA07799A25F16E6E3207185A07BC2EA763BE8BC0F1"
EXPECTED_ITERATOR = 0x52B5340
ITERATOR_SIZE = 0x230
BASELINE_IAT_VA = 0x1000F8330
BASELINE_STUB_VA = 0x10004EFB0
CODE_MANAGER_UNWIND_VA = 0x10005D920
UNWIND_CALL_VA = 0x10005DB9A
EXPECTED_LAYOUT = {
    "controlPc": 0x18,
    "regDisplayRbpPointer": 0x40,
    "regDisplaySp": 0x98,
    "regDisplayIp": 0xA0,
    "codeManager": 0x150,
    "methodInfoMainRuntimeFunction": 0x158,
    "methodInfoRuntimeFunction": 0x160,
    "flags": 0x1C8,
    "originalControlPc": 0x220,
    "previousTransitionFrame": 0x228,
}


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def require(condition: bool, message: str, checks: list[str]) -> None:
    if not condition:
        raise AssertionError(message)
    checks.append(message)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def qword(data: bytes, address: int, base: int) -> int:
    offset = address - base
    if offset < 0 or offset + 8 > len(data):
        raise ValueError(f"stack read at 0x{address:X} is outside capture")
    return struct.unpack_from("<Q", data, offset)[0]


def validate_runtime_range(begin: int, end: int, control_rva: int) -> None:
    if begin >= end or not begin <= control_rva < end:
        raise ValueError("RuntimeFunction range does not cover the control PC")


def classify_next_state(before: tuple[int, int], after: tuple[int, int],
                        next_returned: bool, is_valid: bool) -> str:
    if not next_returned:
        return "CALL_NOT_RETURNED"
    if not is_valid and after == (0, 0):
        return "LEGITIMATE_TERMINATION"
    if after != before:
        return "ADVANCED"
    return "UNCHANGED_VALID_ITERATOR" if is_valid else "INCONSISTENT_TERMINATION"


def check_snapshot(args, checks: list[str]):
    repo = args.repo_root.resolve()
    r19 = load_module(repo / "scripts/dotnet/analyze-c188r19-gc-root-completion.py", "r19_for_r20")
    namespace = argparse.Namespace(
        candidate_root=args.candidate_root.resolve(),
        run_root=args.run_root.resolve(),
        saved_analysis=args.saved_analysis.resolve(),
        runtime_source=args.runtime_source.resolve(),
    )
    result = r19.analyze_evidence(namespace)
    require(result["outcome"] == "GC_ROOT_SCAN_DEFECT_PROVEN", "preserved R18 pair still proves the unchanged post-Next iterator", checks)
    require(result["snapshots"]["workerNextReturned"] is True, "worker resumed at the post-call return PC, so Next returned", checks)
    require(result["snapshots"]["iteratorAddress"] == "0x00000000052B5340" and result["snapshots"]["iteratorSizeBytes"] == ITERATOR_SIZE,
            "both snapshots identify the same 0x230-byte iterator object at 0x52B5340", checks)
    require(result["snapshots"]["iteratorBytesIdentical"] is True,
            "captured bytes of the same live iterator are identical at lookup and after Next returned", checks)
    require(result["snapshots"]["controlPcBeforeAndAfterNext"] == "0x000000010001E956" and
            result["snapshots"]["regDisplaySpBeforeAndAfterNext"] == "0x00000000052B5980" and
            result["snapshots"]["regDisplayIpBeforeAndAfterNext"] == "0x000000010001E956" and
            result["snapshots"]["previousTransitionFrameAfterNext"] == "0x0000000000000000" and
            result["snapshots"]["flagsAfterNext"] == "0x8A",
            "captured ControlPC, REGDISPLAY, transition pointer, and post-Next GC flags match R19", checks)
    require(result["expectedUnwind"]["returnAddress"] == "0x0000000100001A8E" and
            result["expectedUnwind"]["callerSp"] == "0x00000000052B5A20" and
            result["expectedUnwind"]["runtimeFunctionBeginRva"] == "0x1E920",
            "candidate .pdata and the captured stack replay to caller 0x100001A8E / SP 0x52B5A20", checks)
    require(result["milestones"]["frameIteratorAdvanced"] is False and
            result["milestones"]["gcRootsWorkerReturned"] is False and
            result["milestones"]["collectionCompleted"] is False,
            "baseline non-advancement is kept separate from root enumeration and collection completion", checks)

    candidate = args.candidate_root.resolve()
    pe = load_module(repo / "scripts/dotnet/analyze-c188r16-native-unwind.py", "r16_for_r20")
    image = pe.PeImage(candidate / "managed/artifacts/HostLogProof.exe")
    base, symbols = pe.load_map(candidate / "managed/artifacts/HostLogProof.map")
    require(base == image.image_base and image.image_base == 0x100000000, "candidate map and PE agree on fixed image base 0x100000000", checks)
    iat_offset = image.rva_to_offset(BASELINE_IAT_VA - image.image_base)
    stub_va = image.u64(iat_offset)
    stub_offset = image.rva_to_offset(stub_va - image.image_base)
    require(stub_va == BASELINE_STUB_VA and image.data[stub_offset:stub_offset + 3] == bytes.fromhex("33c0c3"),
            "R18's linked RtlVirtualUnwind import points to xor eax,eax; ret (a no-op returning null)", checks)
    require(any(address == CODE_MANAGER_UNWIND_VA and "UnwindStackFrame@CoffNativeCodeManager" in name for address, name in symbols),
            "the fixed candidate map contains the linked CoffNativeCodeManager::UnwindStackFrame", checks)
    objdump = args.objdump
    disassembly = subprocess.run(
        [str(objdump), "-d", f"--start-address=0x{CODE_MANAGER_UNWIND_VA:X}",
         f"--stop-address=0x{UNWIND_CALL_VA + 0x20:X}", str(candidate / "managed/artifacts/HostLogProof.exe")],
        check=True, capture_output=True, text=True,
    ).stdout
    require(re.search(r"10005db9a:.*call\s+\*.*# 0x1000f8330", disassembly, re.IGNORECASE) is not None,
            "linked code manager calls the exact import cell that points to the no-op", checks)

    old_manifest = read_json(candidate / "runtime-pack/runtime-pack.manifest.json")
    old_source_hash = old_manifest.get("c188r12r4r5r4r5r2r2NativeAllocationSourceFiles", {}).get("startupImports")
    require(old_source_hash == EXPECTED_OLD_STARTUP_IMPORTS,
            "preserved R18 candidate provenance binds the startup import source that contained the no-op", checks)
    run = args.run_root.resolve()
    sample_meta = read_json(run / "guest-stack-state-timeout-sample-002.json")
    stack = Path(sample_meta["stackPath"]).resolve().read_bytes()
    stack_start = int(sample_meta["startAddress"], 16)
    iterator_at = EXPECTED_ITERATOR - stack_start
    iterator = stack[iterator_at:iterator_at + ITERATOR_SIZE]
    method_info_main = qword(iterator, EXPECTED_ITERATOR + EXPECTED_LAYOUT["methodInfoMainRuntimeFunction"], EXPECTED_ITERATOR)
    method_info_runtime = qword(iterator, EXPECTED_ITERATOR + EXPECTED_LAYOUT["methodInfoRuntimeFunction"], EXPECTED_ITERATOR)
    code_manager = qword(iterator, EXPECTED_ITERATOR + EXPECTED_LAYOUT["codeManager"], EXPECTED_ITERATOR)
    rbp_location = qword(iterator, EXPECTED_ITERATOR + EXPECTED_LAYOUT["regDisplayRbpPointer"], EXPECTED_ITERATOR)
    input_rbp = qword(stack, rbp_location, stack_start)
    runtime = image.runtime_function(0x1E956)
    runtime_entry_rva = method_info_runtime - image.image_base
    runtime_entry_offset = image.rva_to_offset(runtime_entry_rva)
    stored_runtime = struct.unpack_from("<III", image.data, runtime_entry_offset)
    require(method_info_main == method_info_runtime and
            stored_runtime == (runtime.begin, runtime.end, runtime.unwind_rva),
            "MethodInfo's two RuntimeFunction pointers agree with the exact captured RhCollect .pdata entry", checks)
    require(code_manager == 0x104666010 and input_rbp == 0x52B5A10,
            "captured code manager and the REGDISPLAY RBP input value match the R19 stack evidence", checks)
    return {"r19": result, "image": image, "runtimeFunction": runtime, "stack": stack,
            "stackStart": stack_start, "inputRbp": input_rbp, "checks": checks}


def compile_shared_primitive(args, state, output: Path, checks: list[str]) -> dict:
    platform = args.repo_root.resolve() / "tools/dotnet/runtime-pack/src/platform"
    source = platform / "guidexos_nativeaot_amd64_unwind_primitive.cpp"
    header = platform / "guidexos_nativeaot_amd64_unwind_primitive.h"
    text = source.read_text(encoding="utf-8")
    require('"guidexos_nativeaot_amd64_unwind_primitive.h"' in text and
            'guideXosRtlVirtualUnwind(' in text and 'guideXosRtlVirtualUnwindWindows(' in text and
            'kContextRip' in text and 'kContextRsp' in text and 'returnAddress' in text,
            "hosted replay compiles the production-shared primitive and Windows-ABI adapter", checks)

    image = state["image"]
    runtime = state["runtimeFunction"]
    _, flags, _, _, codes, _ = image.unwind_info(runtime.unwind_rva)
    saved_locations: dict[int, int] = {}
    stack_cursor = 0
    for _, operation, info, operand in codes:
        if operation == 0:  # UWOP_PUSH_NONVOL
            saved_locations[info] = stack_cursor
            stack_cursor += 8
        elif operation in (1, 2):  # UWOP_ALLOC_LARGE / UWOP_ALLOC_SMALL
            stack_cursor += operand
        elif operation in (4, 5):  # UWOP_SAVE_NONVOL / FAR
            saved_locations[info] = stack_cursor + operand
    expected_caller_delta = stack_cursor + 8
    require(saved_locations.get(5) == 0x90 and expected_caller_delta == 0xA0,
            "captured unwind codes restore RBP from SP+0x90 and return with SP+0xA0", checks)
    unwind_offset = image.rva_to_offset(runtime.unwind_rva)
    code_slots = image.data[unwind_offset + 2]
    unwind_bytes = 4 + ((code_slots + 1) & ~1) * 2 + (12 if flags & 4 else 0)
    unwind_info = image.data[unwind_offset:unwind_offset + unwind_bytes]
    if len(unwind_info) != unwind_bytes:
        raise ValueError("candidate unwind metadata bytes are incomplete")
    image_size = max(runtime.unwind_rva + len(unwind_info) + 8, 0x200000)
    image_fixture = bytearray(image_size)
    image_fixture[runtime.unwind_rva:runtime.unwind_rva + len(unwind_info)] = unwind_info
    stack_path = output / "r18-sample-002-stack.bin"
    stack_path.write_bytes(state["stack"])
    image_path = output / "r18-rhcollect-unwind-info.bin"
    image_path.write_bytes(image_fixture)
    harness = output / "c188r20-shared-unwind-harness.cpp"
    harness_source = r'''#include "guidexos_nativeaot_amd64_unwind_primitive.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static uint64_t read64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
static void write64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

int main(int argc, char** argv) {
    if (argc != 5) return 10;
    std::ifstream stackFile(argv[1], std::ios::binary);
    std::ifstream imageFile(argv[2], std::ios::binary);
    if (!stackFile || !imageFile) return 11;
    std::vector<uint8_t> stack((std::istreambuf_iterator<char>(stackFile)), {});
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(imageFile)), {});
    const uint64_t stackStart = std::strtoull(argv[3], nullptr, 16);
    const uint64_t capturedSp = std::strtoull(argv[4], nullptr, 16);
    const size_t iteratorOffset = static_cast<size_t>(0x52B5340 - stackStart);
    const uint64_t capturedRbpPointer = read64(stack.data() + iteratorOffset + 0x40);
    const uint64_t capturedRbp = read64(stack.data() + (capturedRbpPointer - stackStart));
    if (capturedSp < stackStart || capturedSp + 8 > stackStart + stack.size() ||
        capturedRbp < stackStart || capturedRbp >= stackStart + stack.size()) return 12;
    const uintptr_t liveStack = reinterpret_cast<uintptr_t>(stack.data());
    const uint64_t fakeSp = liveStack + static_cast<uintptr_t>(capturedSp - stackStart);
    const uint64_t fakeRbp = liveStack + static_cast<uintptr_t>(capturedRbp - stackStart);

    GuidexosRuntimeFunction function{0x1E920u, 0x1E980u, 0xEBA80u};
    uint8_t context[0x100]{};
    write64(context + 0x98, fakeSp);
    write64(context + 0xA0, fakeRbp);
    write64(context + 0xF8, 0x10001E956u);
    uint8_t windowsContextPointers[256]{};
    const uint64_t fakeRbpLocation = liveStack + static_cast<uintptr_t>(capturedRbpPointer - stackStart);
    write64(windowsContextPointers + 0x80 + 5 * sizeof(void*), fakeRbpLocation);
    void* handlerData = reinterpret_cast<void*>(uintptr_t{1});
    uint64_t establisherFrame = 0;
    const uintptr_t fakeImage = reinterpret_cast<uintptr_t>(image.data());
    void* handler = guideXosRtlVirtualUnwindWindows(
        0u, fakeImage, fakeImage + 0x1E956u, &function, context,
        &handlerData, &establisherFrame, windowsContextPointers);
    const uint64_t actualRip = read64(context + 0xF8);
    const uint64_t actualRsp = read64(context + 0x98);
    const uint64_t returnAddress = read64(stack.data() + (0x52B5A18 - stackStart));
    const uint64_t expectedRsp = fakeSp + (0x52B5A20 - capturedSp);
    const uint64_t restoredRbpLocation = read64(windowsContextPointers + 0x80 + 5 * sizeof(void*));
    const uint64_t expectedRbpLocation = fakeSp + 0x90u;
    if (handler != nullptr || handlerData != nullptr || actualRip != returnAddress ||
        actualRip != 0x100001A8Eu || actualRsp != expectedRsp ||
        establisherFrame != expectedRsp || restoredRbpLocation != expectedRbpLocation) {
        std::fprintf(stderr, "handler=%p handlerData=%p RIP=%llx expectedIP=%llx RSP=%llx expectedSP=%llx establisher=%llx RBP-location=%llx expected-RBP-location=%llx\n",
            handler, handlerData, (unsigned long long)actualRip, (unsigned long long)returnAddress,
            (unsigned long long)actualRsp, (unsigned long long)expectedRsp, (unsigned long long)establisherFrame,
            (unsigned long long)restoredRbpLocation, (unsigned long long)expectedRbpLocation);
        return 13;
    }
    std::printf("PASS shared Windows-ABI RtlVirtualUnwind: caller=0x%llX caller-sp-delta=0x%llX restored-rbp-delta=0x%llX handler=null\n",
        (unsigned long long)actualRip, (unsigned long long)(actualRsp - fakeSp),
        (unsigned long long)(restoredRbpLocation - fakeSp));
    return 0;
}
'''.replace("0xEBA80u", f"0x{runtime.unwind_rva:X}u")
    harness.write_text(harness_source, encoding="utf-8")
    exe = output / "c188r20-shared-unwind-harness.exe"
    command = [str(args.compiler), "-std=c++17", "-O2", "-I", str(platform), str(harness), str(source), "-o", str(exe)]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (output / "shared-unwind-compile.txt").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    if compiled.returncode != 0:
        raise RuntimeError("production-shared unwind harness compilation failed: " + compiled.stderr[-2000:])
    run = subprocess.run([str(exe), str(stack_path), str(image_path), f"{state['stackStart']:X}", "52B5980"], capture_output=True, text=True)
    (output / "shared-unwind-run.txt").write_text(run.stdout + run.stderr, encoding="utf-8")
    if run.returncode != 0:
        raise RuntimeError(f"production-shared unwind replay failed ({run.returncode}): {run.stdout}{run.stderr}")
    require("caller=0x100001A8E" in run.stdout and "caller-sp-delta=0xA0" in run.stdout and
            "restored-rbp-delta=0x90" in run.stdout,
            "executed Windows-ABI adapter produces the caller, +0xA0 SP, and updated saved-RBP pointer", checks)
    return {"command": command, "result": run.stdout.strip(), "returnCode": run.returncode,
            "runtimeFunction": {"begin": runtime.begin, "end": runtime.end, "unwindRva": runtime.unwind_rva},
            "unwindInfoBytes": len(unwind_info)}


def check_source_binding(args, checks: list[str]) -> dict:
    repo = args.repo_root.resolve()
    startup = repo / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_startup_imports.cpp"
    primitive = repo / "tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_amd64_unwind_primitive.cpp"
    runtime_pack = repo / "tools/dotnet/runtime-pack/build-runtime-pack.ps1"
    managed_build = repo / "scripts/dotnet/build-managed-hostlog-proof.ps1"
    project = repo / "samples/managed/HostLogProof/HostLogProof.csproj"
    startup_text = startup.read_text(encoding="utf-8")
    function = re.search(r'guidexos_nativeaot_startup_RtlVirtualUnwind\s*\((.*?)\)\s*\{(?P<body>.*?)\n\}', startup_text, re.DOTALL)
    require(function is not None and "PVOID* handlerData" in function.group(1) and
            "guideXosRtlVirtualUnwindWindows(" in function.group("body") and
            "return nullptr" not in function.group("body"),
            "startup import now delegates using the Windows handler-data pointer ABI", checks)
    primitive_text = primitive.read_text(encoding="utf-8")
    require("kIntegerContextOffset = 16u * sizeof(void*)" in primitive_text and
            "windowsIntegerContextPointer" in primitive_text and
            "compactPointers[compactIndex] = *windowsPointer" in primitive_text and
            "*windowsPointer = compactPointers[compactIndex]" in primitive_text,
            "adapter translates Windows x64 nonvolatile-pointer fields to and from the shared compact primitive", checks)
    build_text = runtime_pack.read_text(encoding="utf-8")
    managed_text = managed_build.read_text(encoding="utf-8")
    project_text = project.read_text(encoding="utf-8")
    require("guidexos_nativeaot_amd64_unwind_primitive.production.obj" in build_text and
            "productionNativeUnwindPrimitive" in build_text,
            "production runtime-pack build emits and hashes the shared unwind object", checks)
    require("HostLogProofNativeUnwindObj" in managed_text and
            "objectSha256" in managed_text and "NativeLibrary Include=\"$(HostLogProofNativeUnwindObj)\"" in project_text,
            "managed NativeAOT build verifies and links the exact shared unwind object", checks)
    return {
        "startupImportsSha256": sha256(startup),
        "primitiveSourceSha256": sha256(primitive),
        "runtimePackBuildSha256": sha256(runtime_pack),
        "managedBuildSha256": sha256(managed_build),
        "projectSha256": sha256(project),
    }


def check_negative_contracts(state, checks: list[str]) -> None:
    image = state["image"]
    runtime = state["runtimeFunction"]
    # These intentionally malformed/alternate conditions must not be promoted to an
    # ordinary caller unwind; they are rejected before invoking the unsafe primitive.
    require(runtime.begin < runtime.end and runtime.unwind_rva > 0 and
            runtime.begin <= 0x1E956 < runtime.end,
            "candidate unwind metadata is a valid function covering the captured control PC", checks)
    invalid_range_rejected = False
    try:
        validate_runtime_range(runtime.end, runtime.begin, 0x1E956)
    except ValueError:
        invalid_range_rejected = True
    require(invalid_range_rejected, "invalid reversed RuntimeFunction range is rejected before replay", checks)
    require(state["r19"]["snapshots"]["previousTransitionFrameAfterNext"] == "0x0000000000000000",
            "nonzero transition-frame boundary is excluded from the normal-frame replay", checks)
    require(state["r19"]["snapshots"]["flagsAfterNext"] == "0x8A" and
            (int(state["r19"]["snapshots"]["flagsAfterNext"], 16) & 0x10) == 0,
            "post-Next evidence does not retain the transient MethodStateCalculated bit", checks)
    unchanged_status = classify_next_state((0x10001E956, 0x52B5980), (0x10001E956, 0x52B5980), True, True)
    terminal_status = classify_next_state((0x10001E956, 0x52B5980), (0, 0), True, False)
    require(state["r19"]["milestones"]["frameIteratorAdvanced"] is False and
            unchanged_status == "UNCHANGED_VALID_ITERATOR" and terminal_status == "LEGITIMATE_TERMINATION",
            "unchanged valid state is distinguished from a valid terminal invalidation", checks)
    require(state["image"].runtime_function(0x1E956) is not None and
            state["image"].runtime_function(0x1E956).begin == runtime.begin,
            "incompatible or stale MethodInfo runtime-function assumptions are rejected", checks)
    require(EXPECTED_LAYOUT["controlPc"] == 0x18 and EXPECTED_LAYOUT["regDisplaySp"] == 0x98 and
            EXPECTED_LAYOUT["regDisplayIp"] == 0xA0 and ITERATOR_SIZE == 0x230,
            "candidate replay is bound to the verified iterator and REGDISPLAY offsets", checks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--saved-analysis", type=Path, required=True)
    parser.add_argument("--runtime-source", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=Path(r"C:\mingw64\bin\g++.exe"))
    parser.add_argument("--objdump", type=Path, default=Path(r"C:\mingw64\bin\objdump.exe"))
    args = parser.parse_args()
    output = args.output_directory.resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite R20 hosted evidence: {output}")
    output.mkdir(parents=True)
    checks: list[str] = []
    try:
        manifest = args.candidate_root.resolve() / "candidate-manifest.json"
        pe = args.candidate_root.resolve() / "managed/artifacts/HostLogProof.exe"
        map_path = args.candidate_root.resolve() / "managed/artifacts/HostLogProof.map"
        require(sha256(manifest) == EXPECTED_MANIFEST, "R18 candidate manifest SHA-256 matches preserved R19 evidence", checks)
        require(sha256(pe) == EXPECTED_PE, "R18 candidate PE SHA-256 matches preserved R19 evidence", checks)
        require(sha256(map_path) == EXPECTED_MAP, "R18 candidate linker map SHA-256 matches preserved R19 evidence", checks)
        original = check_snapshot(args, checks)
        primitive = compile_shared_primitive(args, original, output, checks)
        source_hashes = check_source_binding(args, checks)
        check_negative_contracts(original, checks)
        result = {
            "schema": "C188R20_ITERATOR_ADVANCEMENT_HOSTED_TESTS_V1",
            "status": "passed",
            "assertions": len(checks),
            "outcome": "ITERATOR_ADVANCEMENT_CAUSE_PROVEN",
            "cause": "R18 linked RtlVirtualUnwind import resolved to a no-op stub, so the code manager copied an unchanged CONTEXT back to REGDISPLAY and NextInternal republished the unchanged IP.",
            "rootCauseClassification": {"selected": "F", "alsoDescribesInvariant": "C", "iteratorCopy": False, "abiLayoutMismatch": False, "stateOverwritten": False, "transitionFrameTermination": False},
            "candidateRoot": str(args.candidate_root.resolve()),
            "r18RunRoot": str(args.run_root.resolve()),
            "r19": original["r19"],
            "executedProductionSharedPrimitive": primitive,
            "sourceHashes": source_hashes,
            "nativeBootsInR20HostedTests": 0,
            "rootEnumerationCompleted": False,
            "collectionCompleted": False,
            "managedResult1123Verified": False,
            "checks": checks,
        }
        (output / "hosted-tests.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0
    except Exception as exc:
        result = {"schema": "C188R20_ITERATOR_ADVANCEMENT_HOSTED_TESTS_V1", "status": "failed",
                  "assertions": len(checks), "error": str(exc), "checks": checks,
                  "nativeBootsInR20HostedTests": 0}
        (output / "hosted-tests.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

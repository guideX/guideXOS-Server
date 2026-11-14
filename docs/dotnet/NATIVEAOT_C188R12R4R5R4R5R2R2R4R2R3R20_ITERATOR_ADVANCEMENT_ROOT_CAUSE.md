# C188R20: NativeAOT iterator advancement root cause

## Result

`StackFrameIterator::Next()` returned without advancing because the candidate's `RtlVirtualUnwind` import resolved to a stub that returned null without changing the Windows `CONTEXT`. `CoffNativeCodeManager::UnwindStackFrame` therefore copied the same `RSP` and `RIP` back into the `REGDISPLAY`, and `NextInternal()` published the unchanged IP as the current control PC. This is the F implementation defect; C describes the observed unchanged-iterator invariant. The evidence rules out an iterator copy, an ABI layout mismatch in the iterator itself, a later state overwrite, and normal transition-frame termination.

The pinned NativeAOT sources place the advancement boundary in the same path: `StackFrameIterator` takes the next control PC from the `REGDISPLAY`, and `CoffNativeCodeManager` delegates normal frame unwinding to `RtlVirtualUnwind` ([StackFrameIterator.cpp](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp), [CoffNativeCodeManager.cpp](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp)).

## Evidence and reproduction

The preserved R18 candidate has fixed image base `0x100000000`. Its linked IAT cell at `0x1000F8330` points to `0x10004EFB0`, whose bytes are `33 C0 C3` (`xor eax,eax; ret`). The linked code manager calls that cell. The R18 runtime-pack manifest binds the old startup-import source hash `7EE4F99ACC296A85BE8E04EA07799A25F16E6E3207185A07BC2EA763BE8BC0F1`.

R18's two preserved samples identify the same live, `0x230`-byte iterator at `0x52B5340`; the first sample is in `LookupUnwindInfoForMethod`, and the second is at the return point immediately after `Next()` and before `IsValid()`. The captured iterator bytes match. In the post-`Next()` sample, `ControlPC` and `REGDISPLAY.IP` are `0x10001E956`, `REGDISPLAY.SP` is `0x52B5980`, flags are `0x8A`, and the previous transition-frame pointer is null. `Next()` returned normally, but the valid iterator still describes the same frame.

The captured `.pdata` entry for that control PC is `[0x1E920, 0x1E980)` with unwind metadata at RVA `0xEB980`. Replaying that entry against the captured stack yields return address `0x100001A8E` from slot `0x52B5A18` and caller SP `0x52B5A20`. This proves the frame had ordinary caller state available for unwinding; it was not a terminal or transition-frame case.

The hosted R20 regression re-analyzes the preserved R18 evidence and executes the production-shared unwind source through the Windows ABI adapter against the captured frame. It passes 31 assertions and produces caller RIP `0x100001A8E`, SP advance `+0xA0`, and the restored saved-RBP pointer at `SP+0x90`. The raw evidence and report are under:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r20-hosted-final2
```

## Repair

`guidexos_nativeaot_startup_RtlVirtualUnwind` now delegates to `guideXosRtlVirtualUnwindWindows`. The adapter preserves the compact pointer-list ABI used by hosted diagnostics while translating the Windows AMD64 `KNONVOLATILE_CONTEXT_POINTERS` layout: 16 floating-context pointer slots precede the integer-register pointer slots. It copies saved-register locations into the compact primitive before unwinding and propagates updated locations back afterward, including RBP.

The production runtime-pack build now compiles and hashes the shared unwind object. The managed NativeAOT build verifies that manifest binding and links that exact object into the proof PE. The candidate builder also passes the object to its native-allocation integration link, so that existing hosted link test exercises the same startup-import dependency as production.

For the fresh R20 candidate, the production-shared source hash is `9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E`, and the diagnostic runtime-pack object hash is `6D4753902EFB2D96C493CFD1FE98E7C7D2C47DBE3AF111161617F06E97EEA9B7`. The linked map places the startup shim at `0x100052210` and the Windows adapter at `0x1000529E0`; the shim jumps to that adapter. The proof-disabled production map also contains the implementation, while all diagnostic attribution profile flags remain false.

## Hosted regressions

All checks below ran without QEMU or QAC unless marked as the final guest qualification.

| Regression | Result |
| --- | --- |
| R20 iterator replay and ABI adapter | Pass, 31 assertions |
| R19 GC-root completion policy | Pass, 12 assertions |
| R17 stack-walk progress | Pass, 10 assertions, using the matching R16 candidate and capture |
| R17 runtime-function lookup | Pass, 625 assertions, 3,063 table entries |
| R17 runner packaging | Pass, 34 assertions across 5 scenarios |
| R13 managed-GC contract | Pass, 75 assertions |
| R12 managed settings object | Pass, 23 assertions, using the R12 candidate profile |
| R4 ThreadStore readiness | Pass, 7 model assertions and 6 telemetry assertions |
| R4 QAC admission | Pass, 35 cases |
| R2R2R4 safe handoff | Pass, 22 analyzer cases and 10 source-contract checks |
| R11 lifecycle and output bounds | Pass, 7 and 6 cases |
| Native allocation hosted integration | Pass; shared slot-table test passed 2,425 assertions and 1,024 reserve/release cycles |

R20 regression outputs and the candidate build are preserved outside the repository under:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r20-regressions
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r20-unwindfix-final-20261114T040000000Z
```

## One-boot qualification

The no-boot preflight passed candidate-integrity verification, isolated the candidate ESP, and prepared the firmware inputs. The runner then observed a clear QAC/QEMU lane and used the candidate-specific one-boot guard. Exactly one QAC guest boot ran; `automaticRetry` was false, the candidate integrity inventory remained valid afterward, and QAC release was verified.

The run completed as `NATIVEAOT_SINGLE_COLLECTION_VERIFIED` (`MANAGED_GC_ROOT_COLLECTION_VERIFIED`). The probe requested and completed one synchronous collection; the collection count moved from `0` to `1`; the managed object survived; and the post-collection result was `0x463` (1123). The managed return was observed, the raw serial was preserved, and the managed-GC analyzer schema was valid. This qualifies the full root scan and managed return on the repaired candidate.

Candidate identity:

- Candidate manifest SHA-256: `CFFD16DC8C3093184EF4A7F1DFB4AA40467EFDC73AC1540E9C6929A8CE0D39EE`
- Integrity inventory SHA-256: `7CBC9ACAAB3CB4B8DE05C0C8CA89C5AFE072F7304C87B62F4EC57F689DBF054C`
- Managed PE SHA-256: `A6D66FC534F38B035F903F4BA41038EA2F26A43B50E2E83ECE83CACDCE36CCB9`
- Managed ELF SHA-256: `261860912AEE956952FEAAECEB86BE9519C4392D5BD05660F5DE6755ED6D6CC9`
- Kernel SHA-256: `D552151AA23FE0B3FD7DCB9F15C3017CA32DA32FDF301CF689EF5D6D2D881C10`

The one-boot guest manifest and its captured serial, QMP, QAC ownership, and analyzer evidence are under:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261114T034806846Z-7856c3fb75104ce082dd6748ba7e3551
```

## Workspace preservation

The task began on branch `v1.1_DOTNET_SUPPORT` at `6644b68455df3db6ac194b3d8b63b5e585cbccbf`. The three pre-existing modified root object files were not staged, and their SHA-256 hashes still match their initial values:

| File | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

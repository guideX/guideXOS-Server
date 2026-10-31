# NativeAOT C188R12R4R5R3 — `RhInitialize` Return Contract

**Outcome C — `INIT_CONTRACT_VERIFIED_NEXT_BOUNDARY_PREPARED`**

This phase verified the native Boolean contract against the pinned upstream implementation, the exact linked R2 adapter, its x64 disassembly, and a fresh R3 candidate. The old guest's printed `1` is a guideXOS adapter failure status, not a native Boolean success. The adapter already treated native `true` as success; no return-flow inversion was found. R3 now captures the native Boolean separately from the adapter status. No guest was launched and no QAC admission was requested.

## Repository and provenance

| Item | Value |
|---|---|
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `e46e0ac708bfda5e39f7ae8d730a641682bff9c9` |
| R3 implementation commit | `33f80beb44e252b6cb4d314f6ab41dac83063dd2` |
| Return-contract runner and correction-addendum commit | `c803cc434ce9f809ae91212fb6909c473e52a38e` |
| Ending source/runner HEAD before this closeout document | `c803cc434ce9f809ae91212fb6909c473e52a38e` |
| Pinned NativeAOT runtime | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |

The starting worktree already contained a modified `ESP/ramdisk.img` and unrelated untracked build outputs. They were preserved. The source ramdisk SHA-256 was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` before and after the work; the candidate ESP copy has the same hash.

## Contract and actual linked call

At the pinned runtime revision, `RhInitialize` is declared `extern "C" bool RhInitialize(bool isDll)`. It returns `false` when `PalInit` or `InitDLL` fails and returns `true` after those steps succeed. The ordinary NativeAOT bootstrap tests `!RhInitialize(false)` as failure, then registers the OS module, calls `InitializeModules`, and only then calls the managed entry point. See the pinned [`startup.cpp`](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/startup.cpp) and [`Bootstrap/main.cpp`](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Bootstrap/main.cpp).

The guest's R2 candidate map resolves `RhInitialize` to `Runtime.WorkstationGC:guidexos_nativeaot_startup_replacement.production.obj` and `GuideXosNativeAotGcStartupMain` to `guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.obj`. That linked adapter used:

```cpp
extern "C" unsigned char RhInitialize(int isDll);
g_startupResult = RhInitialize(0) ? 0u : 1u;
```

The R2 adapter object directly relocates to `RhInitialize`; the disassembly calls it with `ECX=0`, tests `AL`, and uses `sete DL` before storing `g_startupResult`. Under the Windows AMD64 ABI, `AL` is the low byte of the `RAX` return register. Thus native `AL=1` becomes adapter status `0` (success), and native `AL=0` becomes adapter status `1` (failure). There is no indirect function pointer or trampoline between this caller and the resolved symbol. The old prototype's `unsigned char(int)` spelling has the same x64 argument/return register locations as the actual byte-wide C++ `bool(bool)` on this call; R3 replaces that spelling with the exact `bool(bool)` declaration and statically verifies the one-byte Boolean and function-pointer type.

| Value | Meaning |
|---|---|
| Native `RhInitialize` `true` / `1` | Initialization succeeded |
| Native `RhInitialize` `false` / `0` | Initialization failed |
| C188 adapter status `0` | Adapter reports success; mapped from native `true` |
| C188 adapter status `1` | Adapter reports failure; mapped from native `false` |
| Separate startup probe status `0` / `-1` | Probe wrapper's success / failure mapping |
| C102 production C bridge | Declares a byte return with an `int` argument and checks `!RhInitialize(0)`; it does not use the C188 adapter status |

The R2 `RhInitialize return=00000001`, footer `rhResult=00000001`, and `RhInitialize: FAIL` therefore mean adapter status one and native false. The raw R2 serial did not contain the raw Boolean, so the native false is reconstructed from the exact linked adapter mapping and disassembly, not read directly from that guest log. The adapter did not mistake a native success Boolean for failure.

The R2 raw guest serial is 7,008 bytes, SHA-256 `0F40D24DEB8BB9E527961BC47F8698E111DCD74902A074BE8210ECC1F4DEC53E`. The unchanged, tracked fixture is `tests/dotnet/fixtures/nativeaot/C188R12R4R5R2-guest.serial.log`; its worktree and committed blob retain that same SHA-256. The fixture is marked binary in `.gitattributes` to prevent newline conversion.

The previous R2 guest printed `count=FFFFFFFF` and `overflow=FFFFFFFF`, with no stage records. Those sentinels mean unavailable getters; they are not evidence of zero trace events or an explicit internal failure stage. Commit `6ea714b7` corrected the optional trace argument slots, and R3 preserves that layout. The R2 singleton snapshot `preGcState=0000001F` shows bits 0–4 published, but the R2 log cannot identify the first internal failing stage. The addenda in the [R1 attribution report](NATIVEAOT_C188R12R4R5R1_RUNTIME_INIT_ATTRIBUTION.md) and [R2 guest report](NATIVEAOT_C188R12R4R5R2_RUNTIME_INIT_GUEST.md) clarify the old footer terminology without changing raw evidence.

## R3 instrumentation and the handoff boundary

R3 records the native Boolean in `g_rhInitializeRawResult` and the adapter status in `g_startupResult`. The kernel serializes both as `nativeBool` and `adapterStatus`; the analyzer checks the Boolean/status mapping, serialized labels, and footer consistency. The R1 trace event remains based on native success/failure, while its numeric result and footer status are explicitly adapter status. Missing stage markers still remain unknown.

The guideXOS route bypasses the ordinary bootstrap entry point and calls `RhInitialize(false)` from its freestanding adapter. The later module path is in `initializeNativeAotModules`: after the reverse-P/Invoke managed-entry transition, it registers the OS module and code manager with `RhRegisterOSModule`, then calls `InitializeModules`. `ManagedMain` is resolved from the linked image and passed to the kernel harness as an AMD64 Microsoft-ABI function pointer. However, this R3 diagnostic candidate returns at the existing R1 diagnostics gate immediately after recording a successful initialization. It does not install the NativeAOT current-thread TLS, enter reverse P/Invoke, attempt module/code-manager registration, call `InitializeModules`, or call `ManagedMain`.

That gate remains because the freestanding path has not qualified safe ThreadStore attachment. A successful `RhInitialize` is not enough to enter managed code or to claim module registration. The next guest using this candidate can establish the raw return and adapter status, then report that the later boundary remains gated. A separate ThreadStore-attachment qualification and a candidate update are needed before a guest can safely test module registration and managed entry. This closeout does not force that transition or claim GC ownership.

## Hosted tests and candidate build

The fresh hosted candidate is:

`out/dotnet/c188r12r4r5r3-init-return-contract/candidate-20261031T155122845Z`

Its manifest result is `PASS_BUILD_READY_FOR_INIT_RETURN_CONTRACT_GUEST`, built from source HEAD `33f80beb44e252b6cb4d314f6ab41dac83063dd2`. The pinned runtime identity is `guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r3-init-return-contract-v1`. Both the isolated diagnostic NativeAOT/kernel and the separate proof-disabled production NativeAOT/kernel passed. Production persistent composite lifecycle remains enabled; its runtime pack, NativeAOT map, and kernel exclude the C188 proof, R1/R3 diagnostics, and raw-result accessor.

The linked-candidate regression disassembled the actual adapter object and confirmed the `RhInitialize` relocation, byte-wide `AL` capture, true-to-zero/false-to-one status mapping, and separate raw-result storage. Hosted tests also cover both Boolean values, impossible Boolean/status pairs, conflicting footer status, the preserved R2 raw serial, missing-stage behavior, an independent module-registration failure after successful initialization, the managed-entry gate, ABI declarations, QMP framing, and serial preservation.

| Test/build | Result |
|---|---|
| R3 return-contract hosted regression with linked candidate | PASS; `out/dotnet/c188r12r4r5r3-init-return-contract/hosted-20261031T161458910Z/` |
| R1 trace/parser and QMP regression | PASS; `out/dotnet/c188r12r4r5r1-runtime-init-attribution/hosted-20261031T161504109Z/` |
| Existing R4R5 hosted GC ownership graph | PASS; `out/dotnet/c188r12r4r5-gc-ownership-proof/hosted-20261031T161354259Z/` |
| C188R12R4 factory predicates | PASS, 9 cases |
| C188R12R4R1 link-boundary predicates | PASS, 9 cases |
| C188R11 runtime lifecycle | PASS, 7 cases |
| C188R11R4 QAC ownership predicates | PASS, 15 cases |
| C188R11R4 QAC stream capture | PASS, 2 cases |
| C188R11R2 runner predicates | PASS, 24 cases |
| Changed PowerShell parser checks and `git diff --check` | PASS |

The existing 40-suite / 969-case managed UI baseline was reused because no UI or application source changed. No test launched QEMU or reserved QAC.

## Candidate artifact hashes

All candidate artifacts, build logs, production qualifications, runtime-pack outputs, and their SHA-256 values are recorded in `candidate-manifest.json`. Principal artifact hashes are:

| Artifact | SHA-256 |
|---|---|
| Candidate manifest | `DD54EE4AFA698FAF0EB951E7DEA7833C85FEFEB6A2F7C882F73876A0E1577456` |
| R3 NativeAOT PE | `298F94998392AA3E245511F9BE228651667C8F6D8FC7A32FB7B7218D5D8651EB` |
| R3 NativeAOT map | `4761F3AE5098B299ECBB3BD955EFB63999EF5FB835EAD874F1ED56EBFC474260` |
| R3 NativeAOT ELF | `6D89CE890C64BC639D997AD16F8EE57C45E85A6F2CA986CEC45D24E4E757BD21` |
| C188 startup adapter object | `5F6BC0D967F475F22038BDF457E1D241BB82A24A6C216746762446C38A96F204` |
| Embedded NativeAOT object | `8FE4F26A412BA0DF5D2F7F5655FD92C3557A7717AD9C2360E3AA805A0B4F63DF` |
| Diagnostic kernel ELF and staged ESP kernel | `9C4F4818DE58FB93C6ED394FD485A64051AB59CCD139A5CF8783B60EE9CE916C` |
| Staged ESP bootloader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Staged ESP ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| R3 runtime-pack platform object | `0686C3F9B9515904AFDD60A7B9B761DC01B255CC168BF181BFC452CA7AFA862F` |
| Adapted `Runtime.WorkstationGC.lib` | `3CB3E9B4663836E3BE12E1587ED379D0BDBE6A578B3CE3A2262C8F7BD91EFF46` |
| R3 runtime-pack manifest | `831F03D576389C2D272E15A59F09193914191DBDF34E5F65F4824A4A2072EFDC` |
| Proof-disabled production NativeAOT ELF | `D655A5A516F639EAE3378C5A758CBD27922DFF87D238B2764F17CCCE651BA255` |
| Proof-disabled production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| R3 hosted-test manifest | `C62719B10AA0CDD0346344E6D6D57E7662DB81A90A0342C0CA48BEAF5A431931` |
| Preserved R2 guest raw serial fixture | `0F40D24DEB8BB9E527961BC47F8698E111DCD74902A074BE8210ECC1F4DEC53E` |
| Source ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

## Prepared one-guest run

The QAC guest runner now recognizes the R3 manifest and return markers, verifies the candidate's kernel and ramdisk hashes, requires a clear exclusive QAC census, preserves raw serial, and records native Boolean and adapter status separately. It will create `out/dotnet/c188r12r4r5r3-init-return-contract/guest-launch-attempt.json` with `CreateNew` immediately before the only QAC invocation. That fresh phase-wide guard does not exist yet; this phase made no guest attempt.

The future one-boot command is:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/run-c188r12r4r5-gc-ownership-qac-guest.ps1 `
  -CandidateRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c188r12r4r5r3-init-return-contract\candidate-20261031T155122845Z
```

On that later run, first obtain exclusive QAC admission, boot this candidate once, retain the unmodified raw serial, and classify native return versus adapter status. If initialization fails, stop at the first captured failure. If it succeeds, this candidate still stops before module registration and `ManagedMain`; do not claim those stages or force the GC graph. Qualify ThreadStore attachment and update the candidate before attempting that later handoff. Always shut down the owned guest and verify QAC release. No retry is allowed under the phase guard.

## Closeout

No adapter success/failure inversion or native ABI defect was proven. The concrete issue was diagnostic naming: the R2 field named `RhInitialize return` held the wrapper status and did not include the raw Boolean. The R3 change fixes that ambiguity and adds mapping/parser coverage without changing the success gate. The historical R2 outcome remains an initialization failure because adapter status one maps to native false, while its earliest internal failing stage remains unknown.

**Smallest next step:** after a separate ThreadStore-attachment safety qualification, prepare a candidate that can reach reverse P/Invoke without weakening the failure gate. Until then, the R3 candidate qualifies only the initialization return contract; it does not qualify module registration, managed entry, or guest GC ownership.

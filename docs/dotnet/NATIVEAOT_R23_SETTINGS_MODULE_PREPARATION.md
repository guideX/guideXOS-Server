# NativeAOT R23 — Settings Module Preparation

**Outcome D — `SETTINGS_PREPARATION_BOUNDARY_ADVANCED`.** R23 advanced the native trace past the code-manager registration call and reached the generated `InitializeModules` call-site. The call did not produce a callee-entry or return marker before the genuine guest readiness timeout. Shared preparation therefore remains incomplete, the cause is not proven, and the real Settings factory was not reached.

## Repository and candidate provenance

- Expected starting HEAD: `caae32b3d8dfaddd6561c0a79db42072ee7bc04a`.
- Candidate source HEAD: `e6c8d6327a1a10a8cd86acbc64d4c445d6413001`.
- Final source/test HEAD before this report commit: `a3b875c36fe8d7348d71022bfc3ee4782d96d039`.
- Branch: `v1.1_DOTNET_SUPPORT`.
- The candidate predates two later test-only commits (`82593cbe`, `a3b875c3`). The R23 predicate suite was rerun after its predicate precision change, and the GC-init hosted regression was rerun after its assertion update. Neither later commit changed the candidate's runtime or kernel source. The report is committed separately; its commit is the final repository HEAD reported with this closeout.

Candidate root:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r23-settings-module-preparation\candidate-20261114T174459288Z-fd3f057c4f7f4d9280d0a6d05e57831f
```

The builder reported `PASS_BUILD_READY_FOR_QAC_PREFLIGHT`, with a 338-file integrity inventory and no failed hashes. The inventory SHA-256 is `51594B7B5B874CF3BCFBDF1EFA35601E6C561F9343F643FF667E116BCE35A752`. Candidate inputs record the three pre-existing modified root object files and their hashes; `unverifiedRootObjectInputsConsumed` is false. Runner packaging rehearsal independently verified all 338 candidate files and matching hashes for the three staged boot files.

| Candidate item | SHA-256 |
| --- | --- |
| Candidate manifest | `E493230EA4F6BBD40BF6D348AEECF1208DE73F3066445A4286785F09D99870CE` |
| Kernel ELF | `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B` |
| Managed ELF | `75680E7CFCC1427E90AF83ED1051BFC09F53F942A4C4665038FE3222B81E948D` |
| Runtime-pack manifest | `E4D343619CA72E6FFCADE775CFE2239900E9C37BD60BA906B8185695FA0F16E0` |

The image uses the locked NativeAOT 9.0.0 Windows x64 runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, Workstation GC, persistent composite lifecycle, no C188 image-backed arena, and the accepted R20 unwind adapter. The candidate map contains `guidexos_nativeaot_prepare_managed_entry` at `0x1000f3fe0`, generated `InitializeModules` at `0x1000ad660`, and the actual `ManagedSettingsCenter___ctor_0` constructor at `0x100073ed0`. Selector 5 still resolves to `GuideXosApplicationRegistry -> ManagedSettingsCenter`; its factory and constructor are managed-to-managed and expose no separate native factory ABI.

## R22 timeout reconstruction

R22 candidate:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r22-settings-bootstrap-handoff\candidate-20261114T161901255Z-4424f0e675da434cb80db343364ddd41
```

Its manifest SHA-256 was `8D767CA63C4CAD696F1CACF392120DB79F23049D55C74E28DF2706B44B2ED2F7`, kernel SHA-256 `43D6FB68D3EA1311F9CDC178CC0090FA899AEAE830A0C65784D112EEA7044E47`, and managed ELF SHA-256 `B49B5D36761B617EC848B5EDDB312E7CA21892D7386C7981FD41D9F6E5589981`.

The R22 `C188B004` marker is emitted by `samples/managed/HostLogProof/c102_startup.c` after NativeAOT runtime initialization and TLS setup, immediately before the C call to `guidexos_nativeaot_prepare_managed_entry()`. It proves that startup reached this call-site. It does **not** prove that the CPU entered the helper. R22's map places the helper at `0x1000f3f90`. Its ABI is exported C linkage, `int __cdecl(void)`: zero means shared module and code-manager state passed the helper's completion checks; nonzero values distinguish invalid TLS/index and incomplete preparation. The helper requires the current thread's installed TLS block and the real generated module/class-library tables.

The next source operation after `C188B004` is that helper call. In its shared path the helper validates TLS and module state, runs `initializeRuntimeState(block)`, registers the real NativeAOT OS module and code manager, calls the generated five-argument `InitializeModules` entry, and returns only when the shared state checks succeed. The generated ABI takes the OS module handle, module-header array and count, and class-library function table and count (16 entries for this image). The later `C188B005` marker is after the helper returns successfully. Since R22 recorded no entry marker inside the helper, its missing `C188B005` alone cannot locate execution inside that call.

R22's preserved serial SHA-256 is `739B4D7A7DC81FA56341D313508851F526BE2CD3681D530B93DEF582332FCFFE`. It ends at `C102-STARTUP stage=C188B004`, with no `C188B005`, `C188B002`, ManagedMain dispatch, or Settings factory marker. The runner terminal was `guest-input-loop-readiness-timeout`; readiness timed out, the runner requested QMP quit, QEMU exited with code 0, and QAC released the reservation. This was a genuine readiness timeout, not acceptance of an intermediate marker. The captured QMP log contains capabilities and quit only. No CPU registers, vCPU state, or stack snapshot were captured, so whether the guest was executing, waiting, halted, or failing is unknown. No stack consumption measurement exists.

## R20 comparison and bounded preparation path

R20 and R22/R23 use the locked NativeAOT Workstation GC without the C188 arena and preserve the R20 unwind correction. R20's candidate is a GC/managed-execution probe, not a Settings workload. Its accepted trace observed the stock reverse-P/Invoke attach path, `InitializeModules` return, module and code-manager readiness before `ManagedMain`, one real Workstation collection (count 0 to 1), and the expected result `1123`.

| Area | Accepted R20 | R22/R23 Settings startup |
| --- | --- | --- |
| Initial order | Registration-only `initializeRuntimeState(block, true)`, read-only readiness snapshot, then full `initializeRuntimeState(block, false)` through the stock reverse-P/Invoke path | Direct call to the shared preparation helper after TLS and before `C188B002` or ManagedMain |
| NativeAOT module/code manager | `InitializeModules` return and both readiness flags observed before managed entry | Same shared service and generated five-argument ABI; R23 observed code-manager registration return, but not completion of `InitializeModules` or the shared helper |
| PAL, TLS, ThreadStore, reverse-P/Invoke | R20 trace crossed the startup/attach path and published thread readiness | R22/R23 stop before managed dispatch; no ThreadStore publication or managed transition evidence |
| GC and unwind | Workstation GC; no C188 arena; accepted shared R20 unwind primitive | Same profile and unwind implementation; no GC collection is evidenced in the R23 preparation trace |
| Settings dispatch | Not part of R20 | Selector 5 and actual factory are linked, but native execution has not reached them |

The earliest meaningful order difference is how module preparation is entered: R20 reaches it via the stock reverse-P/Invoke readiness sequence, while R22/R23 invoke the shared preparation helper directly after TLS. R23 did not establish that this order difference causes the timeout; once full initialization runs, the same shared preparation routine and generated initializer are intended to execute.

The inspected preparation sequence is:

1. Startup has installed PAL and GC hooks, `RhInitialize` has returned, and the current thread's TLS block is available.
2. The helper validates the TLS block and module index, then enters shared runtime-state initialization.
3. `RhRegisterOSModule` reads the image exception directory, allocates a real `CoffNativeCodeManager`, registers it, and registers unboxing stubs. The reviewed implementation uses allocations and atomic publication; no blocking event or wait was found in this path.
4. The generated `InitializeModules` function is called with the real module and class-library tables. Its first body operation is `RuntimeImports.RhpRegisterOsModule(osModule)`, followed by type-manager creation, section and dehydrated-data setup, managed type-manager and GC-static table allocation/publication, frozen-object setup, handle allocation, and eager class constructors.
5. The generated startup method begins with reverse-P/Invoke attachment. The shared helper sets its module-initialized reentry guard before the generated call so a nested runtime initialization does not recurse; that early guard assignment alone is not proof that the generated call returned.
6. The helper validates completed module and code-manager state, then returns. Only its caller can emit `C188B005` and continue to `C188B002` and ManagedMain.

Source inspection found no demonstrated recursive defect, malformed ABI, missing import stub, explicit wait, or invalid module metadata in this path. `InitializeModules` performs allocations and depends on valid GC/PAL services, but R23 has no evidence of a particular allocation failure or GC entry. The 16 KiB bootstrap stack is configured; remaining stack was not measured. The old 5,624-byte value was heap capacity, not stack. Those are unverified possibilities, not findings.

## R23 checkpoints and native result

R23 adds two bounded PAL startup checkpoints without changing runtime state:

- `C188B006` immediately before the actual `RhRegisterOSModule` code-manager registration call.
- `C188B007` immediately after that registration succeeds and immediately before the generated `InitializeModules` call.
- `C188B005` remains the caller-side evidence that shared preparation returned successfully.

The sole R23 QAC boot used the fresh candidate, a fresh writable guest state, an exclusive QAC reservation, and one consumed guard. The read-only preflight found the coordinator online with zero QEMU processes, reservations, sessions, and queued requests. The serial SHA-256 is `EEDCC841641B796E79A4BDABB6875F8237CF6B10FF9700048938DAF40A2627BE`.

| Native milestone | R23 evidence |
| --- | --- |
| Native initialization and `C188B004` | Reached |
| Code-manager registration call | `C188B006` reached; following `C188B007` proves `RhRegisterOSModule` returned successfully |
| Generated `InitializeModules` | `C188B007` and `initialize-modules-call` event prove the call-site was reached; no callee-entry checkpoint exists |
| Shared preparation return | Not observed; no `C188B005` |
| Managed startup and selector-5 dispatch | Not observed; no `C188B002`, ManagedMain, or managed Settings launch marker |
| Real Settings factory entry, constructor progress, or return | Not observed |
| QEMU shutdown and QAC release | Independently verified; QEMU process count is zero, exit code 0 and QMP quit are recorded, QAC status/list are clear |

The terminal cause was again the genuine `guest-input-loop-readiness-timeout`. The runner requested quit after timeout and did not claim readiness. The R23 boundary predicate reports `firstMissingMilestone=InitializeModules-call-site-to-shared-preparation-return`, `initializeModulesCalleeEntryObserved=false`, `prepareReturned=false`, and `factoryExecutionVerified=false`. The QMP log SHA-256 is `53C53FAC0C21C25D3CEB83E2955A5FE8657502E8BFDC172D9DA7B9558CA81CA0`; it contains no register or stack snapshot. Therefore R23 shows successful code-manager registration and arrival at the next call-site, but does not establish that execution entered the generated callee or identify the first incomplete instruction/operation inside it.

The QAC reservation was `bc010a11cd45475a91bc36cb93776044`. The one-boot guard is at:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r23-settings-module-preparation\one-boot-guards\attempt-r23-candidate-20261114T174459288Z-fd3f057c4f7f4d9280d0a6d05e57831f.json
```

It is consumed, has `attemptNumber=1`, `automaticRetry=false`, and `retryAllowed=false`. No retry was made. The runner itself did not write its normal final acceptance manifest because it exited through the timeout exception; independent post-run QAC and process census verification passed. Its record notes the runner release claim as false while confirming QAC release, QEMU exit code 0, QMP quit, and zero QEMU processes.

## Hosted regression evidence

The candidate builder passed its R22 bootstrap suite (29 cases), R23 preparation-boundary suite (12 cases), and preserved R20 unwind/iterator regression (31 assertions). R23's 12-case suite was rerun after the predicate correction and passed; R22's 29-case suite was rerun at final source state and passed.

Other focused hosted checks passed with the following script-reported counts where available:

| Area | Reported result |
| --- | --- |
| R21 Settings factory | 9 checks passed |
| R11 runtime lifecycle, runner predicates, output bounds | 7, 30, and 6 checks passed |
| R11R2 runner predicates; R11R4 QAC ownership | 24 and 15 checks passed |
| R12R1 handoff; R12R2R1 dispatch; R12R3 lifetime | 37, 12, and 13 checks passed |
| R12R4 factory; R12R4R1 link boundary | 9 and 9 checks passed |
| R4 QAC admission; ThreadStore readiness | 35 checks; 7 mode assertions and 6 telemetry assertions passed |
| R12R4R5 GC ownership; R12R4R5R1 init attribution; R2R2R4 safe handoff and runner contract; R3 init-return contract | Passed; these scripts did not print a case total |
| R2 semishared; QAC stream capture; R7 managed entry | 30 assertions; 2 cases; 23 assertions passed |
| Native allocation hosted suite | Passed: 8 backend cases, 2,425 VM slot-table assertions, and 1,024 reserve/release cycles; no QEMU or QAC used |
| R12R4R5R4R5 GC-init probes | Passed after correcting a stale exact runner-profile assertion |
| Candidate packaging rehearsal | Passed; 338 candidate files and three staged boot-file hashes verified |

An archived R12R3 transition runner fixture was also checked and rejected because its recorded analyzer hash is stale (`Transition analyzer hash mismatch`). This is a fixture-provenance rejection, not a failure in the fresh R23 package. The transition aggregate was not run because its required historical R4 safe-handoff serial fixture is absent. All candidate and native qualification evidence remains outside the source tree.

`git diff --check` passed. The three pre-existing root `.obj` files remain the only modified working-tree paths, with their recorded SHA-256 values unchanged:

| File | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Finding and next step

R23 proves that real NativeAOT code-manager registration returns and narrows the remaining gap to the generated `InitializeModules` call-site through the shared preparation return. It does not prove entry into that callee, a hang, a deadlock, stack exhaustion, GC failure, or any Settings-specific defect. The Settings factory remains linked and intact but unexecuted.

The next useful observation is a small entry/return pair at the first operation inside generated `InitializeModules`: the native runtime import `RhpRegisterOsModule(osModule)`. This will tell whether the generated call reaches its first native dependency and whether that operation returns. Preserve the real five-argument ABI and initialization state. If that pair shows no entry, inspect the exact indirect call target and ABI at the call boundary. Any later guest run requires a new candidate, clear QAC preflight, fresh writable state, and a new one-boot guard; this consumed R23 guard must remain closed.

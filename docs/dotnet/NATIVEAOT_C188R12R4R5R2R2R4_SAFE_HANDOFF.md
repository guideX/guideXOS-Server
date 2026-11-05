# NativeAOT C188R12R4R5R2R2R4 Safe Handoff

## Result

**Outcome: `SAFE_HANDOFF_HOSTED_READY`.** The diagnostic profile has an explicit, opt-in stopping boundary after real `RhInitialize`, TLS/FLS setup, and actual module/code-manager registration. It returns before `InitializeModules`, `RhpReversePInvoke`, managed entry, allocation, or collection. The transition semantics are unchanged; normal builds retain their original module initialization and startup path.

This was a hosted-only phase. No QEMU boot was launched and no QAC reservation was acquired. The staged candidate is ready for the separate one-boot safe-stop qualification described below. It is not evidence of guest attachment, managed execution, or GC acceptance.

## Baseline and preservation

| Item | Value |
| --- | --- |
| Repository / branch | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` / `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `5d6d354272effb56490e9e1823b5e2ec229d97c2` |
| Ending implementation HEAD | `7f5ebb39640cc127aa0d30fa6f6dbc6502f6201c` |
| Runtime source pin | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Local implementation commit | `7f5ebb39640cc127aa0d30fa6f6dbc6502f6201c` (`Add NativeAOT safe handoff diagnostic gate`) |
| Guest/QAC activity in this phase | None |

The closeout document is committed as a documentation-only follow-up to the implementation commit above. The final repository HEAD after that follow-up is reported in the task completion record.

`ESP/ramdisk.img` remained unchanged at SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The staged diagnostic ramdisk has the same hash. The three tracked `.obj` files that were already modified at the start remain unstaged and unchanged:

| File | Preserved SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

The previous guest report is [NATIVEAOT_C188R12R4R5R4R5R2R2R4R1_PAL_CAPACITY_CORRECTION_GUEST_ACCEPTANCE_RESULTS.md](NATIVEAOT_C188R12R4R5R4R5R2R2R4R1_PAL_CAPACITY_CORRECTION_GUEST_ACCEPTANCE_RESULTS.md). Its raw serial, analyses, QAC census/ownership records, QMP log, and one-boot guard remain in `out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/qac-guest-r2r2r4-001/` and the parent phase directory. A recovery manifest was not present in the retained guest folder or phase root.

| Retained evidence | SHA-256 |
| --- | --- |
| `serial.log` | `D9F14AC9DB9E39868D26F2CDBA798815D6A33C89A475DC9BEAFE998FFAD28FBF` |
| `serial-analysis.json` (runner analysis) | `F12A36E27F6D284A78453F0BCB37476E8A6E30B251B12AE4165736D8794EFD06` |
| `serial-semishared-analysis.json` | `2BB12231A1377B19246F3638113E7AC48AE7D301078135D35C0C7845A11583C3` |
| `serial-threadstore-analysis.json` | `1EB029BC353F6449D61706450DBAB14890809BBE2D3D32284E189C742E1BAB0D` |
| `native-allocation-analysis.json` | `482AD600128C58AF30D50A8BFFC9810B0CFA6D6084BEBEE35147B97B52DE686E` |
| `guest-manifest.json` | `2B0CE3156CB94707A8BC0E2C2D16E173ED988AD39F182F2CF8916BCF57E494E6` |
| `acceptance-closeout.json` | `1A81272ECC6B2656B3DD4997709F6A88CAD3344F4621EF0C03F46025B8EAFAEC` |
| `qac-guest-ownership-identity.json` | `F24984DEC88CECD469D9C046D475C7CF3C9FDF77F3B85DC0EFBC2115C097ADAF` |
| `qmp.log` | `9261D1BF5211A44D7C81D480F8C3D47C039A1EDFA4BEC5B5A3CD25166475F87A` |
| one-boot guard | `DB9611F6474CAE04C1618A82A34E355E22BD7F8964D00562ED4AF57BA8B60C13` |

The original R4R1 serial and runner analysis were hashed before and after the hosted analyzer fixtures; both remained identical. QAC before/during/after census records are retained. Before and after the previous guest run, QAC reported zero QEMU processes and zero reservations. The safe-handoff phase did not reserve QAC.

The previous guest acceptance remains limited to its recorded milestones: 256-slot PAL occupancy passed the old 16-slot boundary; the 24-byte native backing request succeeded; `GCEvent::Impl` allocation/construction was reached; `init_semi_shared()`, `GCHeap::Initialize()`, `RhInitialize`, and module registration passed. That guest did not establish ThreadStore attachment, `ManagedMain` body entry, an object graph, or collection.

## Linked dispatch path and fail-fast

The diagnostic NativeAOT PE map and bounded disassembly slices are retained beside the candidate manifest. The linked boundaries are:

| Symbol | Linked PE address | Role |
| --- | --- | --- |
| `GuideXosNativeAotGcStartupMain` | `0x10004EC10` | Startup adapter calls the real `RhInitialize(false)`. |
| `RhInitialize` | `0x100058250` | Pinned Workstation GC initialization implementation. |
| `initializeRuntimeState` | `0x10004C250` | TLS/FLS and runtime-state preamble. |
| `initializeNativeAotModules` | `0x10004BF20` | Builds the classlib table, validates module/code ranges, and performs registration. |
| `RhRegisterOSModule` | `0x10005CB90` | Actual NativeAOT module registration. |
| `InitializeModules` | `0x100029020` | Generated module-initialization entry. |
| `RhpReversePInvoke` | `0x10004D320` | GuideXOS transition wrapper taking the NativeAOT frame pointer. |
| `guideXosStockRhpReversePInvoke` | `0x100053300` | Pinned stock ThreadStore attachment/transition helper. |
| `GuideXosNativeAotC188R12R4R5R2R2R4SafeHandoffPrepare` | `0x10004C860` | Diagnostic-only registration-and-stop helper. |

The normal direct-ELF path calls `GuideXosNativeAotGcStartupMain` through a kernel function pointer using `GUIDEXOS_NATIVEAOT_PAL_CALL` (Microsoft x64 ABI). The startup adapter calls the real `RhInitialize(false)` and returns its native Boolean and adapter status. After that return, the kernel installs the current thread's NativeAOT TLS vector/GS base. In the old R4R1 profile it opened the managed-entry gate and attempted the `ManagedMain` function pointer using the same Microsoft x64 ABI.

The source path is direct: `initializeRuntimeState` calls `initializeNativeAotModules`; after constructing the classlib function table and validating `osModule`, `__modules_a`, `__modules_z`, code bounds, and unboxing bounds, `initializeNativeAotModules` calls `RhRegisterOSModule` with the actual module ranges and the 16-entry classlib function table. On success it marks code-manager registration complete. The ordinary path then marks modules initialized and directly calls `InitializeModules(osModule, __modules_a, count, classlibFunctions, 16)`. There is no callback or adapter trampoline for that call.

The linked `InitializeModules` body begins with a call to `RhpReversePInvoke`; the wrapper validates the frame/TLS, calls `initializeRuntimeState`, records the transition boundary, and forwards the same frame to `guideXosStockRhpReversePInvoke`. The stock helper is the attachment/transition implementation. The guest trace's `module_initialize_enter` followed by `transition_enter` is consistent with entering this module-initialization/transition boundary. It does not prove the stock helper attached the thread or that the managed method body began. The recorded `managed_main_call` is dispatch attempted; `ManagedMain` was not reached.

The fail-fast code `0x50414C06` is emitted by `PalPrintFatalError(const char*)`, whose source discards the message and calls `guidexos_nativeaot_pal_fail_fast(0x50414C06)`. This identifies the generic PAL print-fatal path, but not the underlying error that caused it. The code remains an uninterpreted diagnostic code. The 25-event init-trace overflow and the runner's incomplete classification are separate diagnostic/tooling outcomes; neither explains nor causes the runtime fail-fast.

The diagnostic gate is inserted after successful code-manager registration and before the module-initialize marker and direct `InitializeModules` call. The kernel calls the helper only under the explicit `NativeAotSafeHandoff` build profile, after successful `RhInitialize` and current-thread TLS installation. The helper calls the same `initializeRuntimeState(block, true)` TLS/FLS and registration preamble, emits its summary, and returns. The runtime `InitializeModules` function and ordinary `RhpReversePInvoke` semantics are untouched. The gate returns distinct status for invalid TLS/index, already-initialized modules, and incomplete registration; only zero means diagnostic-safe-stop-ready. The kernel then reports `initialization=accepted`, `moduleRegistration=accepted`, `managedTransition=not_attempted`, `managedEntry=not_reached`, and `runtimeError=none`. A deliberate stop is not reported as managed-runtime acceptance.

The source and linked kernel disassembly put this stop at the narrowest supported boundary. No eager ThreadStore attachment was added, and no required production module operation is bypassed.

## ThreadStore and transition prerequisites

| Prerequisite | Qualification | Evidence / limit |
| --- | --- | --- |
| Current-thread TLS vector/block and GS base | Guest-proven by R4R1; hosted/source-qualified for the new path | Prior TLS observations exist. R2R1 must reconfirm the installed vector/block and GS base before the stop. |
| Runtime instance identity | Guest-proven | Prior `runtime_instance` marker identified the runtime. |
| ThreadStore identity | Guest-proven as a published pointer only | Publication does not establish that the current thread is attached. |
| Current-thread attachment / ThreadStore membership | Unqualified | Prior `current_thread_state` showed the available-thread accessor was null (`raw1=0`). No attachment-complete marker was observed. |
| PAL stack bounds | Hosted-qualified source/link contract | The replacement PAL bounds routine is selected in the linked map. The safe stop occurs before a guest transition can validate live stack bounds. |
| Reverse-P/Invoke frame layout | Source-only for the transition; ABI boundary linked | `RhpReversePInvoke(void* frame)` and the stock helper both receive the same frame pointer. The safe path does not construct or enter a transition frame. |
| Cooperative/preemptive state | Source-only and guest-unqualified | The stock transition helper has not been run on the safe path; no state transition is claimed. |
| Module registration | Guest-proven by R4R1 and source/link-qualified in this build | The previous guest recorded registration complete; this candidate retains the real `RhRegisterOSModule` call. R2R1 must reconfirm it. |
| Module initialization | Source-only, intentionally not attempted in R2R1 | The ordinary code still directly calls `InitializeModules`; the safe build returns before that call. |
| Managed-entry calling convention | Hosted-qualified ABI; guest method entry unqualified | Kernel function pointer is `GUIDEXOS_NATIVEAOT_PAL_CALL`/Microsoft x64 and is linked to `ManagedMain`; no managed method body is entered by this phase. |
| Thread suspension and GC readiness | Unqualified | No safe-stop operation requests suspension, walks roots, creates an object graph, or collects. |

ThreadStore publication is not treated as current-thread attachment. Later transition stages remain disabled until a separate phase qualifies their prerequisites.

## Record overflow and bounded correction

The old initialization recorder had 64 fixed **event** slots. The guest emitted 89 initialization events; all 64 slots were filled and 25 later events were dropped. The first dropped event is inferred from the raw ordering and source as `GCVMCommit PASS` (stage `0x00000052`), immediately after the last stored `BEGIN`. The append-only recorder did not overwrite earlier records. The raw serial retained the later fail-fast line, but the bounded init trace did not preserve enough full event detail for complete runner classification. The runner therefore classified the evidence as incomplete. Overflow changed diagnostic completeness, not runtime control flow.

The event volume came from recording each repeated `BEGIN`/`PASS`/`OBSERVE` event separately across initialization stages, alongside separate GC-internal, semi-shared, and native-allocation records. Source inspection shows a bounded append and return on capacity exhaustion; the sink writes serial text and does not call back into the event recorder. There is no recursive logging loop.

The diagnostic profile retains 64 fixed stage slots, but stores four saturating 7-bit counts per stage, the last event/result/singleton bits, a separate first-failure stage/result/singleton record, and global event/coalescing/overflow counts. Emission writes one compact summary row per stage, then the separately preserved first failure (if any) and total counts. Counter saturation or stage-capacity exhaustion increments an explicit bounded overflow count. The trace uses no dynamic allocation, lock, or early-runtime synchronization. The analyzer separately reports `diagnosticErrorStatus`, `runtimeErrorStatus`, `diagnosticEvidenceComplete`, and tooling/postprocessing status; missing terminal or overflow evidence cannot be accepted as success.

The combined fixed serial sink remains bounded at 768 lines. Prior accepted volumes were 193 semi-shared lines, 16 GC-internal lines, and 483 native-allocation lines with backend capacity 8. The observed ninth backend request adds 34 lines, so the diagnostic-only backend capacity is 9 and the projected total with 26 compact trace/failure rows is 752 lines, leaving 16 lines of margin. The native-allocation attempt capacity remains 8; only the backend summary capacity was corrected to cover the observed startup call. The 256-slot PAL design is unchanged. Hosted shared-slot tests exercise 256 slots, not guest memory.

## Hosted qualification

| Qualification | Result |
| --- | --- |
| Safe-handoff analyzer fixtures | PASS: 13 scenarios and 10 source-contract checks. Covers successful `RhInitialize`, registration, intentional stop, premature transition, `0x50414C06`, missing ThreadStore prerequisites, managed entry absent, both overflow classes, full bounded trace, contradictory ordering, tooling failure, and original R4R1 fixture. |
| Original raw fixture preservation | PASS: serial `D9F14A…28FBF` and runner analysis `F12A36…EFD06` unchanged before/after analysis. |
| Native-allocation hosted tests | PASS: 2,425 shared 256-slot-table assertions; 1,024 reserve/release cycles; linked nothrow operator-new/PAL integration test; synthetic analyzer fixtures. No QEMU or QAC. |
| Safe-mode build gate / production isolation | PASS: safe profile explicitly opt-in; proof-disabled production manifest and map exclude safe-handoff profile and symbols. |
| Runtime initialization and registration sequencing | PASS: source/ABI checks and linked map/disassembly; safe return follows real registration and precedes `InitializeModules`. |
| GC initialization probes / semi-shared attribution | PASS: GC-init probe regression and 30 semi-shared assertions. |
| R1/R3 init contracts and GC ownership | PASS: initialization attribution, return-contract, and ownership hosted regressions. |
| QAC and serial tooling contracts | PASS: 32 QAC admission predicates, 15 QAC ownership predicates, two QAC stream-capture cases, and QMP/serial-preservation checks. These are hosted tooling tests only. |
| C188 factory / lifecycle regressions | PASS: factory predicates and lifecycle checks. |
| UI baseline | PASS: 40 suites / 969 cases. |

PowerShell parser checks and `git diff --check` passed. The 256-slot tests use a synthetic reserve/commit/release backend and do not establish guest memory behavior. The safe-handoff analyzer uses synthetic serial fixtures plus the unchanged original raw guest fixture. No hosted result is claimed as guest attachment, managed entry, or collection proof.

## Builds and artifacts

Fresh builds completed successfully for the diagnostic NativeAOT PE/ELF, diagnostic kernel, proof-disabled production NativeAOT ELF, and production kernel. Required NativeAOT symbols, the PAL ABI, reverse-transition linkage, stack-bounds replacement, and the 256-slot PAL correction were present in the diagnostic link. The converted NativeAOT ELF is ELF64 x86-64 `EXEC`, has no dynamic section, and the PE import-address-table directory is empty. Production reports `persistentCompositeLifecycle=true`; its runtime pack has `safeHandoffProfile=false`, and its map excludes the safe-gate export. The production application selectors and existing Notes path remain on the existing production profile; no Notes or Settings sources were changed.

Build logs contain warnings but no material new compiler or linker errors. Recorded warning classes include the `NU1903` ILCompiler package advisory, `CS0162`, `CA2014`, `C4005` macro redefinitions, linker `LNK4099` missing PDB notices and `LNK4216` entrypoint export notice, plus existing kernel unused-code, macro-redefinition, type-limit, and `-Wstringop-overflow` warnings. The complete logs and build provenance are in the candidate directory.

Candidate root:

```text
out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/candidate-20261105T054500000Z-linkedtrace/
```

Staged diagnostic candidate:

```text
out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/candidate-20261105T054500000Z-linkedtrace/staged-resume-20261105T055402859Z/
```

The complete candidate manifest SHA-256 is `48316C803A164C9C99B278617540A75FC2F1691FC39A52BAA919799D70B5BCA9`. The staged qualification manifest SHA-256 is `70CEA263AE01D191A1EB53CF3227053AEAC73150C03956E5D7C9CF7B964AA42B`; it lists all 18 staged files and their hashes. The safe-handoff stage map hash is `4EEE4BA6FA31BF9727D21EEA4A881B144DE9861C378BDB52A2BC4066970F34C4`. Hosted results hash: `5D30D684960903FA096C2052454E3985C918BCD8290D0245FAFD3098F223CABB`.

Key built artifact identities:

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `247D1D97FD773BA6BB588C0D62EFF78C2B969CF31DB4AABF32C86EA53BB05EB8` |
| Diagnostic kernel / staged `kernel.elf` | `8D84045384518E1E2D1390AD22A86915DD1B2027D477CE2AB31B676A3FE63966` |
| Staged diagnostic ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Staged bootloader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Production NativeAOT ELF | `11F98B89F2F3AE89C5A81C2BB0DA6717FAFC06065BD1C7CFE9DC2E5D28225CEE` |
| Production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Diagnostic runtime-pack manifest | `0362608634FC6B3FAA0125727127633BB7E44A62FA95E7019DD666E27AF2209E` |
| Production NativeAOT qualification | `946635D9767A00061D593C361EE003EEE51385375E26AEABD6A4B2854DEA65F7` |
| Production kernel qualification | `6B63811D4B102A640CAD8F5481838F879D9278167B9A667CFDC6EE8F4759077E` |
| Staged qualification manifest | `70CEA263AE01D191A1EB53CF3227053AEAC73150C03956E5D7C9CF7B964AA42B` |

All staged file hashes:

| Staged file | SHA-256 |
| --- | --- |
| `diagnostic/analyze-c188r12r4r5r2r2r4-safe-handoff.ps1` | `49ED2A111698485096FF97B1D30CB70C860CA1CDF10408DC87EACFD1380288F7` |
| `diagnostic/analyze-c188r12r4r5r4r5r2r2-native-alloc.ps1` | `C9A1F4B2BC26F90C951B346B0C530052C8EAFED184E54DE989D292EEFF26516D` |
| `diagnostic/run-c188r12r4r5r2r2r4-safe-handoff-qac-guest.ps1` | `EFD296C289B8EEE68E89FCE472D644BA46BD9CA1CEE79B87BA14E8AF5FA4E8E6` |
| `diagnostic/safe-handoff-hosted-test-results.json` | `5D30D684960903FA096C2052454E3985C918BCD8290D0245FAFD3098F223CABB` |
| `diagnostic/safe-handoff-stage-map.json` | `4EEE4BA6FA31BF9727D21EEA4A881B144DE9861C378BDB52A2BC4066970F34C4` |
| `diagnostic/test-c188r12r4r5r2r2r4-safe-handoff.ps1` | `EFB0F79471AB9B084C1240862E023372346CEDD85B591D1BC9272162F1F0F5FF` |
| `diagnostic/esp/kernel.elf` | `8D84045384518E1E2D1390AD22A86915DD1B2027D477CE2AB31B676A3FE63966` |
| `diagnostic/esp/ramdisk.img` | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| `diagnostic/esp/EFI/BOOT/BOOTX64.EFI` | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| `diagnostic/runtime-pack/gc.cpp` | `12032F90901CECC4F8D1AA9923E135FAEAB7DE300AAAF407412430FDB0FAC37D` |
| `diagnostic/runtime-pack/gcwks.cpp` | `399CC34A0C1D3EE9EE544CFF63AFA9A9FD213DB410467D8B931C688C0465E446` |
| `diagnostic/runtime-pack/guidexos_gcenv.cpp` | `93F17C5E56FC83295FEEBF8AE2F5E8FC3DE568D1825F439B584A6BFB632789F5` |
| `diagnostic/runtime-pack/guidexos_nativeaot_native_alloc_record.cpp` | `042A10A3C849D259287D3155F2C1326192A413505AEB0936E616059112AB117D` |
| `diagnostic/runtime-pack/guidexos_nativeaot_native_alloc_record.h` | `2CC16FA567A64FB067DFD0539C4A7CD56EA7481EAB6CFD21C29C1052EEC90919` |
| `diagnostic/runtime-pack/guidexos_nativeaot_pal_contract.cpp` | `C5DDFAF982A9A9DA118EB47969804BC62B610B991A48904BE2C300247F78CCAA` |
| `diagnostic/runtime-pack/guidexos_nativeaot_semishared_record.cpp` | `AFDE4E8797EA9CA98B0BF67489426864AA1FCFADA09FD3F3DF771D5131FA8AD4` |
| `diagnostic/runtime-pack/guidexos_nativeaot_semishared_record.h` | `B2021B42EB8107C77298B574E58376366B120E16126FF5E5F07D4335A4721B7D` |
| `diagnostic/runtime-pack/guidexos_nativeaot_startup_imports.cpp` | `7EE4F99ACC296A85BE8E04EA07799A25F16E6E3207185A07BC2EA763BE8BC0F1` |

The candidate manifest also hashes the linked diagnostic PE/map/ELF, each disassembly slice (`safeHandoffPrepare`, `initializeRuntimeState`, `gcStartup`, `reversePInvoke`, `initializeModules`, and kernel dispatch), runtime-pack objects/sources, production maps/ELFs, and build logs.

## Remaining uncertainties and next guest phase

The staged candidate has not been booted. The next phase is **C188R12R4R5R4R5R2R2R4R2R1 — NativeAOT Safe-Handoff Guest Qualification**. Its one-boot runner is staged at `diagnostic/run-c188r12r4r5r2r2r4-safe-handoff-qac-guest.ps1`. It should take a fresh QAC census, acquire exclusive admission, create a new one-boot guard, boot this candidate once, verify the accepted PAL/GC initialization milestones and module registration, confirm the intentional stop and complete-or-explicitly-overflowed trace, retain ThreadStore prerequisite state, confirm there was no `InitializeModules` transition/fail-fast along the gated path, then shut down its owned guest and verify QAC release.

That qualification must stop there. It must not attempt managed allocation, collection, or Notes/Settings execution. A later, separately qualified phase may enable real transition stages once current-thread attachment, frame/state contracts, stack bounds, module initialization, suspension, and GC readiness have evidence. No ThreadStore or GC correction is justified by the current source or hosted evidence.

The next smallest step is to run the one-boot safe-stop qualification with a fresh QAC guard. This phase does not claim `MANAGED_TRANSITION_DEFECT_PROVEN`; `0x50414C06` remains unattributed. It does not claim managed runtime acceptance or guest collection.

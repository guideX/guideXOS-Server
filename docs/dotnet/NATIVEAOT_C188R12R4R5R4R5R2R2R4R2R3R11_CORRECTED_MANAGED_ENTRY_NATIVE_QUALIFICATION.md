# C188R12R4R5R4R5R2R2R4R2R3R11 — Corrected Managed Entry Native Qualification

**Outcome A — `NATIVEAOT_MANAGED_ENTRY_RETURN_VERIFIED`**

On the corrected R10 candidate, one QAC-owned guest boot completed NativeAOT initialization, entered the compiled C# `ManagedMain` body, computed and returned `49`, preserved both managed-side markers, restored the preemptive top-of-stack transition frame, and shut down cleanly.

Execution timestamps are recorded in UTC. The guest ran from `2026-11-09T03:12:22Z` to `2026-11-09T03:12:40Z` (November 8, 2026, 7:12 PM Pacific).

## Repository state and preservation

- Branch: `v1.1_DOTNET_SUPPORT`.
- Initial HEAD: `4f5dc1e0b3f2a17befaf8dab745d2836a1f3e6f5`.
- Final implementation HEAD after native qualification and before this report-only closeout: `4f5dc1e0b3f2a17befaf8dab745d2836a1f3e6f5`. R11 made no runtime or runner source changes.
- R10 correction commit: `ad4b720007c928898b27c8bbf9e584391619b289` (`codex: repair managed-entry thread-mode handoff`).
- No push was performed.
- The three pre-existing modified root `.obj` files were left untouched and unstaged. Their SHA256 values before and after R11 are:
  - `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj`: `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD`
  - `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj`: `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3`
  - `guidexos_nativeaot_native_alloc_record.obj`: `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183`

The only repository change for R11 is this qualification report. Candidate artifacts, R9/R10 evidence, and the consumed R11 one-boot guard remain under the existing artifact root.

## R10 cause and corrected candidate identity

R10 proved that the old diagnostic telemetry wrote through `runtimeCell(block) + 0x40`, which aliases `Thread::m_pTransitionFrame` in the pinned layout. The stock reverse-P/Invoke return restored the preemptive marker, then the telemetry pop overwrote it with the earlier null value. R10's guest ran that pre-fix candidate only; its result is not used as R11 execution evidence.

R11 used this distinct, corrected candidate:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\candidate-r10-threadstore-telemetry-fix-20261108T202921768Z`

The pre-fix guest candidate used by R10 had manifest SHA256 `A33029D69FD49933D1A80369A13BE035830687037F5E4BBC75991A4FDEB6D6A9`. R11 used only the corrected candidate with manifest SHA256 `8AEE184F12F2BF29AE4761AC7A50B4574F99863AF05D448C1B32A01DC8E2787C`.

The corrected candidate manifest records source HEAD `8b6134df4c14683c704779e0aad2e63a85ded124`, branch `v1.1_DOTNET_SUPPORT`, the NativeAOT source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, and the R10 telemetry correction. Its staged platform source has the same SHA256 as the corrected repository source. The candidate's `runtime-pack.manifest.json` binds that source to `guidexos_nativeaot_platform.obj`; its object hash matches the candidate manifest and complete inventory. The compile record enables both `GUIDEXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS` and `GUIDEXOS_NATIVEAOT_C188R12R4R5R2R3_MANAGED_ENTRY_PROBE`.

| Artifact | SHA256 |
|---|---|
| Candidate manifest | `8AEE184F12F2BF29AE4761AC7A50B4574F99863AF05D448C1B32A01DC8E2787C` |
| Complete integrity inventory (1,132 files) | `A8E4342FE8F715FC0C40E1A1787CC56A88348B52A70DC9CCC507FA8A0BC65696` |
| Runtime-pack manifest | `127CD673DD9D8FA4BCB8137D316F4ABDF8555436FCB7F444AA16988E0EB35B9D` |
| Runtime-pack platform object | `66C400AC4A46CC72877E4317696D60CDA46E66BE811DC29B158CE711B3A8582A` |
| Adapted `Runtime.WorkstationGC.lib` | `95E2B77685987E6AAFFE1BFE0C5D1A1DB1EC790E6F03CADFD40CEC8019D76516` |
| Managed PE | `00C26A131C779AC320EAE05EE39BC6DCF6B1C777F0C21007BB3B80F70AF25417` |
| Diagnostic managed ELF | `BC0CB5B085A76143B02106754EEBEFD33AA9DC93C08554A67B812B3B97B3047D` |
| Managed method map | `7523AB2CE5CECAA3DD88C1ED62A67BC52E3D23A4E3A0BF3A82D0F364C586CD01` |
| Diagnostic kernel and staged ESP kernel | `6659374B3260AE6028486D8DC1A981F5A11341C0CB4A1F1A3809A0E64A1A27F1` |
| Candidate ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Corrected platform source | `26EA3603E848B91C72E82F7A5EFF79DD612474ABB22C1A9F845DFEF00B6CFFED` |
| ThreadStore telemetry side-table header | `80BDC61A62AD9849EC625965FBB3A318E27409B0DD85F48BD52210585C5C96C2` |
| Kernel transition snapshot source | `91599CC059D98C4C75F697901F5549A5DE4CA9B3D2034E2FE31C4D2898721AB3` |
| Managed-entry analyzer | `B0F5F97A3CD3A213FCD635AD4A468FA9A901AC34B2E8D71C35A5069A35FE0E45` |
| Corrected transition call-site map | `22761B81B271CC948712477163F90D8D8CCF673FDB6D56F07D61CA918383CDC1` |

The runtime-pack identity is `guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-managed-entry-v1`: NativeAOT 9.0.0, AMD64, Workstation GC, `net9.0`/`win-x64`, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The candidate preserves the R5/R6 ThreadStore and PAL LocalStorage work: the manifest enables ThreadStore readiness, records the pinned runtime source hashes, and identifies the PAL common replacement object at SHA256 `E0B7E67A6003163FDC25D8ACC359C8F8AE067B2BDCF24AE66B63B05386AE52E0`. The guest later reports the same attached Thread and PAL FLS identity at dispatch and return.

Production isolation checks all pass. The diagnostic profile is separate from the proof-disabled production kernel and NativeAOT image; the production maps exclude the managed-entry probe, transition attribution, and safe-handoff markers. No production runtime behavior was changed for R11.

## Telemetry side-table verification

The corrected source does not use `Thread::m_pTransitionFrame` or any `ReversePInvokeFrame` field as telemetry storage. Under the candidate's ThreadStore profile, the legacy direct TLS-cell write is excluded by the compile-time guard. Telemetry nesting uses the separate fixed `g_threadReadinessTransitions` array and depth counter.

`GuideXosThreadStorePushTelemetryFrame` rejects null frames and overflow, records the active frame and previous telemetry frame, and increments a bounded depth of eight. Pop requires the exact top frame, returns the previous telemetry value, and decrements the depth. A popped slot is outside the active depth and is overwritten before reuse. The stock NativeAOT return helper runs before the telemetry pop; telemetry does not synthesize preemptive mode, change GC policy, or suppress a fatal.

The table is process-global. The candidate explicitly has `managedThreads=false`, the QAC guest uses `-smp 1`, and this probe observed one runtime thread throughout. This qualifies identity and nesting for this single-threaded entry path; concurrent managed-thread behavior and cross-thread side-table reuse remain unqualified.

The candidate's prior hosted evidence covers bounded nesting, LIFO order, overflow rejection, invalid-order rejection, and preservation of the live transition field. R10 recorded seven ThreadStore mode-model checks, six side-table checks, 45 corrected-candidate transition assertions, 23 managed-entry contract assertions, 20 runner/analyzer contract assertions, and 32 QAC admission cases. R11 reused those results where their candidate provenance matched and performed the fresh integrity and runner preflight below.

## Hosted preflight and QAC admission

The runner's hosted-only preflight passed at `2026-11-09T03:09:27Z` with correlation ID `C188R12R4R5R2R3R7-20261109T030927955Z-ffd921ba9e994b568d831686b02f7f81`.

- Candidate inventory verification passed for all 1,132 files; the candidate manifest and inventory hashes matched the values above.
- A separate writable guest-state copy was created with three ESP files and a fresh writable UEFI variables image. The copied ramdisk hash matched the preserved source hash.
- QAC was not queried, no attempt guard was created, and no guest was launched in this preflight.
- Hosted preflight record SHA256: `D3ED85E68F7EC28DF829F6BCF0C20A2E303A6DCB480FFA1781CEEABAEA433925`.

Immediately before the native run, the runner captured a complete QAC status/list and Windows QEMU process census: coordinator online, zero reservations, zero managed sessions, and zero local or foreign QEMU processes. It then atomically created the candidate-manifest-specific one-boot guard and obtained exclusive QAC reservation `2713d6033d6943d9a4dc93b8cd4ede9f` for project `guideXOSServer`, mode `acceptance`. The reservation inspection tied QAC owner PID `16052` to the QEMU child PID `21856`, executable path, creation time, and unique QMP port. No foreign process or reservation was terminated, replaced, or bypassed.

The consumed guard is:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\guest-launch-attempt-8AEE184F12F2BF29AE4761AC7A50B4574F99863AF05D448C1B32A01DC8E2787C.json`

Its SHA256 is `F297B33139120EF0FC435936BD0EF3B155DC5617A74276E810356274064BFF06`. It binds the corrected candidate manifest, kernel, ELF, ramdisk, runner, and QEMU executable; `maxGuestBoots=1` and `automaticRetry=false`. The runner records exactly one guest boot.

## Native guest evidence

Run directory:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r2r3-managed-entry\qac-guest-managed-entry-C188R12R4R5R2R3R7-20261109T031214579Z-02e3683a22da40c4832987f06572daf6`

| Checkpoint | R11 evidence |
|---|---|
| Native startup and NativeAOT initialization | Serial reports runtime foundation, PAL and GC platform installation, and `RhInitialize` success. The init record completes through stage `0x63`; the guest records `InitializeModules` returned. |
| ThreadStore and PAL FLS | The same Thread `0x3D78360` is attached and published. Before `ManagedMain`, ThreadStore membership and attached flags are `1`; runtime and observed OS thread IDs are both `1`; PAL FLS points to `0x3D78360`. |
| First reverse-P/Invoke return and telemetry | Stock return reports saved and live transition frames both `0xFFFFFFFFFFFFFFFF`. The telemetry-pop event returns its side-table previous value of null; the subsequent `before_managed_main` checkpoint still reports the top-of-stack marker `0xFFFFFFFFFFFFFFFF`. |
| Managed dispatch and reverse-P/Invoke prologue | Serial records `ManagedMain` dispatch with `UnmanagedCallersOnly-Cdecl`, seed `14`, and no collection request. Bridge checkpoints retain the same Thread and top-of-stack frame. The second stock reverse-P/Invoke return again reports saved and live frames both `0xFFFFFFFFFFFFFFFF`. |
| Genuine compiled C# body | The candidate's mapped `ManagedMain` symbol is at `0x100001920`. Its retained disassembly stores marker `0x5237454E`, computes `seed * 3 + 7` using `lea` plus `add 7`, stores the result, stores completion marker `0x5237444E`, and returns the computed value. |
| Managed-side record and native return | The serial record is `entry=5237454E input=0000000E computed=00000031 completion=5237444E return=00000031 expected=00000031 record=PASS`. `0x31` is decimal `49`. The dedicated analyzer reports `managedEntryAttempted=true`, `managedEntryReturned=true`, return value `00000031`, and both original record markers. |
| Post-return runtime state | Dedicated analysis reports `runtimeStateReady=true`, status `0`, ThreadStore membership `1`, attached flags `1`, FLS match `1`, runtime/observed thread IDs both `1`, stack bounds ready and in-bounds, modules and code manager registered, current Thread equal to PAL FLS Thread `0x3D78360`, and restored frame `0xFFFFFFFFFFFFFFFF`. |
| Fatal and cleanup | Fatal field is `0`; no `0x50414C06` recurred. QMP records `SHUTDOWN reason=host-qmp-quit`; QEMU exited `0`; QAC release is verified. The after-run census is complete with zero QEMU processes, zero reservations, and zero managed sessions. |

The dedicated managed-entry analyzer classified the serial as `MANAGED_ENTRY_PROBE_RETURNED_RUNTIME_STATE_READY`, exit code `0`, with no parse error. It preserved the raw serial hash before and after analysis. The guest manifest also has separate aggregation fields `serialAnalysisComplete=false` and `managedMarkerCount=0`; the profile-specific analyzer independently parsed the existing 16-byte managed record and validated its entry marker, completion marker, result, and return. The compiled body disassembly independently shows those stores and arithmetic instructions.

## Evidence hashes and cleanup

| Evidence | SHA256 |
|---|---|
| Raw serial log | `F2203ED1DFD13FC1C3A548C68EE4DED4E2A03DE5CB040174AE79749263A04FCA` |
| QMP log | `93AFA23A37AC2886F733407A214354AEAB35C784A3D7E569CC02983347DAA713` |
| Managed-entry analyzer result | `21A1836068A935C326205F6B98290CC6C0D15AE1E4D1993ADD48CB2426450FA0` |
| Transition analyzer result | `6E97F1928CCD6FC4C85C65DC070CE8402EC5169F77D0514E2B178AB99EA3FC21` |
| Guest manifest | `F105A58C47FCFACD330E0B219A9AB880E4642A0A2EAFA62FDB55CE4580E31DDE` |
| QAC ownership identity | `1700627A28E239A020A40586C358ED17C29D55F52006D5DC1E166BB372D1EA84` |
| Consumed one-boot guard | `F297B33139120EF0FC435936BD0EF3B155DC5617A74276E810356274064BFF06` |
| Post-run QAC status | `3B2EDDD7B0504ADD94FB8EC2E16596FEFEE6F383C90A3F91A81D259C1EF3C717` |
| Post-run QAC list | `152201354443030DB19021C440110A78DCBB8C70A4C6072E6AE09D457827BBC5` |
| Post-run QEMU census | `4F88E5F132B87A119277D2CA33F5B48B577B0EB3D632C639B8E7664842A2514D` |

The candidate inventory was rechecked after the guest. The runner reports `candidateIntegrityPreservedAfterGuest=true`; manifest, inventory, runtime-pack manifest, platform object, managed ELF, and staged kernel still match their recorded hashes. Original serial, QMP, QAC admission/release snapshots, ownership inspection, analyzer outputs, writable guest media, and consumed guard are preserved in the run directory and phase root.

## R9/R10/R11 comparison

| Phase | Native result |
|---|---|
| R9 | Fatal `0x50414C06` at `RhpReversePInvokeAttachOrTrapThread2`; no managed body marker, result, or return. ThreadStore and FLS identity were valid, but the frame was null and mode cooperative. |
| R10 | Root cause proved. Its only authorized guest boot used the pre-fix candidate; after the stock helper restored the top marker, telemetry overwrote it with null. The corrected candidate had hosted evidence only. |
| R11 | The side-table candidate preserves the restored top marker after initialization, passes the second reverse-P/Invoke entry, reaches compiled managed instructions, records entry/completion markers, computes and returns `49`, and leaves ThreadStore, FLS, frame, and mode ready. |

R11 therefore establishes the first genuine NativeAOT managed-entry milestone for guideXOS Server: actual C# instructions executed and returned a value safely to native code.

## Remaining unverified capabilities and next phase

This result qualifies one allocation-free `UnmanagedCallersOnly` method on the single runtime thread. It does not qualify general managed allocation, GC collections or suspension, managed exceptions, multiple managed threads, persistent managed lifetimes, or production application behavior. No collection was requested. The process-global diagnostic telemetry stack has not been qualified for concurrent entry.

Recommended next phase: qualify the first controlled managed allocation and GC-safe boundary, keeping this R11 one-boot guard consumed and using a separately identified candidate and fresh guard for any future native run. Expand to collections or managed exceptions only after the allocation and thread-state boundary is proven.

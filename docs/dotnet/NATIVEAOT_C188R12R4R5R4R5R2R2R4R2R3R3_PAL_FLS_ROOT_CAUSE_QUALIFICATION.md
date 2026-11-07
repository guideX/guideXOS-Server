# C188R12R4R5R4R5R2R2R4R2R3R3 — PAL FLS Root-Cause Qualification

## Outcome

**Outcome C — PAL_FLS_HOSTED_READY_GUEST_DEFERRED.** The target PAL and ThreadStore contract is mapped, focused hosted checks pass, and a fresh diagnostic candidate with tracing on the active QEMU PAL FLS bridge is built and integrity-checked. The one authorized native boot stopped safely at gate 6, but it used the earlier candidate whose tracing missed that bridge. No second boot was attempted. The cause of the empty PAL FLS cell remains unresolved; no runtime repair is justified.

This outcome does not establish managed InitializeModules, Settings factory, or persistent lifecycle behavior. The candidate still exits before ManagedMain.

## Repository and preservation

| Item | Value |
|---|---|
| Repository | D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT |
| Branch | v1.1_DOTNET_SUPPORT |
| Starting HEAD | 89b8c276ae19926b5b7ea66027889da395030c9f |
| Starting modified files | Three user-owned .obj files listed below |
| Native guest boots in this phase | Exactly one |
| Push | None |

The three object files were preserved with their original hashes and left unstaged:

| File | SHA-256 |
|---|---|
| c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj | 3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD |
| c188r12r4r5r4r5r2r2_native_alloc_record_test.obj | 605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3 |
| guidexos_nativeaot_native_alloc_record.obj | 2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183 |

The prior report remains [NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R2_QAC_TRANSITION_PROFILE.md](NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R2_QAC_TRANSITION_PROFILE.md). Its archived default safe-handoff analyzer-hash mismatch (49ED… stored versus 99F6… current) remains a separate compatibility issue. No archived candidate or analyzer pin was changed.

## Target PAL FLS and ThreadStore contract

The candidate pins the NativeAOT runtime source to commit 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3. In the pinned [ThreadStore implementation](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/threadstore.cpp), AttachCurrentThread calls PalAttachThread before Thread::Construct, before setting TSF_Attached, and before adding the thread to the list. Its detach path calls PalDetachThread before unlinking. The pinned [Thread implementation](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/thread.cpp) sets m_threadId from PalGetCurrentOSThreadId during construction. These sources define the generated ThreadStore contract used by this runtime pack; the target PAL hooks below define how the guest implements it.

In the target [PAL replacement](../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_minwin_replacement.cpp), PalInit frees any prior slot, then allocates the PAL FLS index through guidexos_nativeaot_pal_fls_alloc(FiberDetachCallback). PalAttachThread(thread) reads that same index, rejects a conflicting non-null value, and writes thread with guidexos_nativeaot_pal_fls_set. PalDetachThread(thread) validates the existing value and clears that index. These functions emit bounded readiness markers in the diagnostic build.

The active QEMU diagnostic PAL is wired in the [QEMU bridge](../../kernel/core/nativeaot_pal_qemu_test.cpp) by fillStartupPalTable(): fls_allocate, fls_get, and fls_set point to bridgeFlsAllocate, bridgeFlsGet, and bridgeFlsSet. This is the actual guest implementation, distinct from the production application hook path. The bridge maps each PAL FLS slot to a generic gxos::runtime::LocalStorageIndex. Get/set resolve a BridgeThreadState using process::current_thread_id() and store a per-slot BridgeCell through getLocalStorageValue and setLocalStorageValue. Detach and release use the same LocalStorage lifecycle.

Therefore, in the stock attachment path, the FLS value should be written before the ThreadStore publishes the thread as attached. If an attached ThreadStore record is already visible while the cell is empty, the ordinary stock sequence does not explain that state. A target-specific alternate startup path, an identity difference between setter and reader, a stale/wrong storage mapping, or an invalid ThreadStore snapshot could still explain it.

The diagnostic gate in the [transition capture](../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp) reads the PAL index through GuideXosNativeAotTransitionGetPalFlsIndex() and obtains the value through GuideXosNativeAotTransitionGetPalFlsValue(), which calls the active PAL getter. Source inspection confirms this is the PAL slot, not the runtime TLS/FLS cell. The gate is a preflight before the generated InitializeModules call; the pinned runtime's stock reverse-P/Invoke attach/frame work remains beyond it. The native trace did not record the bridge get/set context, so it does not prove that writer and gate reader used the same context.

## Native sequence and result

R2 evidence is preserved in the prior report and its QAC artifacts. R2 had a valid record with a current ThreadStore thread, an empty PAL FLS value, gate 6, safe status 0x106, no fail-fast, and no entry to InitializeModules or Settings.

This phase used exactly one QAC-owned QEMU boot:

- Booted candidate: out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T120952201Z
- Booted candidate manifest SHA-256: E32C113D7B7D92C1B77798CE0DABA7C04A687F08FB1A6FAB5B996F6B370021AC
- Booted candidate inventory SHA-256: 02930C20EC48235BC2199C1E2B3FCC6A1EED7A4D9441566ADA8F904EF3A1477C; 1,116 entries, verified after the guest with zero mismatches
- Booted diagnostic kernel SHA-256: 079BC15312F7F122DD10DBA594774865E3B6A32E11C17C6D6EDD867679B8E8C4
- Run evidence: out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T122644213Z-7d79c60a52384aecbc2cb4095a8bb9be
- Candidate-scoped one-boot guard: guest-launch-attempt-candidate-20261107T120952201Z.json
- QAC preflight: coordinator online, zero reservations/sessions/queued/blocked work, and zero QEMU processes (zero foreign); post-run census also had zero QEMU processes.
- QAC reservation: 2891b78615db4ef28d6a1924bca0f173
- QAC result: TRANSITION_NATIVE_FAILURE_BOUNDARY_PROVEN; admitted and capture complete; classification TRANSITION_GATE_BLOCKED_SAFE_STOP; QAC release verified.
- QEMU exited with code 0 after the runner's owned safe shutdown. No retry occurred.
- Serial SHA-256 before and after offline analysis: 7227EE8046FF6CF29DF0236C1D0F291EEB778990D4C4F2B005DCF192B347D4AE.

The chronological guest evidence is:

1. PAL initialization allocated PAL FLS index 0 successfully (pal_fls_allocate raw0=0 raw1=1).
2. RhInitialize returned success. TLS was installed, and the runtime instance and ThreadStore pointers were present.
3. An earlier current_thread_state marker reported raw Thread pointer 0x3D76360 with GetCurrentThreadIfAvailable false. The later transition snapshot reported rawThread=currentThread=0x3D76360, attachment 2 (present). This change in observation makes the exact publication point relevant.
4. OS module registration completed. The real generated InitializeModules entry had not occurred.
5. The snapshot read PAL FLS index 0 as null (palFlsValue=0, flsState=0) and returned gate 6.
6. The runner captured result=safe_stop status=00000106, attempted=0, completed=0, no fail-fast, and managedEntry=not_reached.

The record also had nativeThreadId=0 and observedOsThreadId=1. The target Thread implementation initializes m_threadId from the PAL OS-thread-ID query, so these values disagree with the expected identity. Gate 6 is evaluated before the later current-thread-state/identity gate. This is a second unresolved signal, not proof of why the FLS cell was empty.

No pal_attach_* readiness marker appeared, and the readiness record reported no overflow. The run also contained no [C188R12R4R5R4_FLS] bridge get/set trace. The initial diagnostic instrumentation mistakenly targeted nativeaot_application.cpp, while this guest uses the QEMU bridge in nativeaot_pal_qemu_test.cpp. The wrong-path instrumentation was reverted. Because the active bridge read/write operations and their caller identities were not observed, absence of a bridge trace cannot establish that no write happened. No runtime initialization or application behavior was changed to force the gate to pass.

| Property | R2 observation | One R3 boot |
|---|---|---|
| ThreadStore current thread | Present; record marked attached | Same pointer later reported, attachment present; earlier availability marker was false |
| PAL FLS cell | Empty | Empty at PAL index 0 |
| Gate 6 | Stopped | Stopped; status 0x106 |
| PAL slot allocation | Not sufficient to explain assignment | Index 0 allocation succeeded |
| Bridge FLS get/set context | Not captured | Not captured; instrumentation targeted wrong hook path |
| Thread identity | Not established | Record fields differ: nativeThreadId=0, OS ID 1 |
| InitializeModules | Not reached | Not reached |
| Settings factory | Not reached | Not reached |
| PAL fail-fast | Not observed | Not observed |
| Diagnostic termination | Safe | Safe; QEMU exit 0, QAC released |

## Changes and hosted qualification

No production attachment algorithm or gate predicate was changed. The in-scope changes add bounded diagnostics and regression checks:

- kernel/core/nativeaot_pal_qemu_test.cpp traces the real guest bridge allocation, get, set, detach, release, generic slot/generation, bridge cell, owning BridgeThreadState, and OS thread ID when the readiness profile is enabled. It also prints observedOsThreadId in the transition state record.
- guidexos_nativeaot_pal_minwin_replacement.cpp, guidexos_nativeaot_platform.cpp, and guidexos_nativeaot_threadstore_readiness.h report PAL slot allocation and detach ordering.
- test-c188r12r4r5r2r3-transition-attribution.ps1 and its compiled fixture cover empty and foreign FLS values, matching attachment, slot and context mapping, attach/detach ordering, repeated PAL initialization, stale attachment detection, and safe gate classification.
- test-c188r12r4r5r2r3-transition-runner-contract.ps1 validates candidate-specific one-boot guards without replacing the preserved R2 guard.
- run-c188r12r4r5-gc-ownership-qac-guest.ps1 assigns a new candidate its own guard while retaining the legacy R2 candidate guard.

Hosted checks all passed:

| Command or check | Result |
|---|---|
| pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/build-c188r12r4r5r1-init-attribution-candidate.ps1 -ThreadStoreReadiness -GcInitProbes -SemiSharedAttribution -NativeAllocationAttribution -SafeHandoff -InitializeModulesAttribution | READY |
| pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r2r3-transition-attribution.ps1 -ProductionNativeAotManifest 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T123816299Z/production/nativeaot-build/runtime-pack/runtime-pack.manifest.json' -ProductionNativeAotMap 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T123816299Z/production/nativeaot-build/nativeaot/artifacts/HostLogProof.map' -ProductionApplicationManifest 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T123816299Z/production/nativeaot-build/production-nativeaot-qualification.json' -CallSiteMapPath 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T123816299Z/transition-callsite-map.json' | PASS, 28 fixtures |
| pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r2r3-transition-runner-contract.ps1 -CandidateRoot 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T123816299Z' | PASS, 19 assertions; hosted only, no QAC query/reservation/guest |
| pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1 | PASS, 32 cases |
| pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1 -CandidateRoot 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T123816299Z' | PASS |
| Hosted LocalStorage tests | PASS, including allocation/release/reuse, stale-index rejection, thread isolation, detach callbacks, callback ordering/failure, and shutdown cleanup |
| Hosted NativeAOT FLS adapter probe | PASS; no RhInitialize, GC initialization, finalizer thread, or collection |

The two hosted native-storage probes were compiled with g++ using C++17, -O2, -Wall, -Wextra, -Wpedantic, the repository include root, and -pthread, then executed from the output directory below. The LocalStorage binary linked guidexos_local_storage.cpp, guidexos_event.cpp, guidexos_native_thread.cpp, and guidexos_local_storage_tests.cpp. The adapter probe linked guidexos_local_storage.cpp, guidexos_event.cpp, guidexos_native_thread.cpp, guidexos_nativeaot_fls_adapter.cpp, and guidexos_nativeaot_fls_adapter_probe.cpp.

LocalStorage test binaries are retained under out/runtime/native-local-storage-r3-hosted-20261107T123634594Z. Transition-attribution hosted output is under out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/hosted-20261107T124413622Z.

## Fresh diagnostic candidate

A second candidate was built after correcting the instrumentation source path. It is prepared for a future separately authorized native run and was not booted in this phase.

| Item | Value |
|---|---|
| Candidate | out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261107T123816299Z |
| Mode | NativeAotInitializeModulesTransitionAttribution |
| Runtime source commit | 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3 |
| Manifest SHA-256 | 72E0346D05235137A21F1F6905509C5B80346FD1960ECC6B358F0AF39C334605 |
| Integrity inventory SHA-256 | A5443FA58C0E9B47E8B34491720B5898E3EB05209146EAF6138DEEDC186F8F19 |
| Inventory | 1,116 entries; verified with zero mismatches by the hosted runner contract |
| Diagnostic kernel SHA-256 | 18AB38DDD4C572C5EE30F8228D64A89B53AF06D778D7B88C06D4A02DBEF4E700 |
| Managed entry | Suppressed; returns before ManagedMain |
| Native boot | Not performed |

The candidate keeps the diagnostic checkpoint ordering and early-return boundary. Its output is separate from the booted candidate and the preserved R2 candidate.

## Limitations and next phase

The phase establishes the source contract, confirms the same native safe-stop boundary, and identifies that the guest did not trace the active QEMU FLS implementation. It does not identify whether the native cause is a missing PalAttachThread call, a startup path that marks ThreadStore attached independently, a context/ID mismatch, a storage mapping error, or a bad snapshot assumption. Investigate the raw ID discrepancy with the actual bridge trace before changing attachment or moving the gate.

Recommended next phase: **C188R12R4R5R2R2R4R2R3R4 — PAL FLS bridge identity qualification.** Use the fresh candidate to capture pal_fls_set and the gate's pal_fls_get with the same PAL slot, generic slot/generation, current bridge state pointer, OS thread ID, cell owner, and returned Thread*. Correlate these with ThreadStore attach publication and m_threadId. Keep gate 6 intact until evidence distinguishes a target attachment defect from a valid diagnostic boundary. Any native boot in that phase requires its own explicit one-boot authorization. Managed Settings testing remains a separate later phase with a candidate that enters ManagedMain.

## Closeout

The R2 candidate and its guard remain unchanged. The three .obj files remain modified and uncommitted. No generated candidate or QAC artifacts are staged. In-scope source, hosted tests, runner updates, and this report are committed together; no push was made.

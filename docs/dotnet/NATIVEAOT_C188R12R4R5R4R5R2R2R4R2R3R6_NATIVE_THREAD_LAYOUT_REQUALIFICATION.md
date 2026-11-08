# C188 R6 — NativeAOT Thread Layout Requalification

## Outcome: `NATIVE_TRANSITION_ADVANCED`

R6 produced a fresh candidate, passed hosted build and admission checks, and completed exactly one authorized QAC guest boot. The native trace shows the valid unattached checkpoint, PAL attachment, FLS assignment, actual ThreadStore publication, and a successful return from `InitializeModules`. The candidate intentionally stopped before `ManagedMain`; managed entry and collection were not requested.

The implementation snapshot qualified by the candidate is `75cea2398d471847d50d653decbeb2aec7916e8c` on `v1.1_DOTNET_SUPPORT`. The starting HEAD was `e694fb4ec337bbb3b190a49f9b5a66fc1980c876`. The report is a documentation-only commit after that tested source snapshot.

## Source and layout verification

The R5 correction remains in place: the TLS startup marker no longer uses `Thread::m_ThreadStateFlags`, and the TLS FLS shadow no longer uses `Thread::m_pCachedTransitionFrame`. FLS access uses PAL LocalStorage, so the PAL getter, setter, and attachment path share the allocated slot and its generation. The R5 source analysis is in [the R5 thread attachment report](NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R5_THREAD_ATTACHMENT_PUBLICATION.md).

R6 builds and records layout evidence from the pinned NativeAOT source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. It checks the included `thread.h` and `AsmOffsets.h` hashes and asserts these `RuntimeThreadLocals` offsets:

| Field | Offset |
|---|---:|
| `m_ThreadStateFlags` | `0x38` |
| `m_pTransitionFrame` | `0x40` |
| `m_pDeferredTransitionFrame` | `0x48` |
| `m_pCachedTransitionFrame` | `0x50` |
| `m_pNext` | `0x58` |
| `m_hPalThread` | `0x60` |

`AsmOffsets.h` independently supplies the first three offsets. The legacy sentinel writes only the runtime cell at `0x30`; it is idempotent and leaves the flags and frame fields untouched. No TLS FLS shadow offset is present. The compiled layout evidence is in the candidate runtime-pack manifest and candidate manifest; the assertions compile against the pinned `thread.h` include rather than another runtime revision.

The diagnostic does not infer attachment from a non-null raw `Thread*`. Its source contract permits the genuine pre-attachment state only when the flags are exactly zero/`TSF_Unknown`, PAL FLS is empty, the native thread ID and PAL stack bounds are valid, and the unchanged stock `ReversePInvokeAttachOrTrapThread` path performs construction and attachment. For an attached thread it requires `TSF_Attached`, matching thread/FLS identities, preemptive mode, and a valid transition frame. It neither forces the flag nor populates FLS. The candidate emits bounded ThreadStore publication event 32 and checks actual current-thread membership with `ThreadStore::Iterator`; a missing publication blocks the transition.

The native run confirmed the post-R5 checkpoint contract. `RhInitialize`, runtime instance, ThreadStore, and TLS were ready. At the pre-call snapshot, a raw `Thread*` was available while `GetCurrentThreadIfAvailable` classified the thread as absent and PAL FLS was empty. The OS thread ID was 1; the runtime Thread's PAL ID was not yet initialized. Gate 6 returned success. The gate reads the actual flags word and permits the absent-thread path only when it is exactly zero; the serial format records the attachment classification and gate result, but does not print that flags word separately.

The stock reverse-P/Invoke path then read the empty PAL slot, set it to the same raw `Thread*`, reported attachment completion, and found that pointer in the actual ThreadStore list. Its post-attach PAL thread ID was 1, matching the observed OS thread ID. The transition returned and `InitializeModules` completed. The analyzer classifies this as `INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED`, with no fail-fast, no gate failure, no first-failure stage, and no managed-entry marker. The emitted state record is the sealed pre-call snapshot; it is printed after the transition result.

No separate constructor marker was emitted, and the raw flags word was not serialized independently. The source contract, successful gate, post-attach PAL identity, FLS value, and verified ThreadStore membership provide the available evidence. No conclusion about Settings, managed lifetime, or collection follows from this run.

## Candidate and build provenance

The fresh candidate is:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\candidate-20261107T213653124Z`

Its manifest reports `TRANSITION_ATTRIBUTION_HOSTED_READY`, pins the runtime sources to the commit above, declares that `ManagedMain` is not entered, and retains the explicit early-return boundary. The candidate’s 1,116-file integrity inventory passed with zero mismatches and zero unlisted payload files.

| Artifact | SHA-256 |
|---|---|
| Candidate manifest | `807613ADFC0D6E5F8F75A0DC9C4B123982FB5AECCCAAFB2F9CB47A9357160EB6` |
| Candidate integrity inventory | `BB7073D628F4BEC82094A25B6D02FB198B48B45B08FF91B404AE538EF0A83740` |
| Diagnostic kernel | `B3514A0C420F339945876A05D3BF6CF727C0FC3B226FE3F0827D0B59C6429A34` |
| Runtime pack | `C542DCFE920DC67B533F12E8C03A184AC6496A6997867A1E88962FE7EC13A426` |
| Proof-disabled production NativeAOT ELF | `F50B08359A348AD1DC22ABC5378703C8D288E86E4C4196F2AFB05FDE4D53768D` |
| Proof-disabled production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |

The runtime-pack manifest is at `candidate\runtime-pack\runtime-pack.manifest.json`; production qualification manifests are at `candidate\production\nativeaot-build\production-nativeaot-qualification.json` and `candidate\production\kernel\kernel-qualification.json`. Candidate compilation and intermediate outputs were isolated on E:. The candidate’s `productionIsolationChecks` all pass: diagnostic instrumentation and transition markers are absent from production outputs, and the production composite lifecycle is retained.

All four incomplete build attempts remain preserved alongside the final candidate; none launched QEMU:

| Candidate | Result |
|---|---|
| `candidate-20261107T211700667Z` | Compile-time layout assertions preceded the pinned `thread.h` definition. The include order was corrected. |
| `candidate-20261107T211844527Z` | Diagnostic image and kernel built; production NativeAOT rejected `Directory.Build.props` outside its then-configured artifact root. |
| `candidate-20261107T212347964Z` | Managed production build exposed a case-insensitive `$artifactRoot`/`$ArtifactRoot` variable collision. The local output variable was renamed. |
| `candidate-20261107T213103522Z` | Production-kernel admission exposed the same typed-parameter collision as a `.Count` access on a coerced string. The matched-root variable was separated and covered by a fixture. |
| `candidate-20261107T213653124Z` | `READY`; all candidate and production build stages completed, then used for the single authorized guest boot. |

The R6 implementation and build fixes are committed locally in `856942bd`, `49227edf`, `850c692e`, `b6882bdd`, and `75cea239`. No changes were pushed.

## Hosted qualification

All checks below are hosted checks and do not establish native guest behavior.

- Candidate build completed, including diagnostic runtime pack/kernel and both proof-disabled production qualifiers.
- Candidate integrity verification: **PASS**, 1,116 files, no mismatches or unlisted payload files.
- Transition attribution regression: **PASS**, 43 fixtures. The compiled shared gate harness reports 42 assertions, including canary checks for flags/frame memory, raw-pointer-versus-attachment semantics, PAL FLS identity/status classification, and valid unattached/attached states.
- Candidate transition runner contract: **PASS**, 20 assertions. It verified the manifest and inventory and confirmed hosted preflight does not query or reserve QAC, create a boot guard, or start QEMU.
- QAC admission predicates: **PASS**, 32 cases.
- Safe-handoff runner contract: **PASS** against this candidate.
- Native LocalStorage hosted suite: **PASS**, including attach/detach, per-thread isolation, callback order, slot generation/reuse, stale-index rejection, and shutdown cleanup.
- NativeAOT PAL FLS adapter probe: **PASS**, including getter/setter identity, worker-thread isolation, detach callback, generation reuse, and stale-index rejection. It reported no `RhInitialize`, GC initialization, finalizer thread, or collection.
- LocalStorage-before-initialization smoke suite: **PASS**, including allocation/get/set/release, attach, shutdown, no hidden pre-init manager state, and lifecycle recovery.
- PowerShell parser and `git diff --check`: **PASS** for the final source scripts.

The candidate-specific hosted preflight also passed and created fresh isolated writable guest state under the E: phase root. Its manifest is in `qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T214936149Z-6d5d6c99e14e42918f16dd5399a06bcb\hosted-preflight.json`. This mode did not query QAC or create a launch guard; the single subsequent boot created the guard listed below.

## Native single-boot evidence and R4 comparison

The first QAC census found a separate AArch64 acceptance reservation, so the boot was deferred at that point. After the user reported the lane was clear, a fresh status/list/inspection and Windows process census at `hosted-r6\qac-preflight-retry-20261108T010934399Z\qac-read-only-preflight.json` returned zero reservations and zero QEMU processes. The runner then repeated its own census immediately before admission and obtained one exclusive reservation for this run.

The single run used QAC project `guideXOSServer`, acceptance mode, AMD64 `qemu-system-x86_64.exe`, TCG, one vCPU, and a 1024 MiB guest. It used a candidate-integrity-verified ESP copy and a run-owned writable UEFI variable image. Automatic retry was false and the one-boot limit was 1. The run correlation ID is `C188R12R4R5R2R3R1-20261108T010954570Z-45becc864dfd4ac78f315f3111c5fccb`; QAC reservation `3561fc445a9444f4a362f8e59a6d8bc2` launched QEMU PID `3892` under owner PID `9056`.

The run evidence root is `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261108T010954570Z-45becc864dfd4ac78f315f3111c5fccb`.

The raw serial is `qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261108T010954570Z-45becc864dfd4ac78f315f3111c5fccb\serial.log`, SHA-256 `A5B142C50FFFD6DAEC2BB0D6035BE3DC510E453E501DF193AF2B122F7112CA5B`. The analyzer output in the same run directory preserves that hash and returns `INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED`. The guest manifest records `guestPass=true`, candidate integrity preserved after the guest, zero managed markers, and no collection request.

The runner performed the owned QMP `quit`, observed QEMU exit code 0, verified release of reservation `3561fc445a9444f4a362f8e59a6d8bc2`, and captured a post-run QAC census with zero reservations and zero QEMU processes. The candidate-specific one-boot guard is `guest-launch-attempt-candidate-20261107T213653124Z.json`; SHA-256 `BE47C45AF059D0B3DD82A84F60CF14FF9EF76E63CE6893240C3A08566CE85D65`.

| Property | R4 evidence | R6 result |
|---|---|---|
| False attached-state indication | Present; startup marker aliased the flags field | Pre-call checkpoint reports attachment absent, empty PAL FLS, and gate success under the zero-flags contract |
| Transition-frame overlap | Source defect proven | FLS shadow removed; PAL LocalStorage owns the value; layout assertions pass |
| PAL FLS getter | Returned null in the R4 trace | Native pre-attach getter returned empty; setter stored the raw Thread in the same LocalStorage identity |
| PAL attachment and ThreadStore publication | Not established | PAL attach completed; exact raw Thread pointer was found in the ThreadStore list |
| Native/OS thread identity | R4 discrepancy observed | PAL thread ID after attachment and observed OS ID were both 1; pre-attach runtime Thread ID was 0 as expected for the absent state |
| Gate 6 / `InitializeModules` | Safe stop; `InitializeModules` not reached | Gate passed; `InitializeModules` returned without a fail-fast |
| `ManagedMain` | Not reached | Not requested by design; no managed-entry marker |

## Next step

The native layout and transition correction advanced through `InitializeModules`. The next qualification should build on that boundary and explicitly scope any managed-entry work. Keep Settings, collection, and managed lifetime behavior unaccepted until those paths execute and produce their own evidence.

# C188 R6 — NativeAOT Thread Layout Requalification

## Outcome: `NATIVE_REQUALIFICATION_DEFERRED`

R6 produced a fresh, hosted-ready candidate and passed the hosted regression, build, integrity, and runner admission checks. Native QEMU qualification was deferred because the QAC lane was occupied by another verified acceptance reservation when the final read-only ownership census ran. No R6 guest was launched, no R6 boot guard was created, and no retry was made.

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

This is source and hosted build evidence only. Since no R6 guest ran, the native thread state at `RhInitialize`, PAL attachment, FLS assignment, ThreadStore publication, gate 6, and `InitializeModules` remains unobserved.

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
| `candidate-20261107T213653124Z` | `READY`; all candidate and production build stages completed. |

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

The candidate-specific hosted preflight also passed and created fresh isolated writable guest state under the E: phase root. Its manifest is in `qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T214936149Z-6d5d6c99e14e42918f16dd5399a06bcb\hosted-preflight.json`. This mode does not query QAC or create a launch guard. The candidate-specific guard remains absent.

## QAC ownership result and R4 comparison

The final read-only QAC census was recorded at:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\hosted-r6\qac-preflight-20261107T214740104Z\qac-read-only-preflight.json`

The coordinator was online, but the lane was not clear: one verified acceptance reservation was running for `guideXOSServer-AARCH64-ABI3-Studio`, with `qemu-system-aarch64.exe` PID `28628` and owner PID `16888`. The Windows process census also found one QEMU process, and the reservation inspection succeeded. `Test-QacClearWithCensus` therefore returned false. This reservation belongs to another active task; R6 did not inspect it beyond the read-only status/list/inspect calls and did not release, cancel, or alter it.

| Property | R4 evidence | R6 result |
|---|---|---|
| False attached-state indication | Present; startup marker aliased the flags field | Source overlap removed and compile-time layout assertions pass; guest state not observed |
| Transition-frame overlap | Source defect proven | FLS shadow removed; PAL LocalStorage owns the value; guest state not observed |
| PAL FLS getter | Returned null in the R4 trace | Hosted getter/setter and slot tests pass; native guest value not observed |
| PAL attachment and ThreadStore publication | Not established | New bounded publication check is linked; no guest sequence observed |
| Gate 6 / `InitializeModules` | Safe stop; `InitializeModules` not reached | Not run because QAC was occupied |
| `ManagedMain` | Not reached | Suppressed by candidate design; no guest was started |

## Next step

Keep this unbooted candidate and its inventory. Once the QAC owner has released its reservation, repeat the status/list/inspection and Windows process census immediately before launch. If the lane is clear and the candidate inventory still verifies, run the existing transition runner exactly once with this candidate. Do not infer runtime attachment, `InitializeModules`, Settings, collection, or managed lifetime behavior from the hosted results in this report.

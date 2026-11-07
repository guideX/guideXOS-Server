# C188R12R4R5R4R5R2R2R4R2R3R5 — Thread Attachment Publication

## Outcome

**Outcome A — `THREAD_ATTACHMENT_CAUSE_PROVEN`.** R4 did not complete normal NativeAOT thread attachment before gate 6. The target startup code wrote its TLS initialization marker over `Thread::m_ThreadStateFlags`, setting `TSF_Attached` before PAL FLS assignment, `Thread::Construct`, or ThreadStore list publication. The snapshot faithfully read the nonzero state flag but misinterpreted that bit as proof of a completed attachment.

The PAL FLS getter was operating on the correct allocated slot and LocalStorage generation. It returned null because no FLS cell had been assigned for the current bridge thread. No PAL attach call reached the active bridge before the gate. The source fix removes the aliased marker write and leaves the gate and stock attach path in place.

## Repository and preservation

| Item | Value |
|---|---|
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `c7a317f3b0c4175395a21b394042523178b5c68e` |
| Final HEAD | R5 closeout commit containing this report; SHA is recorded in the delivery summary |
| Pinned NativeAOT source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| New QAC boot | None; source and preserved trace establish the cause |
| Push | None |

The three pre-existing modified `.obj` files were preserved and excluded from the commit. The R4 candidate, integrity inventory, evidence, and one-boot guard remain unchanged. The R5 platform object compile used a copy of R4's compiler command with only its `/Fo` output redirected into `out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\hosted-r5-platform-compile-004`.

## R4 evidence

The preserved one-boot run is:

`out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T135044173Z-d2e925c535474d88a45e0eb60a5dbde8`

| Artifact | Evidence |
|---|---|
| Serial | `serial.log`, SHA-256 `595296EE25A6FE61D1E4CA4E76E1F7368D59D8B75AB51E1DD0BCE67ED80D8119` |
| Candidate manifest | SHA-256 `72E0346D05235137A21F1F6905509C5B80346FD1960ECC6B358F0AF39C334605` |
| Integrity inventory | SHA-256 `A5443FA58C0E9B47E8B34491720B5898E3EB05209146EAF6138DEEDC186F8F19`; 1,116 entries, no mismatches |
| QAC result | Safe stop `0x106` at gate 6; no fail-fast; `InitializeModules` and `ManagedMain` were not reached |

The prior R4 report says the bridge allocation record at sequence 0 is missing. It is present in the raw serial, though serial interleaving removed its opening `[` character. The active bridge records sequence 0 as `pal_fls_allocate`, followed by sequences 1–6 for the gate getter. The updated hosted regression parses the unmodified serial and checks this ordering.

The allocation record names PAL index 0 and generic LocalStorage slot 1, generation 1. The getter reports LocalStorage success for that same slot and generation, with a null marker, cell, owner, and returned value. The bridge thread context is `0x3D77600`, OS thread ID 1. The getter succeeded because the slot existed; its current thread had no stored `BridgeCell` value.

The bridge trace capacity is 32 records and contains seven events. It has no overflow marker. The transition record has `initOverflow=0` and `threadOverflow=0`. The active bridge source records setter calls, and the preserved trace has no `pal_fls_set_*` event. Thus neither filtering nor trace exhaustion accounts for the missing setter. The PAL source's `pal_attach_*` readiness markers are compiled conditionally and were not enabled in the PAL minwin replacement object, so their absence alone is inconclusive. The bridge getter/setter trace is the direct operation evidence.

## Attachment call graph and state meaning

The startup sequence for this profile is:

1. `GuideXosNativeAotGcStartupMain` calls `RhInitialize(false)`.
2. Pinned `RhInitialize` calls `PalInit`, then `InitDLL`. `RuntimeInstance::Initialize` creates and publishes the `ThreadStore`; it does not attach the startup thread.
3. The kernel installs TLS and calls the R2R3 attribution prepare export. `initializeRuntimeState(block, true)` records a readiness sample, prepares the platform TLS state, and performs real `RhRegisterOSModule` code-manager registration.
4. The R2R3 preflight captures the snapshot and stops before generated `InitializeModules`.
5. On the normal path, generated `InitializeModules` enters through its reverse-P/Invoke prologue. `RhpReversePInvokeAttachOrTrapThread2` calls `Thread::ReversePInvokeAttachOrTrapThread`; if the thread is not attached, that calls `ThreadStore::AttachCurrentThread`.
6. Pinned `ThreadStore::AttachCurrentThread` calls `PalAttachThread`, then `Thread::Construct`, then sets `TSF_Attached` and links the thread into `m_ThreadList`.

These observations are distinct:

| Observation | What it proves |
|---|---|
| `RawGetCurrentThread()` returns non-null | The address of the current TLS `tls_CurrentThread` object is available. |
| `GetCurrentThreadIfAvailable()` returns non-null | `Thread::IsInitialized()` sees a nonzero `m_ThreadStateFlags`; it does not inspect the ThreadStore list. |
| `m_ThreadStateFlags & TSF_Attached` | The attached bit is set; by itself it does not prove PAL FLS assignment or list membership. |
| PAL FLS returns the current Thread | PAL attachment state is present for this thread and slot. |
| `Thread::Construct` completed | The startup fields such as OS thread ID and top-of-stack transition frame were initialized. |
| ThreadStore list contains the Thread | The thread was published in the actual ThreadStore list. |

The required ordering is precise: `PalAttachThread` assigns the current Thread to the PAL FLS slot before `Thread::Construct`, before setting `TSF_Attached`, and before linking the entry into `ThreadStore::m_ThreadList`. At the gate's pre-`InitializeModules` point, an uninitialized raw Thread with zero state flags and empty PAL FLS is a valid startup condition: the stock reverse-P/Invoke helper is expected to attach it. If an already attached thread reaches the gate, its PAL FLS value must match. Gate 6 checks this distinction and does not need to move.

## First causal divergence

The raw R4 serial gives:

- TLS block: `0x3D77330`
- raw Thread: `0x3D77360`
- therefore `rawThread = tlsBlock + 0x30`
- snapshot: `attachment=2`, `nativeThreadId=0`, `observedOsThreadId=1`, `palFlsValue=0`, `flsState=0`, `frame=0`

The R4 runtime-pack manifest records `runtimeCellOffset=0x30` and `runtimeCellInitializedOffset=0x38`. The old initialization code used `cell + kRuntimeCellInitializedOffset`. Since `cell` is `block + 0x30`, that address is `block + 0x68`, or `rawThread + 0x38`. Pinned AMD64 `AsmOffsets.h` places `Thread::m_ThreadStateFlags` at `Thread + 0x38`. The marker write therefore stored `1` directly into `TSF_Attached`.

The write occurred inside `initializeRuntimeState`, after its initial `current_thread_state` readiness sample and before `RhRegisterOSModule`. It explains why that earlier sample reports `GetCurrentThreadIfAvailable() == false`, while the later gate snapshot sees a non-null current Thread and the attached bit. Registration itself did not attach the thread.

The zero `nativeThreadId` and zero transition frame are consistent with `Thread::Construct` never having run. Pinned `Thread::Construct` assigns the native ID from `PalGetCurrentOSThreadId` and sets the transition frame to the top-of-stack marker. The bridge independently observed OS thread ID 1 in the same startup context, so `nativeThreadId=0` is not evidence that the PAL getter observed another OS thread; it is evidence the Thread was unconstructed.

The forged attached bit also makes `Thread::ReversePInvokeAttachOrTrapThread` skip its normal attach call. This is **case B — attachment bypassed**: target startup code published the attached bit without completing the normal PAL, construction, and ThreadStore publication contract. The diagnostic's bit read was accurate, but it treated that bit as proof of the entire contract.

## Correction

The code change removes `kRuntimeCellInitializedOffset` and the write through `cell + 0x38`. The non-managed legacy sentinel now initializes `*cell` only when it is null. The platform's `FlsGetValue` and `FlsSetValue` wrappers now use the same PAL LocalStorage slot as `PalAttachThread`, and the old TLS `+0x80` shadow has been removed. That shadow mapped to `Thread + 0x50`, `m_pCachedTransitionFrame`, so removing it fixes a second source-proven TLS alias. The PAL attach sequence and gate location are unchanged.

This is the smallest correction for the proven first divergence. The new hosted regression asserts the raw/uninitialized, initialized/unpublished, PAL-FLS-before-publication, null-FLS-after-purported-publication, identity mismatch, and gate ordering cases. The R4 serial checks also cover missing-marker interpretation, event order and overflow, the actual LocalStorage result, and production diagnostic isolation.

## Hosted verification

| Check | Result |
|---|---|
| Transition attribution and R4 bridge evidence | PASS, 38 fixtures; C++ harness has 32 assertions |
| Transition runner contract | PASS, 20 assertions; verified 1,116-entry candidate inventory and hosted-only isolation |
| QAC admission predicates | PASS, 32 cases |
| Safe-handoff regression | PASS |
| Safe-handoff runner contract | PASS |
| Modified platform source compile | PASS with the R4 candidate profile flags; output kept outside the candidate |
| New QAC boot | Not run; exact source alias and preserved trace establish the cause |

Transition regression output is in `out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\hosted-r5-attachment-regression-007`. The latest modified platform source object is in `out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\hosted-r5-platform-compile-004`. QAC-admission output is in `out\dotnet\c188r12r4r5r4-threadstore-readiness\hosted-qac-admission-20261107T151945481Z`. Safe-handoff output is in `out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\hosted-r5-safe-handoff`.

## Validation boundary

No R5 guest boot was run: the preserved source and R4 trace prove the first divergence, so another boot was unnecessary for diagnosis. The repair is covered by the hosted source regression and a compile of the modified platform object. The candidate was not rebuilt or booted after the repair; therefore post-fix guest behavior remains unobserved, and this report makes no claim that execution reached `InitializeModules` or managed entry.

The R5 source, tests, and report are committed in the closeout commit identified in the delivery summary. No native guest state was consumed, no candidate guard was changed, and no push was performed.

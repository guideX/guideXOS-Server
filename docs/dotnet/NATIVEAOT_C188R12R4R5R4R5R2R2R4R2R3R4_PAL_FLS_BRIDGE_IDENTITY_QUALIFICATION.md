# C188R12R4R5R4R5R2R2R4R2R3R4 — PAL FLS Bridge Identity Qualification

## Outcome

**Outcome B — `QEMU_FLS_BOUNDARY_NARROWED`.** The verified R3 candidate completed one QAC-owned native boot. The active QEMU bridge read succeeded for its LocalStorage index and generation, on bridge thread ID 1, and returned an empty value. The later ThreadStore snapshot reports an attached current thread, but the capture contains no PAL attach or bridge setter records. The bounded traces did not overflow.

This is strong evidence that the PAL attachment write was not present in the bridge state read at gate 6. The capture does not provide a positive attach entry or setter result, so it cannot distinguish a skipped PAL attach from an inaccurate ThreadStore attachment snapshot. No runtime repair is justified from this run alone. Gate 6 remained a safe stop; `InitializeModules`, `ManagedMain`, and Settings were not reached.

## Repository, artifact, and preservation state

| Item | Value |
|---|---|
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting local HEAD | `3acca743bc37daa9a41f62c3efad86a8d7526519` |
| Remote branch HEAD at qualification | `3acca743bc37daa9a41f62c3efad86a8d7526519` |
| Candidate | `out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\candidate-20261107T123816299Z` |
| Candidate native boots before this run | 0 |
| Candidate native boots in this run | 1 |
| Push | None |

The candidate remained at its original D: path. The configured E: artifact root was searched and did not contain this candidate. The runner's strict D: phase-root membership check accepted the original candidate; no E: candidate path, junction, manifest rewrite, candidate copy, or runner admission relocation was used.

Candidate identity and integrity matched the R3 record before and after the guest:

| Artifact | SHA-256 / result |
|---|---|
| Candidate manifest | `72E0346D05235137A21F1F6905509C5B80346FD1960ECC6B358F0AF39C334605` |
| Integrity inventory | `A5443FA58C0E9B47E8B34491720B5898E3EB05209146EAF6138DEEDC186F8F19` |
| Inventory entries | 1,116; 0 mismatches; 0 unlisted payload files |
| Diagnostic kernel and staged ESP kernel | `18AB38DDD4C572C5EE30F8228D64A89B53AF06D778D7B88C06D4A02DBEF4E700` |
| Diagnostic NativeAOT ELF | `36109D9AC05136F13DF37DE3AC205AA27437FEBA594D929070F5D8BDE1FDC758` |
| Candidate ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Post-run inventory verification | `PASS`; candidate manifest and inventory unchanged |

The three pre-existing modified object files remain untouched and unstaged:

| File | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Active bridge and trace qualification

`fillStartupPalTable()` in `kernel/core/nativeaot_pal_qemu_test.cpp` binds `fls_allocate`, `fls_get`, and `fls_set` to `bridgeFlsAllocate`, `bridgeFlsGet`, and `bridgeFlsSet` (lines 2000–2015). The candidate kernel build log contains both the ThreadStore-readiness and transition-attribution defines. The candidate records the bridge source hash as `01E25778252F001C34FBF64249F451176AA84A890B3EC34A0A1772CBEBDCAE1A`, matching the source used for this candidate. The serial trace's `pal_fls_get_*` events confirm that the guest executed the bridge getter.

The runtime-pack manifest identifies one `threadStorePalCommonReplacement` archive member, `guidexos_nativeaot_pal_common_replacement.threadstore.obj`, with SHA-256 `42E54D1A3530062E5E2DA88BAE92200699D2F1FAB75EDC563D6EF4957C418720`. The diagnostic PAL replacement's attach markers are in `guidexos_nativeaot_pal_minwin_replacement.cpp`; `PalAttachThread()` logs entry, existing value, and completion around its FLS get/set.

The bridge trace remains bounded at 32 records (`palFlsReadinessMarker`, `nativeaot_pal_qemu_test.cpp:589–608`). The only readable bridge records are the six gate-get records numbered 1–6. The counter had advanced once before that read, consistent with the allocation event, but the sequence-0 allocation line is absent from the serial file. No overflow event was reported. ThreadStore readiness reports `threadOverflow=0`; the initialization trace reports `initOverflow=0`. A normal single PAL attach plus the gate read would use about 17 bridge records: one allocation, six for the attach's existing-value get, four for its set, and six for the gate get. The cap was not exhausted, so no diagnostic kernel rebuild or trace-budget change was made.

The correct analyzer is `scripts/dotnet/analyze-c188r12r4r5r2r3-transition-attribution.ps1`, SHA-256 `D31AC77EC0E6692D1E274190AB8081338E9ABA8C643E7D9135763B0E21F03B00`. The candidate pins this hash; the runner contract checks analyzer resolution from its own directory. Analysis exited 0 and returned `TRANSITION_GATE_BLOCKED_SAFE_STOP`. The archived default safe-handoff candidate's known analyzer-hash mismatch remains separate and was not changed.

## Hosted preflight and runner correction

The original hosted preflight verified the candidate, copied three ESP files into isolated writable guest state, and copied a fresh UEFI variable image. It did not query or reserve QAC and did not create a one-boot guard. Focused hosted checks passed:

| Check | Result |
|---|---|
| Transition attribution fixtures | PASS, 28 fixtures |
| Transition runner contract after correction | PASS, 20 assertions |
| QAC admission predicates | PASS, 32 cases |
| Safe-handoff runner contract | PASS |
| Transition candidate profile, inventory, analyzer pin, and production-isolation contract | PASS |
| Fresh ESP and UEFI-variable isolation | PASS |

An initial live admission sample found an unrelated AArch64 QAC reservation. The runner stopped before creating the candidate guard or starting QEMU. That blocked path exposed an uninitialized `$safeHandoffTerminal` variable while writing the outcome manifest under strict mode. The runner now initializes that boolean before the protected admission flow, and the runner contract asserts this path. The change does not alter candidate membership, the candidate-specific guard path, or QEMU behavior. This blocked sample reserved no QAC slot and consumed no guest boot.

The archived preflight candidate mismatch was not relaxed. No candidate artifacts or runtime source were rebuilt or modified for this phase.

## One authorized native boot

The candidate-specific guard was absent before admission. After QAC status/list and the local QEMU census were clear, the runner created the guard once and made one QAC invocation:

| Item | Evidence |
|---|---|
| Run correlation | `C188R12R4R5R2R3R1-20261107T135044173Z-d2e925c535474d88a45e0eb60a5dbde8` |
| Run evidence directory | `out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution\qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T135044173Z-d2e925c535474d88a45e0eb60a5dbde8` |
| QAC reservation | `c495d663f9ae4bb1957dfd5bfba2c39a`, project `guideXOSServer`, mode `acceptance` |
| Runner / QEMU owner | PID 13756 / PID 22768; owner identity verified |
| Candidate guard | `guest-launch-attempt-candidate-20261107T123816299Z.json`; SHA-256 `9FE568506BAABD6DF29CE5A45E1BF5CED6F7C3BAC839C92ACBF89D476D7A7B07` |
| Guard policy | `maxGuestBoots=1`, `automaticRetry=false`; binds candidate manifest and runner hashes |
| QAC admission and capture | admitted; serial/QMP capture complete |
| Guest result | safe stop, status `0x106`, gate 6; QEMU exit 0 |
| Fail-fast | none |
| `InitializeModules` / `ManagedMain` | not reached |
| QAC release and post-run census | release verified; zero reservations and zero QEMU processes |

The preserved serial file is 110,937 bytes with SHA-256 `595296EE25A6FE61D1E4CA4E76E1F7368D59D8B75AB51E1DD0BCE67ED80D8119`. Its before/after analyzer hashes match. The QMP transcript and complete runner manifest are preserved in the run directory above. The runner reports exactly one guest boot and `candidateIntegrityPreservedAfterGuest=true`.

## Chronology and identity comparison

The pinned runtime source is `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. In that source, `ThreadStore::AttachCurrentThread()` calls `PalAttachThread()` before `Thread::Construct()`, before setting `TSF_Attached`, and before linking the thread (`threadstore.cpp:106–144`). `Thread::Construct()` sets `m_threadId` from `PalGetCurrentOSThreadId()` (`thread.cpp:260–276`). The target PAL's FLS read/set behavior is in `guidexos_nativeaot_pal_minwin_replacement.cpp:157–185`; the bridge stores values by the current `BridgeThreadState` and a generic LocalStorage index.

| Event | Thread identity | PAL slot | LocalStorage identity | Result |
|---|---|---:|---|---|
| PAL initialization | QEMU current OS thread later observed as 1 | — | — | `RhInitialize` returned success |
| PAL slot allocation | ThreadStore marker `pal_fls_allocate` | 0 | Allocation trace line for sequence 0 is missing; gate read later identifies slot 1, generation 1 | PAL allocation marker reports index 0 and success |
| ThreadStore initialization | Runtime instance `0x1004B6010`; ThreadStore `0x1004B7010` | 0 | — | Both pointers present |
| Earlier current-thread observation | Raw Thread pointer `0x3D77360` | 0 | — | `GetCurrentThreadIfAvailable` observation was false before module registration |
| PAL attachment attempt | No `pal_attach_enter`, `pal_attach_existing`, or `pal_attach_complete` record | 0 | No writer identity captured | Missing observation; do not infer non-execution from absence alone |
| FLS write | No `pal_fls_set_*` record | 0 | No setter slot/generation or result captured | Missing observation; cannot classify a failed write |
| FLS read at gate snapshot | `BridgeThreadState=0x3D77600`; OS thread ID 1 | 0 | Generic slot 1, generation 1 | LocalStorage result 0 (success); stored marker, cell, owner, and returned value are all null |
| ThreadStore snapshot | Raw/current Thread pointer `0x3D77360`; attachment `2` (present); `nativeThreadId=0`; `observedOsThreadId=1` | 0 | No matching writer context available | ThreadStore reports attached while PAL FLS read is empty |
| Gate 6 | Same snapshot | 0 | Reader context above | `palFlsValue=0`, `flsState=0`; safe stop `0x106`, no fail-fast |

The first **demonstrated discrepancy** is the gate snapshot: the ThreadStore fields say the current thread is attached while the active PAL getter successfully reads an empty LocalStorage cell for slot 0. The ordinary storage read did not fail. The snapshot's PAL thread-ID marker returns value 1 with result 0, while the stored `Thread::m_threadId` is 0.

## Case analysis

| Case | Finding |
|---|---|
| A — no PAL attachment write | Best fit. There is no setter record, and the gate read is the only readable bridge operation after the one consumed sequence. No PAL attach marker appears and neither bounded trace overflowed. This strongly narrows the boundary to a missing PAL assignment before the attached snapshot, but the missing attach-entry record is not by itself proof that the function did not execute. |
| B — write failed | Not demonstrated. The gate getter reports LocalStorage success. No setter result exists, so a write failure cannot be assessed. |
| C — context mismatch | Unresolved. The reader uses BridgeThreadState `0x3D77600` and OS ID 1; there is no setter context for comparison. |
| D — slot/generation mismatch | Unresolved. The reader uses generic slot 1, generation 1; no writer identity was captured. |
| E — storage lifetime failure | No evidence of a prior non-null value, detach, release, clear, or replacement. The trace cannot rule these out without a writer record. |
| F — thread identity or snapshot defect | Still plausible. The snapshot reports `nativeThreadId=0` against PAL OS thread ID 1, and the earlier current-thread observation was false. The gate checks PAL FLS before the later native-ID equality check, so the zero ID is not the direct gate-6 predicate; its relation to the missing assignment remains unknown. |
| G — invalid gate assumption | Not demonstrated. The gate reads the actual PAL FLS getter for the allocated slot. The pinned stock attachment contract requires PAL attachment before publishing `TSF_Attached`; no evidence shows that contract is intentionally deferred at this point. |

`nativeThreadId=0` is therefore a separate confirmed inconsistency and a possible related symptom, not a proven cause of the empty FLS cell. The evidence needed to settle that relation is the writer's PAL attach entry, bridge state pointer, OS thread ID, FLS set result, cell address/value, and LocalStorage slot/generation in the same trace.

## Next phase

Do not change the runtime from this evidence alone. The highest-value next step is to trace the exact path that changes `GetCurrentThreadIfAvailable()` from false to true across module registration and verify that it invokes the pinned `ThreadStore::AttachCurrentThread()` path. If that path is bypassed, the smallest repair is to route it through `PalAttachThread()` before `Thread::Construct()` and before setting `TSF_Attached`. If the runtime state is accurate but the snapshot is wrong, correct the snapshot source instead. Any further native boot requires a separate one-boot authorization and a fresh candidate guard.

## Closeout

The diagnostic candidate remains immutable and is verified after its one boot. No trace capacity increase, runtime attachment change, managed entry, Settings test, or push was performed. The only source/test change in this report is the one-line strict-mode runner initialization and its hosted regression assertion. The three user-owned `.obj` modifications remain unstaged.

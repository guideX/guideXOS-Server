# NativeAOT R28 GC-State Guard Discrimination

**Outcome E — `GC_STATE_GUARD_EVIDENCE_ADVANCED`.** The fresh R28 candidate captured a valid first-failure record at a reason-8 `RhpNewArray` guard, but that guard is the **pre-allocation** check. It ran before the pinned stock array allocator and before R28's discriminator-enabled **post-allocation** check. The captured record therefore does not identify whether the pre-allocation state read failed or one of its output pointers was null. No runtime cause is proven, and no speculative runtime fix was made.

Exactly one guest boot completed under QAC ownership. QAC release and the post-run zero-QEMU/zero-managed-session state were verified. The earlier guarded launch attempt stopped before QAC admission and did not boot a guest. No further guest run was made and nothing was pushed.

## What R27 established, and what R28 changed

R27's preserved fatal-marker snapshot has SHA-256 `F4A7B7A39E8D2A986D44E14760C0E885318167DC7BF891A555CB4BCE9A970FB9`; its corrected analysis has SHA-256 `CE34E51C3B130DC7AB897C0876CFE70459B4D53841AC1912D1E9E194F9CA6174`. R27 recorded reason `8`, Windows fast-fail code `7`, and caller return PC `0x1000F3A32`. Linked disassembly tied that PC to the second reason-8 site in the guideXOS `RhpNewArray` wrapper, after `guideXosStockRhpNewArray` returned and after the wrapper's null-result check. Its aggregate predicate was:

```cpp
guidexos_nativeaot_gc_read_state(...) != 0 ||
currentThreadAfter != currentThread ||
gcHeapAfter != gcHeap
```

Thus R27 proved the post-allocation aggregate guard was entered with a non-null result, but its legacy 80-byte record did not preserve the read status or identities. The terminal fast-fail code `7` is the generic `FAST_FAIL_FATAL_APP_EXIT` code, not the original reason or the failing operand. R27's exact evidence and source reconciliation are recorded in [the R27 report](NATIVEAOT_R27_PRELAUNCH_RECOVERY_AND_FIRST_FAILURE_QUALIFICATION.md).

R28 added the discriminator to that post-allocation guard. On a successful state read, it records thread and heap comparisons separately; a thread mismatch is the first subcondition when both identities differ. If the read fails, it records that subcondition and does not claim later comparisons. It retains the existing failure checks and fail-fast path. The schema-2 record uses 18 fixed 64-bit words (144 bytes): the original ten-word R25 record, eight guard-evidence words, and the publication marker last. The analyzer still accepts legacy schema-1/80-byte R25 records. Storage remains static and first-writer-owned; capture uses no allocation, locks, or logging and does not dereference captured identity values.

## R28 native result

The fresh candidate captured caller PC `0x00000001000F3C20`, mapped to `RhpNewArray` in `guidexos_nativeaot_platform.obj`. The bounded linked-image disassembly around that PC is:

```text
1000f3c16: call 0x1000f3450   ; recordAllocationStage
1000f3c1b: call 0x1000f2f80   ; R25 first-failure capture helper
1000f3c20: mov  %rax,%rdx     ; captured return PC
1000f3c23: mov  $8,%ecx       ; original reason 8
1000f3c28: call 0x1000f2f90   ; generic R25 fail-fast wrapper
1000f3c2d: int3
```

This is the earlier pre-allocation check at `guidexos_nativeaot_platform.cpp:34713–34722`:

```cpp
if (guidexos_nativeaot_gc_read_state(...) != 0 ||
    currentThread == 0 || gcHeap == 0) {
    ...
    guideXosFailFast(8u);
}
```

The guard precedes the direct call to `guideXosStockRhpNewArray` at line 34820. Consequently, this run did not call the stock array allocator, did not produce an allocation result, and never reached the post-allocation discriminator helper at `0x1000F3070`.

The schema-2 record is structurally valid and published. Its original reason is `8`, fast-fail code is `7`, and all eight discriminator words are zero. Those zeros are the untouched static fields: this earlier guard called the generic capture path, which has no pre-allocation discriminator data. They do **not** mean that the state read succeeded or that either identity comparison matched. The corrected offline analysis classifies the record as `GC_STATE_FAILURE_NOT_DISCRIMINATED`, with `firstFailedSubcondition="UNRECORDED"` and `discriminatorStatus="reason_8_failure_without_guard_details"`. The native wrapper's original `guardSubcondition="NONE"` result reflected this analyzer omission; the preserved record was not changed.

The pre-allocation predicate narrows the failure only to one of these evaluated terms:

1. `guidexos_nativeaot_gc_read_state` returned nonzero; or
2. after a successful read, `currentThread` was null; or
3. after a successful read, `gcHeap` was null.

The state reader returns `-1` when `ThreadStore::GetCurrentThreadIfAvailable()` is null, the thread has no allocation context, or `GCHeapUtilities::IsGCHeapInitialized()` is false. It otherwise writes the thread and heap outputs. The record does not say which reader failure condition or null-output term occurred. M1 (runtime initialization reached) and M12 (QAC cleanup) are true; M2–M11 are false. The Settings factory was not entered.

## Candidate and QAC evidence

The candidate was built from source HEAD `269dd58c91b477145678f478d1bd46b8e6cfdfb3`, after the R28 evidence-root compatibility fix. Its source tree has 347 candidate files; the candidate manifest verified integrity. It retains the real `GuideXosApplicationRegistry` selector-5 `ManagedSettingsCenter` factory, Workstation GC, no C188 image-backed arena, and the accepted R20 unwind implementation.

| Artifact | Identity |
| --- | --- |
| Candidate | `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r28-gc-state-guard-discrimination\candidate-20261115T041328158Z-136c106f7d4346b69ed7c9132e51eacb` |
| Candidate manifest SHA-256 | `9C63CF7525A990F90CECE5FC4D4164D3F386E058180BCD1FB703C23E44440537` |
| Candidate integrity SHA-256 | `5733E07381974E68E6AFC590D29805EFB51D35FE98558B3F811716C8F9960946` |
| R28 candidate manifest SHA-256 | `19610BA0F183A9F4AE954F8288ECDDE6DBD2C81B366224B6652E9F791D4E81B8` |
| R25 run evidence SHA-256 | `6F53C90D3190661D4FE989426C70CFDF14BF6411D71E46974089328987D4875D` |
| Original run analysis SHA-256 | `D0A4BFADAAA757F6C395FEC56AC6C2E0964B25429552404305E76B83B8E47441` |
| Corrected offline analysis | `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r28-gc-state-guard-discrimination\r28-offline-reanalysis-reason8.json` |

The first candidate was built from the initial R28 commit, but its guarded run was stopped before QAC reservation: the shared R26 precreated-evidence validator only accepted the legacy 80-byte capture configuration. The consumed prelaunch guard `attempt-r25-ce822e9dba2a4b1186d0d2e7c5a4b789.json` and its evidence are preserved; that attempt made zero QEMU launches and used zero QAC reservations. Commit `269dd58c` fixed the validator to accept a 144-byte R28 capture only when the capture config and both candidate manifests agree on schema, size, address, and R28 marker. A fresh candidate was then built from that commit and passed a clear QAC preflight.

The fresh candidate's single native run produced `GC_STATE_GUARD_EVIDENCE_ADVANCED`, `guestBootCount=1`, `runnerInvocationCount=1`, and `automaticRetry=false`. QAC reservation `393cf6146ab4449ab2a482d6fbe95401` was released with `qacReleaseVerified=true` and M12 true. Preserved before/after QAC status snapshots show the coordinator online, zero QEMU processes, zero reservations, and zero managed sessions. The candidate build log and run evidence are under the candidate evidence root above; the R25 record is at `0x000000010056D9A0`.

## R20 comparison

R20's accepted native run completed a real Workstation GC collection, moved the collection count from 0 to 1, retained a live managed object, and returned `1123`. Its accepted unwind adapter remains pinned at source hash `9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E`. The R28 candidate preserved that runtime foundation and the real Settings workload, but this R28 run failed earlier, in `RhpNewArray`'s pre-allocation check, before the stock allocator, any collection, managed entry, or Settings factory. The evidence establishes an ordering difference; it does not establish why the R28 state-reader condition differed from R20's successful path.

The fresh candidate build also replayed the hosted R20 unwind regression: 31 assertions passed, including the production-shared Windows AMD64 ABI adapter check (`caller=0x100001A8E`, caller SP delta `0xA0`, restored RBP delta `0x90`, null handler). This regression ran without another guest boot.

## Hosted verification

The candidate's preserved hosted build log captured 15 R28 tests; after native capture, the analyzer's undiscriminated-reason-8 case was corrected and covered by the current 16-test suite. That analyzer/test-only change did not alter or rebuild the runtime candidate. The post-capture source checks were:

| Check | Result |
| --- | --- |
| `python scripts/dotnet/test-c188r28-gc-state-guard.py` | 16 tests passed, including the Windows AMD64 MSVC classifier harness |
| `python scripts/dotnet/test-c188r25-failfast-origin.py` | 21 tests passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1` | 34 assertions passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r26-precreated-evidence-root.ps1` | 16 assertions passed |
| R26 first-failure fixtures; R27 prelaunch rehearsal | 13; 12 passed |
| R24 snapshot classifier and R24 analyzer | 14; 39 passed |
| R23 preparation; R22 bootstrap; R21 Settings linkage | 12; 29; 9 passed |
| R20 unwind regression in fresh candidate build | 31 assertions passed; no guest boot |
| ThreadStore readiness mode and telemetry; GC ownership; QAC admission | 7 and 6 assertions; hosted qualification passed; 35 cases passed |
| `git diff --check`, Python byte-compilation, and PowerShell parser checks | Passed |

The candidate's own hosted qualification captured R25 at 21 tests/34 assertions and R28 at 15 tests. Counts above distinguish those candidate-build results from the current post-capture 16-test R28 result.

## Repository state and next diagnostic

The implementation sequence began at `b74b29d4d2d5f5734cad021b2fa04c1140c385fc` and includes:

- `11326c928270a00f2a03d9a70ec3cc76297e5415` — add the R28 classifier, schema, candidate build/run path, and tests.
- `269dd58c91b477145678f478d1bd46b8e6cfdfb3` — bind and accept R28's 144-byte precreated capture evidence.
- `7bbf2112201e3848b1565af3698309f4aa8f3aa9` — classify a valid reason-8 record with empty discriminator fields as undiscriminated, not as a pass or unrelated record.

The following three preexisting modified object files remain outside the commits and retain their required SHA-256 values:

| Object | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CABF4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

If a later qualification needs the exact pre-allocation cause, add bounded capture of the state-reader return value and the two null checks at that earlier guard, keeping the safety predicate unchanged. Then build a fresh candidate and use a separately authorized one-boot QAC run. The R28 post-allocation discriminator cannot answer a guard that executes before its own state read.

# NativeAOT R27 Prelaunch Recovery and First-Failure Qualification

## Result

**Outcome B — `FIRST_FAILURE_CAUSE_PROVEN`, at the aggregate post-allocation GC-state guard.** One authorized guest boot captured a valid first-failure record. The caller resolves to `RhpNewArray`, and the exact linked call site identifies the post-allocation GC-state consistency guard. The evidence proves that the guard's combined condition fired. It does not identify which of the three operands failed, so the underlying GC-state discrepancy still needs a targeted capture.

The actual Settings factory was not entered. No NativeAOT or Settings source was changed for this run, no application or runtime binaries were rebuilt, and no second guest boot was attempted.

## Repository and provenance

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `f952e01e0f6bb52e400186df928582a394a5b9b2`
- Final HEAD: the commit containing this report and the R27 tooling changes; its exact ID is in the completion summary.
- No push was made.

The original frozen R25 candidate remains at:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\candidate-20261114T223556232Z-46e7ccf8b0a64e5b80c343f7d42b0c0e`

Its original candidate manifest SHA-256 is `161B4DC3A0E7168A041690917190ACE0DDB70D9139A7E3401752180BCE3E277F`, its integrity inventory SHA-256 is `7F17D22B3572F2AC0C0486785D91EEF2DE95BE7A7B223527C15ACDE5EEA5E798`, and its external R25 manifest SHA-256 is `B1A577D51AFDB3AE1E3072C4B295C18E8BCA82AB08D3C120A26F33A70798F0C0`. The 347-file inventory was retained.

## R26 prelaunch failure

R26 attempt identity `27f8538cfb4343c48e5f74a3b0db87ef` created a consumed one-boot guard at:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\one-boot-guards\attempt-r25-27f8538cfb4343c48e5f74a3b0db87ef.json`

The guard is attempt 1 of 1, `consumed=true`, and disallows retry. Its SHA-256 is `4B01D33A49B530E7427F3AF43902B6A1BAAFD606C6364C2A80384D07B912714F`.

The R26 parent had created the evidence root and capture configuration. The nested runner rejected that already-existing root at its initial lifecycle check before requesting QAC. R26 evidence has no reservation ID; its nested stderr contains the rejection; its QAC and QEMU logs contain no launch; and the later ownership census was clear. The R26 run record's `nativeExecutionAttempted=true` denotes that the host invoked the nested PowerShell runner. It does not mean QEMU or the guest ran. R26 is therefore a prelaunch tooling failure, not a NativeAOT failure. Its evidence and consumed guard were preserved and never reused.

## Fresh R27 authorization and candidate

The documented workflow allowed a distinct, path-bound candidate copy and a new attempt package while retaining the R25 binaries. R27 did not rename, delete, reset, or reuse the R26 guard.

The new candidate is:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\candidate-r27-20261115T002254837Z-a59fe4b0a51d47e2bb815647f27061ad`

The attempt ID is `a59fe4b0a51d47e2bb815647f27061ad`. The package is `candidate-r27-20261115T002254837Z-a59fe4b0a51d47e2bb815647f27061ad-r27-attempt-package.json`, SHA-256 `112A100D3CD7B2B72687E687B37C6F7A52BCB9722C4A24BFCB5284A61DA1D060`.

The package binds the original frozen R25 candidate and manifest to the consumed R26 guard and no-launch evidence. It records the new candidate's 347-entry inventory, corrected runner provenance, a distinct evidence root, a one-boot maximum, and no retry. The R27 candidate manifest SHA-256 is `03B8911431861AF745BF96CECB0FFDDAC0CDBA0AF03BAD9300BF4589FDFB1C42`; its integrity inventory SHA-256 is `63B41368F86B4E3D1F53C67C90594C4E80FD1286E70BE07BBB7129DC8AFDFC6F`.

The candidate was rechecked after execution: all 347 expected files were present and matched; no inventory entry differed. The kernel remained `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B`, the managed ELF remained `CB12F45663C6B45EA1DA47E5FFF9C79CC1C6A46246D6CE7F6D33FAEEBDAC4F38`, and the runtime-pack manifest remained `62656660363842F604148542C06A7E7220C2EC2D49EB53633919F4B2FF9234DE`. The package records `binaryRebuildPerformed=false`.

At staging, the package recorded source HEAD `f952e01e0f6bb52e400186df928582a394a5b9b2` and hashes for the R27 stager, R27 admission runner, R25 one-boot runner, R24 orchestrator, corrected nested Settings handoff runner, evidence-root validator, and analyzers. The R27 admission runner invoked the R25 runner once after admission. The archived run record binds that invocation to runner SHA-256 `2BF6E4AB6D4892BE62F562F69A01B1995C65A15139559ABD8E2167CC86BE9BEE`; the corrected nested runner provenance in the package is `4833E85AADB8D14A1A1C8A753BC5E0BF6F3095B3D06CA74822A346A29880D00C`.

After the guest stopped, offline tooling was corrected to accept QEMU HMP memory-dump addresses without a `0x` prefix and to handle a missing optional `caller` field in older wrapper output. These edits did not alter the candidate or its archived run package. The captured raw snapshot and original R25 analysis remain unchanged.

## Prelaunch recovery and rehearsal

The corrected nested runner now accepts an evidence root precreated by its authorized parent only when the root is a non-reparse sibling, ownership matches the current identity, contents are empty or consist of the candidate-bound R25 capture configuration, and the parent supplies a consumed one-boot guard that binds the exact candidate, inventory, and evidence path. The production parent passes that guard path. A consumed candidate attempt remains blocked on repeat invocation.

The actual nested runner entry point was exercised with `-PrelaunchRehearsal`; it exited before QAC or QEMU. The 12 assertions covered the authorized precreated configuration, authorized empty root, runner-created absent root, unexpected contents, candidate/config mismatch, historical guard replay, unsafe path, foreign owner rejection, and production parent-to-child authorization handoff.

The complete R27 package preflight passed before QAC admission: 347 files and linked analyzer checks passed; no guard was created and no QEMU was launched. The preflight command was:

`pwsh -NoProfile -File scripts/dotnet/run-c188r27-first-failure.ps1 -AttemptPackagePath E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\candidate-r27-20261115T002254837Z-a59fe4b0a51d47e2bb815647f27061ad-r27-attempt-package.json -PreflightOnly`

## Hosted qualification

All listed commands were offline or hosted checks; none of these tests requested a guest boot.

| Command | Result |
| --- | --- |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r25-failfast-origin.ps1` | 34 assertions passed |
| `python scripts/dotnet/test-c188r25-failfast-origin.py` | 21 tests passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r26-precreated-evidence-root.ps1` | 13 assertions passed |
| `python scripts/dotnet/test-c188r26-first-failure.py` | 13 tests passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1` | 12 assertions passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r24-initializemodules-snapshot.ps1` | 14 assertions passed |
| `python scripts/dotnet/test-c188r24-initializemodules.py` | 39 assertions passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r23-settings-module-preparation.ps1` | 12 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1` | 29 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r21-settings-factory.ps1` | 9 cases passed |
| `python scripts/dotnet/test-c188r20-iterator-advancement.py --repo-root D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT --candidate-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z --run-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa --saved-analysis E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa\managed-gc-root-analysis.json --runtime-source D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_amd64_unwind_primitive.cpp --output-directory E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\r27-r20-unwind-regression-20261114-r2` | 31 assertions passed; no guest boot |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11-runner-predicates.ps1` | 30 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11-runtime-lifecycle.ps1` | 7 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11-output-bounds.ps1` | 6 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-keyboard-contracts.ps1` | 27 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11r2-runner-predicates.ps1` | 24 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r11r4-qac-ownership-predicates.ps1` | 15 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r1-settings-handoff-predicates.ps1` | 37 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r2r1-settings-dispatch-predicates.ps1` | 12 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r3-lifetime-predicates.ps1` | 13 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4-factory-predicates.ps1` | 9 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r1-link-boundary.ps1` | 9 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` | 35 cases passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5-gc-ownership.ps1` | Hosted production ownership proof passed |
| `pwsh -NoProfile -File scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1` | Runner contract passed |

One additional legacy integration check, `test-c188r12r4r5r2r3-transition-runner-contract.ps1`, failed its initial shared-runner acceptance assertion against the preserved R12 candidate under `out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z`. Direct hosted-only invocation reported `Transition analyzer hash mismatch` before its success marker. This archived candidate has a 1,482-entry inventory. The failure occurred before QAC status/admission, guard creation, or QEMU; the candidate was not modified. This R12 fixture result is separate from the R27 candidate and the passing R27 nested-runner rehearsal.

## QAC admission and single guest boot

An earlier census found an unrelated project's QEMU process. It was left running and was not terminated. A later bounded R27 admission check, captured at `2026-11-15T00:23:30.1811992Z`, returned `CLEAR`: coordinator online, status/list consistent, complete host QEMU census with zero processes, zero reservations, zero managed sessions, and exclusive authorization. The preflight record states `reservationRequested=false`, `attemptGuardCreated=false`, and `qemuLaunched=false`; it created no guard before admission.

The R25 runner then created and consumed the new one-boot guard for run identity `da528b79efbd48efb2b1bde5e87f419f`:

`E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\one-boot-guards\attempt-r25-da528b79efbd48efb2b1bde5e87f419f.json`

The guard is attempt 1 of 1, `consumed=true`, and retry is false. Its SHA-256 is `B23960D13030AE9837A889381398F6D64D41BFDD5187B117C6FE502599221754`.

QAC granted reservation `44a5c86d31a14815b2410d8b3350a923`, launched QEMU PID 15764 at `2026-11-15T00:23:46.4569888Z`, and recorded QEMU exit code 0 at `2026-11-15T00:26:48.0126109Z`. QAC released the reservation. The guest did not reach its kernel input-loop readiness checkpoint; the nested runner captured the first-failure fatal marker and readiness-timeout snapshots, then exited with code 1. There was one runner invocation, one guest boot, and no automatic retry.

The R27 wrapper retains its original structured outcome string `INITIALIZEMODULES_EVIDENCE_BLOCKED` and its parser-era `r25FirstFailureRecordValid=false`. Those fields reflect the pre-fix HMP parser and optional-property error, not the corrected post-run analysis below. They were not rewritten.

## First-failure record and source operation

The immutable fatal-marker snapshot is `r25-failfast-origin-fatal-marker-snapshot.json`, SHA-256 `F4A7B7A39E8D2A986D44E14760C0E885318167DC7BF891A555CB4BCE9A970FB9`. QEMU HMP printed valid memory values on lines whose address prefixes omitted `0x`; the original analyzer required that prefix and left its archived analysis invalid. Offline, a separate normalized input was created at `r27-normalized-first-failure-analysis-input.json` (SHA-256 `ED448C4A85EDBA764DC218F1AD90B02C6668742AF52C6FBEACB7C110DA843A3E`). The raw QMP response and original analysis were left untouched.

The corrected R25 analyzer returned `PASS` for the exact R27 ELF and this normalized snapshot. Its separate output is `r27-first-failure-origin-analysis.json`, SHA-256 `CE34E51C3B130DC7AB897C0876CFE70459B4D53841AC1912D1E9E194F9CA6174`. The ten-word record gives:

| Field | Captured value |
| --- | --- |
| Original source reason | `0x8` |
| Terminal fast-fail code | `0x7`, `FAST_FAIL_FATAL_APP_EXIT` |
| First-failure caller return PC | `0x00000001000F3A32` |
| Fail-fast implementation | `0x00000001000F2F70` |
| Initialization stage | `1`, written immediately before `InitializeModules` and retained until it returns |
| Publication marker | `0x5055424C49534844` |

The candidate map SHA-256 is `444CBDD8827196415A024E4A25828FC475713DC5C1C422E8CEF1639B4EF326A1`. It maps the caller to `RhpNewArray` in `guidexos_nativeaot_platform.obj`. The candidate ELF hash is the R27 hash above. Its linked disassembly, archived at `r27-first-failure-caller-disassembly.txt` (SHA-256 `E5EF6DAE0F91D241EFE3A164CFDA2620EFAA43AB04F3FE83CA30BE51DDDDCD71`), confirms the capture-helper call at `0x1000F3A2D`, return PC `0x1000F3A32`, immediate reason `8`, and following call to the R25 fail-fast wrapper.

The pinned source `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp` has SHA-256 `B8434CB122DF0C511AC307676FEB412B9EEE278B39ABE5F6AF453257C1BB1996`, matching the candidate manifest. In `RhpNewArray`, the first reason-8 site checks GC state before allocation. The captured return PC instead matches the second reason-8 site at source lines 34783–34793, after `guideXosStockRhpNewArray` and a second GC-state read. That site's condition requires the read to succeed and requires the current-thread and GC-heap identities to remain unchanged:

`guidexos_nativeaot_gc_read_state(...) != 0 || currentThreadAfter != currentThread || gcHeapAfter != gcHeap`

The captured call site proves this aggregate post-allocation guard fired. The 80-byte first-failure record does not preserve which operand was true. Therefore this report proves the guard-level contract violation, but does not claim whether the state read failed, the thread identity changed, or the heap identity changed. The separate source reconciliation, `r27-postrun-contract-analysis.json` (SHA-256 `4D581FD458A978A9630E2D69537F551B007B8575189DAA3C6D41121F4E40BED1`), records that distinction and binds the interpretation to the immutable snapshot and candidate hashes. The terminal CPU snapshot (`RIP=0x1000F2F77`, `RCX=7`) is only the later fatal loop; `RCX=7` is not the original reason.

## Initialization milestones

| Milestone | Result | Evidence |
| --- | --- | --- |
| M1 Runtime initialization reached | Yes | Runtime seam, host initialize-begin, and startup markers are present in serial output. |
| M2 Generated InitializeModules call site reached | Yes | C188B007 is emitted immediately before the generated five-argument call. |
| M3 Generated initializer entered | Unconfirmed | The record remains at stage 1 and the terminal CPU snapshot is outside the initializer. No compatible in-initializer boundary was captured. |
| M4 RhpRegisterOsModule entered | Yes | C188B006 is emitted immediately before the real registration call. |
| M5 Registration returned | Yes | C188B007 follows the registration call's success path; failure would take the fail-fast path before that marker. |
| M6 First-failure record captured | Yes | The live fatal-marker snapshot contains a complete published record; offline normalization only repaired HMP line parsing. |
| M7 Original caller resolved | Yes | The return PC follows the capture-helper call in the exact R27 ELF and maps to RhpNewArray. |
| M8 Original source reason identified | Yes, at guard level | Numeric reason 8 maps to the post-allocation GC-state guard; its individual failing operand is unknown. |
| M9 Initialization returned | No | No initializer return marker or CPU boundary was captured. |
| M10 Genuine Settings factory entered | No | The guest timed out before the managed input/Settings action; factory evidence is absent. |
| M11 Runtime and guest cleanup completed | No | The guest stopped in the fatal path; only the host/QAC cleanup completed. |
| M12 QAC ownership released | Yes | QEMU exited with code 0 and QAC recorded reservation release. |

## Cleanup, preservation, and next action

Post-run QAC status and list both reported the coordinator online, zero QEMU processes, zero reservations, and zero managed sessions. A fresh host process census was empty. The R27 reservation was released and no other project's process was terminated.

The following pre-existing modified object hashes are unchanged and remain outside the commit:

| Object | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

The real Settings factory and accepted R20 unwind implementation were not modified. The next native qualification should use a fresh package and one-boot guard only if another guest run is needed, and should capture the post-allocation GC-read result plus the before/after current-thread and GC-heap identities at this exact guard. That will distinguish the three possible failing operands without repeating the same uninformative run.

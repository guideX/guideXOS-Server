# NativeAOT R26 First-Failure Native Qualification

## Result

**R26 disposition: `FIRST_FAILURE_EVIDENCE_INCOMPLETE` at prelaunch.** QAC admission was clear, but the established nested runner rejected its precreated evidence directory before invoking QAC. No QAC reservation or guest boot is evidenced, and no first-failure record exists. The one-boot guard was nevertheless consumed by the outer runner, so R26 made no retry. This is outside outcomes A–F; outcome G's guest-executed condition was not met.

The runner defect was corrected offline and covered by a focused hosted test. The corrected runner was not used to start another guest.

## Repository and candidate identity

- Starting HEAD: `c1b568f0eb41dedc463b2c9dd86a49bcb4602c81`
- Branch: `v1.1_DOTNET_SUPPORT`
- Final HEAD: the R26 commit containing this report and the tooling correction; the commit hash is included in the completion summary.
- The three pre-existing modified `.obj` files remain modified and retain the R25-recorded SHA-256 values listed below.
- No push was made.

The exact frozen candidate remains:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\candidate-20261114T223556232Z-46e7ccf8b0a64e5b80c343f7d42b0c0e
```

| Artifact | SHA-256 |
| --- | --- |
| R25 candidate manifest | `161B4DC3A0E7168A041690917190ACE0DDB70D9139A7E3401752180BCE3E277F` |
| Candidate integrity inventory | `7F17D22B3572F2AC0C0486785D91EEF2DE95BE7A7B223527C15ACDE5EEA5E798` |
| R25 external manifest | `B1A577D51AFDB3AE1E3072C4B295C18E8BCA82AB08D3C120A26F33A70798F0C0` |
| Kernel | `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B` |
| Managed ELF | `CB12F45663C6B45EA1DA47E5FFF9C79CC1C6A46246D6CE7F6D33FAEEBDAC4F38` |
| Runtime-pack manifest | `62656660363842F604148542C06A7E7220C2EC2D49EB53633919F4B2FF9234DE` |

The complete inventory still contains 347 files: all 347 expected paths were present and every file hash matched after the R26 attempt. The `unverifiedRootObjectInputsConsumed` flag remains `false`. Kernel, ELF, runtime-pack manifest, candidate manifest, and inventory hashes still match. The candidate was not rebuilt, restaged, or edited.

Before R26 admission, no guard for this candidate and no R25 guest evidence directory existed. The prior R25 run `ca75205e465047dc9c27bed88edec89e` was deferred before boot. R26 created guard `attempt-r25-27f8538cfb4343c48e5f74a3b0db87ef.json`; it records attempt 1 of 1, no retry, and `consumed=true`. The guard is preserved. This candidate cannot be run again under the same one-boot policy.

The candidate manifest identifies `GuideXosApplicationRegistry` selector 5 as the genuine `ManagedSettingsCenter` factory and `ManagedSettingsCenter___ctor_0`. It records Workstation GC, no image-backed arena, an isolated kernel build directory, and the linked R20 `guideXosRtlVirtualUnwind` adapter. The linked unwind object and its recorded hash remain present; the candidate's preserved R20 regression artifact reports 31 passing assertions. No R20 runtime or candidate inputs changed in R26.

The three pre-existing modified objects retained these SHA-256 values:

| Object | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## First-failure capture review

With the R25 profile enabled, every `guideXosFailFast(reason)` macro call evaluates `guideXosNativeAotR25CaptureCallerReturnAddress()` and passes that value with the original reason to the R25 wrapper. The no-inline helper returns `_ReturnAddress()`, which is the return PC at the helper call inside the original caller function. It does not return the wrapper's return address. The candidate analyzer verifies that the captured PC follows a direct `E8 rel32` call to the capture helper in the exact candidate ELF.

The first event wins an interlocked compare/exchange claim. The producer writes the fixed 80-byte record containing the source reason, fast-fail code, caller return PC, fail-fast implementation address, initialization stage, flags, and publication marker. The marker is published last. Later fatal events call the terminal path without replacing the record. UART output checks the transmit-ready bit before each byte and may stop early; it does not wait for output.

The capture path performs no allocation, lock acquisition, managed execution, GC/ThreadStore lookup, stack walk, or transition-frame access. The record is static writable memory, so it remains available after entry to the terminal `__fastfail(7)` loop if QEMU memory capture succeeds. The R25 guest runner pauses through QMP before reading registers, the ten record words, and up to eight stack words. The old R25 analyzer verifies the record and caller call instruction; the R26 analyzer additionally reconciles record, CPU, serial, and milestone evidence.

The record preserves the numeric `guideXosFailFast(reason)` argument. The value 7 is the terminal Windows `FAST_FAIL_FATAL_APP_EXIT` code and is not the original source reason. Candidate map and ELF data can attribute a valid captured PC to a function and object. They do not supply a source line or prove the violated contract. If an attributed function is an intermediate helper, dynamic caller context may remain unresolved because R25 deliberately captures no thread or transition state.

The absence rules are conservative: invalid or partial words are invalid; an unmapped caller remains unattributed; a fatal UART marker with an invalid record is contradictory; and no record snapshot does not create a caller. The R26 analyzer reports the furthest execution milestone supported by the candidate map, CPU boundary, serial markers, and runner evidence. Stage 2 means “immediately before or inside `InitializeModules`”; without a CPU boundary or return marker it does not establish that the generated initializer entered, nor whether `RhpRegisterOsModule` returned.

No R26 guest record, CPU snapshot, serial log, or stack capture was produced. There is no native fast-fail category, original reason, caller, or execution milestone to attribute in this run. The existing R24 `RCX=7` remains only the previously classified terminal fast-fail category.

## Offline analyzer and packaging rehearsal

The frozen R25 analyzer and runner source hashes matched the candidate before the QAC attempt. R25 linked verification returned `PASS` for the record at `0x00000001001E14E0`, the capture helper at `0x00000001000F2F80`, and the fail-fast implementation at `0x00000001000F2F70`.

R26 added a separate offline analyzer at [analyze-c188r26-first-failure.py](../../scripts/dotnet/analyze-c188r26-first-failure.py), leaving the R25 analyzer and frozen candidate unchanged. Its 13-case rehearsal passed:

| Case | Rehearsal result |
| --- | --- |
| Valid record and distinct source reason / fast-fail category | Pass |
| No failure with verified initializer return | Pass |
| Valid record with unknown caller | Pass; no attribution claimed |
| Invalid caller address | Pass; rejected |
| Known caller mapped to the candidate function and object | Pass |
| Record absent because the guest was not launched | Pass; no caller invented |
| Partial or unpublished record | Pass; rejected |
| Contradictory record, CPU, and serial evidence | Pass; classified incomplete |
| Failure in native-module preparation | Pass |
| Stage 2 ambiguity before/inside `InitializeModules` | Pass; M3 not claimed |
| Failure after initializer return and module-registration sequence | Pass |
| Guest timeout without a valid record | Pass; classified incomplete |
| Post-QEMU artifact-hash package serialization using fixtures | Pass; no guest launched |

The separate PowerShell runner-root regression passed 6 assertions. The offline package fixture hashed its serial and snapshot files, serialized and reloaded the post-QEMU manifest, and checked QEMU exit and QAC release fields. It did not write into historical evidence or launch QEMU.

## Hosted qualification

All results below were run during R26, except the candidate's preserved R20 regression artifact, which is explicitly identified above.

| Coverage | Actual R26 result |
| --- | --- |
| R25 Python tests | 20 passed |
| R25 PowerShell assertions | 33 passed |
| R25 linked candidate analyzer | Pass |
| R26 analyzer and packaging rehearsal | 13 passed |
| R26 precreated evidence-root regression | 6 assertions passed |
| R24 initializer checks on the R25 link | 34 passed; shared call offset `0x225` |
| R24 snapshot checks on the R25 link | 14 passed |
| Preserved R24 initializer checks | 39 passed; shared call offset `0x211` |
| Preserved R24 snapshot checks | 14 passed |
| R12 settings handoff / dispatch / lifetime / factory / link boundary | 37 / 12 / 13 / 9 / 9 passed |
| R12 QAC admission / semishared | 35 / 30 passed |
| R11 runner / R11R4 QAC ownership | 30 / 15 passed |

R20 and R23 runtime regressions were not rerun: the candidate and their linked inputs were unchanged, and the R26 source correction was limited to the host runner's precreated evidence-root handling. No unrelated NativeAOT audit was performed.

## QAC admission and prelaunch defect

The R26 run identity is `27f8538cfb4343c48e5f74a3b0db87ef`. QAC was online and responsive. The complete preflight status reported `QEMU processes: 0 (0 foreign)` and `Managed sessions: 0; queued: 0; blocked: 0`; the independent list reported zero reservations and zero managed sessions. The saved status and list were internally consistent, so preflight was `CLEAR`.

The outer R25 runner then created the candidate-bound one-boot guard and precreated the boot evidence directory to write `r25-failfast-capture.json`. Its nested `run-c188r12r1-settings-handoff.ps1` rejected that same directory with `Guest evidence root already exists` before copying candidate media or invoking the QAC CLI. The nested runner stdout was empty; stderr contains the rejection. There is no reservation ID, QAC request/release marker, QEMU child evidence, serial file, QMP log, or CPU snapshot. The boot evidence directory contains only the R25 capture config and the outer runner's after-run QAC status/list snapshots.

The outer runner reports `nativeExecutionAttempted=true` because it launched the nested PowerShell process; that field does not prove that QAC or QEMU started. The R26 analyzer classifies this evidence as `guest_not_launched_runner_rejected_evidence_root`, with no fast-fail category, reason, caller, or guest milestone.

After the failed attempt, QAC status and list both succeeded and remained clear: coordinator online, zero QEMU processes, zero reservations, zero sessions, zero queued or blocked work. A separate host process census found zero QEMU processes. `qacReleaseVerified=false` for this run because no reservation was acquired and no reservation release can be attributed. The final global process census is clean.

## Tooling correction

The shared R12 handoff runner now permits a precreated evidence directory only when it contains exactly one `r25-failfast-capture.json` file whose schema, 80-byte record size, address format, and candidate-manifest SHA-256 validate. All other preexisting directories still fail closed. The six-assertion R26 PowerShell test covers the valid config, extra file, wrong manifest hash, wrong record size, wrong schema, and missing directory.

This is a post-candidate host-runner correction. The R25 candidate manifest recorded the old `run-c188r12r1-settings-handoff.ps1` SHA-256 as `2F776311CFF96FCAD6B714D6AFF638C42AA38FC7F3B6DEBD1D8A684DA1F0BCDD`; after the correction the current source hash is `0B4624585BDF3DF0AB7CFF7BFB936DED3FFBB58C2413EDBD984E9D388088842A`. This does not change any candidate file or binary hash. The source delta is recorded here rather than presented as the original runner version.

The consumed R26 guard was not reset or reused. The corrected runner received hosted regression coverage only; it was not used for another native boot.

## Candidate, evidence, and working-tree preservation

- R25 candidate identity and its 347-file integrity inventory are preserved.
- The R26 guard is preserved in its consumed state; there is no retry authorization.
- The original R25 deferred evidence and historical guest logs were not modified.
- The new R26 attempt artifacts are under `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r25-failfast-origin\` and remain separate from the frozen candidate files.
- The three pre-existing `.obj` files remain modified with their recorded hashes; no reset, clean, or stash was performed.
- R26 report and tooling corrections are committed locally; nothing was pushed.

## Recommended next phase

Do not retry the consumed R25 guard. For a future native attempt, preserve this R25 candidate and prelaunch evidence, use the corrected runner, and create a separately identified, fully inventoried one-boot package with explicit parent hashes. Run one fresh QAC admission check and only then authorize a single boot. The next result should be classified from QAC reservation evidence and the guest's actual serial, CPU, record, and runtime-state captures.

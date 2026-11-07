# C188R12R4R5R4R5R2R2R4R2R3R2 — QAC Transition Profile and Native Guest Qualification

## Outcome

**Outcome B — `TRANSITION_NATIVE_FAILURE_BOUNDARY_PROVEN`.** One QAC-owned native QEMU guest boot reached the transition-attribution preflight and stopped safely before `InitializeModules`. The valid guest record identifies `GX_TRANSITION_GATE_PAL_FLS` (`gate=6`): ThreadStore exposed a current thread while the PAL FLS cell was empty. This proves the earliest blocked boundary in this candidate; it does not identify why those states disagree or establish an underlying CLR/PAL root cause.

The guest produced no `0x50414C06` fail-fast. `InitializeModules`, the Settings factory, and the persistent composite lifetime transition were not reached. The candidate itself intentionally returns before `ManagedMain`, so this candidate cannot prove Settings-factory behavior even if its transition gate is later passed.

## Repository and preservation

| Item | Value |
|---|---|
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `982a1a7c0c556460d4ab94b3fe2f3a910b04c907` |
| HEAD at guest launch | `982a1a7c0c556460d4ab94b3fe2f3a910b04c907` |
| Ending HEAD | The closeout commit containing this report; its exact SHA is recorded in the final repository status for this task. |

The three pre-existing modified object files remain modified and unstaged. Their hashes still match the R1 report:

| File | SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

No CLR, PAL, managed application, production runtime, candidate binary, or candidate manifest was edited. No reset, clean, or push was performed.

## Original R1 rejection and correction

R1 recorded this exact failure at `scripts/dotnet/run-c188r12r4r5-gc-ownership-qac-guest.ps1:77`:

```text
CandidateRoot must be a unique candidate below D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c188r12r4r5-gc-ownership-proof.
```

The candidate manifest selected mode `NativeAotInitializeModulesTransitionAttribution`, which the shared runner did not recognize. The runner therefore chose its generic GC-ownership phase root, while `Test-QacCandidatePath` correctly required the candidate to be beneath the selected profile's authorized root. The failure happened before QAC census, guard creation, or QEMU launch; it was a missing profile mapping, not a candidate layout or integrity failure. See `NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R1_TRANSITION_GUEST_QUALIFICATION.md` for the prior evidence.

The shared runner now recognizes the explicit transition mode and maps it to `out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution`. The admission module validates the canonical candidate boundary, profile metadata, pinned runtime identity, candidate manifest, 1,482-entry inventory, staged kernel/ramdisk/EFI hashes, hosted results, transition call-site map, prerequisite matrix, phase plan, and one-boot template before QAC admission. It continues rejecting outside paths, reparse points, malformed manifests, missing files, hash mismatches, and unlisted candidate payload.

The runner creates a unique per-run correlation ID and run directory outside the candidate. It copies the three-file candidate ESP into a private writable run directory and copies QEMU's UEFI variables template into a separate writable pflash image. The firmware code drive remains read-only. The QEMU package's x86_64 code and i386 variables images are the installed pair documented by the [QEMU x86_64 firmware descriptor](https://github.com/qemu/qemu/blob/master/pc-bios/descriptors/60-edk2-x86_64.json).

The hosted-only branch exits before QAC commands and the one-boot guard. After the guest attempt, hosted preflight remains usable for inspection; it never creates or overwrites the already materialized guard. Live mode still rejects that guard before a second QAC invocation.

## Candidate and integrity

Candidate:

`out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z`

| Item | SHA-256 / result |
|---|---|
| Profile | `NativeAotInitializeModulesTransitionAttribution` |
| Candidate manifest | `3880A326C5BDAC0E5DCE789B024BB63C095C84A5CB79B728A1F157A87A349057` |
| Integrity inventory | `16DB9CF39CAB5EB5FF5E86BB207A3E346480537374898FA2C01EDB274E8E4FBC` |
| Diagnostic NativeAOT ELF | `98B621619530EC75916B9D03302BAD03090C65188056DAAF687836DDD9ACF6DA` |
| Diagnostic kernel ELF | `061C3FAADC6645728A22B69B9248A65434CEA7A73119E8FD9CA1C2BF7201B7C0` |
| Candidate/source ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Transition call-site map | `643259EDC9D4FC7EDE8E6132066071B90A82945A806C08ADD98A73EB3CF45222` |
| Runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Integrity after guest | **PASS — 1,482/1,482 entries, 0 mismatches, 0 unlisted payload files** |

The candidate manifest and inventory hashes remained unchanged after the guest. The protected source ramdisk and QEMU firmware code/variables templates also retain their recorded hashes. The per-run writable variables image began as an exact 540,672-byte copy of the template and ended with SHA-256 `C957AE1F9C2D6A171245A72110EBEE8548B6EC2DC579C6A70B731AB539371F40`, confirming QEMU wrote only to the run-owned image.

The build-time candidate manifest still says `qemuLaunched=false` and `qacReserved=false`; those fields describe candidate preparation. The one-boot guard and run records outside the candidate contain the actual execution evidence.

## Hosted runner regression

All hosted runs used PowerShell 7 (`pwsh`) and exited successfully. Hosted contract assertions exercise the runner without QAC reservation or guest execution.

| Command | Result |
|---|---|
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r2r3-transition-runner-contract.ps1` | **PASS, 17 assertions.** Accepted the real candidate; verified 1,482 inventory entries, ESP and variables isolation, no QAC/QEMU/guard action in hosted mode, path/manifest/integrity/artifact rejection, prep-failure safety, analyzer path identity, runtime-start marker, admission/ownership/QMP ordering, and baseline preservation. |
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r2r3-transition-attribution.ps1 -ProductionNativeAotManifest 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z/production/nativeaot-build/runtime-pack/runtime-pack.manifest.json' -ProductionNativeAotMap 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z/production/nativeaot-build/nativeaot/artifacts/HostLogProof.map' -ProductionApplicationManifest 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z/production/nativeaot-build/production-nativeaot-qualification.json' -CallSiteMapPath 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z/transition-callsite-map.json'` | **PASS, 20 transition fixtures.** Output: `out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/hosted-20261107T043637139Z`. The fixtures are synthetic/source-backed hosted evidence, not guest acceptance. |
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1` | **PASS, 32 QAC admission cases.** Output: `out/dotnet/c188r12r4r5r4-threadstore-readiness/hosted-qac-admission-20261107T043545867Z`. |
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1 -CandidateRoot 'out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/candidate-20261106T042749067Z'` | **PASS.** Existing safe-handoff runner contract confirmed capacity metadata, census/guard/launch order, QAC child ownership, QMP shutdown and release verification, and analyzer/wrapper identity. This is a hosted source/contract regression, not a second guest run. |
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1` | **FAIL at `candidate-pinned safe-handoff analyzer matches`.** The archived safe-handoff candidate pins analyzer SHA-256 `49ED2A111698485096FF97B1D30CB70C860CA1CDF10408DC87EACFD1380288F7`; the current analyzer is `99F6EE403B0F5F0B95C88A380A453C8BDC172DE901C2138715200050454111E0`. The failure occurs in the existing candidate's artifact-identity check before any QAC or QEMU operation. Both stored safe-handoff candidates have this same stale pin. No candidate or analyzer was changed to mask the mismatch. |

The transition-runner hosted contract's final preflight evidence is under `out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T045130254Z-d0214d915d8f48eea8e0ac5c5528133b/hosted-preflight.json`: it records `qacQueried=false`, `qacReserved=false`, `qemuLaunched=false`, `attemptGuardCreated=false`, and `guestBoots=0`.

The transition candidate passes the safe-handoff shared-runner contract, but the archived `NativeAotSafeHandoff` candidate acceptance remains blocked by its stale analyzer hash. This limitation is separate from the transition guest result and should be reconciled in a future safe-handoff candidate refresh.

### Post-run finalization defect and recovery

The sole live invocation launched and completed the guest, then exited with code 1 during runner post-processing. Under strict mode, the runner read `$candidateManifest.transitionAnalyzer`, a path property absent from the manifest; the analyzer identity is stored as a hash under `artifactHashes.transitionAnalyzer`. QAC had already released its reservation and the raw serial/QMP/ownership evidence was complete. I ran the existing analyzer directly against that preserved serial and candidate call-site map; this did not access QAC or launch a guest. The runner now resolves the analyzer relative to its script directory, whose SHA-256 is pinned by candidate metadata. The hosted runner contract was rerun afterward and passed 16 assertions. The failed post-processing did not consume another boot or cause a retry.

## QAC reservation and native launch

Before launch, the QAC CLI reported coordinator online, `Reservations: 0`, `Managed sessions: 0`, and `QEMU processes: 0 (0 foreign)`. A separate Windows QEMU process census was empty. The run had no existing one-boot guard. The shared runner repeated the QAC status/list, Windows process census, and reservation-inspection checks immediately before guard creation.

| Item | Captured value |
|---|---|
| Qualification correlation ID | `C188R12R4R5R2R3R1-20261107T043814194Z-4f9f1e5571da4d1298b3994c0c0f07ad` |
| QAC reservation | `5f1d27d0e90b4d64bc6c2707712b0842` |
| QAC owner PID / user | `25048` / `LEON2_RDT\guideX` |
| QEMU child PID / parent PID | `22632` / `25048` |
| QEMU executable SHA-256 | `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02` |
| QMP port | `56303` |
| Boot timeout / admission window | 180 seconds / 30 seconds |
| One-boot guard | Created with `CreateNew`; `maxGuestBoots=1`, `automaticRetry=false` |
| QEMU start / exit | `2026-11-07T04:38:19.5302461Z` / `2026-11-07T04:38:30.8568375Z` |
| QEMU exit code | `0` |
| Release verification | QAC reservation released; post-run status/list showed 0 reservations and 0 managed sessions; Windows QEMU census showed 0 processes |

The QEMU argument vector recorded in `qac-run-arguments.json` and the inspected QEMU process was:

```text
qac run --project guideXOSServer --mode acceptance -- "C:\Program Files\qemu\qemu-system-x86_64.exe" -accel tcg -machine pc -smp 1 -drive "if=pflash,format=raw,readonly=on,file=C:\Program Files\qemu\share\edk2-x86_64-code.fd" -drive "if=pflash,format=raw,file=<runRoot>\uefi-vars.fd" -drive "file=fat:rw:<runRoot>\esp,format=raw,if=ide,index=0" -m 1024M -vga std -display none -serial "file:<runRoot>\serial.log" -qmp tcp:127.0.0.1:56303,server,nowait -boot order=c -no-shutdown -rtc base=utc,clock=host
```

`<runRoot>` is `out/dotnet/c188r12r4r5r4r5r2r2r4r2r3-transition-attribution/qac-guest-transition-attribution-C188R12R4R5R2R3R1-20261107T043814194Z-4f9f1e5571da4d1298b3994c0c0f07ad`. The ESP argument points at the per-run copy. QMP observed `status=running`; after the transition safe-stop record appeared, the runner sent QMP `quit` to its owner-verified child. The QMP log records `SHUTDOWN` with reason `host-qmp-quit`; QAC then recorded child exit code 0 and reservation release.

Raw evidence is retained in that run directory: `serial.log`, `qmp.log`, `qac-run-arguments.json`, `qac.stdout.log`, `qac.stderr.log`, before/during/after QAC snapshots, the one-boot guard at the phase root, ownership identity, local child identity, `transition-attribution-analysis.json`, and `offline-postrun-evidence.json`.

## Transition checkpoints

The analyzer classified the serial as `TRANSITION_GATE_BLOCKED_SAFE_STOP`. The raw serial hash was `34D0BF9891003D5DE42C4AE333DD84E09F161B721694463B94F62323D3E8499B` before and after analysis. The record was valid, had no overflow, and the candidate map hash matched the pinned value.

| Checkpoint | Observed | Evidence | Conclusion |
|---|---|---|---|
| Native boot | **Yes** | QMP `query-status` returned `running=true`; serial contains `kernel_adapter_dispatch` and runtime initialization records. QAC identified and owned the QEMU child. | One native QEMU guest boot ran. |
| Runtime startup | **Yes** | `rhinitialize_return raw0=1`; state reports `runtime=1`, `rhInitialize=1`, `tlsReady=1`, `gcReady=1`, and `lastSuccess=0x62`. | Initialization and the diagnostic preflight ran; this is not application startup acceptance. |
| ThreadStore initialization | **Yes, pointer/state observed** | Serial `event=threadstore raw0=00000001004B7010 raw1=1`; state `threadStore=1`. | ThreadStore was present in the captured state. |
| Thread attachment | **Inconsistent** | `current_thread_state` is nonzero; state reports `attachment=2` (present), `rawThread=currentThread=0x3D76360`, but `palFlsValue=0`, `flsState=0` (empty), and gate `6`. It also reports `coop=2` and `frame=0`. | The first failed prerequisite is the attached-thread/PAL-FLS match. The later preemptive/frame checks were not the gate that stopped this run. |
| `InitializeModules` entry | **No** | Transition record `attempted=0`; no `module_initialize_enter`; gate rejected before the second initialization call. | Not reached. |
| `InitializeModules` completion | **No** | `completed=0`; transition result `result=safe_stop`. | Not reached. |
| Settings factory entry | **No** | No factory marker. Candidate profile metadata sets `managedMainEnteredByCandidate=false` and says it returns before `ManagedMain`. | This candidate is not an application-factory test, even on a passing transition. |
| Settings factory completion | **No** | No factory-return marker. | Not reached. |
| Persistent composite lifetime transition | **No** | No lifetime marker; managed entry is deliberately suppressed by this profile. | Not reached. |
| Fatal condition or successful completion | **Safe stop; no fail-fast** | Valid transition record: `snapshot=2`, `attempted=0`, `completed=0`, `gate=00000006`, `fatal=0`, `reason=0`, `firstFailureStage=FFFFFFFF`, `lastSuccess=00000062`, `initOverflow=0`, `threadOverflow=0`. Serial ends with `result=safe_stop status=00000106 managedEntry=not_reached collection=not_requested runtimeError=none`. No `FAIL_FAST` marker or `0x50414C06` occurred. | The candidate rejected its transition gate cleanly. QEMU exit code 0 is the controlled host shutdown result, not proof that `InitializeModules` or the app succeeded. |

### Earliest proven boundary and interpretation

`gate=6` maps to `GX_TRANSITION_GATE_PAL_FLS`. The pinned evaluator requires an attached current thread's PAL FLS value to match that thread. The captured record says the current thread is present while PAL FLS is empty, so the gate returns `0x100 + 6 = 0x106` before entering generated `InitializeModules`. This is the earliest proven blocked boundary. The evidence does not establish which earlier initialization or publication step left the PAL FLS cell empty; no specific runtime source defect is assigned here.

The earlier 88-byte allocation request with 64 bytes available was reported during Settings construction, after application dispatch had reached the Settings factory in the separate R3 allocation trace. This candidate stopped before `InitializeModules` and does not enter `ManagedMain`, so it emitted no such allocation observation and could not reproduce or explain it. The earlier observation is not evidence for the PAL-FLS gate in this boot. See `NATIVEAOT_C188R12R4R5R4R5R2R2R4R3_ARRAY_CAPACITY_CORRECTION.md` for the prior allocation boundary.

## Recommended next action

In a separate phase, inspect the PAL FLS publication and ThreadStore current-thread state immediately before `GuideXosNativeAotC188R12R4R5R2R3InitializeModulesAttributionPrepare` captures its snapshot. Establish why the thread is exposed as attached while its PAL FLS cell is empty; keep the gate contract intact until that mismatch is understood. Any runtime correction should be the smallest justified change, with a newly qualified candidate and a separately authorized one-boot run.

To observe the Settings factory and persistent composite lifetime, first produce a candidate that intentionally dispatches managed entry; this transition-attribution candidate explicitly suppresses it. That application-level qualification belongs in a later phase. No second guest boot was performed or attempted here.

## Closeout

The in-scope source, hosted contract test, and this report are committed together. The three pre-existing `.obj` modifications remain uncommitted. No generated guest artifacts are staged, and no push was made. The exact final HEAD and working-tree state are included in the task closeout response.

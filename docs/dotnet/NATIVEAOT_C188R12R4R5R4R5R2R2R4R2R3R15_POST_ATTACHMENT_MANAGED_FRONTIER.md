# C188R12R4R5R4R5R2R2R4R2R3R15 — Post-Attachment Managed Frontier

## Result

**Outcome C — `POST_ATTACHMENT_FRONTIER_ADVANCED`.** The one authorized R15 QAC guest was stopped only after its bounded 180-second wait expired. A QMP pause and register capture show a later runtime instruction pointer than the last serial event: the CPU was in the exact candidate's `StackFrameIterator::NextInternal`, immediately after an indirect code-manager unwind call. This proves the serial marker was not the last executed instruction.

The sampled instruction does not identify its caller. It is compatible with a GC root walk, but it does not prove a collection, allocation, `ManagedMain` body entry, or any GC milestone. The guest produced no fatal marker. This remains incomplete GC evidence, not a proven GC failure.

## Baseline, provenance, and preservation

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting repository HEAD: `c56d68e1d8138023897f143f5c0b52dc454080e4`.
- Candidate provenance records the same source HEAD and branch.
- Pinned runtime: NativeAOT 9.0.0, AMD64, Workstation GC, `net9.0`/`win-x64`; runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- Runtime pack identity: `guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r2r3-managed-settings-object-v1`.
- Candidate builder SHA256: `BFBB13350F2D74F3D4521A96067AF4164077533AD59D968BBE1D5A21CAFB9BB0`. Runtime-pack builder SHA256: `AC0BD3E74E78FB1A8AC15FF3B934ACC4CC077198E54211E79B3D5DF5EBA9BDD3`; lock-file SHA256: `6C0ABA2EE3FFA9E6600FF8307210AF15D3028A258264240E4BAE724DF85C069B`.
- Candidate root: `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-20261111T164842868Z`.
- Earlier R15 candidate `candidate-20261111T163205135Z`, R14 candidate `candidate-r14-20261111T133436496Z-56112718`, prior candidates, original R14 serial and analyses, and all consumed one-boot guards were preserved.

The three pre-existing modified `.obj` files retained their original SHA256 values throughout the build, hosted runs, guest boot, and offline analysis:

| File | SHA256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## R14 frontier reconstruction

R14's last serial marker was `[C188R12R4R5R4_THREAD] ... event=transition_attach_complete raw0=0000000003D79360 raw1=0000000000000001`. The exact R14 serial is under `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T134615409Z-15d34566623c4111b2b4b1aaf32c6168\serial.log`.

The marker is emitted by native runtime code in `RhpReversePInvoke`, after `guideXosStockRhpReversePInvoke(frame)` returns, after ThreadStore membership is checked, and after the transition telemetry frame is pushed to the bounded side table. The marker's `raw0` is the attached Thread and `raw1` is its PAL thread ID. It proves those steps completed. The telemetry path does not change NativeAOT Thread fields or manufacture a transition frame.

R14's `[C188R13_GC_ROOT_ENTRY] event=attempt` was printed by native dispatch immediately before the indirect call to `ManagedMain`. It is a call-attempt marker, not evidence that generated code or the C# body started. The subsequent `managed_entry_bridge_*` events and `transition_attach_complete` show that generated `ManagedMain` reached its reverse-P/Invoke prologue and entered the native `RhpReversePInvoke` helper. They do not prove that the helper returned to the first C# body instruction. The R14 report's M3 label “managed body entered” was too strong and is corrected here.

In the exact R14 candidate map, `ManagedMain` begins at `0x1000019e0`; its generated prologue calls `RhpReversePInvoke` at `0x100001a0b`, target `0x10004dca0`. If the helper returns, the first C# body instruction is at `0x100001a10`, storing the entry marker into the caller-owned 44-byte record. `RhpNewFast` is not called until `0x100001a20`, target `0x10004d970`. Thus the expected next unproven step after the attachment helper is its return to generated `ManagedMain`, followed by the first body store. R14 did not prove either step.

R14 had no explicit fail-fast, exception, trap, or watchdog record. QMP still reported the guest running at the bounded timeout; the host then sent `quit`. The runner's post-capture schema issue was fixed separately: the original top-level R13 analysis omitted `recordIntegrity`, which existed in the nested transition analysis. The corrected analyzer forwards `recordIntegrity` and `transitionParseError`; the runner validates missing, malformed, or contradictory values without inventing data. R13 and R14 regression fixtures now preserve this distinction.

R14 also had a transient incomplete in-run QAC process census. The next census completed with the same owned QEMU process and no degraded flag; the final census completed with zero QEMU processes, reservations, or sessions. The incomplete observation was transient, not a persistent coordinator failure or report-generation problem. It remains distinct from the later verified clear census.

## R12 and R13 execution paths

The preserved R12 candidate at `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object\c12-20261109T083000Z` has `ManagedMain` at `0x100001960`, a `RhpReversePInvoke` call at `0x10000197d` to `0x10004db80`, its first managed-body marker at `0x100001982`, and a `RhpNewFast` call at `0x100001992` to `0x10004d850`. Its one guest returned `1122` after stock allocation, construction, and field reads; ThreadStore, PAL FLS, stack, and transition-frame checks passed. R12 requested no collection.

R14 and the final R15 candidate retain the same generated entry/allocation shape at `0x1000019e0` / `0x100001a0b` / `0x100001a10` / `0x100001a20`. The first new R13 body actions are scalar writes to the native stack record, then the same `RhpNewFast` object allocation and `Calculate()` path. R13 next reads native counters, records the collection request, calls `RhCollect`, reads counters again, mutates the same object, computes `1123`, and returns. The added counter imports and collection are later than the first allocation, so they cannot explain a failure before that allocation.

The final candidate's generated code maps the relevant calls as follows:

| Generated address | Operation |
|---|---|
| `0x100001a0b` → `0x10004dca0` | Reverse-P/Invoke helper in the generated prologue |
| `0x100001a10` | First managed-body store: marker `0x52313345` |
| `0x100001a20` → `0x10004d970` | `RhpNewFast` |
| `0x100001a3c` → `0x100001910` | `DisplaySettingsProbe` constructor |
| `0x100001a44` → `0x100001930` | `Calculate()`; `1122` is stored at `0x100001a49` |
| `0x100001a63` → native counter API | Pre-collection counter read |
| `0x100001a75` | Collection-request marker stored in the record |
| `0x100001a89` → `0x10001e920` | `RhCollect` call |
| `0x100001ae8` → `0x10004e110` | Reverse-P/Invoke return helper |

The managed code does not print these record fields as it executes. Native dispatch prints them only after `ManagedMain` returns. Therefore missing allocation, request, collection, and return serial records cannot by themselves prove the body did not reach those instructions. Conversely, source order and generated code do not prove that any of them executed in this guest.

## R15 candidate and hosted qualification

The final candidate was freshly built from the stated source HEAD and pinned runtime. The candidate builder ran the hosted checks, production-isolation checks, and candidate packaging before the one boot. A separate `HostedPreflightOnly` run passed with `qacQueried=false`, `qacReserved=false`, `qemuLaunched=false`, `attemptGuardCreated=false`, and `guestBoots=0`.

| Artifact | SHA256 |
|---|---|
| Candidate manifest | `1BFCE8D0B0F15CF2990D35C55E8101E635B68766C2DAEA38AB2AB35C51C63592` |
| Candidate inventory | `DE532CF462B9A5E589AAD7D8D00558674DDE5B60434087B9FBE4F2839E3C0265` |
| Runtime pack | `34C857FAC854B00A2A32EA40C6D0DF076A931E1FF9F246E142785BFF2236150F` |
| Runtime-pack manifest | `125FEFDD1C6179CCF648E24113F23BF55241896E21C66865BB8A948B1CE25AF9` |
| Managed PE | `CAB3253A7934C39A4C2FE67E28431E4A6B3FB9B69FDD555A4D281E92065BB9E1` |
| Managed ELF | `EAB5BDA4A8746386868B7B52822B934EA9A9A2EF3D0B08FC1ECE96A391E738BA` |
| Managed map | `96219304BABCA30929431DAC730AABCF6A257A78945414C58B5663103A4561F8` |
| Diagnostic kernel | `A8F7982D684496B26D885AFB27DD56672482F89D81D9EA7F1B91DAD46CEE2357` |
| Candidate ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Production NativeAOT ELF | `8A4308EE4AA8ACAA3D64DF0ECD853CB335878558853DE2C5D9B136C3FBC63741` |
| Production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |

Production outputs are isolated from the diagnostic profile. The candidate records that R13, managed-entry, transition-attribution, safe-handoff, native-allocation, and GC-ownership proof instrumentation is absent from production; the persistent composite lifecycle check passed. Candidate integrity passed after the guest and after hosted reruns: 1,182 files, zero unlisted payload files, zero mismatches; manifest and inventory hashes stayed unchanged.

Hosted outcomes (none of these results claim native collection):

| Suite | Result and evidence |
|---|---|
| R13 managed-GC probe and runner/analyzer contracts | PASS, 64 assertions. Includes R13/R14 marker semantics, deferred CPU capture under an exclusive synthetic serial-file lock, post-exit serial hashing, schema failures, event ordering, incomplete frontiers, and synthetic progression. `qemuLaunched=false`, `qacReserved=false`, `collectionExecuted=false`. |
| R12 object-allocation regression | PASS, 23 assertions on the preserved R12 candidate. |
| R11 managed-entry regression | PASS, 23 assertions, embedded in the final candidate hosted run. |
| Transition attribution | PASS, 45 assertions in the final candidate hosted run. |
| Native allocation | PASS; 2,425 shared PAL slot-table assertions, 1,024 reserve/release cycles, allocation-record and backend-summary checks, and linked `operator new` path check. |
| ThreadStore/PAL | PASS; 7 mode-model and 6 telemetry assertions; two frame-preservation cases, one nested case, and overflow rejection. No native guest attempted by this suite. |
| Safe-handoff analyzer/source | PASS; 22 analyzer cases and 10 source-contract checks. |
| Safe-handoff runner contract | PASS on a NativeAotSafeHandoff candidate; verifies fresh census, one-boot guard, QAC ownership, QMP shutdown, and release. |
| QAC admission predicates | PASS, 32 cases. |
| Production isolation | PASS in the final candidate manifest and builder checks. |

Exact build and focused rerun commands follow. The candidate builder command ran the candidate-contained R11, R13 (59 assertions at build time), transition, safe-handoff, native-allocation, and production-isolation checks. The later R13 rerun adds five focused CPU-capture/order cases for 64 total.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1 -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT -ArtifactRoot E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT -ThreadStoreReadiness -GcInitProbes -SemiSharedAttribution -NativeAllocationAttribution -SafeHandoff -InitializeModulesAttribution -ManagedEntryProbe -ManagedGcRootProbe

$repo = 'D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT'
$candidate = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-20261111T164842868Z'
$hosted = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r15-hosted'
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\test-c188r13-managed-gc-root.ps1 -RepoRoot $repo -OutputDirectory (Join-Path $hosted 'final-r13-cpu-capture') -RuntimePackManifest (Join-Path $candidate 'runtime-pack\runtime-pack.manifest.json') -ManagedMap (Join-Path $candidate 'managed\artifacts\HostLogProof.map') -ManagedMainDisassembly (Join-Path $candidate 'managedEntryProbe.disassembly.txt') -GcCollectWrapperDisassembly (Join-Path $candidate 'gcCollectWrapper.disassembly.txt') -ConstructorDisassembly (Join-Path $candidate 'displaySettingsProbeConstructor.disassembly.txt') -CalculateDisassembly (Join-Path $candidate 'displaySettingsProbeCalculate.disassembly.txt') -KernelBuildLog (Join-Path $candidate 'kernel-build.log') -ProductionNativeAotMap (Join-Path $candidate 'production\nativeaot-build\nativeaot\artifacts\HostLogProof.map') -R11HostedTestResults (Join-Path $candidate 'managed-entry-probe-regression-hosted-tests\hosted-test-results.json') -CallSiteMapPath (Join-Path $candidate 'transition-callsite-map.json')

$r12 = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object\c12-20261109T083000Z'
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\test-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1 -RepoRoot $repo -OutputDirectory (Join-Path $hosted 'r12-runner-regression-final') -RuntimePackManifest (Join-Path $r12 'runtime-pack\runtime-pack.manifest.json') -ManagedMap (Join-Path $r12 'managed\artifacts\HostLogProof.map') -ManagedMainDisassembly (Join-Path $r12 'managedEntryProbe.disassembly.txt') -ConstructorDisassembly (Join-Path $r12 'displaySettingsProbeConstructor.disassembly.txt') -CalculateDisassembly (Join-Path $r12 'displaySettingsProbeCalculate.disassembly.txt') -KernelBuildLog (Join-Path $r12 'kernel-build.log') -ProductionNativeAotMap (Join-Path $r12 'production\nativeaot-build\nativeaot\artifacts\HostLogProof.map') -R11HostedTestResults (Join-Path $r12 'managed-entry-probe-regression-hosted-tests\hosted-test-results.json') -CallSiteMapPath (Join-Path $r12 'transition-callsite-map.json')

pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\test-c188r12r4r5r4-threadstore-readiness.ps1 -RepoRoot $repo -OutputDirectory (Join-Path $hosted 'final-threadstore-pal')
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\test-c188r12r4r5r4-qac-admission.ps1 -RepoRoot $repo -ArtifactRoot (Join-Path $hosted 'final-qac-admission')
pwsh -NoProfile -ExecutionPolicy Bypass -File tests\dotnet\runtime-pack\c188r12r4r5r4r5r2r2_native_alloc_hosted_tests.ps1 -RepoRoot $repo -OutputDirectory (Join-Path $hosted 'native-allocation-verified') -DiagnosticMap (Join-Path $candidate 'managed\artifacts\HostLogProof.map') -ProductionRuntimeManifest (Join-Path $candidate 'production\nativeaot-build\runtime-pack\runtime-pack.manifest.json') -StartupImportsObject (Join-Path $candidate 'runtime-pack\guidexos_nativeaot_startup_imports.production.obj') -PalContractObject (Join-Path $candidate 'managed\artifacts\guidexos_nativeaot_pal_contract.obj')
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\test-c188r12r4r5r2r2r4-safe-handoff.ps1 -RepoRoot $repo -OutputDirectory (Join-Path $hosted 'final-safe-handoff')
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1 -RepoRoot $repo -CandidateRoot 'D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c188r12r4r5r4r5r2r2r4-safe-handoff\candidate-20261105T054500000Z-linkedtrace'
```

The QAC admission command returned `PASS (32 cases)`.

## One authorized R15 guest

Pre-run QAC status/list showed the coordinator online with zero reservations, sessions, or QEMU processes. Strict admission passed. The consumed candidate-specific guard is `E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\guest-launch-attempt-candidate-20261111T164842868Z.json`, SHA256 `B37855300E8C6C3335758976402E14C02362295C74680BF2D35D5B50FA6A8F5F`; it records `maxGuestBoots=1` and `automaticRetry=false`.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1 -CandidateRoot E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-20261111T164842868Z -ArtifactRoot E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT -CandidateIntegrityInventory E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-20261111T164842868Z\candidate-integrity-inventory.json -BootTimeoutSeconds 180 -AdmissionWindowSeconds 30
```

The runner used SHA256 `6AEA7153BA226DFEDE91F4BB0D67A49EB9FF95DDC976D45C5F294923F8424BE9`. QAC granted reservation `af28cddcff134d10a6b7e7584dfd9029` for `guideXOSServer` / `acceptance`, owner PID `8784`, and its verified QEMU child PID `15344`. QEMU was launched once at `2026-11-11T16:56:37Z`; no retry occurred.

| Milestone | R15 evidence |
|---|---|
| M1 — Native initialization | PASS; initialization records present. |
| M2 — `InitializeModules` returned | PASS; transition marker present. |
| M3 — Transition attachment complete | PASS; ThreadStore publication and attachment marker present. |
| M4 — Genuine managed-body entry | Not proven. The reverse-P/Invoke prologue ran; no evidence shows it returned to the first C# body instruction. |
| M5 — First allocation | Not proven. No guest allocation record or body-level witness. |
| M6 — Initial `1122` | Not proven. |
| M7/M8 — Collection started/completed | Not proven. |
| M9 — Live object accessed after collection | Not proven. |
| M10 — `1123` returned to native | Not proven. |
| M11 — Post-return runtime state | Not proven. |
| M12 — Shutdown and QAC cleanup | PASS; QEMU exited `0`, release verified, final census complete and empty. |

The runner reported `MANAGED_GC_ROOT_EVIDENCE_INCOMPLETE`, `recordIntegrity=1`, blank `transitionParseError`, and `managedGcStopCause=bounded-timeout-guest-was-running`. The analyzer recorded no managed return, allocation, collection, live-object, or post-return state evidence. Serial ended with the complete `transition_attach_complete` line, sequence `0x3B`, and no fail-fast. Its SHA256 is `AC10CBB77C83A5CACC4CB7AD32E2818877667DCEB30C525515E27475A7B7B591` (120,002 bytes). This length alone does not establish a serial cap; no dropped-output record proves truncation.

At timeout, QMP `stop` returned and emitted `STOP`; `query-status` reported `paused` / `running=false`; HMP `info registers` returned a full register set; a second status check remained paused; then the host issued `quit`. QMP logged `SHUTDOWN reason=host-qmp-quit`; QAC exited `0`. The CPU register file was captured from the paused guest and is 2,121 bytes, SHA256 `0E055B7A96B0C50262B78DA99113EFDC1F27FFABFC6D625889F69DD1A52E5ACC`:

```text
RIP=0000000100057ecc
RSP=00000000052b5200
RBP=00000000052b5360
```

The exact R15 managed PE and map resolve `RIP=0x100057ecc` to `?NextInternal@StackFrameIterator@@AEAAXXZ`, function start `0x100057e20`, offset `+0xAC`. At `0x100057ec9` the code calls indirectly through the code manager; `0x100057ecc` is `test %al,%al`, immediately after the call. Pinned `StackFrameIterator::NextInternal` invokes `GetCodeManager()->UnwindStackFrame` and is used for generic stack walking, including GC walks. No caller stack was captured, so the sample does not establish whether this was a GC root walk, an allocation-time walk, or another runtime stack walk. RSP and RBP lie inside the serial-reported stack bounds `[0x52b2000,0x52b6000]`.

The original `guest-manifest.json` records CPU-state packaging as failed because the runner tried to hash `serial.log` while QEMU still held it open. QMP had already paused the VM and returned the register text, which the runner wrote to `guest-cpu-state-timeout.txt`. The raw QMP log SHA256 is `30FDABD89F474E21E30C5B46C54A7F42C2E3802DAB0AAC2C27F5D138BFD7BC12`. After QEMU exited, this report's offline recovery wrote `guest-cpu-state-timeout-recovered.json`, binding the register, QMP, serial, candidate, and exact disassembly hashes. Its SHA256 is `362B3CFBC81F6361A0AC8BFC1798B599A9554333D568D9CD262A2668CE7E4525`. The original manifest and raw logs were not rewritten.

R15's final QAC status/list reported zero processes, reservations, and managed sessions. The Windows QEMU census and reservation-inspection capture both completed with zero entries. Candidate inventory verification then returned `PASS`, 1,182 files, zero unlisted payloads, zero hash mismatches. No additional boot or retry was made.

## Narrow correction and next phase

The evidence supports **B — diagnostic visibility gap**. It does not identify a source-level execution defect, an ABI mismatch, or a GC-entry failure. The R13 record is deliberately printed only after managed return, so it cannot localize a timeout inside the method. R15 adds a bounded host-side CPU snapshot and post-exit serial hash finalization without changing the probe, runtime behavior, reverse-P/Invoke contract, GC, Thread fields, or side-table telemetry.

The guest used the pre-fix runner (hash above). The post-run runner source SHA256 is `839F6C8762A09DBF8D9A523322109A67BFEF2B914D28C0FEB89EE2172B6FC11A`; the hosted test source SHA256 is `7E583D5E61841D1BC6169DB0E7E7BE168E85345526FE052822A0AB8EFDB725BF`. The runner was corrected so `Capture-R13TimeoutCpuState` no longer hashes the live serial file. It writes register/candidate/QMP metadata independently, then `Complete-R13TimeoutCpuStateSerialHash` attaches the serial hash only after QAC exits. If serial hashing fails, the captured registers remain valid and the hash failure is recorded separately. The hosted lock regression holds a synthetic serial file exclusively while mocked QMP returns a paused register snapshot, then verifies successful post-release hashing and preservation of register evidence when final hashing is unavailable. This post-run tooling correction was not rebuilt into or run in a second guest.

The next phase should answer one question: **what caller led to `StackFrameIterator::NextInternal`, and did the reverse-P/Invoke helper return to the first managed body instruction?** First assess a bounded, QAC-owned paused-guest stack-memory capture against the exact map and stack bounds, or add only a fixed-size independent progress record at the helper-return/body boundary if the existing infrastructure cannot safely expose that state. Keep any capture ownership-gated, verify it cannot alias Thread fields, frames, GC roots, object headers, or allocation contexts, and run hosted checks before requesting a new one-boot authorization. Do not infer a collection from this RIP or broaden the probe.

Remaining qualification requires actual guest evidence of a managed body witness, `RhpNewFast` allocation and construction, initial `1122`, one `RhCollect` with an increased native collection counter, a post-collection access through the same live reference, result `1123` returned to native, valid post-return ThreadStore/FLS/frame state, and clean QAC release.

## Final repository state

R15 changes are committed on `v1.1_DOTNET_SUPPORT`; the exact final HEAD is included in the final task handoff. No push was performed. The only unrelated worktree changes are the three preserved `.obj` files listed above. The final commit contains the R15 runner correction, hosted regressions, fixtures, and this report.

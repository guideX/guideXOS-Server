# C188R12R4R5R4R5R2R2R4R2R3R13 — Managed GC Root Qualification

**Outcome E — `NATIVEAOT_GC_EVIDENCE_INCOMPLETE`**

The hosted and linked-code checks qualified a bounded NativeAOT probe and the candidate passed strict admission. One authorized QAC boot reached the R13 managed-entry attempt, but the runner stopped on the earlier R12 `InitializeModules` marker and sent QMP `quit` before allocation or collection evidence appeared. The guest therefore proved neither a collection nor survival of a live managed reference. The one-boot guard is consumed; no retry was made.

## Repository and R12 baseline

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `56cb6b18757848be893ea419813c97f7b05b6da0`.
- R12 implementation baseline: `78802b4053e6e43401c2c4ff5d743e34378d18d1`.
- R13 implementation commit before this report-only closeout: `b6647fe34396289fd908f18ea1e71f1d0565349b`.
- The report closeout commit follows the implementation commit. No push was performed.

The preceding result is documented in [the R12 managed-object qualification](NATIVEAOT_C188R12R4R5R4R5R2R2R4R2R3R12_MANAGED_SETTINGS_OBJECT_QUALIFICATION.md). Its raw serial remains identified by SHA256 `ABF237C9189771E65562DA212878A25F3784CE1CFF35AEB7F26768E08A4AD664`; its original online result was not overwritten.

### R12 analyzer correction

The initial R12 online analysis returned `MANAGED_SETTINGS_OBJECT_INCOMPLETE` with `r12_managed_entry_return_contract_mismatch` (analyzer SHA256 `39D08E6C9DBC8D136DB80A74418B8A5C1AFD1207EAC9A52D8D4D5429C31B73F0`). It rejected the valid nested-call sequence in which `ManagedMain` emits its attempt marker, `RhpNewFast` emits allocation evidence while the managed method is running, and the method then returns. The corrected interpretation accepts exactly `attempt < allocation < returned < post-call state`, while still rejecting missing, duplicate, malformed, contradictory, and out-of-order records. The offline correction reported `MANAGED_SETTINGS_OBJECT_RETURNED_RUNTIME_STATE_READY` (source SHA256 `135D5FDFC4068EA5174E8A1B0D0D4FB9275444A58273ED0676A54F803C1B4791`).

R13 hosted regressions exercise both sides of that contract: a valid allocation-before-return fixture passes, and allocation-after-return fails. Additional negative fixtures reject a zero collection-count delta, duplicate return evidence, and contradictory post-return ThreadStore state. The preserved R12 evidence was not rewritten.

## GC readiness and probe contract

The diagnostic runtime uses the pinned NativeAOT source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` and the real Workstation GC. In that source, the NativeAOT `RhpCollect` helper reaches the Workstation GC collection path ([pinned `GCHelpers.cpp`](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/GCHelpers.cpp)). The linked candidate contains the compiled `GC.Collect` wrapper and `RhpCollect`, NativeAOT `.managedcode$I` metadata, and the GC slot decoder symbols. Its generated `ManagedMain` keeps the same object reference in RSI across the collection call, then mutates and reads the object through separate compiled methods. The hosted checks also confirmed `supportsWriteWatch()` is false for this configuration, so the runtime gate does not enable background concurrent GC.

The source and hosted evidence supported proceeding with one synchronous, single-managed-thread diagnostic. R12 had already observed heap initialization, a stock managed allocation, current-thread attachment, ThreadStore membership, PAL FLS identity, stack bounds, module initialization, and code-manager registration. R13 adds no finalizers, managed worker threads, persistent managed lifetime, pressure loop, native object pointer, or finalizer wait.

This readiness decision is not a native root-enumeration result. The candidate’s metadata and disassembly establish a managed reference across the call and link the runtime decoder, but offline inspection did not establish the exact decoded R13 stack slot or prove that the guest enumerated or updated that root. Those require the collection to execute in the guest.

`ManagedGcRootProbe.ManagedMain` is intended to construct `DisplaySettingsProbe(640, 480, 2)`, verify `1122`, request exactly one synchronous `GC.Collect()`, require a native collection-counter increase, mutate `Scale` to `3`, verify `1123`, keep the reference alive through its last use, and return `1123`. The native record carries scalar evidence only and is 44 bytes. The kernel rejects a pass without a real collection-count delta and a successful post-collection counter read.

## Hosted qualification

All commands below were hosted-only and used no QEMU boot or QAC reservation.

| Check | Result |
|---|---|
| R13 managed GC/root suite | PASS, 36 assertions. Includes link/disassembly, root liveness, native record, counter, analyzer ordering, production isolation, and R11/R12 regression checks. |
| Accepted managed-entry regression | PASS, 23 assertions. |
| Native allocation hosted suite | PASS; 2,425 GC/VM slot-table assertions, 1,024 reserve/release cycles, allocation-record/backend-summary/operator-new checks. |
| Transition attribution | PASS, 44 assertions. |
| Safe-handoff analyzer/source suite | PASS, 22 analyzer cases and 10 source-contract checks. |
| Safe-handoff runner contract | PASS; fresh census, atomic no-retry guard, ownership, QMP shutdown, release, and identity checks. |
| QAC admission predicates | PASS, 32 cases. |
| Strict R13 candidate profile and integrity inventory | PASS, 1,409 files. |
| Production compatibility | Production NativeAOT and kernel qualification manifests both report PASS; R13 suite confirms the diagnostic probe is absent from the proof-disabled production map. |
| PowerShell syntax | PASS for the R13 analyzer, R13 tests, QAC runner, and managed-entry test. |

The R13 suite’s full invocation was:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r13-managed-gc-root.ps1 `
  -RepoRoot $PWD `
  -OutputDirectory out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/managed-gc-root-hosted-tests `
  -RuntimePackManifest out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/runtime-pack/runtime-pack.manifest.json `
  -ManagedMap out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/managed/artifacts/HostLogProof.map `
  -ManagedMainDisassembly out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/managedEntryProbe.disassembly.txt `
  -GcCollectWrapperDisassembly out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/gcCollectWrapper.disassembly.txt `
  -ConstructorDisassembly out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/displaySettingsProbeConstructor.disassembly.txt `
  -CalculateDisassembly out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/displaySettingsProbeCalculate.disassembly.txt `
  -KernelBuildLog out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/kernel-build.log `
  -ProductionNativeAotMap out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/production/nativeaot-build/nativeaot/artifacts/HostLogProof.map `
  -R11HostedTestResults out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/managed-entry-probe-regression-hosted-tests/hosted-test-results.json `
  -CallSiteMapPath out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z/transition-callsite-map.json
```

The other reruns used `tests/dotnet/runtime-pack/c188r12r4r5r4r5r2r2_native_alloc_hosted_tests.ps1`, `scripts/dotnet/test-c188r12r4r5r2r3-managed-entry-probe.ps1`, `scripts/dotnet/test-c188r12r4r5r2r3-transition-attribution.ps1`, `scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff.ps1`, `scripts/dotnet/test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1`, and `scripts/dotnet/test-c188r12r4r5r4-qac-admission.ps1`. Their preserved outputs are under `out/dotnet/c188r13-managed-gc-root/hosted-reruns/` (QAC admission output is under the existing ThreadStore readiness artifact root). The managed-entry runner-contract assertion was updated to recognize the new R13-specific guard branch while still requiring R7/R10’s manifest-bound guard, atomic creation, and no-retry policy.

## Candidate identity

Candidate root:

`out/dotnet/c188r13-managed-gc-root/candidate-r13-final-20261111T062000000Z`

The one-boot guard pinned these pre-boot values:

| Artifact | SHA256 |
|---|---|
| Candidate manifest | `51DA2728EFAAC65AC26709BFDE062A4572147AB98BC5D853F621FA265ACED4A8` |
| Candidate integrity inventory | `9D65447A02B6B1E46998C61F6515643A249ED7C84A13CC3DA8C619BDD50021F4` |
| Diagnostic NativeAOT ELF | `3FFDC4A60F2DCBDF993B4E34474AB208779CA89F0031F835B63B8CC8004F31F8` |
| Diagnostic kernel | `695F8EAB2BCAFE75CFB65A7FF9BC709389886577514FCC9EA20934172B7018B0` |
| Candidate ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

After the guest, the R13 hosted suite was rerun against the fixed runner and its result increased from 35 to 36 assertions. The hosted-result hash and hosted-test-script hash were then updated in the candidate manifest and the complete 1,409-file inventory was regenerated. The current manifest SHA256 is `AB72AC0BF142383220F9677F4D164BA86B03404CFCA59D935A307561A9F042EA`; the current inventory SHA256 is `CE08882D5ADBA094E6D69CF023A5EDF0D8417DA95ABDD6AB5268FB1F4EBE95DC`. The current strict profile and inventory checks pass. The diagnostic ELF, kernel, and ramdisk still match the pre-boot hashes pinned by the consumed guard.

## One authorized QAC boot

- Consumed guard: `out/dotnet/c188r13-managed-gc-root/guest-launch-attempt-candidate-r13-final-20261111T062000000Z.json`.
- Guard SHA256: `8385F2A159115CB4C40FC27EC7ABA17C25F56730505B846F96345DE0646DB1DF`.
- The guard has `maxGuestBoots=1` and `automaticRetry=false`; its pre-boot runner SHA256 is `56D826E8BFD743F55D114F9ECB0F4E5BE79AEC6746E12B78887FC10CC0645AB6`.
- QAC reservation: `0be93debbc0341c39629c50a7e7da91c`.
- QEMU child PID: `25300`; it launched once and exited `0` at `2026-11-11T12:34:49.986Z`.
- QMP records the host `quit` and `SHUTDOWN reason=host-qmp-quit`. QAC release was verified. The post-run coordinator was online with zero reservations, zero managed sessions, zero QEMU processes, and a complete Windows QEMU census of zero processes.

Run root: `out/dotnet/c188r13-managed-gc-root/qac-guest-managed-gc-root-C188R13-20261111T123433309Z-770437c826e945e0a8c83a52d0780877`.

| Milestone | Guest evidence |
|---|---|
| M1 — NativeAOT initialized | Baseline initialization records are present. |
| M2 — `InitializeModules` returned | `[C188R12R4R5R2R3_TRANSITION] result=initialize_modules_returned` is present. |
| M3 — R13 managed entry reached | `[C188R13_GC_ROOT_ENTRY] event=attempt` is present. |
| M4 — Object allocated/constructed | Not observed; no R13 allocation record. |
| M5 — Initial `1122` | Not observed. |
| M6/M7 — Collection started/completed | Not observed. |
| M8 — Live object survived | Not observed. |
| M9/M10 — Post-collection `1123` and managed return | Not observed. |
| M11 — Post-call ThreadStore/FLS/frame checks | Not observed. |
| M12 — QEMU shutdown and QAC release | PASS; QEMU exited `0`, release verified, and post-run census was clear. |

The preserved raw serial SHA256 is `ECB0E492184B281810C03F58C95A47587CEC15AC1998815281DB4BDB920A41E8`; QMP log SHA256 is `039227783824FBBD5CEE6E65C8E62972A249BF145D544202FFD72C220F7C9B6F`.

The offline R13 analysis is `MANAGED_GC_ROOT_INCOMPLETE`, with `parseError=null` and `evidencePreserved=true`. It records managed entry attempted, but no managed return, successful allocation, collection request/start/completion, live-object survival, or post-call runtime state. Its SHA256 is `AF19EFE2B307760D7A70E5B552121BF98D2140AA42CC44870BF2CDD8EC5F42B0`. The transition analysis reports `INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED`.

## Runner defect and correction

R13 is included in the generic transition-attribution profile. The polling loop therefore treated the earlier R12 `InitializeModules returned` marker as terminal and sent QMP `quit` before R13’s allocation and collection records. QEMU and QAC cleanup were successful, but the guest evidence stopped at the R13 attempt marker. No final `guest-manifest.json` was written, so the runner’s automatic post-run candidate-integrity summary is unavailable.

The runner now gives R13 a dedicated terminal condition: it waits for the post-`ManagedMain` `[C188R12R4R5R2R3_ENTRY_STATE] ... result=PASS|FAIL` record or an explicit fail-fast. The new hosted assertion verifies that R13 cannot stop on the earlier transition marker. The corrected runner SHA256 is `11C6A8AA82D8346BCABC37393F1B9A4C4921F321CC7A6B5EF42FED9350CBAB6F`. This correction is committed for a future candidate; it did not alter the consumed guard, the recorded boot, or the preserved raw serial. No second guest was launched.

## Limits and next milestone

R13 does not qualify native collection, root enumeration, object relocation/update, post-collection field access, or collection-time GC-to-OS service behavior. It also does not prove general allocation workloads, finalization, managed worker threads, concurrent collection, or persistent Settings lifetime. This is an incomplete-evidence result, not evidence that `RhpCollect` failed or that the GC prerequisite gate is blocked.

Next: build a separately identified candidate with the corrected runner and a new candidate-specific one-boot guard, rerun strict admission and hosted checks, then arrange a new explicit one-boot authorization. The consumed R13 guard must remain preserved and must not be retried.

## Preservation

The three pre-existing modified object files remain unstaged and at their recorded hashes:

| File | SHA256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

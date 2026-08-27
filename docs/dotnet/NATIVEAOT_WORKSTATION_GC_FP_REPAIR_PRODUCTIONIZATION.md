# NativeAOT Workstation GC FP Repair Productionization (C011EC50)

Date: 2026-08-27
Branch: `v1.1_DOTNET_SUPPORT`
Predecessor: C49 `a0cc1fc6f3680e9948e24da605b85177d75fa982` (`Complete NativeAOT second Workstation GC collection`)

## Result

C011EC50 is Outcome A / Success Level 5. The C46 caller-FP and C48 iterator-FP corrections now live in a checked-in NativeAOT runtime-pack patch and are compiled into the ordinary productionized runtime-pack. The C50 proof configuration applies that patch, but does not perform the former C46/C48 semantic source rewrites. Three fresh QEMU 11.0.0 boots completed the authentic second Workstation GC collection path and bounded managed continuation; three separate ordinary guideXOS boots reached the kernel main loop and Navigator PASS.

The required conclusion is:

> C50 moved the C46/C48 FP corrections into the durable NativeAOT runtime path, removed semantic dependence on proof-harness rewriting, preserved C18 fail-closed behavior, completed repeated authentic Workstation GC collections with managed continuation, and proved the productionized runtime through ordinary guideXOS boot.

The runtime identity stayed locked to NativeAOT 9.0.0, AMD64, Workstation GC, GC interfaces 5.3 / 2, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

## C46/C48 placement audit

Before C50, neither C46 nor C48 changed the locked NativeAOT source. The smoke script copied the locked sources and rewrote generated copies for the bounded experiment.

| Fix or surface | Location before C50 | Class before C50 | Durable C50 location |
|---|---|---|---|
| C46 caller-FP publication | `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, generated `CoffNativeCodeManager.cpp` and `StackFrameIterator.cpp` | Class C — proof-harness transformation | Class B patch applied to locked runtime sources; production runtime library contains the result |
| C48 iterator-owned FP transfer | same smoke script, generated `StackFrameIterator::Next` | Class C — proof-harness transformation | Class B patch applied to `StackFrameIterator.cpp`; production runtime library contains the result |
| REGDISPLAY definitions | locked `regdisplay.h`; audited, no C46/C48 semantic edit | Class D — diagnostic/audit surface | unchanged; `GetFP()` remains the owner-neutral dereference of `pRbp` |
| C46/C48 observations | diagnostics header/platform and smoke parser | Class D / C | retained for historical proof modes; disabled in C50 production mode |

The checked-in repair is `tools/dotnet/runtime-pack/patches/nativeaot-amd64-fp-handoff.patch`, applied by `tools/dotnet/runtime-pack/apply-nativeaot-fp-repair.ps1`. The runtime-pack builder accepts `-NativeAotFpRepair`; the wrapper forwards that switch.

## Durable ownership contract

The semantic owner is the earliest layer that establishes each value:

1. On AMD64, `RtlVirtualUnwind` establishes the logical caller FP in `CONTEXT.Rbp`. `contextPointers.Rbp` is restored-register storage metadata and is not the authoritative FP value for `UWOP_SET_FPREG`.
2. `CoffNativeCodeManager::UnwindStackFrame` saves the incoming `pRbp` storage pointer before nonvolatile-register promotion. For an ordinary AMD64 unwind it publishes `context.Rbp` through that stable pointee and restores the saved `pRbp` pointer after promotion.
3. A reverse-P/Invoke frame retains the incoming iterator-owned slot-base contract rather than overwriting it with the reverse transition's unrelated unwind `CONTEXT.Rbp`. This preserves valid `-0x70` reverse-P/Invoke metadata semantics.
4. `StackFrameIterator::Next` obtains the current logical FP through `GetFP()`, copies it into `m_FramePointer`, then re-homes `pRbp` to `&m_FramePointer`, in that order.
5. `CalculateCurrentMethodState` preserves a nonzero iterator-owned FP across a method-state query that reports no method-local FP and explicitly republishes the value through the stable pointee.

The production code is AMD64-specific because the contract is about `CONTEXT.Rbp` and Windows AMD64 unwind metadata. It is not a reverse-P/Invoke-only fix: ordinary NativeAOT AMD64 unwinds benefit from correct caller-FP publication, while reverse-P/Invoke uses the guarded slot-base exception above. Downstream root scanners and metadata consumers do not compensate for stale storage because the REGDISPLAY contract is repaired at its owner.

The patched source regions are the `StackFrameIterator::Next` hunk around locked source line 1523, `CalculateCurrentMethodState` around line 1938, and `CoffNativeCodeManager::UnwindStackFrame` around line 830. The zero-context patch applies cleanly with `git apply --unidiff-zero --check` to a fresh archive of the locked source.

## Harness dependency

Before C50, the smoke harness injected C46 semantics in three places: it preserved/restored the FP around `GetFramePointer`, rewrote `StackFrameIterator::Next` to copy FP before re-homing `pRbp`, and rewrote the CMM promotion boundary to publish `context.Rbp`. C48 added the iterator ownership transfer to that generated source.

After C50, `$useC011EC46SemanticInjection` is false in `productionized-second-collection`. The generated C50 compile command has no C46, C47, or C48 semantic defines, and the generated durable sources contain the checked-in patch. C44/C45 observations and the prior C18–C41 proof instrumentation remain for provenance and regression checks; they do not implement the FP contract. The historical C46/C48 modes retain their old transformations so their prior milestones remain reproducible.

## Production artifact

Build command:

```text
pwsh -NoProfile -File tools/dotnet/runtime-pack/build-runtime-pack.ps1 -NativeAotFpRepair -OutputRoot out/dotnet/runtime-pack-c50-production -Clean
```

Manifest identity: `guidexos-nativeaot-runtime-pack-amd64-workstationgc-fp-repair-v1`
Locked source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
Patch SHA-256: `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`

| Artifact | SHA-256 |
|---|---|
| `sdk/Runtime.WorkstationGC.lib` | `EEF392642A07759E06BE9B11DA8DDA3F7723C2AC7B8C83FF279D700FD097AA07` |
| `StackFrameIterator.cpp.fp-repair.obj` | `ACD8013A1CF995BBE38BB61B08C4FC49392A3B83A02ABA6F74EF0F6F2775AC52` |
| `CoffNativeCodeManager.cpp.fp-repair.obj` | `C2F878AD6207F1DA89CB3BB91E955201BA7A98C85A837261F025049B579CEE3F` |
| `guidexos_nativeaot_platform.obj` | `74FA5A5EAD93414DACCAD0158165C949ECE02DE46B6BFCCF37F572932DEAE5F4` |

The final C50 proof artifact was separately instrumented around the production seams. Its hashes were:

| Proof artifact | SHA-256 |
|---|---|
| managed NativeAOT `HostLogProof.exe` | `25444FAE1DF3FD9F166D2DE652608E28A2BF6CD480CDDD03D0A5367341EC9E7D` |
| proof PE `NativeAotGcSingleThreadSuspendEe.exe` | `60AD2C4F798C16B6DF5E72C6F69AED47E306E906503D4E5153714002D5C26781` |
| proof ELF `NativeAotGcSingleThreadSuspendEe.elf` | `21C374644C8CC877A9ED7E40B7C13DC76EA5396C0EE43A58280DB3365422CA39` |
| proof map | `FBA9BB002315457F736E53E26567208A233284470847CF0F62D68F54829B1632` |

## C18 and root-scan validation

All three C50 boots reported the authentic managed PC, a non-null `CoffNativeCodeManager`, `FindMethodInfo` result `1`, and the reverse transition slot/provenance path. The C18 valid path passed. C18 fail-closed source guards were not changed: invalid PC, null code manager, and failed `FindMethodInfo` remain rejecting paths. The malformed historical C44 regression remained fail-closed and C45 reverse-P/Invoke provenance remained passing.

C26/C28 passed on all three boots. The expected managed frame was scanned with four promoted roots: one register root and one stack root were present in the root-source accounting, and the mark queue closed with the existing C28 result. The C50 parser required C18, C26, C28, C34, static durable-source checks, zero former faults, and four promoted entries before accepting the collection result.

## Repeated natural collection result

The existing bounded workload produced two authentic allocation-pressure collections in one managed execution: C37 Collection 1 and C49/C50 Collection 2. C50 did not call internal GC machinery, forge counters, force a generation, or force the planner result. The second collection completed on every fresh boot:

| Collection | Trigger / ordinal | Condemned generation | Planner and phase | Restart/resume |
|---|---|---|---|---|
| 1 | natural workload allocation pressure; C37 predecessor | workload-selected | prior C37 authentic collection path | C37 completion and managed continuation passed |
| 2 | natural workload allocation pressure; ordinal `0x2` | `0x1` (maximum `0x2`) | planner `0x1` = `COMPACT`; compacting `1`, relocating `1`, sweep `0` | collection-done, `RestartEE`, and managed resume all `1` |

Stable second-collection values on all three C50 boots:

- planner-entry collection count `4`; heap `0x1016F730`; active segment `0x104010710`
- promoted roots `4`; register-root accounting `0x1A`; stack-root accounting `2`
- relocation callbacks `0x18`; root-update callbacks `0x19`; rewrites `0x0C`; unchanged `0x0D`
- live plugs `6`; dead gaps `5`; invariant failures `0`; sensitive diagnostic allocations `0`
- `RestartEE` entered and completed; allocation contexts were repaired; eight post-GC allocations resumed

The managed objects retained across relocation remained valid. The tracked object moved from `0x100A01F38` to `0x100901F50`, retained payload sentinel `0x40`, and had size `0x58`; the post-update object path remained valid at `0x100A10058`. No stale reference or relocation invariant was reported.

Post-GC allocator provenance recorded seven fast allocations, one rare/refill allocation, one refill, and eight total bounded allocations. The active context and supplying segment/generation were valid. This is continuation evidence, not an artificial infinite stress loop.

The C40 reclaimed range was naturally observed as `[0x100900028, 0x100943000)` on segment `0x104010668`, generation `1`: C40 reported it reclaimed and eligible; C41 reported `tailConsidered=0` and `tailConsumed=0`, so it was not reused or force-consumed. The C40/C41 regression passed. Existing bounded weak/dead-object behavior remained passing; no new weak/finalization feature was added.

## Three-boot authoritative evidence

Evidence root: `out/dotnet/c011ec50-productionized-second-collection/run-20260827-065251862/`
QEMU: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`
C50 marker: `C011EC50`
Semantic harness rewrite required: `false`
Semantic signature: `0x00000001|0x00000001|0x00000002|0x00000008|5`

| Boot | C18/root scan | Collection | Planner | Restart/resume | Post-GC | Serial SHA-256 |
|---|---|---|---|---|---|---|
| 1 | PASS / 4 promoted | 2, gen 1 | COMPACT / relocate | PASS / PASS | 8 allocations | `C1F2047F2756AE2A274DB1D3F92203C40FF98DE4F8FD348D1E231D7287140C3F` |
| 2 | PASS / 4 promoted | 2, gen 1 | COMPACT / relocate | PASS / PASS | 8 allocations | `44B910C18A64F247E5F281BD4F48D0EC3B4F03AB8003C5534A3766EFC371FC3A` |
| 3 | PASS / 4 promoted | 2, gen 1 | COMPACT / relocate | PASS / PASS | 8 allocations | `C2838FE48F649227618DF7F2A3AF2AC7374C91BD971D5462F024F0ED849420A2` |

QEMU debug-log SHA-256 values, in boot order:

`16652A1380984FF6608B16B209236A84DE57A9088AC28336C91927794FAB54DC`
`F8AF7829E0389AA99F075E2E626925A812C57E2E4AC1659111B5BE533FE22442`
`F612A2CB0BF4FAAC08566B9F1AE4B84976C9BBC004553F7FB2EAEAD90E970171`

## Proof versus production comparison

The C49 proof-oriented three-boot configuration passed its C46/C48 diagnostic markers and the same C18 → roots → mark closure → planner → compaction → `RestartEE` → managed-resume chain, but required semantic source rewriting in the smoke harness. The C50 productionized configuration passed the same semantic chain with no C46/C47/C48 semantic compile defines and with static checks proving the durable patched StackFrameIterator/CMM source. Planner results were observed rather than forced. The ordinary runtime-pack build carries the same production semantics without the proof-only diagnostics.

## C42 and diagnostics

C42 remains intentionally disabled. It is a historical third-collection lifecycle experiment with different weak/tail timing and was superseded for this milestone by the C49 second-collection continuation. Re-enabling it would add stress and a separate lifecycle question, not strengthen the C46/C48 productionization result; C50 therefore does not inflate the collection count with an internal trigger. C42 remains available as a regression mode and its exclusion is recorded in the C50 manifest.

Diagnostics retained permanently or in proof builds include C18/C26/C28 root and mark evidence, C34/C39/C40/C41 collection evidence, and historical C43–C49 provenance. The C46/C48 semantic transformations are superseded in C50 production mode but remain behind their historical modes for reproducibility. No broad diagnostics cleanup was attempted.

## Ordinary boot validation and restoration

Three disposable ordinary UEFI/QEMU boots used the ordinary restored kernel and ESP. Each emitted `[NAVIGATOR-SMOKE] result=PASS` and `[KERNEL] Entering main loop (waiting for input)...`; none emitted a fail-fast or page-fault marker.

| Ordinary boot | Serial SHA-256 | Main loop | Navigator | Fail-fast/page fault |
|---|---|---|---|---|
| 1 | `2772A4B85CABBDB13B07159A5A063EB93C2677F7D5D63EF1FB243F802B636E34` | PASS | PASS | none |
| 2 | `03E0700BE95CA9C70ACE50944545E7874BEECA784F960E1FA2177062BF7230AF` | PASS | PASS | none |
| 3 | `A72A8687B3F300C1D6F59A6D4A2F9381B04AD253F6968337F0E7ACF10A61A8E4` | PASS | PASS | none |

The specialized proof kernel and ESP were not left installed. The ordinary `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` were restored to the canonical SHA-256 `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`. The final C50 manifest records the same restoration hash for both.

The generic `smoke-navigator-kernel.ps1 -ScenarioFilter no_policy` wrapper itself returned failure because that lane's broad checker expected unrelated Navigator scenario markers; its serial still contained the ordinary PASS and main-loop markers. The direct ordinary criteria above are the authoritative boot result.

## Regression matrix

| Regression | Result |
|---|---|
| C18 valid state | PASS; authentic manager and `FindMethodInfo == 1` |
| C18 invalid state / fail closed | PASS; invalid PC, null manager, and failed lookup guards retained; malformed C44 state remains fail-closed |
| C26 root scanning | PASS; expected managed frame and four promoted roots |
| C28 mark closure | PASS; queue closed with no invariant failures |
| C34 preflight | PASS; relocation/root-update boundary retained |
| C37 repeated GC | PASS; first and second collection chain retained |
| C39 planner / COMPACT | PASS; authentic planner decision observed |
| C40 reclamation | PASS; compacted frontier/tail state valid |
| C41 allocator provenance | PASS; 8 allocations, 7 fast / 1 rare-refill |
| C42 | Intentionally excluded; historical mode retained |
| C43–C45 | PASS; safety/provenance history retained |
| C46 | PASS; durable caller-FP publication and production execution |
| C47 | PASS; no former invalid base/slot or page fault |
| C48 | PASS; durable iterator ownership and production execution |
| C49 | PASS; second-collection continuation retained |
| C19–C49 chronology guards | PASS in the C50 parser and predecessor manifests |
| PE → ELF | PASS; proof ELF produced and booted |
| linker/source/table guards | PASS; locked source, symbol, and runtime-pack checks passed |
| MASM | N/A; no assembly change |
| ordinary smoke | PASS; three direct ordinary boots |
| `git diff --check` | PASS; only normal LF/CRLF warnings from existing PowerShell files |

## Remaining limitation and next milestone

C50 proves the productionized single-thread AMD64 Workstation path and ordinary boot. It does not add Server GC, concurrent/background GC, LOH, finalization, multi-thread GC, reclaimed-tail forcing, or allocator redesign. The next smallest milestone is to keep this runtime-pack patch in the normal NativeAOT packaging/release pipeline and add a non-invasive CI check that applies the patch to the locked source and verifies the ordinary artifact manifest; no new collector feature is required.

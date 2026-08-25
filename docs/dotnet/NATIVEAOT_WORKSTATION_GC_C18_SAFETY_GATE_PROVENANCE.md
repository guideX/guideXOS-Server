# C011EC43 — C011EC18 safety-gate provenance and safe continuation

## Result

C011EC43 is **Outcome C — gate correctly remains blocking**, with **success
level 1**. The later C42 collection entry is authentic, but the state reaching
the C011EC18 lookup is not a valid later GC phase or a valid native frame. It
is a malformed transition-frame snapshot. The existing fail-closed guard was
therefore retained and no continuation was attempted.

The exact conclusion is:

> C43 preserved the original C18 safety guarantee by proving that the later
> state does not satisfy the original transition-frame/root-scanning
> invariant. Weakening the guard would permit GC root enumeration to consume
> a non-code value as a control PC and would be unsafe.

## Locked identity and repository baseline

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Starting branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `89447e2e2ed7367ce53a1046246baee45ec9a42e`
- Starting subject: `Add C011EC42 reclaimed gen1 lifecycle proof`
- Upstream: `origin/v1.1_DOTNET_SUPPORT`
- Starting ahead/behind: `0 / 0`
- Starting worktree: clean, no untracked files
- NativeAOT: `9.0.0`
- Architecture: AMD64
- GC: Workstation
- GC interfaces: `5.3 / 2`
- Locked runtime source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

C42 was already present at the upstream tip. This differs from the expected
handoff note that C42 had not been pushed, but the history was clean and the
commit was an exact ancestor, so no synchronization or history rewrite was
performed.

## C011EC18 provenance

C011EC18 was introduced by commit
`4f6a1f2ea5b38694260bf6deb4d0dfcaff96054f`, **Fix NativeAOT transition frame
control PC**, on 2026-08-16. Its original objective was to prove that the
stock AMD64 NativeAOT `PInvokeTransitionFrame::m_RIP` is the authentic managed
control PC and that the production `CoffNativeCodeManager` can resolve that PC
and its method metadata. The original C18 run proved:

- transition RIP in the registered managed range
  `[0x10001000, 0x10050950)`;
- a non-null production code manager;
- successful `FindMethodInfo` with non-null method metadata; and
- a valid frame-pointer boundary.

The proof-only injection is in
`scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1:888-912`.
The locked runtime implementation is
`src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp`, function
`StackFrameIterator::CalculateCurrentMethodState`, line region `1913-1935`.
The exact guards are:

```cpp
m_pCodeManager = dac_cast<PTR_ICodeManager>(
    m_pInstance->GetCodeManagerForAddress(m_ControlPC));
FAILFAST_OR_DAC_FAIL(m_pCodeManager);
FAILFAST_OR_DAC_FAIL(m_pCodeManager->FindMethodInfo(
    m_ControlPC, &m_methodInfo));
```

For the C42 failure specifically, `GetCodeManagerForAddress(0x1030BDF0)`
returned null, so the first guard at the lookup site failed before a method
lookup could be attempted for that malformed frame. The earlier
`c18FindMethodInfoAttempts=9` and `c18FindMethodInfoSuccess=9` values in the
C43 marker belong to the already-proven valid iterator observations from the
same suspended collection; they do not validate the later null-manager frame.

The C18 proof tag is `C011EC18`; the locked PAL fail-fast emitted at the C42
boundary is `FAIL_FAST reason=47435354 detail=0000000000000000`. The C42
diagnostic boundary is `C0420010` / `C011EC42-BLOCKED`; C43 adds the
allocation-free observation marker `C011EC43-C18-GATE` but does not alter the
locked fail-fast.

The invariant is Type A for a frame that reaches the managed-frame path:
`CalculateCurrentMethodState` must have an authentic executable managed
control PC, a non-null code manager, and valid method metadata before
`GcScanRootsWorker` calls `EnumGcRefs`. It protects transition-frame and
stack/unwind correctness, managed-frame identity, and root-scanning safety.
It is not a collection-ordinal, condemned-generation, planner, allocator, or
reentrancy assumption.

The runtime already has a separately proven C23 native-frame path. C42 was
built with the retained C23-C27 instrumentation, but the observed state does
not match that path: it has no native unwind classification, no valid native
runtime-function frame, and no valid managed frame either.

## Exact C42 mismatch

C42's managed workload used genuine `new byte[65536]` pressure after the C40
and C41 predecessor proofs. It performed exactly two allocations before the
next collection entry. No `GC.Collect`, internal GC entrypoint, allocator
routine, allocation pointer, generation boundary, segment selection, or
collection ordinal was manually changed.

The authoritative C42 three-boot evidence and a fresh one-boot reproduction
agree on the following state immediately before the locked guard:

| Field | C42 observed value |
|---|---:|
| Collection count before entry | `4` |
| Natural allocations before entry | `2 x 65536` payload bytes |
| Thread | `0x000000000397FCC0` |
| Allocation context | `0x000000000397FCC0` identity in C42 record |
| GC heap | `0x0000000010168EF0` |
| Allocation pointer / limit | `0x100A10318 / 0x100A12060` |
| Ephemeral boundary | `[0x100900000, 0x100B00000)` |
| Ephemeral segment / generation | `0x104010710 / 0` |
| Collection entry | observed naturally; planner not reached |
| Condemned generation | not yet validly published; C42 field remains `0` sentinel |
| C18 iterator frame | `0x100811F38` |
| C18 iterator control PC | `0x1030BDF0` |
| C18 iterator SP / FP | `0x4E3D2C1B0AF9E8D7 / 0x10000` |
| C18 iterator flags | `0xDAC9B8A7` |
| Code manager | `0` |
| C18 fail-fast | `47435354` |

The observed control PC `0x1030BDF0` is the C42 target array EEType value,
not an executable managed or native instruction address. The frame address
`0x100811F38` exactly equals the prior C40 `neighborDestinationEnd` in the
relocated heap region:

```text
C40 neighborDestinationStart = 0x100801F20
C40 neighborDestinationEnd   = 0x100811F38
C40 recovered tail            = [0x100900028, 0x100943000)
```

The C42 state therefore differs from the valid C18 state in all safety-critical
dimensions: frame provenance, executable control-PC identity, stack register
validity, manager lookup, and method metadata. It is not a legitimate later
collection identity that can be admitted by changing a collection ordinal.

## Why the mismatch is authentic but unsafe

The collection trigger is authentic allocation pressure. The serial chronology
is C37 completion, C40 compaction/reclamation, C41 post-GC allocator
provenance, C42 preflight, two ordinary 64-KiB allocations in the separate
generation-0 ephemeral domain, C42 collection entry, then the C18 iterator
initialization and null-manager guard.

The three earlier C42 boots agreed on the same malformed values. A fresh C42
one-boot reproduction from the clean C42 HEAD produced the same values and
restored both ordinary artifacts. The state is thus naturally reached, but
natural reachability does not make it safe. `GcScanRootsWorker` would otherwise
continue from an invalid transition frame and call the managed-frame root
enumerator without a valid manager or method record.

No C43 marker claims planner, root scan completion, mark, compaction/sweep,
restart, managed resume, allocator eligibility, or reclaimed-tail reuse.

## C43 instrumentation change

C43 makes no production-GC semantic change and does not refine acceptance. It
adds one append-only diagnostic field for the last iterator-initial frame and
emits `C011EC43-C18-GATE` immediately before the existing C42 bounded stop and
the locked C18 fail-fast. The marker uses fixed scalar fields and serial
helpers only; it performs no managed allocation, dynamic formatting, lock
acquisition, stack scan, or re-entry.

The marker records `oldGateWouldFire=1`, `laterStateRecognized=0`, and
`safeRefinementPredicates=0`. `C011EC43-C18-SAFE` is intentionally absent.

## Authoritative C43 validation

The corrected C43 run used QEMU 11.0.0
(`v11.0.0-12122-ga4bb4b10c9`) and three independent fresh boots:

```text
evidence root = out/dotnet/c011ec42-reclaimed-gen1-lifecycle/run-20260824-193342707
proof kernel  = 89D63EFF477888BDDBEDC9C795F9EB4CCBD46B2AED94A95C46513122A60CBB4B
first-run     = 6494D5A952A491AF718FA3765692974DD600500791F599061FBD7475EC707084
repeat-1      = EA481ED64D0F88764A85E437B9A95A45A4FC40E610BC9C1CA9D4B0B6EF83DE92
repeat-2      = 6FC900EF63C5AFADE98C50FC3A1315B828AE506A8BD787F6DA8E9CA8C33BDBC7
```

All three C43 gate records agreed on the semantic fields:

```text
collectionBefore=4 allocationCount=2 collectionEntry=1
c18IteratorFrame=0x100811F38 c18IteratorControlPC=0x1030CE00
c18ObservedCodeManager=0 c18ExpectedManagedManager=0x102613D0
c18FailFastReason=0xEC1801 oldGateWouldFire=1
laterStateRecognized=0 safeRefinementPredicates=0
```

The two allocations were ordinary `new byte[65536]` allocations in the
generation-0 ephemeral segment `0x104010710`, with allocation pointer/limit
`0x100A10318 / 0x100A12060`. The next collection entry was therefore the
natural counter transition after `collectionBefore=4` (the next ordinal would
be 5), but no valid condemned-generation value was published before the
fail-closed stop. The locked PAL fail-fast followed with
`reason=47435354`.

The C43 marker was emitted after the exact C18 manager lookup observer, so the
reported observed manager is the actual null lookup result, not a previous
successful manager. The collection stopped before root-scan completion, mark,
planner, compaction/sweep, `RestartEE`, or managed resume. No C40-tail
eligibility or reuse conclusion was made.

The direct valid-state C18 regression was also exercised in
`out/dotnet/gc-stack-provider-transition-frame-control-pc/run-20260824-193942122`.
Its first fresh boot emitted the original managed-range proof, a non-null
production manager, `C011EC18 FindMethodInfo ... result=00000001`, and reached
`C011EC15`. The script's final assertion could not parse the later
`c18FindMethodInfoSuccess` field because the very long C011EC15 UART line was
truncated immediately after `c18AuthenticManager`; this is a harness-output
postcondition issue, not a runtime C18 failure. The direct C18 marker and
successful `FindMethodInfo` line are intact, and the historical C18 document
records the three-boot accepted-state proof. C43 did not change the C18
acceptance predicate.

## Regression matrix and restoration

The predecessor evidence remains authoritative and was not relabeled:

- C37 repeated collection: retained and PASS in the C42 predecessor matrix.
- C39 authentic `COMPACT`: retained and PASS in the C42 predecessor matrix.
- C40 authentic reclamation: retained and PASS; the published tail is
  `[0x100900028, 0x100943000)`, generation 1, segment `0x104010668`.
- C41 allocator provenance: retained and PASS; ordinary resumed allocations
  use the separate generation-0 ephemeral domain.
- C42 natural next-collection entry: retained and reproduced 3/3; C43 adds
  the gate record but does not claim continuation.
- C19-C42 chronology and locked source/linker/table guards: retained; no
  production runtime source or locked source was changed.
- PE-to-ELF converter: PASS (`tools/dotnet/test_pe_to_elf_v2_fixed_base.py`).
- PowerShell parser: PASS.
- MASM compile: not applicable; C43 changed no assembly file.
- `git diff --check`: PASS before handoff.

The proof harness restored the ordinary kernel and ESP after every run. Both
ordinary artifacts were restored to SHA-256
`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
No repository-owned QEMU process remained after validation.

## Preserved predecessor chain

- C37 repeated-collection proof remains retained.
- C39 retained the authentic `COMPACT` planner result.
- C40 retained authentic reclamation:
  `[0x100900028, 0x100943000)`, size `0x42FD8`, generation 1, segment
  `0x104010668`.
- C41 retained separate generation-0 allocator provenance on segment
  `0x104010710`.
- C42 retained natural continuation, two 64-KiB allocations, and next
  collection entry without claiming tail reuse.

C43 does not change any predecessor chronology guard or claim reclaimed-tail
eligibility after the blocked collection.

## Safety and outcome

- Category: **Type A — permanent runtime/root-scanning invariant**.
- Later state authentic: **yes**, through natural allocation pressure and
  repeated independent QEMU boots.
- Later state safe: **no**, because the transition-frame snapshot is invalid
  and the control PC is not executable code.
- Gate change: **none**; only allocation-free provenance capture was added.
- Original valid C18 state: retained by the C18 historical proof and strict
  manager/method predicates.
- Original invalid state: still reaches the locked fail-fast.
- Later valid state: not admitted, because no later valid state was proven.
- Arbitrary invalid state: remains fail-closed at the same manager/method
  guards; no unconditional success or warning path was added.

Therefore C43 is Outcome C / Success Level 1. The next smallest milestone is
to isolate and repair the source of the malformed transition-frame pointer in
the later allocation-pressure collection, while retaining this gate. That is
a separate runtime correctness investigation, not a safety-gate refinement.

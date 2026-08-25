# C011EC47 — Post-Root-Scan Relocation-Root Page-Fault Provenance

## Result

C011EC47 reached Success Level 3 and is Outcome B: the exact first
valid-to-invalid state transition was proven, but no repair is retained.
The address `0xFFFFFFFFFFFFFF90` is produced by the authentic NativeAOT
reverse-P/Invoke transition-frame slot path, not by `GCHeap::Relocate`, a
managed object reference, or a GC relocation delta.

The final three independent single-boot QEMU reproductions agree on:

* NativeAOT 9.0.0, AMD64, Workstation GC, GC interfaces 5.3 / 2.
* collection 2, condemned generation 0, during stack-root enumeration;
* `CoffNativeCodeManager::UnwindStackFrame` at `0x1008D6F8`;
* signed reverse-P/Invoke slot offset `-0x70`;
* FP base `0`, producing `0 + (-0x70) = 0xFFFFFFFFFFFFFF90`;
* the first invalid state immediately after C46 rehomes `REGDISPLAY.pRbp`
  to iterator-owned `m_FramePointer` storage whose value is zero.

No null guard, skipped root, suppressed callback, C18 weakening, C34 bypass,
or page-fault semantic change was added. A candidate follow-on handoff fix was
tested once, reached the planner in that boot, but did not establish stable
three-boot post-repair behavior and was reverted. It is not part of this
result.

## Repository handoff and identity

| Item | Evidence |
| --- | --- |
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Starting branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `f203d2c8f5dfec5bce871d3cbf004533259ca412` |
| Starting subject | `Fix NativeAOT REGDISPLAY frame-pointer handoff` |
| Upstream | `origin/v1.1_DOTNET_SUPPORT` |
| Starting divergence | ahead 0 / behind 0 |
| Starting worktree | clean |
| C46 ancestry | C46 was HEAD and a clean ancestor before C47 changes |
| C46 push state | C46 was already present at the configured upstream; it was not amended |
| Final worktree before commit | only the focused C47 script, diagnostics, platform, and this document |

Locked runtime identity:

* NativeAOT / runtime pack: `9.0.0`.
* Architecture: AMD64.
* GC: Workstation.
* GC interfaces: `5.3 / 2`.
* Runtime source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The identity is recorded in
`docs/dotnet/GUIDEXOS_NATIVEAOT_MINIMAL_RUNTIME_PACK.md` and the existing
Workstation GC proof documents. No runtime, GC flavor, architecture, ABI, or
interface version was changed.

## C46 handoff retained

C46 remains the prerequisite and is not undone. The final C47 serial records
retain the authentic managed PC, non-null `CoffNativeCodeManager`, successful
`FindMethodInfo`, C18 acceptance, one managed frame, four promoted roots, and
mark closure. The retained C46/C26/C28 values are:

* managed frames: `1`;
* total promoted roots: `4`;
* promote attempts / entries: `4 / 4`;
* register roots: `1`;
* stack roots: `1`;
* mark writes: `8`;
* objects scanned: `8`;
* queue invariant failures: `0`;
* `C011EC26 COMPLETE`: reached;
* `C011EC28 COMPLETE`: reached;
* `C011EC34-PREFLIGHT`: reached during the first collection.

C18 remains fail-closed and was not changed. C34 remains an authentic
relocation-root preflight and was not bypassed. The C46 representation rule
also remains: `REGDISPLAY.pRbp` is storage for the FP value, while `GetFP()`
dereferences that storage.

## Fault context

The fault is not in the first collection's C34 relocation callback. C34 first
proves a real managed root update path with `condemnedGeneration=0`,
`compacting=1`, `relocating=1`, first root slot
`0x0000000004EC1A80`, and old managed object
`0x0000000100A01F38`. The C47 fault occurs in the next collection's authentic
stack-root walk, before that collection's relocation preflight.

| Field | Value |
| --- | --- |
| Collection ordinal | `2` (`C2-GC-ROOTS`) |
| Condemned generation | `0` |
| Current phase | `GCToEEInterface::GcScanRoots` → thread stack provider → managed-frame unwind |
| Current managed PC | `0x00000000100023AA` |
| Current method info | run-specific, e.g. `0x0000000004EC1408` |
| Current SP | `0x0000000004EC1AE0` |
| Current FP before divergence | `0x0000000004EC1B70` |
| Current frame | `0x0000000004EC1A78` |
| Current frame thread storage | `0x0000000004EC1AE0` |
| Runtime thread identity | `0x0000000003985CC0` |
| Faulting RIP | `0x000000001008D6F8` |
| Fault address / CR2 | `0xFFFFFFFFFFFFFF90` |
| Error | page-not-present read, vector 14, error code `0`, CPL 0 |
| Flags | `RFLAGS=0x00000206` |

The final three runs produced the same RIP and CR2. The fixed scalar QEMU
register record from the final run was:

```text
RAX=000000000000000A RBX=FFFFFFFFFFFFFF90 RCX=000000000000000A RDX=00000000000003F8
RSI=0000000000000000 RDI=0000000004EC12C0 RBP=0000000004EC0A00 RSP=0000000004EC08C0
R8 =0000000000000046 R9 =0000000000000000 R10=0000000010120950 R11=0000000010121B8D
R12=0000000000000003 R13=FFFFFFFFFFFFFF90 R14=0000000004EC1408 R15=0000000000000000
RIP=000000001008D6F8 RFL=00000206 CR2=FFFFFFFFFFFFFF90
```

The relevant CPU operands are therefore `RBX=0xFFFFFFFFFFFFFF90`,
`R15=0`, and `R13=0xFFFFFFFFFFFFFF90` (`-0x70`). The production instruction
reads `[RBX]` and faults. The probe captured those operands without reading
the invalid slot value.

## Exact production path

The exact call chain is:

```text
GCToEEInterface::GcScanRoots
  -> thread stack-root provider
  -> StackFrameIterator::CalculateCurrentMethodState
  -> StackFrameIterator::Next / NextInternal
  -> CoffNativeCodeManager::UnwindStackFrame
  -> reverse-P/Invoke GC-info decode
  -> basePointer + slot
  -> PInvokeTransitionFrame** slot read
```

The locked source is:

* `src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:1913-1948`,
  `CalculateCurrentMethodState`;
* `src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:1523-1566`,
  `Next` and `NextInternal`;
* `src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp:651-707`,
  `UnwindStackFrame` and the reverse-P/Invoke branch.

The faulting source statement is the locked production statement at line 692:

```cpp
*ppPreviousTransitionFrame = *(PInvokeTransitionFrame**)(basePointer + slot);
```

The final C47 artifact disassembly maps `0x1008D6F8` to:

```asm
1008D6F3: call 0x1007F4D0       ; bounded C47 pre-read observer
1008D6F8: mov (%rbx),%rbx       ; production transition-frame slot read; faults
1008D6FB: lea (%r15,%r13,1),%rcx
```

The code-manager function begins at `0x1008D4A0`. The decoder's signed slot
is sign-extended at the generated code's `movslq` operation, and the reverse
branch selects FP because the decoded stack-base register is `5` (RBP).

## Arithmetic and descriptor provenance

The exact C++ inputs captured immediately before the production read are:

| Input | Value |
| --- | --- |
| `stackBaseRegister` | `5` (RBP) |
| `basePointer` | `0x0000000000000000` |
| `slot` | signed `INT32 -0x70`, sign-extended as `0xFFFFFFFFFFFFFF90` |
| `slotAddress` | `0xFFFFFFFFFFFFFF90` |
| relocation delta | not involved |
| destination object | not involved |
| interior offset | not involved |
| `previousTransitionFrameOut` storage | run-specific, e.g. `0x0000000004EC14D8` |

Thus:

```text
uint64(basePointer) + sign_extended_int32(slot)
= 0x0000000000000000 + 0xFFFFFFFFFFFFFF90
= 0xFFFFFFFFFFFFFF90
```

This is null base plus `-0x70`, not a small invalid managed object, unsigned
relocation underflow, negative relocation delta, truncated destination, or
poisoned interior pointer. The offset is valid metadata for this function.

The authoritative metadata from the final runs is:

| Metadata | Value |
| --- | --- |
| Runtime function / main runtime function | `0x0000000010316324` / `0x0000000010316324` |
| Unwind-info address | `0x00000000101353EC` |
| Unwind-info size | `0x16` |
| GC-info descriptor | `0x0000000010135403` |
| Block flags | `0x8` (`UBF_FUNC_REVERSE_PINVOKE`) |
| Decoder | `GcInfoDecoder(GCInfoToken(p), DECODE_REVERSE_PINVOKE_VAR)` |
| Offset type | signed `INT32`, then pointer-width sign extension |

No signed-offset or descriptor-decoder defect was found. No structure packing,
alignment, or field-width mismatch is implicated. The layout under test is
the locked AMD64 `REGDISPLAY` storage contract.

## First-divergence checkpoints

For the failing second-collection frame, the C47 iterator markers are:

| Checkpoint | `fp` from `GetFP()` | `pRbp` storage pointer | `m_FramePointer` | Meaning |
| --- | --- | --- | --- | --- |
| `C011EC47` 1 | `0x4EC1B70` | `0x4EC1A80` | `0` | valid input before method-state calculation |
| `C011EC47` 2 | `0x4EC1B70` | `0x4EC1A80` | `0` | valid state after method-state calculation; `GetFramePointer` returned null |
| `C011EC47` 3 | `0x4EC1B70` | `0x4EC1A80` | `0` | last valid state before `NextInternal` |
| `C011EC47` 4 | `0` | `0x4EC12B0` (`&m_FramePointer`) | `0` | first invalid state after C46 rehome |
| `C011EC47-RELOC-CALC` | `0` | `0x4EC12B0` | `0` | FP-selected base becomes zero |
| `C011EC47-ROOT-UPDATE` | not read | — | — | records computed transition-frame address only |

The first valid checkpoint is iterator checkpoint 3. The first invalid
checkpoint is iterator checkpoint 4. The exact transition is the C46
`StackFrameIterator::Next` rehome of `m_RegDisplay.pRbp` to `&m_FramePointer`
before `NextInternal`; for this frame `m_FramePointer` is null. The code
manager then correctly calls `GetFP()` through that now-authoritative pointer,
correctly selects FP from GC metadata, and correctly computes the address from
the invalid base. This is why the exact classification is stale/invalid
REGDISPLAY state rather than a wrong `-0x70` decoder result.

## Root inventory and four-root correlation

The faulting record is deliberately not called a managed GC root:

* `rootOrdinal=0xFFFFFFFF`;
* `rootKind=7`, C47's bounded label for a reverse-P/Invoke transition-frame
  slot;
* `pinned=0`, `interior=0`, `byref=0`;
* `slotValueObserved=0` because reading the slot is the faulting operation.

The C46 four-root set remains distinct. The bounded first-collection records
show:

| Root observation | Slot address | Stored value | Flags / kind | Relocation correlation |
| --- | --- | --- | --- | --- |
| register-root candidate | `0x4EC1A08` | `0x4EC0B18` | flags `1`, category 3 | no faulting-record match |
| managed stack-root candidate | `0x4EC1A80` | `0x100A01F38` | flags `0`, category 3 | C34 first-root slot; no faulting-record match |
| bounded category-3 null candidate | `0x3985D48` | `0` | category 3 | not the faulting record |
| post-stack source | slot/value not surfaced by the bounded record | non-null promotion observed | source code `3` | not the faulting record |

C26 proves these four promotion entries, four attempts, one register root, and
one stack root. The first managed object `0x100A01F38` was marked and included
in the eight-object mark closure. C34's first relocation preflight reports the
same authoritative managed root slot `0x4EC1A80` and object value
`0x100A01F38`.

The second collection's bounded managed candidates immediately before the
fault were separate from the transition-frame read:

* `0x4EC1A98 -> 0x100A01058`;
* `0x4EC1AA0 -> 0x100A11088`;
* `0x4EC1AA8 -> 0x100A210B8`;
* `0x4EC1AC0 -> 0`.

They have ordinary object-reference shape and flags `0`; the page fault occurs
after these candidate observations while the iterator is reading transition
metadata. No candidate object address, object header, EEType, relocation
destination, or handle slot was substituted for the transition-frame slot.

## Domain separation and lifetime audit

The following domains were kept separate throughout the proof:

* root slot address: a managed location such as `0x4EC1A80`;
* managed reference value: an object pointer such as `0x100A01F38`;
* object header / EEType: not read by the C47 probe;
* relocation destination / delta: not involved in the fault;
* transition-frame slot address: `basePointer + slot`, which becomes
  `0xFFFFFFFFFFFFFF90`;
* `pRbp`: a pointer to FP storage, not the FP value;
* `GetFP()`: the value loaded through `pRbp`.

The active stack frame and thread identity remain live at the first invalid
checkpoint. This is not a stale stack slot after frame lifetime ended. It is an
in-walk REGDISPLAY handoff state transition. The prior C44 transition-frame
residue is not the faulting storage: no old physical RBP location, old reverse
P/Invoke slot, or pre-C46 saved RSI location is dereferenced by C47's probe.
C46's corrected handoff is the contributing state; C44's malformed-frame
storage is exonerated as the direct producer of the fault address.

## Classification

* Wrong-root vs wrong-value vs wrong-computation: Branch C in the requested
  taxonomy—correct reverse metadata, but an invalid FP-derived base feeds the
  address computation. It is not Branch A (bad managed reference), Branch B
  (wrong managed root slot), Branch D (dead stack lifetime), or Branch E
  (observer fault).
* Root-cause code: **Code 10 — stale REGDISPLAY/stack state**.
* Exact cause: C46's iterator-owned `pRbp` handoff is applied while
  `m_FramePointer` is null for this active frame, so the subsequent FP-based
  reverse-P/Invoke slot calculation consumes zero as its base.
* C34 invariant: not violated. C34 first-collection preflight passes; the
  failing second-collection frame faults before that collection's relocation
  preflight.
* Null-root status: this is not a valid null managed root. The null is the
  iterator's FP value, and the production reverse-slot operation is not
  specified to read from a null base.

## Repair decision and progression

No repair is retained. The production statement and page-fault behavior remain
unchanged. A candidate that captured the pre-rehome `GetFP()` even when
`pRbp` was still physical was tested once. That boot advanced through authentic
root relocation, planner entry/return, compact-or-sweep return, and GC done;
subsequent boots under the diagnostic build stalled at different earlier
points. Because three-boot post-repair semantic agreement was not proven, the
candidate was reverted rather than presented as a safe runtime fix.

Final no-repair progression:

* root scan and C18: reached;
* C26 root enumeration completion: reached;
* C28 mark closure: reached;
* C34 first-collection relocation preflight: reached;
* second-collection stack-root walk: reached;
* former transition-frame read: faults at `0xFFFFFFFFFFFFFF90`;
* planner, `COMPACT`/sweep, `RestartEE`, managed resume, and C40 reclaimed-tail
  eligibility: not reached in the final no-repair runs.

## Diagnostics and safety

The C47 diagnostics are bounded and allocation-free:

* eight iterator records and eight root-update records maximum;
* fixed scalar fields only;
* no arbitrary memory read in the pre-read observer;
* no lock, recursion, dynamic allocation, or page-fault handler change;
* the invalid slot value is explicitly marked `valueObserved=0`;
* production dereference remains the original operation under test.

Markers:

* `C011EC47-ITERATOR` — four iterator checkpoints;
* `C011EC47-RELOC-CALC` — descriptor, base, signed offset, and computed slot;
* `C011EC47-ROOT-UPDATE` — bounded transition-frame slot record;
* the existing `C011EC34-PREFLIGHT`, `C011EC26 COMPLETE`, and
  `C011EC28 COMPLETE` markers remain active.

## Three-boot evidence

Each entry below is a fresh independent QEMU process from a separate
single-boot invocation on the final no-repair C47 source state. QEMU was
11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), with `-accel tcg,thread=single` and
`-smp 1`.

| Evidence run | Fault RIP | CR2 | Serial SHA-256 | QEMU debug SHA-256 | Proof kernel SHA-256 |
| --- | --- | --- | --- | --- | --- |
| `run-20260825-101446938` | `0x1008D6F8` | `0xFFFFFFFFFFFFFF90` | `CDCBD1D447CF7CC06634AF3093D5937DB5DBF9BD51CED7ABA44147ED3CAFC0B8` | `2C0E1839A4E29259E566E5AF6976BD49458F0AAE910B86A68C097DBE89112153` | `4105FD033E6321981A521F17522E783BCA0B4ACC6FE40C085D9AA7746C73B299` |
| `run-20260825-101719807` | `0x1008D6F8` | `0xFFFFFFFFFFFFFF90` | `349A9CC121840C3FADA63F6E62064EC6938C04C9E20CA8B2939BD3526F9B356D` | `38ECBBE9C08B5C0219E8CD03FF8F1CFF428A8146103EE3B731CC86C6CA8688FC` | `F73A65C72795085394B7FB44EB1CD05855F61BF83A8EA93DDA70AA34E4B48AF3` |
| `run-20260825-101935042` | `0x1008D6F8` | `0xFFFFFFFFFFFFFF90` | `41C20C929E76AE1566C01CFC53D53A1CA0EA481AF6FBEB027D75CC91F123EE5F` | `351ECEAA38BB4FA7B6B844A53D05523C9614DD0061CFE3AE27F96BC52CEE6811` | `9E624A3D3B65F6D49CD6A7DF3D1446708C0BF88AFB65BBA79CC37A1A23570B19` |

All three runs agree on the root kind, null base, signed `-0x70` offset,
faulting RIP, CR2, C18/root-scan completion, and first divergence. No
invariant failure was emitted. No fail-fast replacement was introduced; the
production page fault remains observable.

The latest final-run proof payload hashes are:

```text
NativeAotGcSingleThreadSuspendEe.exe  5037661D9808800C1807501C2E52062719AD10032210E69ED0E9360B3C2520A1
NativeAotGcSingleThreadSuspendEe.elf  429465C56369695160661883E81EF818228896167C48918DCAED179382A9C881
NativeAotGcSingleThreadSuspendEe.map  0B304D08BAC6A83093DA5FC89B54253196E2F410C1CE9F1E77EDBC706A1574C6
```

## Regression and artifact checks

| Check | Result |
| --- | --- |
| C18 valid-state path | PASS, retained through C46 and observed in all C47 boots |
| C18 invalid-state fail-closed path | PASS retained from C43; not rerun in this narrow fault run |
| C37 repeated-GC behavior | PASS retained first-collection behavior; C2 reaches root walk before fault |
| C39 authentic COMPACT | retained from C46; not reached after the final no-repair fault |
| C40 authentic reclamation | retained from C46; not reached after the final no-repair fault |
| C41 allocator provenance | retained from C46; not reached in the final fault window |
| C42 natural later trigger | retained from C46; not reached in the final fault window |
| C43 C18 provenance | retained; no C18 source change |
| C44 frame provenance | retained; transition-frame residue exonerated as direct fault producer |
| C45 wrong-base provenance | retained and extended by C47 checkpoints |
| C46 REGDISPLAY repair | PASS; no C46 code path was undone |
| C19–C46 chronology guards | PASS in the C47 harness and final serials |
| PE→ELF converter | PASS in each final harness build |
| linker/source/table guards | PASS in each final harness build |
| MASM | not applicable; no assembly source changed |
| ordinary boot smoke | PASS as retained harness baseline; ordinary artifacts restored afterward |
| PowerShell parse | PASS |
| `git diff --check` | PASS, with only Git's LF/CRLF normalization warnings |

Sensitive allocations: `0` in the C47 diagnostic path. No unrelated QEMU
process was terminated. The harness cleaned up only its owned QEMU instances.

## Ordinary artifact restoration

The ordinary kernel and ESP kernel were restored by the harness `finally`
path. Both hashes were independently checked against the known ordinary hash:

```text
kernel/build/amd64/bin/kernel.elf  75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
ESP/kernel.elf                     75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

## Files and next milestone

Files changed for C47:

* `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1` — C47 mode,
  bounded marker collection, exact pre-read parsing, and final evidence
  classification;
* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`
  — fixed-size C47 scalar records;
* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
  — allocation-free iterator/pre-read observers;
* this document.

Documentation path:
`docs/dotnet/NATIVEAOT_WORKSTATION_GC_RELOCATION_ROOT_FAULT_PROVENANCE.md`.

Outcome: **B — exact cause proven, repair deferred**.

The next smallest milestone is to correct the active-frame null-local-FP
handoff in the NativeAOT iterator/code-manager boundary, with a new focused
proof that first validates stable C18 and C46 behavior, then proves the same
transition-frame root read uses a legitimate FP base across three fresh boots.

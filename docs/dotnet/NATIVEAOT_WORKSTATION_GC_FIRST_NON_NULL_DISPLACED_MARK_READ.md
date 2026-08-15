# guideXOS Server NativeAOT Workstation GC - C011EC14 First Naturally Displaced Root Mark-State Read

## Final classification

**Outcome D - natural non-null displacement requires a new semantic boundary.**

This audit traced the locked Workstation queue and ran three fresh QEMU boots.
The authentic path inserted the real NativeAOT thread-static storage object
once, selected slot 0, advanced the cursor from 0 to 1, and observed
`old_o == nullptr`. The existing proof-only continuation boundary then
failed fast before a second insertion. That is not a C011EC14 marker and is
not a crash/corruption classification: the source-valid route to a naturally
non-null displaced object requires sixteen more real queue insertions before
slot 0 is selected again, which is a new root/mark-progression semantic unit.

No queue entry, cursor, object, mark bit, sentinel, or runtime identity was
fabricated or modified. No `marked(old_o)` call occurred because no non-null
`old_o` was naturally produced. The target milestone therefore remains
unproven and is correctly stopped at Outcome D.

## 1. Starting Git state and C011EC13 prerequisite

- Starting HEAD: `53e043517c944a191b532d0c948473cbcb562709`
- Branch: `v1.1_DOTNET_SUPPORT`
- Upstream: `origin/v1.1_DOTNET_SUPPORT`
- Starting ahead/behind: `0/0`
- Starting worktree: 139 pre-existing untracked entries under `out/`; no
  tracked modifications. They were preserved. The proof run itself then
  added its intentional harness edit and a new run directory.
- Starting ordinary kernel:
  `kernel/build/amd64/bin/kernel.elf`, SHA-256
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
- Starting ordinary ESP payload: `ESP/kernel.elf`, SHA-256
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
- C011EC13 was already in actual HEAD ancestry. The checkpoint commit is
  `684b6fb507e4158191e52489af32a894dde8fc75`, whose report records the first
  authentic post-queue `old_o == nullptr` decision. It is an ancestor of the
  starting HEAD.

The fresh proof run started from the same committed HEAD after the deliberate
Outcome-D wording change in the proof harness; its recorded pre-run status is
retained in the run manifest. No unrelated files were changed or discarded.

## 2. Locked runtime identity and root identity

The locked identity remained unchanged:

- NativeAOT `9.0.0`
- AMD64
- Workstation GC
- GC interfaces `5.3 / 2`
- locked runtime source commit
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- active adapted PAL archive SHA-256
  `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`

The managed field was `[ThreadStatic] byte[]? s_gcProofThreadRoot`.
Fresh live values in all three boots were:

- selected sentinel: `0x0000000100A01F38`
- NativeAOT thread-static storage object: `0x0000000100A02F50`
- root slot: `0x000000000393BBE0`
- root slot value: `0x0000000100A02F50`

The first collector root is the storage object, not the sentinel directly.
Managed assignment/readback, suspension, allocation-context fixup, root-slot
identity, storage-object identity, sentinel identity, duplicate-address, and
object-history validators remained proof-only. The sentinel was not directly
processed, and no second object was supplied.

## 3. Locked mark-queue structure

The locked source is the NativeAOT 9.0.0 Workstation checkout used by the
runtime-pack build.

`gcpriv.h:147-168` defines `USE_REGIONS` for this 64-bit non-standalone
configuration and enables `MARK_PHASE_PREFETCH`. The declaration at
`gcpriv.h:1487-1504` is:

```text
class mark_queue_t
{
#ifdef MARK_PHASE_PREFETCH
    static const size_t slot_count = 16;
    uint8_t* slot_table[slot_count];
    size_t curr_slot_index;
#endif
public:
    mark_queue_t();
    uint8_t *queue_mark(uint8_t *o);
    uint8_t *queue_mark(uint8_t *o, int condemned_gen);
    uint8_t* get_next_marked();
    void verify_empty();
};
```

This is a 16-entry bounded exchange table with a circular, linearly
advancing cursor. It is not hashed and it is not a recursive displacement
chain.

The constructor at `gc.cpp:27290-27301` sets `curr_slot_index` to zero and
clears all sixteen slots. The active one-argument `queue_mark` at
`gc.cpp:27303-27335` uses the current cursor directly:

```text
slot_index = curr_slot_index;
old_o = slot_table[slot_index];
slot_table[slot_index] = o;
curr_slot_index = (slot_index + 1) % slot_count;
```

Thus insertion 1 selects slot 0, insertions 2 through 16 select slots 1
through 15, and insertion 17 selects slot 0 again. A populated slot is first
selected again only after cursor wraparound, unless a source `get_next_marked`
operation drains and clears it first. There is no natural second-insertion
replacement of slot 0.

`get_next_marked` at `gc.cpp:27373-27399` starts at the current cursor, clears
each visited slot, tests non-null entries, and advances the cursor when it
returns a newly marked object. `drain_mark_queue` at `gc.cpp:28054-28091`
consumes those entries and, for pointer-containing objects, enters
`go_through_object_cl`. The collector calls `drain_mark_queue` immediately
after the root scan at `gc.cpp:29897-29903`.

## 4. Exact source-valid progression and natural reachability

The observed authentic path was:

```text
soh_try_fit
 -> a_fit_segment_end_p
 -> trigger_ephemeral_gc
 -> GCHeap::GarbageCollectGeneration
 -> GCToEEInterface::SuspendEE
 -> real ThreadStore lock
 -> single-mutator EE suspension
 -> fix_allocation_contexts(TRUE)
 -> GCToEEInterface::GcScanRoots
 -> real managed-thread enumeration
 -> real NativeAOT thread-static provider
 -> real inline root slot
 -> EnumGcRef
 -> genuine non-null storage-object root
 -> WKS::GCHeap::Promote
 -> is_in_find_object_range(o) = true
 -> valid Workstation single-heap-null semantics
 -> is_in_condemned_gc(o) = true
 -> real pre-mark checks
 -> real mark_object_simple
 -> mark_queue.queue_mark(o), insertion 1
 -> slot 0 write, cursor 0 -> 1
 -> old_o == nullptr
 -> existing proof-only NativeAOT continuation fail-fast
```

`mark_object_simple` is locked `gc.cpp:27987-28029`; its first queue call is
at `:28007`. `queue_mark` returns null at `:27321-27322` for the first empty
slot, so that call does not traverse the storage object. The locked collector
then drains the queue after `GcScanRoots`, before a later root-processing unit.

The minimum authentic progression to displace the first storage object is 17
queue insertions total: the storage object plus 16 additional genuine
objects/root promotions before the slot-0 insertion. The fresh run achieved
one authentic insertion. It did not cross the required semantic boundary.

Therefore:

- natural displacement requirement: cursor wraparound, not two insertions;
- displacement insertion ordinal if the queue is not drained: 17;
- additional root/object: yes, at least sixteen genuine source-discovered
  objects or an equivalent later mark-phase source unit;
- child traversal: not required for the queue arithmetic, but it would be a
  new semantic boundary and was not entered;
- broad graph processing, arbitrary root processing, queue draining changes,
  cursor changes, capacity changes, direct `queue_mark`, and direct `marked`
  were not permitted and were not performed.

This is Outcome D under the requested classification. The observed PAL
`RaiseFailFastException` boundary (`reason=0x47435354`) is retained as the
runtime evidence for why this bounded workload did not supply the next
insertion; it is not relabeled as a crash, corruption, deadlock, or C011EC14.

## 5. Queue insertion evidence

For each of the three fresh boots:

- insertion ordinal: 1;
- selected slot/index: 0;
- cursor before/after: `0 -> 1`;
- prior slot value / `old_o`: `0x0000000000000000`;
- new slot value: `0x0000000100A02F50`;
- `old_o != nullptr`: false;
- queue slot provenance: the slot was constructor-cleared and selected by the
  authentic cursor; no slot was preseeded or manually changed;
- displaced-object provenance: none, because the selected prior slot was
  null;
- queue slot address: not exposed by the proof ABI; the source slot is
  `slot_table[0]` in the real `mark_queue_t` instance;
- queue writes: one real slot write and one real cursor write;
- no mark-bit write, segment write, or post-decision C011EC14 mutation.

The live root/storage values and insertion facts were identical in all three
boots. The proof did not claim `old_o == 0x100A02F50`: the storage object is
the newly inserted value, while the naturally selected prior value was null.

## 6. Mark-state helper and representation

The locked definitions are:

- `marked(i)` macro: `gc.cpp:11587`, `header(i)->IsMarked()`;
- `set_marked(i)` macro: `gc.cpp:11588`, `header(i)->SetMarked()`;
- `GC_MARKED`: `gc.cpp:4659`, `(size_t)0x1`;
- `CObjectHeader::SetMarked`: `gc.cpp:4783-4787`;
- `CObjectHeader::IsMarked`: `gc.cpp:4789-4792`.

The mark bit is in the raw method-table word returned by
`RawGetMethodTable()`. `IsMarked` reads that raw word and tests mask `0x1`;
it does not read a separate segment or region field. `SetMarked` would read
the same raw method-table word and OR `0x1` through `RawSetMethodTable`.

This run performed:

- `marked(old_o)` requests: 0;
- mark-state machine-word reads: 0;
- raw word address/value: not applicable and intentionally not supplied;
- mask evaluation/result: not applicable;
- duplicate mark-state reads: 0;
- object-header/method-table reads added: 0;
- segment/region reads added for this milestone: 0;
- `set_marked` attempts/executions: 0/0;
- mark-state writes: 0.

Because `old_o` was null, the source correctly returned before `marked`. The
next source operation for a naturally non-null, unmarked displaced object
would be `set_marked(old_o)` at `gc.cpp:27333`. The prior locked-compatible
post-queue disassembly records its inline form as:

```text
1004daa3: 49 83 4d 00 01    or QWORD PTR [r13+0x0],0x1
```

That instruction was not reached or executed in this Outcome-D run. The
current run has no C011EC14 safe-stop instruction because it did not reach the
non-null branch. The observed fail-fast current RIP was
`0x0000000010004A7B`, which is a bounded continuation boundary, not a
mark-state safe stop.

## 7. Machine-code correlation

The current proof artifact is
`out/dotnet/gc-first-non-null-displaced-mark-read/run-20260814-222001966/artifact-disassembly.txt`.
The inherited locked-compatible correlation for the inline queue path is
retained in
`regression-c011ec13/run-20260814-210415240/post-queue-machine-code.txt`:

- queue slot write: `mov QWORD PTR [rbx],rsi` at `0x1004DA10`;
- cursor write: `mov QWORD PTR [rip+0x209828],rax` at `0x1004DA21`;
- null test: `test r13,r13` at `0x1004DA69`;
- null branch: `je 0x1004E1E2` at `0x1004DA6C`;
- `marked` helper: inlined raw method-table-word `& 0x1` test;
- `set_marked` next mutation: `or QWORD PTR [r13+0x0],0x1` at
  `0x1004DAA3`, not executed.

The machine evidence is used only to correlate the locked source and the
already-proven null branch. No extra raw-header read was added for proof
convenience.

## 8. Validation and EE invariants

All three fresh serial logs recorded the same live root/storage identity and
the same `c14-predecision` line:

```text
queueInsertions=00000001 callbacks=00000001 candidateLoads=00000000
markHelpers=00000001 old_o=0000000000000000 rawMarkReads=00000000
rootSlot=000000000393BBE0 rawRoot=0000000100A02F50
storageObject=0000000100A02F50
```

At the bounded stop:

- one registered managed thread;
- collection initiator remained current;
- ThreadStore lock held: 1;
- EE suspended: 1;
- managed entry prohibited: 1;
- allocation contexts fixed/cleared by the inherited path;
- registry corruption: none observed;
- restart requests/entries: 0/0;
- managed resume: 0;
- graph traversal: 0;
- child-reference reads: 0;
- child objects discovered: 0;
- additional root/object processing after the selected root: 0;
- sweep, compaction, relocation: 0/not entered.

The managed sentinel and user objects remained unchanged. No unexpected
duplicate or overlap was observed in the fresh proof run.

## 9. QEMU evidence

Fresh evidence directory:

`out/dotnet/gc-first-non-null-displaced-mark-read/run-20260814-222001966/`

Exact command lines are in its `commands.txt`. QEMU was
`11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. Proof-kernel SHA-256:
`D3E73E21165CAAD84082E7C62E3035766CA06C523BD122F394EEA932C5EE6739`.

| boot | serial SHA-256 | insertions | slot | old_o | mark reads | result |
|---|---|---:|---:|---|---:|---|
| first-run | `2E9E1F313565A7418DBD53505687AC5E488C632B7EAB681E12453482799C1662` | 1 | 0 | null | 0 | Outcome D |
| repeat-1 | `08DD4BF48468D10E4A316A59D66089C0A9767DDB9D026B812829496FD1AD66BF` | 1 | 0 | null | 0 | Outcome D |
| repeat-2 | `755787F707A2EF7858B1EFDA52231F0BD08A7CEECB64843A73432B6E619F9AD6` | 1 | 0 | null | 0 | Outcome D |

All three runs independently reproduced the same authentic progression,
fail-fast boundary, root identity, null result, zero mark-state reads, zero
mark-state writes, zero traversal, and zero restart/resume.

## 10. Regressions and retained classifications

Focused evidence retained in this session:

- C011EC14: Outcome D, 3/3 fresh QEMU 11.0.0 boots, as above;
- C011EC13: Outcome B, 3/3, under
  `regression-c011ec13/run-20260814-210415240/`;
- C011EC12: Outcome D, 3/3, under
  `regression-c011ec12/run-20260814-210657706/`;
- C011EC11: Outcome A, 3/3, under
  `regression-c011ec11/run-20260814-210937738/`;
- C011EC10: Outcome A, 3/3, under
  `regression-c011ec10/run-20260814-211213783/`;
- C011EC09: fresh attempt retained as NON-CLEAN because its existing
  validator rejected the expected object/duplicate validation line; it was
  not relabeled PASS;
- C011EC08 through C011EC01, primitive/reference/combined `[ThreadStatic]`,
  segment/refill/commitment/allocation, FLS/local storage, native-thread,
  runtime-pack, ELF, stack bounds, exact/general builds, and the other
  validators remain at their historical classifications and were not weakened.

Static checks passed for PowerShell parsing, JSON manifest parsing, serial
evidence parsing, ordinary restoration, and `git diff --check`.

Retained failures remain explicitly retained: historical 64-KiB failure,
stale-cache attempts, initial runtime-pack identity mismatch, native-stack
wrapper/compiler-stderr failure, local-storage teardown failure, C011EC09
serial-wrap validator failure, and historical C011EC11 non-clean/build-race
evidence. The current focused C011EC11 rerun remains separately recorded as
clean Outcome A.

## 11. Ordinary restoration

The proof-only build was restored in the harness `finally` path. Final hashes:

- ordinary build:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`;
- ordinary ESP:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.

No proof QEMU process remained running. No production-shared runtime identity
was upgraded or altered. The only tracked source change in this audit is the
proof-harness classification correction; production NativeAOT support paths
remain unchanged.

## 12. Git handoff and next milestone

The corrected report, proof-harness classification, and selected fresh audit
evidence are intentional milestone files. Pre-existing unrelated/untracked
evidence remains untouched and is not silently included.

Recommended next smallest bounded milestone: establish a source-valid
pre-drain route that supplies the minimum additional genuine root promotions
needed to occupy queue slots, first determining whether NativeAOT's exact
`GcScanRoots` path can expose multiple real roots before the locked
`drain_mark_queue()` call. If it cannot, record the runtime-contract boundary
as a separate classification; do not seed the queue, force wraparound, or
begin broad graph traversal.

## Cross-references

- [C011EC13 post-queue decision](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md)
- [C011EC12 first mark mutation](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md)
- [C011EC11 pre-mark boundary](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md)
- [condemned-generation decision](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md)
- [heap resolution](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md)
- [membership classification](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md)
- [callback entry](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md)
- [NativeAOT thread-static runtime support](NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md)
- [prior bounded non-null old_o report](NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md)

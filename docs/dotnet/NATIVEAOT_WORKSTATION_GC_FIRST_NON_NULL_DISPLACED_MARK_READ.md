# guideXOS Server NativeAOT Workstation GC — C011EC14 First Naturally Displaced Root Mark-State Read

## Final classification

**Outcome E — bounded continuation fail-fast before natural displacement.**

The locked Workstation GC source requires a naturally occupied queue slot to be
selected again after sixteen intervening queue insertions, so the first
non-null displacement is insertion ordinal 17 when the queue is not drained.
The live source-valid workload reached exactly one authentic insertion of the
NativeAOT thread-static storage object into slot 0, observed `old_o == nullptr`,
and then failed through the existing NativeAOT `RaiseFailFastException` bridge
before a second queue insertion. No queue entry was fabricated, no cursor or
slot was changed by proof code, and no `marked(old_o)` read was obtained.

This is not a C011EC14 marker. It is a complete bounded reachability result;
the target mark-state read remains unproven.

## 1. Starting Git and locked identity

- HEAD: `0186ef317267aa9310440bb0943841df36f71740`
- Branch: `v1.1_DOTNET_SUPPORT`
- Upstream: `origin/v1.1_DOTNET_SUPPORT`
- Starting ahead/behind: `0/0`
- Starting worktree: clean (`git status --porcelain=v1` contained only the
  branch header).
- C011EC13 was already present in HEAD and in actual history. Its checkpoint is
  `684b6fb507e4158191e52489af32a894dde8fc75`, “Prove post-queue null old_o GC
  boundary”.
- Ordinary kernel before proof:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
- Ordinary ESP kernel before proof:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
- Locked runtime: NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces
  `5.3 / 2`, source commit
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The existing proof-only C011EC14 observers were audited and reused. No
production runtime identity or ordinary-support path was upgraded.

## 2. Root identity and authenticity

The managed field was `[ThreadStatic] byte[]? s_gcProofThreadRoot`.

The fresh proof values were:

- selected sentinel: `0x0000000100A01F38`;
- NativeAOT thread-static storage object: `0x0000000100A02F50`;
- root slot: `0x000000000393BBE0`;
- root slot value: `0x0000000100A02F50`.

The first root is the storage object, not the sentinel directly. The live
serial evidence showed the same root/storage identity in all three fresh
boots. No second managed object was forced into the queue, no sentinel was
processed directly, and no synthetic object or historical address was used.

The managed assignment/readback and the existing suspension, allocation-
context, root-slot, storage-object, sentinel, duplicate-address, and object-
history validators remained proof-only. No validation failure, overlap, or
unauthorized object mutation was observed before the fail-fast boundary.

## 3. Locked mark-queue source trace

The source was read from the locked runtime checkout used by the proof and
verified against the runtime-pack identity above.

### `mark_queue_t`

Locked `src/coreclr/gc/gcpriv.h:1487-1504` declares:

```text
#ifdef MARK_PHASE_PREFETCH
    static const size_t slot_count = 16;
    uint8_t* slot_table[slot_count];
    size_t curr_slot_index;
#endif
```

The active configuration defines `MARK_PHASE_PREFETCH` under `USE_REGIONS` at
`gcpriv.h:147-168`. The constructor at locked `gc.cpp:27290-27301` initializes
`curr_slot_index` to zero and all sixteen `slot_table` entries to null.

This is a bounded exchange table with a circular, linearly advancing cursor.
It is not a hash table and `queue_mark` itself is not a recursive queue drain.
The cursor selection is:

```text
slot_index = curr_slot_index
curr_slot_index = (slot_index + 1) % slot_count
```

Therefore the first insertion selects slot 0 and changes the cursor `0 → 1`.
Insertions 2 through 16 select slots 1 through 15. Insertion 17 selects slot
0 again with cursor `0 → 1`, unless an intervening `get_next_marked` has
cleared slots and advanced the cursor.

`get_next_marked` at locked `gc.cpp:27373-27399` starts at the current cursor,
clears each visited slot, tests a non-null entry, and can mark/return it. It
does not preserve a populated slot for later displacement. `drain_mark_queue`
at `gc.cpp:28054-28091` repeatedly consumes that operation and, for a returned
object containing references, performs child traversal.

### `queue_mark`

The active one-argument definition is locked `gc.cpp:27303-27335`:

1. `Prefetch(o)` at `:27311`;
2. capture `slot_index = curr_slot_index` at `:27316`;
3. capture `old_o = slot_table[slot_index]` at `:27317`;
4. write `slot_table[slot_index] = o` at `:27318`;
5. write `curr_slot_index = (slot_index + 1) % slot_count` at `:27320`;
6. return null at `:27321-27322` when `old_o == nullptr`;
7. otherwise evaluate `marked(old_o)` at `:27328`;
8. return null at `:27330-27331` if already marked;
9. otherwise execute `set_marked(old_o)` at `:27333` and return `old_o`.

The two-argument overload at `gc.cpp:27337-27371` only applies the source
heap/condemned-generation predicates before delegating to the one-argument
overload. It does not change slot selection or displacement semantics.

`queue_mark` does not recursively process a displaced object. The caller
`gc_heap::mark_object_simple` at locked `gc.cpp:27987-28029` receives a returned
object; only then can the caller add promotion bookkeeping and enter
`go_through_object_cl` at `:28014-28025`.

## 4. Natural reachability in this exact path

The source-valid path is:

```text
soh_try_fit
→ a_fit_segment_end_p
→ trigger_ephemeral_gc
→ GCHeap::GarbageCollectGeneration
→ GCToEEInterface::SuspendEE
→ real ThreadStore lock
→ single-mutator EE suspension
→ fix_allocation_contexts(TRUE)
→ GCToEEInterface::GcScanRoots
→ real managed-thread enumeration
→ real NativeAOT thread-static provider
→ real inline root slot
→ EnumGcRef
→ genuine non-null storage-object root
→ WKS::GCHeap::Promote
→ is_in_find_object_range(o) = true
→ Workstation single-heap null semantics
→ is_in_condemned_gc(o) = true
→ real pre-mark checks
→ real mark_object_simple
→ authentic queue_mark insertion ordinal 1
→ slot 0 write and cursor 0 → 1
→ post-queue `old_o == nullptr` decision
→ existing NativeAOT continuation fail-fast
```

The first root's queue call returns null, so `mark_object_simple` does not
traverse the storage object at that point. The locked Workstation collector
calls `drain_mark_queue()` immediately after `GCScan::GcScanRoots` returns at
`gc.cpp:29897-29903`. Consequently, a non-null displaced entry cannot be
obtained from this one root by simply continuing the same callback: the queue
would be drained before the next root-processing phase.

The minimum source-valid route to a non-null displacement is either:

- seventeen authentic queue insertions before a drain, requiring the first
  storage object plus sixteen additional real root/object promotions; or
- a separate source-valid mark phase/root-processing unit that leaves a real
  entry occupied and later performs the wraparound insertion.

The current workload has one registered managed thread and no additional
candidate root before the continuation boundary. Reaching more roots would be
additional root processing; reaching children would cross the `drain_mark_queue`
and `go_through_object_cl` boundary. Broad traversal, arbitrary extra roots,
queue draining changes, cursor changes, capacity changes, queue seeding, and
direct `queue_mark` calls were not permitted. Therefore the minimum authentic
progression was one insertion, not seventeen, and the milestone is blocked at
Outcome E.

## 5. Mark-state source semantics

The locked macros at `gc.cpp:11587-11589` are:

```text
#define marked(i)      header(i)->IsMarked()
#define set_marked(i)  header(i)->SetMarked()
```

`CObjectHeader::IsMarked` at `gc.cpp:4789-4792` performs one raw method-table
word read through `RawGetMethodTable()` and tests `GC_MARKED`. The locked
constant is `GC_MARKED = (size_t)0x1` at `gc.cpp:4659`. `CObjectHeader::SetMarked`
at `gc.cpp:4783-4787` would read the same raw method-table word and OR bit
`0x1` back through `RawSetMethodTable`.

The exact machine form captured by the prior locked-compatible C011EC13 linked
disassembly for the next unmarked mutation is:

```text
or QWORD PTR [r13+0x0],0x1
```

at the prior linked address recorded in
`out/dotnet/gc-first-root-post-queue-mark-decision/run-20260814-174936183/post-queue-machine-code.txt`.
That instruction is source evidence for the next mutation only. It was not
executed in this C011EC14 run, and there is no current-run raw header address,
raw word, mask result, or safe-stop instruction because no non-null `old_o`
was reached.

## 6. Fresh QEMU evidence

Primary evidence:

`out/dotnet/gc-first-non-null-displaced-mark-read/run-20260814-205844830/`

The exact QEMU command lines are in `commands.txt`. QEMU was
`11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. The proof kernel SHA-256 was:

`80C96C1AE7312D11D5017A48222C33DB88F54019483AB8BEB1F0498626C8E7A6`.

Three fresh boots independently produced the same progression class:

| boot | serial SHA-256 | queue insertions | selected slot | old_o | mark-state reads | result |
|---|---|---:|---:|---|---:|---|
| first-run | `B2D09093A9E9C6169E23B2A70B13FE1F28E52844316D7C65AE5B402EB8A0E60F` | 1 | 0 | null | 0 | Outcome E |
| repeat-1 | `5DF136458F66702374A2003CFE3F3EAEA6119450F7C6CF9DFE1C8C99A9BDB72E` | 1 | 0 | null | 0 | Outcome E |
| repeat-2 | `755787F707A2EF7858B1EFDA52231F0BD08A7CEECB64843A73432B6E619F9AD6` | 1 | 0 | null | 0 | Outcome E |

The serial boundary was `FAIL_FAST reason=47435354`, followed by
`c14-predecision queueInsertions=00000001 callbacks=00000001 ...
old_o=0000000000000000 rawMarkReads=00000000 rootSlot=000000000393BBE0
rawRoot=0000000100A02F50 storageObject=0000000100A02F50`.

At the last valid state, ThreadStore lock held, EE suspended, and managed entry
prohibited were all `1`; restart and managed resume were both `0`. Graph
traversal, child-reference reads, child objects, second-object marking,
mark-state reads/writes, and post-decision mutations were all `0` or not
reached.

## 7. Regressions and retained classifications

Fresh focused regressions in this session:

- C011EC13: Outcome B, 3/3 fresh boots; evidence under
  `regression-c011ec13/run-20260814-210415240/`.
- C011EC12: Outcome D, 3/3 fresh boots; evidence under
  `regression-c011ec12/run-20260814-210657706/`.
- C011EC11: Outcome A, 3/3 fresh boots; evidence under
  `regression-c011ec11/run-20260814-210937738/`.
- C011EC10: Outcome A, 3/3 fresh boots; evidence under
  `regression-c011ec10/run-20260814-211213783/`.
- C011EC09: fresh three-boot rerun remained NON-CLEAN at the existing
  validator assertion for object/duplicate evidence; the validator reported
  `objectHistoryOverflow=00000000` and `duplicateObjectAddresses=00000000`
  but did not find them in the expected validation line. The raw attempt is
  retained under `regression-c011ec09/`; it is not relabeled PASS.
- Script parsing, JSON manifest parsing, serial-evidence parsing, ordinary
  restoration, and `git diff --check`: PASS.

The broader historical validators were not silently relabeled or widened for
this blocked continuation. Existing evidence and classifications are retained
for C011EC09 through C011EC01, primitive/reference/combined `[ThreadStatic]`,
FLS/local storage, native-thread, runtime-pack, ELF, stack-bounds, exact/general
build, script/manifest/serial parsing, and segment/refill/commitment/allocation
validators where prior reports recorded them.

Retained failures include the historical 64-KiB failure, stale-cache attempts,
initial runtime-pack identity mismatch, native-stack wrapper/compiler-stderr
failure, local-storage teardown failure, C011EC09 serial-wrap validator
failure, and C011EC11 historical non-clean/build-race evidence. The current
fresh C011EC11 rerun is reported separately as clean Outcome A; the historical
non-clean record is preserved.

## 8. Restoration and Git handoff

After all proof and focused regression runs:

- ordinary `kernel/build/amd64/bin/kernel.elf`:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`;
- ordinary `ESP/kernel.elf`:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`;
- no proof QEMU process remained. One unrelated QEMU process was observed for
  `D:\dev\guideXOSServerV0.2\ESP`; it was not touched.

The complete audit manifest is
`out/dotnet/gc-first-non-null-displaced-mark-read/run-20260814-205844830/audit-manifest.json`.
The raw harness manifest is in the same directory as `manifest.json`.

The only source-tree change for this continuation is this report plus the
milestone audit manifest/evidence. The existing proof-only C011EC14
instrumentation remains unchanged; no production-shared code changed in this
continuation.

## 9. Next smallest bounded milestone

The next smallest valid milestone is to establish a source-valid pre-drain
route that supplies the minimum additional real root promotions needed to
occupy queue slots, without modifying queue state and without beginning broad
child traversal. The first question for that milestone is whether the exact
NativeAOT `GcScanRoots` path can expose more than one real managed root before
the locked `drain_mark_queue()` call. If not, the next step must be a separately
classified runtime-contract boundary; it must not force wraparound or fabricate
the missing objects.

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

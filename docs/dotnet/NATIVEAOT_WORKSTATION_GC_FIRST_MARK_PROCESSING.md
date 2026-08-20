# NativeAOT Workstation GC First Mark Processing

Milestone: C011EC27
Result: Outcome C — first post-root queue item consumed, first genuine mark write, and first child/reference slot read
Evidence root: `out/dotnet/c011ec27-post-root-queue-mark-processing/run-20260819-223036483`

## Result

C011EC27 proves that the locked NativeAOT Workstation collector can leave root enumeration, consume its real mark queue, perform a production mark-state transition, and begin object graph traversal while the EE remains suspended. No diagnostic changed queue contents, cursor state, mark state, or child values.

The success level is 3:

1. The first real queue item was consumed.
2. The GC read the object-header mark state and executed its genuine `SetMarked` operation.
3. The collector naturally entered `go_through_object_cl` and read a real child slot.

The first consumed item was the previously proven storage object `0x100A02F50`, from queue slot/index 0. Its raw object-header word changed from `0x10261310` to `0x10261311` using the locked `GC_MARKED` mask `0x1`. The first child slot read was `0x100A02F58`, containing the retained sentinel `0x100A01F38`.

## Locked identity and starting boundary

The runtime identity remained exactly:

- NativeAOT 9.0.0
- AMD64
- Workstation GC
- interfaces 5.3 / 2
- locked source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

C011EC26 was retained without reopening stack walking:

- one managed frame found by genuine `FindMethodInfo` and GC-info decoding;
- four category-3 stack roots: three register roots and one stack root;
- two genuine native unwinds and no third unwind;
- structural `_start.halt` bottom classified as terminal;
- `StackFrameIterator.m_ControlPC = 0`, then `IsValid() == false`;
- one stack-provider callback entry and return;
- one `Thread::GcScanRoots` entry and return;
- one `GCScan::GcScanRoots` entry and return;
- normal root enumeration completion;
- ordinary post-stack ThreadAbort source code 3 retained;
- `GCToEEInterface::AfterGcScanRoots` reached.

At the iterator terminal there were six roots: four category-3 roots, three register roots, and one stack root. The normal post-stack ThreadAbort source increased the full root count to seven. The three boots recorded `promoteAttempts=6`, `promoteEntries=6`, and `promoteReturns=5`, with the historical stack-derived accounting retained at `4 / 4 / 4`.

## Locked post-root control flow

The source order is important. In this locked WKS build, the queue drain is performed by the mark phase after `GCScan::GcScanRoots`; `AfterGcScanRoots` is an EE callback boundary after that production drain, not a request to start a synthetic drain.

| Boundary | Locked source | Role |
|---|---|---|
| Root caller | `src/coreclr/gc/gc.cpp:29899-30063` | `gc_heap::mark_phase` calls `GCScan::GcScanRoots`, drains the mark queue, scans later root classes, drains again, and finally reaches `AfterGcScanRoots`. |
| Root enumeration | `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:94-133` | Enumerates inline statics, ordinary thread statics, and `Thread::GcScanRoots`; clears `thread_under_crawl`. |
| Post-root EE callback | `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:145-155` | `GCToEEInterface::AfterGcScanRoots`; locked runtime callout boundary. |
| Mark-phase entry | `src/coreclr/gc/gc.cpp:29637` | `gc_heap::mark_phase`. |
| First queue consumer | `src/coreclr/gc/gc.cpp:28054-28090` | `gc_heap::drain_mark_queue`. |
| Dequeue/pop | `src/coreclr/gc/gc.cpp:27373-27402` | `mark_queue_t::get_next_marked`; clears a slot, advances the ring scan index, tests the object, and marks a newly unmarked object. |
| Mark representation | `src/coreclr/gc/gc.cpp:4783-4792`, `:11587` | `CObjectHeader::SetMarked` ORs `GC_MARKED` into the raw method-table word; `marked(o)` calls `IsMarked`. |
| Child scanner | `src/coreclr/gc/gc.cpp:28069-28090` | `contain_pointers_or_collectible`, `go_through_object_cl`, child `queue_mark`, and subsequent worklist processing. |

The first C27 preflight line is emitted only at the authentic `AfterGcScanRoots` callback after the queue consumer, mark-state read, and C26 completion checks have already occurred.

## Queue ownership and semantics

The queue is the locked `gc_heap::mark_queue` field (`gcpriv.h:3475`) of type `mark_queue_t`. Its declaration is `gcpriv.h:1487-1504`:

```text
MARK_PHASE_PREFETCH:
    uint8_t* slot_table[16]
    size_t curr_slot_index
```

`mark_queue_t::mark_queue_t` at `gc.cpp:27290-27301` initializes the cursor to zero and clears all 16 slots. The one-argument `queue_mark` at `gc.cpp:27303-27335` writes the incoming object at `slot_table[curr_slot_index]`, advances `curr_slot_index` modulo 16, and only examines/marks the displaced object when it is non-null. `get_next_marked` starts at the current cursor, clears each selected slot, wraps modulo 16, and returns a newly marked object.

For the first queue insertion, the existing C26/C15 observer retained the following semantics:

- owner: the `gc_heap`-embedded `mark_queue_t` instance;
- base: the address of `slot_table[0]`, `0x10244EC0` in all three boots;
- insertion producer: `mark_queue_t::queue_mark(uint8_t*)` called by root promotion;
- insertion cursor: ring insertion index/cursor, not a count;
- first insertion: slot 0, cursor `0 -> 1`, old slot value null, new value `0x100A02F50`;
- first-consumption cursor: `5`, because five root-derived queue insertions had occurred before draining;
- first dequeue: slot 0, cleared to null, scan cursor advanced `0 -> 1` for the dequeued slot;
- later child insertions: real `queue_mark` calls; C27 recorded nine additional observed queue insertions by the AfterGcScanRoots boundary;
- no diagnostic wrote a queue slot, changed a cursor, or selected an object.

The distinction between the first inline thread-static/storage-object insertion and later root-derived insertions is retained. The first queue item matched the first queue insertion and the first root value, with provider category 1 (`InlinedThreadStaticRoot::m_threadStaticsBase`). The queue item was therefore a previously proven root-derived storage object, not a manually selected sentinel.

## Three-boot evidence

All three fresh QEMU 11.0.0 boots reached `C011EC27-PREFLIGHT` and `C011EC27` with the same semantic fields:

| Field | first-run | repeat-1 | repeat-2 |
|---|---:|---:|---:|
| result | Outcome C | Outcome C | Outcome C |
| success level | 3 | 3 | 3 |
| C26 total roots | 7 | 7 | 7 |
| queue cursor before drain | 5 | 5 | 5 |
| first consumed index | 0 | 0 | 0 |
| first consumed object | `0x100A02F50` | `0x100A02F50` | `0x100A02F50` |
| queue items consumed naturally | 12 | 12 | 12 |
| mark reads total | 14 | 14 | 14 |
| mark writes total | 10 | 10 | 10 |
| first mark state before | clear | clear | clear |
| first mark word before | `0x10261310` | `0x10261310` | `0x10261310` |
| first mark word after | `0x10261311` | `0x10261311` | `0x10261311` |
| child scan attempts | 5 | 5 | 5 |
| child reads total | 197 | 197 | 197 |
| first child slot | `0x100A02F58` | `0x100A02F58` | `0x100A02F58` |
| first child value | `0x100A01F38` | `0x100A01F38` | `0x100A01F38` |
| queue invariant failures | 0 | 0 | 0 |
| object invariant failures | 0 | 0 | 0 |
| EE suspended | yes | yes | yes |

The exact serial hashes are:

- first-run: `F3248589BE693F06A598C27ED6B90B46C538233C5A761D59B62DFD8C06AEA498`
- repeat-1: `7BCC6E4D06837296DFC937E2DD39411C1FA4B5EBB37013B0A965D4287987F5D3`
- repeat-2: `F699E3DDF403A69F2FF9404D97C2B544FA0A4339FF4E70605E8B9FF8E06567EE`

The serial hashes differ because boot logging includes run-local timing/serial details; the semantic marker fields above agree exactly.

## Mark-state evidence

This WKS configuration does not use a separately diagnosed bitmap/table for the first mark. The relevant mark state is the low `GC_MARKED` bit in the raw object-header method-table word:

- `marked(i)` macro: locked `gc.cpp:11587`, dispatching to `header(i)->IsMarked()`;
- `CObjectHeader::IsMarked`: locked `gc.cpp:4789-4792`, reads `RawGetMethodTable()` and tests `GC_MARKED`;
- `GC_MARKED`: locked WKS header-bit constant, observed mask `0x0000000000000001`;
- `CObjectHeader::SetMarked`: locked `gc.cpp:4783-4787`, ORs the bit into the raw method-table word;
- first mark word address: `0x100A02F50`;
- first word before: `0x0000000010261310`;
- first mask: `0x0000000000000001`;
- first test result: clear (`0x0`);
- genuine write attempted: yes;
- first word after: `0x0000000010261311`;
- newly marked: yes.

C27 observed the production `set_marked(o)` operation immediately around the write. It did not synthesize a mark, call a private marking helper, or replace the WKS representation.

## First child boundary

The first marked object naturally entered `gc_heap::drain_mark_queue` and passed the locked `contain_pointers_or_collectible` test. The first scanner invocation was:

- parent: `0x100A02F50`;
- method table: `0x10261310` (the raw method-table value before the mark-bit OR);
- child scan attempts: 5 total during the bounded natural drain;
- first child slot: `0x100A02F58`;
- first child value: `0x100A01F38`;
- child value classification: non-null object reference, and it is the retained sentinel;
- child reads: 197 total;
- child Promote/queue attempts: 197 total;
- graph traversal counter: 5 object scans.

No child was selected to force success. The observed first child was the value already present in the production object slot. The run stopped at the `AfterGcScanRoots` C27 boundary after the natural bounded drain, rather than continuing into collection completion.

## Promote chronology and root accounting

The retained chronology is source-qualified rather than using ambiguous “first/second Promote” labels:

| Chronology | Source category | Evidence | Queue effect | Return |
|---|---|---|---|---|
| 1 | Inline thread-static root, provider category 1 | `InlinedThreadStaticRoot::m_threadStaticsBase`; value/storage object `0x100A02F50` | First `queue_mark`; slot 0; cursor `0 -> 1` | Included in the retained full-scan `6 / 5` accounting |
| 2–5 | Thread-stack provider, category 3 | Four proven stack-derived roots: three register roots and one stack root | Four genuine root-driven queue insertions; cursor `1 -> 5` | `4 attempts / 4 entries / 4 returns` |
| 6 | Normal post-stack ThreadAbort source, source code 3 | Locked `thread.cpp:566-568` path, retained without suppression | No diagnostic insertion; full root count becomes 7 | Included in full-scan `6 entries / 5 returns`; source is normal and retained |
| 7+ | Collector queue processing | `drain_mark_queue` child `queue_mark` calls, not root `Promote` calls | Nine additional observed queue insertions by `AfterGcScanRoots`; 12 queue items consumed in total | No root Promote return is attributed to this phase |

At the C26 iterator terminal the stack scan reported `6` roots, `4` category-3 roots, `3` register roots, `1` stack root, and stack-derived Promote `4 / 4 / 4`. At the normal post-stack boundary the root total was `7`, with the ThreadAbort source count `1`. At C27, root enumeration remained complete and the first queue drain began from cursor 5.

## Invariants and sensitive path

All three boots retained:

- queue index within the 16-slot bound;
- queue slot address equal to `queueBase + index * sizeof(uintptr_t)`;
- valid object alignment;
- consumed slot cleared to null;
- no queue overwrite or diagnostic insertion;
- queue invariant failures: 0;
- object invariant failures: 0;
- sentinel: `0x100A01F38`;
- storage object: `0x100A02F50`;
- stack base: `0x0`;
- `ScanContext.stack_limit`: `0x0`;
- consumed stack bounds: `0`;
- ThreadStore owner: GC initiator thread, recursion depth 1;
- EE suspended: 1;
- current thread cooperative: 1;
- current thread preemptive: 0;
- restart/resume: `0 / 0`;
- allocation-free and bounded diagnostics;
- no dynamic strings, collections, arbitrary heap scans, arbitrary stack scans, managed re-entry, or scheduler transitions.

## Artifact hashes and restoration

Proof artifacts for the three-run evidence root:

- proof kernel: `D5718C934C4532D69FC841A3F1A3225715E328E5BCC596619FE5273A49ADEF0A`;
- PE: `422C1730C96CEDB1C8672A5B1E2205ABEDC48AE0A43259DBD0808043ECE8C980`;
- ELF: `0A1ED2FCD43671EE0E53A4F932D0561327F1F248E6BF962CB544B5D335FDF483`;
- MAP: `F2AA7A9309B03FB9A5FB669BF39682A1D90E6C86AE6845F7291273D07B0EE6B5`.

The ordinary kernel and ESP were recorded before proof deployment and restored after testing. Both final ordinary hashes are:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

All QEMU processes were terminated after testing. The harness retained C19–C26 chronology guards, terminal/iterator completion checks, linked/physical kernel alias tests, native unwind-provider checks, standalone helper and second-function unwind checks, kernel-main return-slot validation, linker/table checks, converter checks, PowerShell parse/source guards, ordinary boot smoke, and `git diff --check`.

## Git and next milestone

Starting repository state was branch `v1.1_DOTNET_SUPPORT`, HEAD `b7491fcb58807cea4dc10ea63b4284d196ffe2e1`, upstream `origin/v1.1_DOTNET_SUPPORT`, ahead 1 / behind 0, clean, with zero untracked entries. The intended C27 changes are the harness, diagnostics header, diagnostics implementation, and this document. Push policy remains unchanged: no push was performed.

The next smallest independent milestone is to retain the first marked-object boundary and investigate the next authentic WKS graph-worklist contract or the first supported object-layout/child-scanner continuation. Full collection completion remains out of scope for C011EC27.

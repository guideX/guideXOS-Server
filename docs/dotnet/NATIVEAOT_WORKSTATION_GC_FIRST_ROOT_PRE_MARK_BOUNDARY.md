# NativeAOT Workstation GC — C011EC11 First Pre-Mark Boundary

Date: 2026-08-12  
Outcome: **A — final clean pre-mark boundary reached**

## Checkpoint and locked identity

The task started at HEAD `ad7bd45485d97acddba2846bd62bdb8066de948d`, branch
`v1.1_DOTNET_SUPPORT`, with a clean worktree. C011EC10 was already committed
(`Outcome A — true; stopped before promotion/marking/mutation.`). The ordinary
kernel and ESP both started at SHA-256
`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.

The runtime identity stayed locked: NativeAOT 9.0.0, AMD64, Workstation GC,
GC interfaces 5.3 / 2, source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

C011EC10 was the prerequisite: the real NativeAOT thread-static storage object
was in range, Workstation's null heap pointer was accepted as the single-heap
sentinel, and `gc_heap::is_in_condemned_gc(o)` returned true for generation 0
with condemned generation 0. The C011EC10 evidence remains historical and was
not rewritten.

## Root and invariants

The managed proof field is `[ThreadStatic] byte[]? s_gcProofThreadRoot`.

| identity | value |
|---|---:|
| selected sentinel | `0x100A01F38` |
| NativeAOT thread-static storage object | `0x100A02F50` |
| root slot | `0x394BBE0` |
| raw root / genuine object | `0x100A02F50` |
| find-range result | true |
| heap lower bound | `0x100000000` |
| relevant upper bound | `0x102600000` |

The sentinel was not used as the root and its child reference was not
traversed. The storage object, sentinel, user objects, and recorded object
identities remained unchanged; duplicate identities, overlap, and mutation
were zero. The validation counter reached `0x94` deterministic checks and
`objectHistoryOverflow=0`.

Workstation semantics were preserved: `MULTIPLE_HEAPS=0`, `hpt=0`,
`heap_of(o)=0`, heap number 0, heap count 1, and
`wksNullHeapValid=1`. The null heap value was not treated as failure.

## Condemned-generation result

The locked helper is declared at `gcpriv.h:3054` and defined at
`gc.cpp:8389-8407`. The real run observed:

* condemned generation: `0`;
* maximum generation: `2`;
* `min_segment_size_shr`: `0x14`;
* generation map: `0x10400F040`;
* map index: `0x100A`;
* region generation: `0`;
* source-required `region_of(o)` segment: `0x104010710`;
* generation-table reads: 1;
* segment lookup: 1.

Therefore the source predicate was `generation <= condemned_generation`, or
`0 <= 0`, and `is_in_condemned_gc(o)=true`.

## Complete true-branch source trace

The exact `WKS::GCHeap::Promote` source range is
`gc.cpp:49474-49544` in the locked source tree
`out/dotnet/pal-runtime-active-replacement-build/locked-source/src/coreclr/gc/gc.cpp`.
After the condemned predicate is true, the source-required sequence is:

1. `dprintf(3, ("Promote %zx", (size_t)o));` at line 49507. In this build
   `TRACE_GC` is disabled and `gc.h:372-388` defines `dprintf(l,x)` empty, so
   this diagnostic is compiled out. It has zero requests, entries, and
   returns; no synthetic call was added.
2. Test `flags & GC_CALL_INTERIOR` at lines 49509-49515. The source value is
   `GC_CALL_INTERIOR=0x1`; raw flags are 0, so the branch is false and
   `find_object` is not called.
3. Under active `FEATURE_CONSERVATIVE_GC`, evaluate
   `GCConfig::GetConservativeGC() && ((CObjectHeader*)o)->IsFree()` at
   lines 49517-49525. Runtime values are conservative GC enabled = 1 and
   `IsFree()=0`. `IsFree` performs one method-table read and does not mutate.
4. Under `_DEBUG`, call `((CObjectHeader*)o)->Validate()` at lines
   49527-49531. The proof-generated source defines `_DEBUG=1`; the
   `_ASSERTE` in this build is a no-op, but `Validate` itself executes. It
   reads the method table, reads `HEAPVERIFY_NO_RANGE_CHECKS=0`, checks the
   segment map (`smallHeapPointer=1`, `largeHeapPointer=0`), and reads
   `HEAPVERIFY_GC=0`; no heap member validation or mutation follows.
5. Test `flags & GC_CALL_PINNED` at lines 49533-49535. The source value is
   `GC_CALL_PINNED=0x2`; raw flags are 0, so the branch is false and
   `pin_object` is not called.
6. The `STRESS_PINNING` block at lines 49536-49539 is not compiled into this
   build.
7. The next source statement is
   `hpt->mark_object_simple(&o THREAD_NUMBER_ARG);` at line 49541. The proof
   observer is placed immediately before this statement and does not return.

`USE_REGIONS` is active. `DEBUG_DestroyedHandleValue` is compiled out. No
source path tests check-app-domain, older-root, dependent-root, or
NativeAOT-specific root bits here. The only root flags consulted by
`Promote` are the interior and pinned bits above.

The true branch was requested once, entered once, and duplicated zero times.
The raw flags were `0`; interior=false and pinned=false. No root flag was
changed and `ScanContext` was not mutated.

## ScanContext and metadata reads

The callback-side source-required observation read six live fields once:

* `promotion=1`;
* `concurrent=0`;
* `thread_count=1`;
* `thread_number=0`;
* `thread_under_crawl=0`;
* `stack_limit=0`.

No `ScanContext` field is consulted again by the condemned=true pre-mark
statements. No plan/mark-phase, relocation, concurrent, pinned-root, object
size, or generation-state predicate executes in `Promote` before the helper.
The generation state used for the decision is the C011EC10 source-required
read described above.

Before the boundary, the run recorded one object-pointer load from the root
slot (`*ppObject` into `o`), two object-header reads, two method-table reads,
two segment metadata reads from debug validation, and three GC metadata reads.
The first metadata read address was `0x100A02F50`; the method table identity
was `0x100E6390`. Mark-state reads were zero. There was no object-size lookup,
object-header write, object write, segment write, relocation write, or graph
read. Pointer arithmetic and segment-map lookup are counted separately from
object-header reads.

The non-mutating helper sequence was:

* `CObjectHeader::IsFree`;
* `CObjectHeader::Validate`;
* `GCHeap::IsHeapPointer`;
* `gc_heap::find_segment`.

None of these helpers mutated GC state.

## First mutation contract

The first mutation-capable helper is the out-of-line
`WKS::gc_heap::mark_object_simple`:

* declaration: `gcpriv.h:2729`,
  `PER_HEAP_METHOD void mark_object_simple(uint8_t** o THREAD_NUMBER_DCL);`;
* definition: `gc.cpp:27989-28029`;
* Workstation parameters: `uint8_t** po`; `THREAD_NUMBER_DCL` is empty;
* return: `void`;
* implementation: reads condemned-generation state and `*po`, then delegates
  to inline `mark_queue_t::queue_mark` at `gc.cpp:27308-27335`.

`queue_mark` has `MARK_PHASE_PREFETCH` active in this build. Its first
mutation-capable operation is the worklist slot write
`slot_table[slot_index] = o` at `gc.cpp:27318-27322`. Only after that write
does it read `marked(old_o)` at `gc.cpp:27328` and later perform the mark-bit
write at `gc.cpp:27333`. Thus a mark-state read would be source-required but
is after the first prohibited mutation for this build. No part of
`mark_object_simple` or `queue_mark` executed.

## Machine-code proof and exact stop

The final proof kernel is SHA-256
`983134AAA3ECBEA1A150D4ED056205C565BB532D3D1B8B94E02C23AD3D3D264B`.
The disassembly is in the run evidence directory.

Relevant linked AMD64 addresses are:

| operation | address |
|---|---:|
| `mark_object_simple` helper | `0x10049B50` |
| observer call in `Promote` | `0x1002BB80` |
| exact retained `mark_object_simple` call site | `0x1002BB8D` |
| return address after that call | `0x1002BB92` |
| first mutation, `mov QWORD PTR [rbx+rdx*8],rax` | `0x10049B6D` |
| later mark-state read, `test al,0x1` | `0x10049B8B` |
| later mark-bit write, `mov QWORD PTR [r14],rax` | `0x10049BAD` |

The call-site sequence is `lea rcx,[rsp+0xf0]` followed by
`call 0x10049b50`. Therefore the poised object argument is `&o`, a
`uint8_t**` pointing at the local root value. The object value at the stop is
`0x100A02F50`. No heap/WKS argument or thread argument is passed to this
Workstation static helper; `hpt=0` remains the valid WKS sentinel. Root flags,
ScanContext, and generation arguments are not helper parameters.

The observer is non-returning in the proof run, so execution stopped before
the call at `0x1002BB8D`, immediately before the first mutation-capable
operation at `0x10049B6D`. `mark_object_simple` is out-of-line; `queue_mark`
is inlined into it.

## C011EC11 state

The safe-stop marker means: the first genuine NativeAOT root traversed all
real condemned=true pre-mark logic and stopped before the first mark,
promotion, or GC graph mutation.

At the marker:

* root slot=`0x394BBE0`; raw root/storage object=`0x100A02F50`;
* membership=true; WKS null-heap sentinel valid; condemned=true;
* true-branch requests/entries/duplicates=`1/1/0`;
* dprintf compiled/requests/entries/returns=`0/0/0/0`;
* raw flags=`0`; interior=false; pinned=false;
* ScanContext fields are the six values listed above;
* object-pointer reads=1; object-header reads=2; method-table reads=2;
* segment reads=2 after the condemned decision; mark-state reads=0;
* mark helper attempts/executions=`0/0`;
* promotion start/count/writes=`0/0/0`;
* marking start/writes=`0/0`;
* worklist writes, graph traversal, child-reference reads, object writes,
  object-header writes, GC metadata writes, segment writes, and relocation
  writes are all zero;
* callback returns, second callbacks, restart, and managed resume are zero;
* one registered/enumerated/included managed thread remains current and is the
  initiator; the ThreadStore lock is held by it; EE remains suspended;
  managed entry remains prohibited; allocation contexts remain fixed/cleared;
  registry mutation is zero.

No mark, promotion, child traversal, callback return, restart, or managed
resume occurred.

## QEMU evidence

QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. The exact command lines are in
`out/dotnet/gc-first-root-pre-mark-boundary/run-20260812-174755489/commands.txt`.
The final run used one initial boot and two fresh repeats. All three reported
C011EC11, the same root, the same true-branch path, the same first mutation
helper/address, and zero mark/promotion/mutation.

| boot | serial SHA-256 |
|---|---|
| first-run | `D5BDDD11FA434F2BFFD20240178C6C4CB3F0B6176AABF1D44FFC5423426BF99D` |
| repeat-1 | `A231DD12525AD1C55BCA9469E515955D31A16080D05F0C9D534FF6E1604C3D4B` |
| repeat-2 | `18F219557D8D2C25423C83AE06F2C7A792A9B0125CEE4C9C92C64C4D039191E2` |

The complete manifest is
`out/dotnet/gc-first-root-pre-mark-boundary/run-20260812-174755489/manifest.json`.

## Regressions and retained evidence

C011EC10 remains proven by its prior 3/3 fresh-boot evidence. A focused
C011EC10 rerun was attempted at
`out/dotnet/gc-first-root-condemned-generation-decision/run-20260812-175331680/`;
it stopped before continuation on the historical zero-promotion/object-read
assertion mismatch and is therefore non-clean, not a pass. The focused
C011EC11 pass did not rewrite historical outcomes. The C011EC09, C011EC08,
C011EC07, C011EC06, C011EC05, C011EC03, C011EC02, C011EC01, segment-transition,
commitment, refill/multiple-refill, first-allocation, 4 KiB, 64 KiB, FLS/local
storage, native thread, runtime-pack, ELF, stack-bound, exact/general build,
script/manifest parsing, serial-evidence, and diff-check validators were not
all rerun as one broad suite in this bounded proof pass. Their existing
artifacts and historical reports are retained; any historically blocked or
non-clean validator remains blocked/non-clean and is not counted as a pass.

The specific old kernel validators pinned to historical kernel hash `D687...`
remain blocked unless independently updated for production-semantic reasons.
No old expected hash was changed to manufacture a pass.

## Ordinary restoration

After proof execution, the ordinary kernel and ESP were restored. Both now
hash to the expected ordinary SHA-256
`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
Production `InitializeModules`, real NativeAOT `[ThreadStatic]` support, and
ordinary Workstation GC integration remain intact. No proof stop is reachable
in ordinary production startup.

## Cross-references

* [C011EC10 condemned-generation decision](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md)
* [C011EC09 heap resolution](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md)
* [C011EC08 membership classification](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md)
* [C011EC07 callback entry](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md)
* [first non-null callback boundary](NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md)
* [NativeAOT thread-static runtime support](NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md)
* [GC startup readiness](NATIVEAOT_GC_STARTUP_READINESS.md)
* [Workstation GC feasibility](NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md)

## Recommended next milestone

The smallest bounded next step is a separate proof-only boundary immediately
after the first `queue_mark` worklist write, with a new marker and a restored
ordinary kernel between runs. That milestone must not be folded into C011EC11
and must not reinterpret the C011EC11 zero-mutation result.

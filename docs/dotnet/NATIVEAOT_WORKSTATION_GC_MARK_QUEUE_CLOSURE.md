# C011EC28 — NativeAOT Workstation GC mark-queue closure

Date: 2026-08-22
Outcome: **A — authentic Workstation mark-queue closure and first post-mark boundary**
Success level: 4
Proof evidence: `out/dotnet/c011ec28-mark-queue-closure/run-20260822-080849550`

## 1. Milestone boundary

C011EC28 continued exactly from the C011EC27 boundary. C26 stack/root completion and C27 genuine marking were retained. The proof did not reopen stack unwinding, force queue emptiness, clear queue slots, set mark bits, or add a diagnostic worklist.

The retained path is:

```text
ManagedMain
→ real managed GC-info
→ four stack-derived roots
→ runFirstRealAllocationImpl native unwind
→ kernel_main native unwind
→ _start.halt structural bottom
→ normal StackFrameIterator completion
→ Thread::GcScanRoots return
→ ThreadAbort root enumeration
→ GCToEEInterface::AfterGcScanRoots
```

C27 then entered the real Workstation marking path:

```text
gc_heap::drain_mark_queue()
→ mark_queue_t::get_next_marked()
→ real queued object
→ genuine mark write
→ genuine child scan
→ genuine child Promote activity
```

C28 proves that this production worklist continues through naturally discovered children, handles already-marked objects, reaches an authentic empty result, returns from `gc_heap::drain_mark_queue()`, and reaches the next production callback boundary:

```text
GCToEEInterface::AfterGcScanRoots
```

The three fresh QEMU runs produced identical semantic C28 fields. Their serial hashes differ because the serial stream contains run-specific address/boot text; this is not a semantic disagreement.

## 2. Locked runtime identity

| Item | Locked value |
|---|---|
| NativeAOT | 9.0.0 |
| Architecture | AMD64 |
| GC | Workstation |
| GC interfaces | 5.3 / 2 |
| Runtime source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| QEMU | 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`) |

No runtime revision, architecture, GC flavor, or runtime-pack identity was changed.

## 3. Exact locked queue semantics

The references below are to the locked WKS source at the runtime source revision above.

| Concern | Locked implementation |
|---|---|
| Declaration | `src/coreclr/gc/gcpriv.h:1487-1504` |
| Constructor | `src/coreclr/gc/gc.cpp:27290-27301` |
| Insertion | `src/coreclr/gc/gc.cpp:27303-27335`, `mark_queue_t::queue_mark` |
| Removal | `src/coreclr/gc/gc.cpp:27373-27402`, `mark_queue_t::get_next_marked` |
| Drain | `src/coreclr/gc/gc.cpp:28054-28090`, `gc_heap::drain_mark_queue` |
| First post-mark callback | `src/coreclr/gc/gcenv.ee.cpp:145-155`, `GCToEEInterface::AfterGcScanRoots` |

The actual WKS object is:

```cpp
static const size_t slot_count = 16;
uint8_t* slot_table[slot_count];
size_t curr_slot_index;
```

There is no stored `head`, `tail`, or `count` field.

* `slot_table[16]` is the complete ring storage.
* `slot_count` is 16.
* `curr_slot_index` is the one stored cursor. At insertion it is the write position; the insertion writes the current slot and advances `(index + 1) % slot_count`. Removal starts a local scan at the current cursor, advances the local index with the same modulo rule, and updates the stored cursor when it returns a newly marked object.
* A persistent read head does not exist. A persistent tail does not exist. C28 reports `head` and `tail` as fixed-scalar diagnostic aliases of the stored cursor so that the observed chronology can be checked without claiming fields that the runtime does not have.
* C28 `count`/occupancy is a bounded diagnostic derivation from observed queue operations. It is not runtime state.
* `get_next_marked()` scans up to all 16 slots. It reads and clears slots as it scans, skips null slots and objects already marked by the locked algorithm, and returns `nullptr` only after the bounded scan finds no newly marked object. A single null slot is therefore not the queue-empty condition.
* `drain_mark_queue()` loops while `get_next_marked()` returns a non-null object. The null result after the full scan is the condition that makes the drain return.
* `queue_mark()` has no explicit full-queue branch. It writes the current ring slot, advances the cursor, and handles a displaced old object according to its mark state. C28 reached two legitimate displacements but did not reach a full condition; capacity was not enlarged.
* In this single-thread WKS configuration there is one mark queue and no `MULTIPLE_HEAPS`, `BACKGROUND_GC`, or `MH_SC_MARK` local/global work-stealing path. A child discovered during an object scan can call the real `queue_mark()` and refill the ring before a later empty scan. The collector invoked `drain_mark_queue()` five times in the observed path; C28 did not short-circuit after a temporary occupancy change.

The C28 proof therefore uses “cursor” for the source field and “head/tail/count” only for explicitly derived diagnostics.

## 4. C28 worklist chronology

All three runs agreed on these semantic values. The C28 proof image placed the queue at `0x102460C0`; C27’s historical image placed it at `0x10244EC0`. The address relocation is an artifact-layout difference, not a queue-semantics change.

| Evidence | Value |
|---|---:|
| Queue type | `uint8_t* slot_table[16]` |
| Capacity | 16 |
| Owner/base | `0x102460C0` |
| Initial cursor | 5 |
| Initial derived head | 5 |
| Initial derived tail/write position | 5 |
| Initial derived occupancy | 5 |
| Dequeue attempts | 142 |
| Successful dequeues | 12 |
| Enqueue attempts | 14 |
| Successful enqueues | 14 |
| Cursor wraps | 0 |
| Displacements | 2 |
| Queue-full events | 0 |
| Queue-full resolutions | 0 |
| Maximum observed occupancy | 5 |
| Queue invariant failures | 0 |
| Empty tests | 15 |
| Final empty result | true |
| Final drain empty tests | 1 |
| Final drain empty result | true |
| Drain entries / returns | 5 / 5 |
| Final derived head | 12 |
| Final derived tail | 12 |
| Final derived count | 0 |

The first real dequeue was index 0, slot `0x102460C0`, object `0x100A02F50`, retaining the C27 evidence. The final successful dequeue was ordinal 12, index 11, slot `0x10246118`, object `0x100A00028`. Immediately after that object’s scan the derived queue state was head 12, tail 12, count 0; its child scan added 0 new children; the subsequent full empty test returned true; and the enclosing `drain_mark_queue()` returned normally.

The observed queue never wrapped and never reached capacity. Thus C28 proves the modulo arithmetic on the exercised indices and the absence of overwrite/invariant failure through the observed chronology, but it does not claim a full-queue event occurred. C27 likewise did not establish a wrap in its bounded evidence.

## 5. Mark and graph totals

The C28 counters are aggregate, bounded counters. Root, stack-derived, ThreadAbort/root-source, and child-derived Promote activity remains separated.

| Evidence | C28 total |
|---|---:|
| Mark tests | 14 |
| Already-marked results/skips | 2 / 2 |
| Newly marked objects | 12 |
| Genuine mark writes | 12 |
| Objects scanned | 10 |
| Reference slots visited | 197 |
| Null references | 137 |
| Non-null references | 60 |
| Child-derived Promote attempts | 197 |
| Child-derived queue-mark entries | 197 |
| Child-derived queue-mark returns | 197 |
| Child-derived queue insertions | 3 |

The child-derived counts are not mixed with the C26/C27 root counts. C26 retained 6 root Promote attempts, 6 entries, and 5 returns; the C28 child counts above are the separate object-graph traversal activity.

Representative mark observations all used mask `0x1` and recorded the raw before/after word without diagnostic mutation:

| Object | Raw word before | Raw word after | Mask | Newly marked |
|---|---:|---:|---:|---:|
| First C27/C28 object `0x100A02F50` | `0x10262510` | `0x10262511` | `0x1` | yes |
| Later object `0x100A02FF0` | `0x10260A50` | `0x10260A51` | `0x1` | yes |
| Final object `0x100A00028` | `0x10261040` | `0x10261041` | `0x1` | yes |

C27’s historical first-mark checkpoint remains `0x10261310 → 0x10261311`, mask `0x1`; the C28 proof image’s raw address changed with artifact layout while the first object and genuine write behavior remained stable.

Representative graph scans recorded the parent, EEType/method-table identity, real reference-field scan, and null/non-null aggregate behavior:

| Scan | Parent | EEType/method table | First child evidence | Result |
|---|---:|---:|---:|---|
| First | `0x100A02F50` | `0x10262510` | `0x100A01F38` | genuine child traversal; first child slot `0x100A02F58` |
| Later | `0x100A00048` | `0x10260868` | aggregate only | genuine scan; counts included above |
| Final | `0x100A00028` | `0x102610F8` | no child slots | normal zero-reference scan |

The final object evidence is:

```text
queue slot       = 0x10246118
queue index      = 11
object           = 0x100A00028
mark state       = marked
newly marked     = yes
child slots      = 0
new children     = 0
cursor before    = 11
cursor after     = 12
```

## 6. C011EC28 gates and production boundary

`C011EC28-PREFLIGHT` was emitted only after all gates were true on every run:

* C26 normal iterator and `GcScanRoots` completion;
* C27 first real dequeue;
* C27/C28 genuine first mark write;
* C27/C28 genuine first child read;
* locked queue semantics validated;
* queue and object invariant failures equal to zero.

`C011EC28` was emitted only after additional real queue processing, an authentic final empty result, and normal return from `gc_heap::drain_mark_queue()`. The first production boundary reached afterward was:

```text
GCToEEInterface::AfterGcScanRoots
locked source: src/coreclr/gc/gcenv.ee.cpp:145-155
observed address: 0x1007475E
```

The collector was stopped at that first meaningful post-mark callback boundary. The entire GC was not attempted.

## 7. C26/C27 and EE/Thread invariants retained

| Invariant | C28 evidence |
|---|---:|
| Iterator completion | normal, 1 |
| `GcScanRoots` entries / returns | 1 / 1 |
| Iterator-terminal roots | 6 |
| Total roots | 7 |
| Category-3 roots | 4 |
| Register roots | 3 |
| Stack roots | 1 |
| Native unwinds | 2 |
| Third unwind attempts | 0 |
| Stack base | `0x0` |
| Stack limit | `0x3961CA0` |
| Stack bounds consumed | 0 |
| ThreadStore lock held | 1 |
| EE suspended | 1 |
| Cooperative / preemptive | 1 / 0 |
| Managed entry prohibited | 1 |
| `thread_under_crawl` | 0 |
| Restart / resume | 0 / 0 |
| Safe-stop reason | 0 |

The proof path recorded zero heap allocations, managed allocations, dynamic strings, collections, diagnostic queue mutations, diagnostic mark mutations, arbitrary heap scans, arbitrary stack scans, managed re-entry, and scheduler transitions after suspension.

## 8. Three-run QEMU evidence and hashes

Evidence root:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec28-mark-queue-closure\run-20260822-080849550`

| Run | Marker | Semantic result | Serial SHA-256 |
|---|---|---|---|
| first-run | C011EC28 | Outcome A, success level 4 | `37399F9EDA206EA9CB4F32BE3413957FCCD49C511A63CD2115A40F2856CD6D8D` |
| repeat-1 | C011EC28 | Outcome A, success level 4 | `BFC1C245F54B0693651F24AFB4A6CF4EE93F360C1056E42BB2CC329456C851F2` |
| repeat-2 | C011EC28 | Outcome A, success level 4 | `9780979E5DFB4BF555AD3453BE5B219FC13975ED2991F0A21F0BE2BE6A642ECE` |

Proof artifact hashes:

| Artifact | SHA-256 |
|---|---|
| Proof kernel | `A8A1AF84BEE1955121DE9494124B9EBDD63B91A01F9091A822EAB14ADDA6AE54` |
| PE | `6AE258A28F2DFFE77649BC2543AC7F77D39ECE511BAA5CCDA49555AFD5618103` |
| ELF | `6D1ABA623456E023CE5A0F3FB744EA632C997A3FE9F39AC7A47413C5776A9848` |
| MAP | `7ECCC14F31EF7045A8401F992B7CBAF1435B1EDE96A6B815EE90271A469CFCE8` |

The proof payload was removed after the runs. The ordinary source-state kernel and ESP were restored and independently verified as:

```text
75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

Both `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` have this hash after restoration. The ordinary production image was booted three times to the kernel main loop; the existing startup-only probe harness did not emit its C28-specific `ALL_PASS` marker against the ordinary image, so that marker is not treated as an ordinary-image proof. The three ordinary serial logs show normal kernel-main-loop entry and navigator smoke `result=PASS`.

All QEMU processes spawned by the C28 proof and this repository's ordinary-smoke cleanup were terminated. A separate QEMU owned by an unrelated task in `D:\dev\guideXOS_NET10_nativeaot-managed-kernel-integration` was observed and preserved so that task was not disrupted.

## 9. Regression and validation record

Retained and/or verified for this milestone:

* C19–C27 chronology guards;
* C26 terminal completion and C27 first mark/child proof;
* native unwind-provider tests;
* linked/physical alias tests;
* `kernel_main` return-slot validation;
* linker/table validation;
* converter regression;
* PowerShell parse validation;
* locked queue source guards;
* ordinary kernel build;
* ordinary boot smoke to kernel main loop;
* `git diff --check`.

The C28 diagnostics use bounded counters and fixed scalar snapshots only. They do not log one serial line per child operation and do not mutate queue or mark state.

## 10. Git and artifact state

Actual starting state before C28 work:

```text
branch:    v1.1_DOTNET_SUPPORT
HEAD:      def3817f14585a2a0f7a81afbacb7ab440fe47cf
upstream:  origin/v1.1_DOTNET_SUPPORT
divergence: ahead 0, behind 0
worktree:  clean
untracked: 0
```

The expected “ahead 2” value was not present; actual Git state was treated as authoritative. No reset, amend, rebase, squash, restore, discard, or rewrite was performed. Push policy was preserved; this milestone is not pushed.

Intended changed files:

* `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`
* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`
* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
* `docs/dotnet/NATIVEAOT_WORKSTATION_GC_MARK_QUEUE_CLOSURE.md`

The proof artifacts under `out/` remain generated evidence and are not staged.

## 11. Next smallest milestone

The smallest next milestone is to trace the first production operation entered after `GCToEEInterface::AfterGcScanRoots` under the same locked NativeAOT 9.0.0 AMD64 Workstation configuration, while preserving this completed mark-closure boundary and the ordinary artifact state.

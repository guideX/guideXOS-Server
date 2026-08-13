# NativeAOT Workstation GC — C011EC12 First Root Mark/Worklist Mutation

## 1. Outcome

Final classification: **Outcome D — the smallest safe authentic mutation requires two inseparable writes**.

The genuine NativeAOT `[ThreadStatic]` storage-object root reached the real Workstation `gc_heap::mark_object_simple` through `WKS::GCHeap::Promote`. In the locked `MARK_PHASE_PREFETCH` build, the first queue state transition is the source-level pair:

1. `slot_table[slot_index] = o`;
2. `curr_slot_index = (slot_index + 1) % slot_count`.

Stopping between those writes would leave the prefetch queue’s slot/cursor invariant incomplete. C011EC12 therefore stops immediately after both writes and before the `old_o == nullptr` test, `marked(old_o)`, mark-bit update, promotion accounting, or child traversal. This is one complete queue mutation unit, not a claim that the object is logically marked or that a GC mark phase completed.

## 2. Starting state and locked identity

The task started at:

- HEAD: `2f1423e036dc27ddf001547936fe4794d9309cae`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting worktree: clean (`## v1.1_DOTNET_SUPPORT...origin/v1.1_DOTNET_SUPPORT [ahead 1]`)
- C011EC11: already committed in HEAD
- Ordinary kernel SHA-256: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`
- Ordinary ESP SHA-256: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

The final C011EC12 proof run began from the intentionally dirty, reviewable proof worktree recorded in its manifest. No commit was created.

Locked runtime identity remained unchanged:

- NativeAOT `9.0.0`
- AMD64
- Workstation GC
- GC interfaces `5.3 / 2`
- locked source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- production `InitializeModules` and genuine `[ThreadStatic]` support retained

## 3. C011EC11 prerequisite and genuine root

C011EC11 remains the immediately preceding committed frontier: the authentic root reached the real condemned=true pre-mark path and stopped immediately before `mark_object_simple`.

The proof field and identities were unchanged:

- managed proof field: `[ThreadStatic] byte[]? s_gcProofThreadRoot`
- sentinel: `0x100A01F38`
- NativeAOT thread-static storage object: `0x100A02F50`
- root slot: `0x394BBE0`
- callback-loaded raw root: `0x100A02F50`
- membership object: `0x100A02F50`
- condemned-generation object: `0x100A02F50`
- `mark_object_simple` object: `0x100A02F50`
- queue input object: `0x100A02F50`

The root is the NativeAOT thread-static storage object, not the sentinel directly. The sentinel remained only an internal payload reference and was not traversed.

## 4. Real callback and helper path

The authentic path was:

`GCToEEInterface::GcScanRoots`
→ NativeAOT `GcEnumObject` / `EnumGcRef`
→ real thread-static storage root slot
→ `WKS::GCHeap::Promote`
→ `is_in_find_object_range(o)` = true
→ Workstation null-heap semantics
→ `is_in_condemned_gc(o)` = true
→ source-valid pre-mark checks
→ `hpt->mark_object_simple(&o THREAD_NUMBER_ARG)`
→ `gc_heap::mark_object_simple`
→ inline `mark_queue_t::queue_mark(o)`
→ first slot write
→ cursor write
→ C011EC12 safe stop.

The locked callback source is `src/coreclr/gc/gc.cpp:49474-49544`; the call is at `gc.cpp:49541`. In the final proof image, the AMD64 call site was `0x1002DA68`, with `RCX = &o`; the pre-mark observer immediately before it returned at `0x1002DA63`.

At C011EC12:

- mark-helper requests: `1`
- mark-helper entries: `1`
- duplicate mark-helper entries: `0`
- helper `po`: `0x4E87370`
- helper object: `0x100A02F50`
- helper address: `0x1004A930`
- helper returns: `0`

The call originated from the real collector path. Proof code did not invoke `mark_object_simple` manually, jump to `queue_mark`, or synthesize a mark request.

## 5. WKS and condemned-generation classification

The final runs independently recorded:

- find-range membership: true (`0x1`)
- `MULTIPLE_HEAPS = 0`
- `hpt = 0`
- `heap_of(o) = 0`
- heap number: `0`
- heap count: `1`
- WKS null-heap sentinel: valid
- root generation: `0`
- condemned generation: `0`
- maximum generation: `2`
- `is_in_condemned_gc(o)`: true

The null heap pointer is the locked Workstation single-heap behavior, not a failure or synthetic substitute.

## 6. Complete locked source trace

### `mark_object_simple`

Declaration: `src/coreclr/gc/gcpriv.h:2729`.

Definition: `src/coreclr/gc/gc.cpp:27987-28029`.

In this `USE_REGIONS`, single-heap Workstation configuration it:

1. Reads `settings.condemned_generation` at `gc.cpp:27991-27996`.
2. Loads `uint8_t* o = *po` at `gc.cpp:27998`.
3. Calls `mark_queue.queue_mark(o)` at `gc.cpp:28007`.
4. Only if that call returns a non-null newly marked object does it run `m_boundary`, compute `size(o)`, account promoted bytes, and call `go_through_object_cl` at `gc.cpp:28014`.
5. The child visitor would call the two-argument `queue_mark(*poo, condemned_gen)` and can recursively enter `mark_object_simple1`; C011EC12 stops before this code.

There is no mark-state read before the first queue writes. `mark_object_simple` does not compute object size first, does not re-read the object header or method table first, does not locate a segment/region first, and does not write a mark bit first.

### `mark_queue_t`

Declaration: `src/coreclr/gc/gcpriv.h:1487-1504`.

The active structure is `mark_queue_t` with:

- `uint8_t* slot_table[16]` under `MARK_PHASE_PREFETCH`;
- `size_t curr_slot_index`;
- constructor initialization at `gc.cpp:27290-27300` setting the cursor to `0` and all slots to null.

The one-argument implementation is `gc.cpp:27303-27335`:

1. `Prefetch(o)`;
2. read `curr_slot_index`;
3. read `slot_table[slot_index]` as `old_o`;
4. write `slot_table[slot_index] = o`;
5. write `curr_slot_index = (slot_index + 1) % slot_count`;
6. test `old_o == nullptr`;
7. otherwise read `marked(old_o)`;
8. if not already marked, call `set_marked(old_o)` and return `old_o`.

The two-argument overload is `gc.cpp:27337-27371`. Under `USE_REGIONS` it first rejects objects outside `is_in_heap_range(o)` and objects whose region generation is greater than `condemned_gen`, then calls the one-argument queue function. That overload is for the later child-reference path; the root call uses the one-argument overload.

`get_next_marked` is `gc.cpp:27373-27399` and is not reached. It later clears queue slots, checks mark state, may set a mark, and advances the cursor.

`queue_mark` is therefore involved, but it is inlined into `mark_object_simple`; there is no separate out-of-line `queue_mark` address in the proof image. The linked helper address `0x1004A930` contains the queue operations.

The queue is a prefetch worklist, not by itself the final logical mark representation. The object is queued first; the later `marked(old_o)` / `set_marked(old_o)` sequence represents the later mark-state action. At C011EC12, logical mark completion is false.

## 7. Exact first mutation and safe boundary

Final proof image machine-code evidence:

`out/dotnet/gc-first-root-first-mark-mutation/run-20260812-200024398/first-mark-machine-code.txt`

Disassembly:

`out/dotnet/gc-first-root-first-mark-mutation/run-20260812-200024398/artifact-disassembly.txt`

Helper: `0x1004A930`.

The relevant instruction sequence is:

```text
0x1004A99B: 48 89 2f              mov QWORD PTR [rdi],rbp
0x1004A9AC: 48 89 05 6d 78 20 00  mov QWORD PTR [rip+0x20786d],rax  # 0x10252220
```

The first mutation is the worklist-slot write:

- semantic type: `mark_queue_t::slot_table[slot_index]` worklist slot write;
- instruction: `0x1004A99B`;
- target: `0x102521A0`;
- old value: `0x0000000000000000`;
- new value: `0x0000000100A02F50`;
- representation: the object pointer is stored directly, without a tag or transform.

The second tightly coupled queue-state write is the cursor update:

- instruction: `0x1004A9AC`;
- target: `0x10252220`;
- old cursor: `0`;
- new cursor: `1`;
- slot index: `0` before and after;
- capacity: `16`.

The queue base is `0x102521A0`; the cursor is the adjacent queue metadata field at `queueBase + 0x80`. The first write and cursor update are the smallest complete source-level queue invariant. Execution stopped immediately after the cursor write and before `old_o == nullptr` at `gc.cpp:27321` and `marked(old_o)` at `gc.cpp:27328`.

The before/after state was captured without a broad heap dump:

| State | Slot index | Cursor | Slot value | Queue target |
|---|---:|---:|---:|---:|
| Before | 0 | 0 | `0x0` | `0x102521A0` |
| After | 0 | 1 | `0x100A02F50` | `0x102521A0` |

Counters at the stop were:

- worklist metadata reads: `2` (`curr_slot_index` and `slot_table[slot_index]`);
- first mutation attempts/executions: `1 / 1`;
- second mutation attempts/executions: `0 / 0`;
- worklist slot writes: `1`;
- worklist cursor/index writes: `1`;
- mark-state reads: `0`;
- mark-bit writes: `0`;
- object-header writes: `0`;
- GC metadata writes: `0`;
- segment/region writes: `0`;
- promotion start/count/state writes: `0 / 0 / 0`;
- logical mark complete: `0`;
- traversal scheduled: `0`.

## 8. Prohibited continuation and validation

At C011EC12 all three runs recorded zero:

- graph-traversal starts;
- child-reference reads;
- child objects discovered;
- second-object mark attempts;
- callback returns;
- second callbacks;
- mark-helper returns;
- restart requests/entries;
- managed resume.

The sentinel and allocation/object validation counters were clean on every run:

- sentinel checks: `0x94`;
- sentinel failures: `0`;
- object validation failures before fixup: `0`;
- object validation failures after fixup: `0`;
- object overlap failures: `0`;
- object pattern failures: `0`;
- duplicate object addresses: `0`;
- object-history overflow: `0`.

The storage object and sentinel identities remained unchanged, and no user object or sentinel was substituted as the mark input.

Thread/EE invariants at the safe stop were clean on every run:

- one registered, enumerated, and included managed thread;
- current thread, initiator, enumerated thread, and lock owner were the same proof mutator;
- ThreadStore lock held;
- EE suspended;
- managed entry prohibited;
- allocation contexts cleared/fixed;
- registry mutation `0`;
- restart `0`;
- resume `0`.

## 9. QEMU evidence

QEMU version: `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`).

Final C011EC12 proof kernel SHA-256:

`594F93FAE39AD510A5F1F4024FECBA2438CED4131FB0DC2F1E6F0A9AD3DAA20A`

Three fresh boots independently reached C011EC12 with the same semantic values:

| Run | Marker | Serial SHA-256 | First write | Cursor write |
|---|---|---|---|---|
| first-run | C011EC12 | `C6C1F3679DBD172FF88DC343B1E34B242A20B3F291D32A24F75EA2DB1C6D09CD` | `0x1004A99B` | `0x1004A9AC` |
| repeat-1 | C011EC12 | `251F441B25109C9832CDAA1A33ABB755D1692C1B43EB4653B4DFCCF1C9A342AC` | `0x1004A99B` | `0x1004A9AC` |
| repeat-2 | C011EC12 | `0AD89213DA4EDF07F1EE764DF6DD720CD70CB633025053B59E39F8B89ACADA1E` | `0x1004A99B` | `0x1004A9AC` |

Exact command lines, serial paths, and watchdog records are in:

`out/dotnet/gc-first-root-first-mark-mutation/run-20260812-200024398/commands.txt`

## 10. Regression and retained-failure status

Executed focused checks:

- C011EC12: PASS, 3/3 fresh QEMU 11.0.0 boots.
- C011EC11 focused regression: PASS, 3/3 fresh QEMU 11.0.0 boots; evidence under `out/dotnet/gc-first-root-pre-mark-boundary/run-20260812-195450835/`.
- PowerShell script parsing: PASS.
- Final C011EC12 manifest parsing: PASS.
- Serial evidence and machine-code evidence: PASS.
- `git diff --check`: PASS with only Git’s LF/CRLF normalization warnings.

Historical and not-rerun statuses remain explicit rather than being counted as passes:

- C011EC10 retains the historical focused validator mismatch involving the zero-promotion/object-read assertion; it is non-clean and was not relabeled.
- C011EC09 through C011EC01, the thread-static variants, FLS/local storage, native-thread, runtime-pack, ELF, stack-bound, no-collection, refill, commitment, segment-transition, and broad regression suite retain their prior evidence/status; they were not rerun as one combined suite in this checkpoint.
- Historical first-64-KiB execution, stale-cache attempts, initial runtime-pack identity mismatch, and the native-stack PowerShell wrapper’s compiler-stderr exit remain retained failures.
- Mark/promotion completion is intentionally out of scope for C011EC12.

## 11. Ordinary restoration

The proof-only behavior remains behind the C011EC12 allocation diagnostic define and source-injected proof mode. Production NativeAOT runtime support was not changed.

After the final proof run:

- ordinary kernel SHA-256: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`;
- ordinary ESP SHA-256: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`;
- no QEMU proof process remained running;
- ordinary hashes equal the required baseline.

## 12. Evidence manifest and cross-references

Final manifest:

`out/dotnet/gc-first-root-first-mark-mutation/run-20260812-200024398/manifest.json`

The manifest includes the starting checkpoint, locked identity, root identities, WKS state, condemned result, C011EC11 prerequisite, helper identity, queue structure, exact before/after mutation, traversal and thread/EE counters, QEMU hashes, regressions, retained failures, report path, and ordinary restoration.

Related documents:

- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md`
- `NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md`
- `NATIVEAOT_GC_STARTUP_READINESS.md`
- `NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md`

## 13. Final worktree and next milestone

The final worktree is intentionally uncommitted. It contains the C011EC12 source-instrumentation changes, the focused proof evidence, the C011EC11 focused regression evidence, and this report. No commit was created.

The next smallest bounded milestone is to enter the first post-queue `old_o == nullptr` / `marked(old_o)` decision with a proof-only stop that preserves the queue invariant, still forbids child traversal and a second object, and separately proves whether the first queued object is logically marked or remains only worklist-resident. Do not proceed to `size`, promotion accounting, `go_through_object_cl`, or the sentinel reference until that decision boundary is independently traced and repeatable.


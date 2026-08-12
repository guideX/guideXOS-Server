# NativeAOT Workstation GC First-Root Condemned-Generation Decision — C011EC10

## Final outcome

Outcome A — the first genuine NativeAOT root completed exactly one real
`gc_heap::is_in_condemned_gc(o)` decision. The result was true. Execution
stopped at `C011EC10` before the selected `GCHeap::Promote` true continuation,
promotion, marking, graph traversal, object metadata reads, or mutation.

The proof used the locked NativeAOT 9.0.0 AMD64 Workstation runtime. The
production `InitializeModules` correction and real NativeAOT `[ThreadStatic]`
provider remained active.

## Starting checkpoint and prerequisite

- HEAD: `fb8228679aae9fe0b99c68d896743984d1022073`
- branch: `v1.1_DOTNET_SUPPORT`
- starting worktree: clean
- C011EC09 already committed: yes
- C011EC09 historical result: Outcome B, preserved unchanged
- ordinary kernel and ESP SHA-256 at start:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`
- locked runtime source commit:
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

C011EC09 is the direct prerequisite. Its historical Outcome B classification
is retained, but its null result is now interpreted correctly: in this WKS
build, `MULTIPLE_HEAPS` is disabled, `HEAP_FROM_THREAD` produces `hpt = 0`,
and `heap_of(o)` returns the source-defined null `__this` sentinel. The valid
state is recorded as `workstationSingleHeapSentinelValid=true`; it is not a
heap-resolution failure and no synthetic heap pointer is used.

## Genuine root identity

Managed proof field: `[ThreadStatic] byte[]? s_gcProofThreadRoot`.

- selected sentinel: `0x100A01F38`
- NativeAOT thread-static storage object: `0x100A02F50`
- callback/root slot in the proof boot: `0x3948BE0`
- callback raw root: `0x100A02F50`
- membership input: `0x100A02F50`
- heap-resolution input: `0x100A02F50`
- condemned-check input: `0x100A02F50`

All five object-input identities were exactly equal in each of the three
successful C011EC10 boots. The sentinel read back unchanged. The bounded
object history was unchanged (`objectBefore=0x25`, `objectAfter=0x25`,
`objectAtStop=0x25`), with zero duplicate addresses, zero overlap, and zero
object mutation. No child reference was read.

The previously established managed-range evidence is preserved:

`0x100000000 <= 0x100A02F50 < 0x102600000` — `inFindObjectRange=true`.

## Locked source contract

The callback is `WKS::GCHeap::Promote`, locked source
`src/coreclr/gc/gc.cpp:49474-49544`:

1. `o = (uint8_t*)*ppObject` at line 49481.
2. `is_in_find_object_range(o)` at lines 49483-49486; this prerequisite was
   already proven true.
3. `HEAP_FROM_THREAD` at line 49494.
4. `gc_heap::heap_of(o)` at line 49496; C011EC09 stopped after this step.
5. `gc_heap::is_in_condemned_gc(o)` at line 49499.
6. A false result returns at lines 49503-49505.
7. A true result reaches the `dprintf` at line 49507. The first actual
   promotion mutation is `hpt->mark_object_simple(&o THREAD_NUMBER_ARG)` at
   line 49541 and was not reached.

The helper declaration is `PER_HEAP_ISOLATED_METHOD bool
is_in_condemned_gc(uint8_t* o)` at locked `src/coreclr/gc/gcpriv.h:3054`.
The definition is locked `src/coreclr/gc/gc.cpp:8389-8407`:

```cpp
bool gc_heap::is_in_condemned_gc(uint8_t* o)
{
    assert ((o >= g_gc_lowest_address) && (o < g_gc_highest_address));

    int condemned_gen = settings.condemned_generation;
    if (condemned_gen < max_generation)
    {
        int gen = get_region_gen_num(o);
        if (gen > condemned_gen)
        {
            return false;
        }
    }

    return true;
}
```

The helper consumes one parameter, `uint8_t* o`, and reads these source-
required values:

- `g_gc_lowest_address` and `g_gc_highest_address` for the assertion;
- `settings.condemned_generation`;
- `max_generation`;
- `gc_heap::min_segment_size_shr` through the region-index helper;
- `gc_heap::map_region_to_generation_skewed` through the generation lookup.

`get_region_gen_num(uint8_t* obj)` is locked at `gc.cpp:12038-12045`. It calls
`get_skewed_basic_region_index_for_address` (`gc.cpp:3754-3761`), which shifts
the address by `min_segment_size_shr`, then reads
`map_region_to_generation_skewed[index] & RI_GEN_MASK`. The generation map and
mask definitions are at `gcpriv.h:1551-1576,4322-4326`.

In this proof build `_DEBUG` is enabled. The locked debug assertion also calls
`region_of(obj)` (`gc.cpp:11997-12005`) and compares the mapped generation with
the segment generation. That is the one source-required segment lookup
recorded below. It is not `find_segment` and is not an additional collector
operation after the condemned decision.

The helper does not call `generation_of(o)`, does not call `find_segment(o)`,
does not use ephemeral bounds, does not inspect an object header or method
table, and does not dereference object memory. It uses address arithmetic and
GC metadata tables only. The WKS null-heap sentinel has no effect on this
helper: `is_in_condemned_gc` is a static/per-heap helper whose contract uses
the global address range, settings, generation map, and generation limits—not
the `heap_of(o)` return value.

The exact boolean contract is:

- true when `condemned_generation == max_generation`, or when
  `generation_from_region <= condemned_generation`;
- false when `condemned_generation < max_generation` and
  `generation_from_region > condemned_generation`.

Thus “condemned generation” is not normalized to requested generation 1. The
collection request was generation 1, while the actual `ScanContext` snapshot
reported condemned generation 0 and maximum generation 2. The helper used
its own `settings.condemned_generation` and `max_generation` values exactly as
shown above.

## Exact decision inputs and result

The successful C011EC10 runs recorded:

| Input | Value |
|---|---:|
| condemned-check object | `0x100A02F50` |
| helper lower bound | `0x100000000` |
| helper upper bound | `0x104000000` |
| `condemned_generation` | `0` |
| `max_generation` | `2` |
| generation-map identity | `0x10400F040` |
| generation-map index | `0x100A` |
| minimum segment-size shift | `0x14` |
| generation derived from map | `0` |
| source-required segment identity | `0x104010710` |
| segment lookups | `1` |
| final result | `true` (`0x00000001`) |

Because generation 0 is not greater than condemned generation 0, the helper
returned true semantically. The proof observer is deliberately noreturn: it
records the logical helper result and enters the safe stop before the native
return instruction can continue `Promote`. This is why
`condemnedCheckReturnCount=1` while the callback return count is zero.

For completeness, the unexecuted source branches are:

- true: continue to `GCHeap::Promote` line 49507 (`dprintf`), then later
  toward promotion; the proof stops before that continuation;
- false: return from `Promote` at lines 49503-49505; the proof did not take
  this branch and did not skip or process another root.

## Machine-code path

The proof callback symbol is
`?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z` at
`0x10029860`, with Microsoft x64 calling convention (`RCX` is the object-slot
argument and the loaded object is kept in `RDI`; `RDX` and `R8` carry the other
callback arguments).

The helper is inlined into `Promote`; there is no independent
`is_in_condemned_gc` symbol. The relevant disassembly is retained in
`artifact-disassembly.txt` in the evidence run:

- `0x1002995F`: call to the condemned-check request observer
  (`0x100106B0`); object input is in `RCX` after `mov rcx,rdi`.
- `0x100299D9`: call to the condemned-check entry observer
  (`0x100105F0`), after the map/table index and bounds have been prepared.
- `0x100299DE`: `cmp esi,0x2`; the full-collection bypass is not selected
  because the run has condemned generation 0 and maximum generation 2.
- `0x100299F8`: call to out-of-line `get_region_gen_num` at `0x10041BA0`.
- `0x10029A04`: call to the generation-query completion observer
  (`0x10010760`), with the derived generation in `EAX/EBX`.
- `0x10029A09`: `cmp ebx,esi`.
- `0x10029A0B`: `jle 0x10029A18`, selected because `0 <= 0`; this is the true
  membership branch.
- `0x10029A20`: call to the result observer (`0x10010500`) with `EDX=1`.
- `0x10029A25`: the post-call boundary; the observer safe-stops before the
  source continuation.

The recorded completion return address was `0x10029A25`. The safe-stop
observer return address was `0x100105C8`. The helper comparison and selected
branch match the source contract and the serial result; no fabricated heap or
generation identity was introduced.

## Bounded counters and invariants

At C011EC10, each successful run recorded:

- condemned requests/entries/completions/logical returns: `1/1/1/1`;
- duplicate condemned checks: `0`;
- generation query start/completion/table reads: `1/1/1`;
- source-required debug `region_of` segment lookups: `1`;
- object dereferences: `0`;
- object-header reads: `0`;
- method-table reads: `0`;
- child-reference reads: `0`;
- promotion start/count/writes: `0/0/0`;
- marking start/writes: `0/0`;
- graph traversal: `0`;
- object, GC metadata, and segment mutation: `0/0/0`;
- callback returns/second callbacks: `0/0`;
- restart requests/entries and managed resume: `0/0/0`.

Thread/EE state was one registered managed thread, one enumerated and included
thread, current equals initiator, enumerated equals initiator, lock owner
equals initiator, lock held at depth 1, EE suspended, managed entry
prohibited, allocation context fixed and cleared, and registry unchanged.

## QEMU evidence

Command used:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1 -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT -ProofMode first-root-condemned-generation-decision -TimeoutSeconds 90
```

QEMU was `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`). All three fresh boots independently reached C011EC10, showed the real `[ThreadStatic]` root, real collection/suspension/fixup/root-provider/Promote path, valid WKS null sentinel, one condemned check, result true, and zero promotion/marking/mutation.

| Boot | Result | Serial SHA-256 |
|---|---|---|
| first-run | Outcome A / C011EC10 | `E87E956F2619405BE006A0FB3B61327A46B39F52E359BCCD6552E6F2648EE72D` |
| repeat-1 | Outcome A / C011EC10 | `4384DF145AEBB8A760F1D36582F105391CA705B0091FAC814A439C020ED8C038` |
| repeat-2 | Outcome A / C011EC10 | `309EA6B8D37D904D27B0F573345C49654796DBB045E4160D69836E6558D40D4B` |

Proof kernel SHA-256: `F6701600CD2E082A7780C8E7DE6B0111C5EFDF036BDA52F1DC98F28B7DB0AE66`.

Evidence directory:
`out/dotnet/gc-first-root-condemned-generation-decision/run-20260812-102315082/`.

## Regression record

The focused predecessor suite was rerun after the instrumentation:

- C011EC09: Outcome B, 3/3; historical classification preserved and null
  sentinel interpretation corrected in this report.
- C011EC08: PASS, 3/3.
- C011EC07: PASS, 3/3.
- C011EC06: PASS, 3/3.
- C011EC05: PASS, 3/3.
- C011EC04: historical PASS retained.
- C011EC03: PASS, 3/3 after replacing stale exact allocation-count checks with
  their source invariants (`objectBefore == objectAfter`, at least four
  sentinel checks).
- C011EC02: PASS, 3/3 after replacing stale exact allocation-count checks with
  positive allocation/refill invariants and zero segment transitions.
- C011EC01: not rerun because its legacy script restores the obsolete
  `D68791B6...` kernel instead of the locked `161B83E9...` ordinary baseline;
  historical validator evidence is retained and not relabeled.
- primitive/reference/combined `[ThreadStatic]`: PASS, 3/3 each.
- runtime-pack state: PASS; runtime-pack static non-allocating and allocating
  checks: PASS.
- native-thread QEMU validation: PASS.
- generic Native ELF: PASS.
- hosted stack-bound checks: PASS; bare-metal stack-bound build: retained
  NON-CLEAN failure in `core/app_launch_target_resolver.cpp`.
- local-storage QEMU: individual storage tests PASS, but process/teardown
  check failed; retained NON-CLEAN.
- segment-transition, commitment, first-refill, multiple-refill,
  first-allocation, 4 KiB, and 64 KiB legacy suites were not rerun because
  their scripts are pinned to the obsolete `D687...` restoration source.

Historical failures remain retained, including the first-64 KiB execution
failure, stale-cache attempts, the initial runtime-pack identity mismatch,
the native-stack wrapper non-clean result, and the local-storage teardown
failure. No blocked or non-clean check is reported as PASS.

## Ordinary restoration

After proof and regression runs, the exact hash-verified ordinary kernel copy
was restored to both deployment targets:

- `kernel/build/amd64/bin/kernel.elf`:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`
- `ESP/kernel.elf`:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

The proof-only macros and C011EC10 safe stop are not deployed in the ordinary
kernel. Production startup and `[ThreadStatic]` support remain active.

## Cross-references

This milestone builds on and does not rewrite:

- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md`
- `NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md`
- `NATIVEAOT_GC_STARTUP_READINESS.md`
- `NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md`

## Recommended next bounded milestone

Execute only the true-branch `GCHeap::Promote` `dprintf` at locked
`gc.cpp:49507`, then stop immediately before the first object metadata read or
`hpt->mark_object_simple` mutation at line 49541. Preserve the same root,
single-heap sentinel state, and zero-mutation boundary.


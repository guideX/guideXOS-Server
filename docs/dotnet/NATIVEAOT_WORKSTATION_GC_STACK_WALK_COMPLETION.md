# NativeAOT Workstation GC Stack-Walk Completion — C011EC26

## Outcome

C011EC26 is **Outcome A — normal stack-walk completion**.

The structurally registered `_start.halt` boundary converted the C011EC25
proof safe-stop into the normal NativeAOT iterator terminal state. The locked
NativeAOT 9.0.0 AMD64 Workstation GC stack walk reached `_start.halt`, returned
`m_ControlPC = 0` through the injected `StackFrameIterator` native branch,
returned from the stack provider, returned from `Thread::GcScanRoots`, and
reached the first authentic post-scan callback,
`GCToEEInterface::AfterGcScanRoots`.

This milestone does not claim Workstation GC completion, queue draining, mark
bit mutation, child traversal, or restart/resume.

## Starting boundary and Git state

C011EC26 started at the retained C011EC25 Outcome B boundary:

```text
historical physical return PC: 0x355D101E
C25 final physical _start.halt: 0x355CF01E
C25 actual physical base:       0x355CF000
linked address for both:         0x10001E
owner:                           _start.halt
```

The actual starting Git state was authoritative and differed from the
requested expected divergence:

```text
branch:       v1.1_DOTNET_SUPPORT
HEAD:         9c3ee5f59037473bb1d212fb0b5cb6ea5371147a
upstream:     origin/v1.1_DOTNET_SUPPORT
divergence:   ahead 0, behind 0
tracked:      clean
untracked:    0
```

No reset, amend, rebase, squash, restore, discard, or rewrite was used.

## Locked identity

```text
NativeAOT:          9.0.0
architecture:      AMD64
GC:                Workstation
runtime interfaces: 5.3 / 2
source commit:     9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3
```

## C011EC25 safe-stop audit

C011EC25 recognition remained in the proof diagnostic/PAL path in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`:

* `c011ec25ValidateBoundary` performs the historical third-PC proof,
  translates the physical alias using the registered linked module base, and
  validates `_start.halt` provenance.
* `guideXosNativeAotC011EC25SafeStop` emits the proof marker and spins.
* `guideXosNativeAotC011EC23TryNativeUnwind` is the narrow PAL/provider bridge
  used by the injected iterator.

C011EC26 does not delete that historical diagnostic path. For the C26 build,
the terminal classification branch runs before the legacy no-metadata path;
the C25 proof spin is not called for a valid terminal descriptor. Unsupported
or malformed native state retains bounded failure behavior.

## Locked end-of-stack contract

The locked source is under
`out/dotnet/gc-feasibility-baseline/nativeaot-runtime/src/coreclr/nativeaot/Runtime`.

The relevant contract is:

* `StackFrameIterator::InternalInit` in `StackFrameIterator.cpp:134-147`
  leaves the iterator initially invalid for `TOP_OF_STACK_MARKER`.
* `StackFrameIterator::IsValid` in `StackFrameIterator.cpp:1518-1521`
  returns `m_ControlPC != 0`.
* `StackFrameIterator::SetControlPC` in `StackFrameIterator.cpp:1963-1970`
  makes zero the invalid terminal state.
* `Thread::GcScanRootsWorker` in `thread.cpp:442-569` loops while the
  iterator is valid, calls `CalculateCurrentMethodState`, enumerates the
  current managed frame, and then performs the locked post-stack root sources
  for exception, GC-frame, and ThreadAbort roots.
* `GCToEEInterface::GcScanRoots` in `gcenv.ee.cpp:94-133` calls the per-thread
  provider and clears `thread_under_crawl` after enumeration.
* `GCToEEInterface::AfterGcScanRoots` in `gcenv.ee.cpp:145-153` is the next
  post-scan callback.

C26 uses the existing invalid iterator state. It does not synthesize a caller,
create an `_start` `ICodeManager`, or create unwind metadata for `.halt`.

## Production terminal-boundary contract

The stable source of truth is the linker/entry topology:

* `kernel/arch/amd64/boot.asm` exports `_start.halt`,
  `__guidexos_native_terminal_start`, and
  `__guidexos_native_terminal_end` at the same instruction boundary.
* `_start` performs the real `call kernel_main`; return lands at
  `_start.halt`, whose only instructions are `hlt` and a self-loop.
* No unwind metadata is emitted for `.boot` or `.halt`.
* `kernel/core/native_unwind_provider.cpp:55-82` resolves the linker symbols
  during ordinary kernel startup, validates them against the registered
  executable range, and stores only bounded RVAs before publishing the module.
* Suspended-path lookup at `native_unwind_provider.cpp:319-353` reconstructs
  the terminal range from the selected descriptor base and the stored RVAs.

The same RVAs work for the linked descriptor and the physical identity-mapped
kernel alias. No physical boot address or proof kernel hash is whitelisted, and
no dynamic symbol lookup occurs during suspension.

`guideXosNativeUnwindClassify` distinguishes:

```text
UNWINDABLE  = 0   ordinary registered runtime-function metadata
TERMINAL    = 1   only the validated linker terminal range
UNSUPPORTED = -1  no registered terminal or usable native metadata
MALFORMED   = -2  inconsistent registered image/provider state
```

The classification hook is carried in startup-table reserved slot 2 by
`kernel/core/nativeaot_pal_qemu_test.cpp:1121-1122` and bridged through
`guidexos_nativeaot_gc_startup_platform_contract.cpp:77-108`.

## Iterator integration

The C26 harness injects only the following locked-runtime changes:

1. In `StackFrameIterator::CalculateCurrentMethodState`, the existing native
   PAL bridge result `2` means terminal. It calls `SetControlPC(0)`, emits the
   allocation-free iterator checkpoint, clears managed-frame state, and
   returns normally.
2. `Thread::GcScanRootsWorker` receives the narrow post-Calculate guard:
   `if (!frameIterator.IsValid()) break;`. This prevents `EnumGcRefs` from
   treating the terminal state as a managed frame.
3. `Thread::GcScanRoots` and the stack-provider wrapper receive scalar
   entry/return checkpoints.
4. The C26 replacement suppresses only the two duplicate stock
   `RhpReversePInvoke` exports because the production PAL bridge already owns
   those ABI exports; the real `Thread::GcScanRoots` implementation remains
   linked.
5. The locked worker's post-stack sources are observed with scalar source
   codes: `1 = ExInfo`, `2 = GCFrameRegistration`, `3 = ThreadAbortException`.

All C26 suspended-path instrumentation uses bounded scalar/pointer fields and
preallocated storage. It performs no heap allocation, managed re-entry,
dynamic collection, registry mutation, arbitrary stack scan, scheduler
transition, or EE resume.

## C011EC26 preflight and completion

`C011EC26-PREFLIGHT` is emitted only after the suspended iterator has recovered
the terminal PC. Every boot proved:

```text
terminal classification:  1 (TERMINAL)
descriptor valid:         1
terminal lookup attempts: 3
terminal lookup successes: 1
terminal input PC:        0x355CE01E
terminal selected PC:     0x355CE01E
terminal linked PC:       0x000000000010001E
terminal physical base:   0x00000000355CE000
terminal linked base:     0x0000000000100000
terminal begin RVA:       0x1E
terminal end RVA:         0x21
terminal RSP:              0x0000000004E99000
native unwinds:            2
third unwind attempts:    0
```

`C011EC26` was emitted only after all of the following were true:

```text
iteratorCompletionCount:       1
stack-provider entries/returns: 1 / 1
Thread::GcScanRoots entries/returns: 1 / 1
root enumeration complete:     1
ThreadStore lock held:         1
EE suspended:                  1
thread_under_crawl:            0
safe-stop reason:              0
```

The first post-scan event was `GCToEEInterface::AfterGcScanRoots` with queue
operation `0`. A later retained C25 safe-stop line appears in the raw serial
after the C26 success marker when the harness teardown races a subsequent GC
attempt; it is not the C26 result and is excluded from the completion marker.

## Root and queue chronology

The historical C011EC19-C25 stack evidence is preserved exactly at the
iterator terminal:

```text
managed frames:          1
roots at iterator end:   6
category-3 roots:        4
register roots:          3
stack roots:             1
stack-derived Promote:   4 attempts / 4 entries / 4 returns
queue cursor:            4 -> 5
mark writes:             0
child reads:             0
graph traversal:         0
```

Because normal `Thread::GcScanRootsWorker` continues after the iterator loop,
the locked ThreadAbortException root source then added one full-root callback
accounting event. The full normal `GcScanRoots` return therefore recorded:

```text
full roots:              7
full Promote:             6 attempts / 6 entries / 5 returns
first post-stack source:  3 = ThreadAbortException
post-stack source count:  1
queue at GcScanRoots return: 5
```

This is reported as new chronology; it does not relabel the historical six
roots or four stack-derived promotions. No queue drain was forced, and queue
insertion is not called a mark write.

## Thread / EE and bounds invariants

Across all three boots:

```text
ThreadStore owner:              GC initiator Thread
ThreadStore recursion:          1 / retained
current/enumerated/initiator:   equal under retained C25 proof
lock held at GcScanRoots return: yes
EE suspended at marker:         yes
cooperative:                    1
preemptive:                     0
thread_under_crawl:             0
managed-entry attempts:         0
managed re-entry attempts:      0
restart/resume:                 0 / 0
stack base:                     0
ScanContext.stack_limit:        0
stack bounds consumed:          0
```

The stack-bound values were not proactively repaired or consumed.

## Three fresh QEMU 11.0.0 boots

All semantic checkpoints agreed:

| boot | serial SHA-256 | physical terminal PC | linked terminal PC | terminal attempts | native / third unwind | iterator / provider return | GcScanRoots return | post-stack source |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| first-run | `0D0CB2D5E89E4C3E1D28D894668D6733FC9AB5BACD370C0075CDA1EA4660CFD1` | `0x355CE01E` | `0x10001E` | `3 / 1` | `2 / 0` | `1 / 1` | `1` | `3` |
| repeat-1 | `90F4533A0155E437C97C21D286CA338BFFEDA3250765C34EC858C7FDBB216465` | `0x355CE01E` | `0x10001E` | `3 / 1` | `2 / 0` | `1 / 1` | `1` | `3` |
| repeat-2 | `24C2D87F13BD0696955BD12CEC6A4FC009642786C9E1810F6719F27409206857` | `0x355CE01E` | `0x10001E` | `3 / 1` | `2 / 0` | `1 / 1` | `1` | `3` |

The physical kernel base was `0x355CE000` in all three boots. The linked
kernel base was `0x100000` in all three boots. The physical `_start.halt` was
`0x355CE01E`; the linked `_start.halt` was `0x10001E`.

## Artifact hashes and restoration

Ordinary source-state was recorded before proof deployment and restored in the
harness `finally` path. A final production-source rebuild after the proof run
produced a different ordinary artifact, so the ESP artifact was refreshed from
that current ordinary build:

```text
ordinary kernel before proof: 89711C8CBD435CF9DDFE7842037D38C17ABBED3DE8A919A8539A5C887B51E247
ordinary ESP before proof:    89711C8CBD435CF9DDFE7842037D38C17ABBED3DE8A919A8539A5C887B51E247
ordinary kernel final source-state: 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
ordinary ESP final source-state:    75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

Final proof-run artifacts:

```text
proof kernel SHA-256: B3C27BEED5610DB92300F852ACE0B2A630E12DC1ADBBC2A74FA4218BE089C2F1
PE SHA-256:          05EC9E81814488E9340F39DAF229D65AA972FC9797495082FF5947DE180F2C19
ELF SHA-256:         BC29140A8D292ED63234198FB7206D80991C266C5F9422003B121398E5C462F9
MAP SHA-256:         1896513AC134E4C367A00734C1B67B100B7D3BBEFC75D1AA1D191B7E25111472
```

All QEMU processes were absent after the run.

## Regression status

The C26 harness and focused validation retained:

* linked terminal classification;
* physical-alias terminal classification;
* nearby non-terminal executable PC;
* arbitrary metadata-less native PC;
* normal unwindable native frame;
* managed PC rejection by the native-terminal classifier;
* kernel native unwind provider, standalone helper, second-function, and
  AMD64 unwind regressions;
* independent `kernel_main` return-slot guard;
* PE-to-ELF conversion and linker/table validation;
* locked runtime identity/source guards and PowerShell parsing;
* C019-C025 chronology without relabeling;
* normal kernel build and ordinary boot smoke;
* `git diff --check`.

The converter unit test also passed:

```text
python tools/dotnet/test_pe_to_elf_v2_fixed_base.py: PASS
```

## Final Git handoff

The production/provider/bridge/harness changes and this document are intended
for one truthful milestone commit with subject:

```text
Complete NativeAOT GC stack walk
```

The final commit hash, final clean worktree, branch, upstream, divergence, and
push result are reported by the completion handoff. The existing push policy
was preserved; no push was performed by this milestone.

## Next smallest milestone

The next smallest milestone is to consume the first authentic post-
`AfterGcScanRoots` Workstation GC operation with the same bounded,
allocation-free instrumentation. Queue draining, mark-bit mutation, child
traversal, and restart/resume should remain separate milestones.

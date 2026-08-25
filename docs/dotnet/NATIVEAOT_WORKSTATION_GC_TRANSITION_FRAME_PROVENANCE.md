# C011EC44 — NativeAOT Workstation GC transition-frame provenance

## Result

C011EC44 is **Outcome C — earliest divergence proven**, with **Success Level
1**. The failing later-collection path is valid through the authoritative
root-source frame and becomes invalid when the locked AMD64
`CoffNativeCodeManager::UnwindStackFrame` reads the reverse-P/Invoke GC-info
stack slot. That read supplies `0x100811F38`, the prior C40
`neighborDestinationEnd`, as the iterator's next transition-frame pointer.

The exact writer that put that value in the slot, or an earlier reason that the
slot is being read as this location, was not proven. No repair was attempted.
C011EC18 remains fail-closed.

## Repository and locked identity

The mandatory preflight was performed before C44 edits:

| Item | Value |
| --- | --- |
| repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| starting branch | `v1.1_DOTNET_SUPPORT` |
| starting HEAD | `202991a4cabe69cd9af779a4b109f9fabe396f1d` |
| starting subject | `Document NativeAOT C18 GC safety gate provenance` |
| upstream | `origin/v1.1_DOTNET_SUPPORT` |
| starting divergence | ahead 1 / behind 0 |
| starting worktree | clean; no untracked files |
| C43 history | present at starting HEAD; C43 is a clean ancestor of the final commit |
| C43 push state | not pushed at preflight |

While C44 was in progress, the repository legitimately advanced to
`4b066f0e6915893210635481c3963cd08c0fba69` (`Implemented Phase 20 as Outcome
A.`), and `origin/v1.1_DOTNET_SUPPORT` advanced to the same commit. C44 did not
reset, stash, clean, amend, rebase, or rewrite that history.

The runtime identity remained locked:

| Item | Value |
| --- | --- |
| NativeAOT | `9.0.0` |
| architecture | AMD64 |
| GC | Workstation; one heap; background/concurrent disabled |
| interfaces | GC `5.3`; EE `2` |
| runtime source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |

## C43 malformed state

C43's historical report recorded control PC `0x1030BDF0`. The fresh C44
three-boot image has the same semantic failure with build-local target value
`0x1030F500`; both values are heap/EEType-like data, not executable managed
code. The stable C44 input to C18 was:

| Field | C43/C44 value |
| --- | ---: |
| iterator frame | `0x0000000100811F38` |
| malformed control PC | historical C43 `0x1030BDF0`; C44 `0x1030F500` |
| SP | `0x4E3D2C1B0AF9E8D7` |
| FP | `0x0000000000010000` |
| flags | `0x00000000DAC9B8A7` |
| expected `CoffNativeCodeManager` | `0x0000000010263AD0` |
| observed manager | `0x0000000000000000` |
| C18 reason | `0x00EC1801` |
| PAL fail-fast | `0x47435354` |

The C44 C18 observer recorded the actual malformed fields after
`StackFrameIterator::InternalInit` interpreted the selected pointer. The
noncanonical SP/FP/flags were not found in the checked-in source as known
poison constants. The locked Coff implementation has unrelated debug-only
`0xDD` initialization for unwind context objects, but C44 did not establish
that as the source of these values.

## Authoritative pipeline audit

The audit used the locked runtime source at the pinned revision. The relevant
pipeline is:

| Stage | Locked producer and range | Authoritative state, owner, lifetime, and consumer |
| --- | --- | --- |
| transition-frame creation | `nativeaot/Runtime/amd64/AsmMacros.inc:286-305`, used by `AllocFast.asm`; the C42 array path is `RhpNewArrayRare:205-244` | `PUSH_COOP_PINVOKE_FRAME` creates stack storage and writes saved caller RSP, nonvolatile registers, flags, thread, FP, and return RIP. `POP_COOP_PINVOKE_FRAME` ends that stack lifetime. |
| allocation helper entry | `nativeaot/Runtime/GCHelpers.cpp:571-604`, `RhpGcAlloc` | Receives `pTransitionFrame`, checks/fixes a hijack target in `m_RIP`, publishes it through `SetDeferredTransitionFrame`, then calls `GcAllocInternal`. |
| helper/GC transition state | `nativeaot/Runtime/thread.cpp:38-89,106-158`; `thread.h:86-93,196,268-299` | `m_pTransitionFrame`, `m_pDeferredTransitionFrame`, and `m_pCachedTransitionFrame` are thread-owned pointers. `WaitForGC` publishes/clears the live pointer; suspension caches a non-null pointer; restart/continuation state is not stored in the heap. |
| suspension | `GCToEEInterface::SuspendEE` path, with `SuspendAllThreads` and the platform after-suspend boundary | The suspended thread/frame and `eeSuspended` state are authoritative only while the thread-store suspension contract is held. C44 observes after suspension is established. |
| root source | `nativeaot/Runtime/thread.cpp:393-403`, `Thread::GcScanRoots` | `GetTransitionFrame()` selects deferred state for the suspending thread and cached state otherwise; that pointer seeds `StackFrameIterator`. |
| initial iterator state | `nativeaot/Runtime/StackFrameIterator.cpp:134-330`, `InternalInit` | Copies `m_RIP` into control PC, maps `m_FramePointer` to FP, consumes preserved registers and saved SP, and records `m_pPreviousTransitionFrame`. |
| reverse-P/Invoke predecessor | `nativeaot/Runtime/windows/CoffNativeCodeManager.cpp:651-711` | Decodes reverse-P/Invoke slot and reads `*(PInvokeTransitionFrame**)(basePointer + slot)` into `ppPreviousTransitionFrame`. `StackFrameIterator::NextInternal:1529-1600` then calls `InternalInit` on that value when native frames are skipped. |
| C18 validation | `nativeaot/Runtime/StackFrameIterator.cpp:1913-1949` | `CalculateCurrentMethodState` must resolve a non-null manager and successful `FindMethodInfo` before root enumeration. The existing fail-fast is unchanged. |

C44 added allocation-free scalar checkpoints in
`guidexos_nativeaot_platform.cpp` and narrow source-site injections in the
existing QEMU harness. It did not change the production frame fields,
iterator selection, code-manager lookup, or fail-fast predicate.

## Authoritative transition-frame object

The valid object is the locked `PInvokeTransitionFrame` from
`nativeaot/Runtime/inc/rhbinder.h:462-482`:

| Property | Finding |
| --- | --- |
| type | `PInvokeTransitionFrame` |
| AMD64 base size | `0x20` bytes before the flexible preserved-register array |
| AMD64 maximum allocation | `0x68` bytes (`9` preserved-register words) |
| storage | caller stack, created by the AMD64 allocation-helper macro |
| owner | current managed/native transition and its owning stack frame |
| valid C44 example | later collection frame `0x0000000004EBEA38`; first allocation frame `0x0000000004EBE8B8` |
| creating function | C42 path: `RhpNewArrayRare` using `PUSH_COOP_PINVOKE_FRAME`; no C++ constructor or heap allocation |
| destruction/unlink | `POP_COOP_PINVOKE_FRAME` on the normal/OOM return path; no destructor; thread slots are cleared or replaced by the locked `Thread` methods |
| chain link | no previous-link field in `PInvokeTransitionFrame`; the iterator's `m_pPreviousTransitionFrame` is populated from reverse-P/Invoke GC-info stack data |
| control PC producer | AMD64 macro reads the caller return address and stores it as `m_RIP`; `RhpGcAlloc` may only correct a hijack target |
| SP producer | saved caller RSP in the preserved-register area, consumed by `InternalInit` when `PTFF_SAVE_RSP` is set |
| FP producer | caller RBP stored at `m_FramePointer` |
| flags producer | `DEFAULT_FRAME_SAVE_FLAGS` in the AMD64 macro; field offset `0x18` |

The value `0x100811F38` is not treated as an authoritative frame object. It is
the raw value read from the later reverse-P/Invoke slot and is classified as a
selected unrelated object/value because interpreting it as a
`PInvokeTransitionFrame` produces the C43 state above.

The checked layout is AMD64: `m_RIP=0x00`, `m_FramePointer=0x08`,
`m_pThread=0x10`, `m_Flags=0x18`, `m_PreservedRegs=0x20`; thread-local slots
are `m_pTransitionFrame=0x40`, `m_pDeferredTransitionFrame=0x48`, and
`m_pCachedTransitionFrame=0x50`. C44 added compile-time assertions for these
offsets. The assembly/native header and observer therefore agree on the
layout; no ABI/layout repair was made.

## Checkpoint results

The records below are from the first of three fresh boots. The same semantic
records and selected divergence fields were stable in all three boots.

| Checkpoint | Frame / source | PC | SP / FP / flags | Manager / managed | Classification |
| --- | --- | --- | --- | --- | --- |
| `FRAME-CREATE`, collection ordinal 4 | `0x4EBEA38` | `0x10001EDB` | `0x4EBEAA0 / 0x4EBEAD0 / 0x80F7` | `0x10263AD0 / 1` | valid |
| `PRE-GC`, ordinal 4 | `0x4EBEA38` | `0x10001EDB` | same | `0x10263AD0 / 1` | valid |
| `SUSPEND`, ordinal 4 | `0x4EBEA38` | `0x10001EDB` | `0x4EBEAA0 / 0x4EBEAD0 / 0x80F7` | `0x10263AD0 / 1` | valid; `suspend=1` |
| `ROOTSOURCE`, ordinal 4 | `0x4EBEA38` | `0x10001EDB` | same | `0x10263AD0 / 1` | valid; authoritative `GetTransitionFrame()` source |
| initial `ITERATOR`, ordinal 4 | `0x4EBEA38` | `0x10001EDB` | same | `0x10263AD0 / 1` | valid; method metadata non-null |
| later valid iterator samples | `0x4EBEA78` and related stack frames | `0x100023AA` | canonical stack values | `0x10263AD0 / 1` | valid |
| `DIVERGENCE`, ordinal 4 | raw value `0x100811F38` | source PC not dereferenced | observer fields intentionally zero | `0 / 0` | invalid raw off-stack slot value |
| final `ITERATOR` / C18 input | `0x100811F38` | `0x1030F500` | `0x4E3D2C1B0AF9E8D7 / 0x10000 / 0xDAC9B8A7` | `0 / 0` | invalid; C18 fails closed |

The C44 divergence observer deliberately does not dereference an arbitrary
off-stack value. Its `pc=0` in the divergence record means “raw producer slot
only”; the later iterator record is the authoritative observation of the
fields that the locked `InternalInit` consumed. The separate ordinal-0 raw
slot observation contained `0x10`; it is retained in serial evidence but is
not conflated with the later C43 target correlation.

A valid frame was required to have an executable managed PC, the correct
non-null code manager, successful `FindMethodInfo`, and valid method metadata.
The first valid checkpoint on the failing ordinal-4 path is the prior valid
iterator/method-info state. The first invalid checkpoint on that path is the
ordinal-4 `C011EC44-DIVERGENCE` reverse-P/Invoke slot load.

## First divergence

The earliest proven valid-to-invalid transition for the C43 failing path is:

```text
CoffNativeCodeManager::UnwindStackFrame
  decoder slot offset       = -0x70 = 0xFFFFFFFFFFFFFF90
  stack base used           = FP = 0x4EBEAD0
  slot address               = 0x4EBEA60
  loaded ppPreviousFrame    = 0x100811F38
  later iterator frame      = 0x100811F38
```

The slot is read by the locked statement at
`CoffNativeCodeManager.cpp:692`. `NextInternal` then selects that non-null
value and calls `InternalInit`; `InternalInit` reads the value as a
`PInvokeTransitionFrame`, producing the non-executable control PC and
noncanonical register values observed at C18. Thus:

- the authoritative root-source frame was valid before this selection;
- iterator initialization is not the first divergence on the failing path,
  although it is where the raw value becomes malformed frame state;
- the production state was not proven corrupt before the slot read;
- the exact memory writer or wrong-offset cause remains one step deeper.

This is **Code 6 — Wrong frame selected** at the proven consumer boundary. It
is not a repairable Code 1/5 conclusion yet.

## C40 `neighborDestinationEnd` correlation

The correlation is exact and direct at the value level:

| Question | C44 result |
| --- | --- |
| numerical equality | exact: `0x100811F38 == 0x100811F38` |
| C40 field | `C011EC40 neighboringLiveDestinationEnd`, reported as `neighborDestinationEnd` |
| target data | C40 `targetEEType` equals the fresh C44 malformed PC `0x1030F500` |
| saved return/register evidence | the final `savedRSI` record equals `0x100811F38` |
| intentional assignment | C40 intentionally assigns its diagnostic neighbor-end field from the compaction destination; no intentional assignment of that value to the transition slot was proven |
| physical adjacency | not shown; valid transition-frame storage is on the stack near `0x4EBE...`, while `0x100811F38` is in the heap/EEType-like address domain |
| C40/C41/C42 write to target | not proven; no broad watchpoint was introduced |
| persistence | exact selected value and correlation were stable in 3 independent boots; cross-collection write history was not captured |
| wrong structure/pointer | possible deeper cause, not proven by this phase |
| heap/stack alias | numerical alias only; physical aliasing was not proven |
| diagnostic overlay/union | none found in the audited C44 structures |

The known diagnostic writer for the C40 field is the C40 compaction observer in
`guidexos_nativeaot_platform.cpp:13740` (the
`neighboringLiveDestinationEnd = destinationEnd` assignment). That is
provenance for the diagnostic field, not proof that it wrote the
reverse-P/Invoke stack slot. No live frame-memory
overwrite was observed or proven, and compaction, allocation/refill, and
suspend/resume overwrite theories remain open.

## Lifetime, restoration, and stack checks

The locked source explicitly documents the lifetime hazard around
`m_pTransitionFrame`: suspension caches the non-null live pointer because
return-to-managed code may temporarily clear the live slot. C44 observed the
valid deferred/cached source before root enumeration and the malformed value
only after the reverse-P/Invoke slot load. It did not prove that a stale
`m_pTransitionFrame`, cached frame, or deferred frame survived `RestartEE`, nor
that allocation helpers reused the valid frame's stack slot. `RestartEE`, mark,
planner, root-scan completion, and managed resume were not reached in the
malformed run.

The observed SP/FP/flags were not identified as a known fill pattern,
endianness error, or structure packing error. A separate C20 diagnostic saved
SP pattern is not treated as proof of this C43 frame. The layout assertions
and AMD64 assembly offsets passed; no layout change was made.

## Diagnostics and repair policy

C44 markers are fixed-size, bounded, allocation-free scalar records:

`C011EC44-FRAME-CREATE`, `C011EC44-PRE-GC`, `C011EC44-SUSPEND`,
`C011EC44-ROOTSOURCE`, `C011EC44-ITERATOR`, and
`C011EC44-DIVERGENCE`.

The producer-slot observer avoids arbitrary pointer dereference. It records
the slot address, signed offset, raw value, base register, thread, collection
ordinal, and source method information. It does not write runtime state,
fabricate a PC, substitute a manager, skip a frame, or suppress C18.

No repair was made because the exact write/lifetime/layout cause is not proven.
The malformed state still fails at the original manager guard with
`0xEC1801`; valid earlier frame state still resolved through the production
manager and valid `FindMethodInfo`. The failed malformed `FindMethodInfo` case
is not reached because the null-manager fail-fast remains first.

## Three-boot evidence

Evidence root:
`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec44-transition-frame-provenance\run-20260824-213248054`

QEMU was `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`). All three fresh independent
processes reached the same `C011EC44` safe stop, C18 reason, malformed frame,
and raw divergence correlation:

| Run | Serial SHA-256 |
| --- | --- |
| first-run | `8DD7E9636D2B308FCF96450E8B2F0D6D004F6EE7DEEC0F4CCA7670007A44BAA9` |
| repeat-1 | `9D2072973B9757296F95F6557436A08A7A5DD321BE086C26320086950D4A9E70` |
| repeat-2 | `F45DA18197E95B78D9A4C6E4FC215140C904DE0B7DD190BE0BD68216C47768DF` |

Proof kernel SHA-256:
`D3CF8C88FDC1D072C9566CB0B71D594F2A5D98C3E75684D6AFAE7EE868181542`.
The PE-to-ELF conversion completed successfully. The exact command log and
serials are in the evidence root's `commands.txt` and run directories.

Final proof artifact hashes from
`out/dotnet/c011ec44-transition-frame-provenance/build-run-20260824-213248054/artifact`:

```text
NativeAotGcSingleThreadSuspendEe.exe                  90A446718D3AB025A43FBD73780EA8DAD6C71F4B6E67EBC714DB57AB3D5EF2B3
NativeAotGcSingleThreadSuspendEe.elf                  4BBE7A7E089FB6415FA24426EA10B7766CA222D1E700AF98D48BA104AD74BEDF
NativeAotGcSingleThreadSuspendEe.map                  2DBAA8A6E0A0D20D859101CBD17AA7CA0519F207E35D890D287AE2FD5A654C95
Runtime.WorkstationGC.guidexos-nativeaot-single-thread-suspend-ee.lib
                                                        39F638858539EFC9EADB1570B80C4311E97E5D72FE0C0F7D6BC1CA60DEC92532
manifest.json                                           3C7A6CC78B7C6373FCA1FE9D3EE22AD995B48A67EF8222666DA729243342D70C
```

## Regression matrix and restoration

| Check | Result |
| --- | --- |
| C18 valid-state regression | PASS: prior valid manager and `FindMethodInfo` result retained |
| C18 invalid-state fail-closed | PASS: null manager and `0xEC1801` fail-fast retained |
| C37 | retained predecessor chronology/guards |
| C39 | retained planner/compaction chronology/guards |
| C40 | retained reclamation chronology/guards |
| C41 | retained allocator provenance chronology/guards |
| C42 | PASS: natural later-collection entry retained |
| C43 | PASS: gate and malformed-state provenance retained |
| C19–C43 chronology | PASS: source guards retained |
| PE→ELF converter | PASS |
| linker/source/table guards | PASS |
| standalone MASM | not applicable; no assembly source changed in C44 |
| ordinary boot smoke | PASS after artifact restoration |
| `git diff --check` | PASS before commit |

The C37–C43 entries marked “retained” are the harness's preserved chronology
and source guards; C44 did not relabel them as independent full reruns. No
diagnostic allocations were observed in GC-sensitive paths, and invariant
failures remained zero.

After proof execution, the ordinary kernel and ESP were restored and verified:

```text
kernel/build/amd64/bin/kernel.elf  75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
ESP/kernel.elf                     75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

No `qemu-system-x86_64` process remained. The C44 manifest is at
`out/dotnet/c011ec44-transition-frame-provenance/run-20260824-213248054/manifest.json`.

## Remaining blocker and next milestone

The remaining blocker is exact write/source provenance for the word at
`0x4EBEA60` (or proof that the locked reverse-P/Invoke slot offset is being
applied to the wrong reconstructed stack/register state). The next smallest
milestone is a similarly narrow, non-invasive audit of the producer slot's
stack contents and write sites, without dereferencing arbitrary frame values
and without weakening C18. No allocator redesign, GC policy change, general
unwinder rewrite, or broader NativeAOT refactor is in scope.

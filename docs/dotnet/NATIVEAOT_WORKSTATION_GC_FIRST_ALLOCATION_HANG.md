# NativeAOT Workstation GC first-allocation hang investigation

Status: 2026-07-26. This report is the hash-specific follow-up to the
original first-allocation experiment. The original document's Outcome C
evidence is retained; this pass preserves that artifact, captures its live
instruction pointer, corrects the reached runtime/image initialization
boundaries, and retries exactly one collector-backed `byte[24]` allocation in
fresh disposable QEMU processes.

## 1. Objective

Locate the exact boundary reached by the first real `RhpNewArray` call, classify
the original non-return as spin/block/fault/indirect-call escape, and correct
only the source-backed invariant at that boundary. No collection, finalizer,
second managed allocation, GC shutdown, or same-process reinitialization was
allowed.

## 2. Authorized runtime and collector identity

| Item | Authorized value |
| --- | --- |
| NativeAOT/runtime source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| ILCompiler/runtime pack | `9.0.0` |
| Architecture | AMD64 |
| GC interface / EE interface | `5.3` / `2` |
| Collector | Workstation GC, one heap |
| Server / concurrent/background GC | disabled / disabled |
| PAL archive | `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F` |
| PAL ABI | version 1, size 232, capabilities `0x1FF` |
| GC startup extension | version 1, size 216, capabilities `0x7` |
| Adapted-GC normalized identity | PASS; fresh normalized builds and semantic archive contents match |

The locked adapted archive member captured for the immutable baseline is
`A36FCA208A5910ACFF11B521C231C99FFE0872BF0A3A9DB09A6DCF9ED48CAF3D`. The
identity gate was not re-investigated.

## 3. Immutable failing artifact

The baseline was copied before instrumentation and was not rebuilt afterward.

| Artifact | SHA-256 |
| --- | --- |
| Allocation PE | `47ACD0179F25F3A4C0D39314E3F73D0629325D4528B92198A710845DDCA7D743` |
| Converted allocation ELF | `047572BB975DC89FA8B53C7E8253770BED852EF7B44A42B3F60DA77554E94D00` |
| Baseline map | `480EBB8FF5FE45E4F5DA6D64CEB6BD17B06D8839A12EEED3DB8C72C87D296A57` |
| Baseline kernel | `5EA55AA12A176605C88932482922BD58F5A6C7431901E7E3FFEA78A3FE7551DA` |
| Normal kernel restored after experiment | `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C` |
| Bootloader | `B4D0113301BBBE9F35078B344E23F0B1936933C392C9C1545AE0F516ABA8E105` |
| Baseline serial | `B585F92F0E4E94E1EB44CBB5E28662899ADB0B1C8A83BDC8AFD821BC0D98BED2` |

The preserved immutable run is under
`out/dotnet/gc-first-allocation-hang/baseline/`. A fresh QEMU confirmation
using the copied PE, ELF, kernel, bootloader, and ESP reached `ManagedMain`
once and reproduced the terminal condition without rebuilding the image.

The serial wording from the earlier experiment described entry to the
allocation path. The live GDB capture of this exact immutable image resolves
the terminal boundary more precisely: the image reaches the reverse-P/Invoke
guard and enters the fail-fast self-loop before the wrapper's `RhpNewArray`
stage marker. This is the exact behavior of the captured hash, while the
historical Outcome C serial remains preserved unchanged.

## 4. Exact allocation call graph

The corrected report image resolves the relevant addresses as follows:

| Edge | Address and transfer | ABI/arguments | Expected result |
| --- | --- | --- | --- |
| `ManagedMain` -> `RhpNewArray` | `0x10060900` -> `0x10001D40`, direct generated call | Win64; RCX = EEType, RDX = 24 | RAX = managed object |
| `RhpNewArray` -> `guideXosStockRhpNewArray` | `0x10001FEA`, direct call to `0x10005F40` | Win64; same EEType/length contract | RAX = object or rare-path result |
| stock helper fast path | `0x10005F40`; reads GS vector, TLS cell + `0x30`/`+0x38` | allocation context pointer/limit | bumps `alloc_ptr`, writes header/length, returns |
| stock helper rare branch | `0x10005FB0`, direct branch when context is empty/insufficient | EEType in RCX, length in RDX | enters GC allocation helper |
| `RhpNewArrayRare` -> `RhpGcAlloc` | `0x10005FE2` -> `0x10003A20`, direct call | Win64; EEType, length, computed size and GC flags are carried in the matching helper frame | RAX = GC object or null |
| `GcAllocInternal` -> `IGCHeap::Alloc` | `0x100032DB`, indirect through cell `0x100F1738` | Win64 virtual call; GC allocation context pointer, size, flags | RAX = object |
| Workstation heap allocation | target `0x10016FE0`, `GCHeap::Alloc` | matching NativeAOT GC interface | publishes object/context or requests the matching slow path |

The stock helper is not opaque: its fast block is `0x10005F40-0x10005F9A`,
its rare/refill block is `0x10005FB0-0x1000601E`, and the Workstation dispatch
is visible in `GcAllocInternal` at `0x100032DB`. The disassembly is preserved
under `out/dotnet/gc-first-allocation-hang/disassembly/`.

## 5. Stage-marker design

The fixed native diagnostic record is single-writer, bounded, preallocated,
and updated with nonblocking scalar stores. It contains the stage, sequence,
RIP/RSP, thread record, GC mode, transition frame, EEType, length, computed
size, allocation context, direct/indirect targets, lock/event IDs, wait reason,
and watchdog tick value. It never calls serial logging or a native allocator.

The emitted source-backed stages are A00 managed entry, A01 reverse-P/Invoke
ready, A02 `RhpNewArray` entry, A03 type/length accepted, A04 size computed,
A05 context loaded, A06 fast path attempted, and A16 return. A07-A15 are
reserved names for boundaries inside the matching stock/GC implementation;
they are not emitted by fragile assembly or collector code in this pass. The
disassembly and final indirect target prove the rare/refill and Workstation
heap edges without adding a diagnostic call inside them. Fail-fast stages F02,
F06, and F08 identify reverse-P/Invoke, metadata/EEType, and GC-state contract
failures. A16 is only written after the real object and post-allocation state
have been checked.

## 6. Watchdog design

The watchdog used the existing PIT/QEMU harness and one external QEMU
monitor/GDB stop/query. The baseline snapshot was taken without requiring the
allocating code to return; the allocating CPU was stopped once, its registers
and instruction window were captured, and the disposable QEMU instance was
quit. The marker record was never printed from the interrupt handler.

The mechanism was validated against the baseline terminal loop and the
completed fresh-process path: it distinguished a running CPU at a self-loop
from a completed allocation. The existing Event, mutex, scheduler, and thread
probes remain the independent coverage for blocked, timed-wait, and lock-wait
states. Dedicated new synthetic opt-in spin/Event/mutex cases were not added
to this GC pass.

## 7. Spin-versus-block classification

For the immutable baseline, classification is **Hang A: terminal active spin
(fail-fast self-loop)**, not a collector lock or Event wait. The CPU was
runnable, interrupts were enabled, `waitReason` was none, and the instruction
was `jmp 0x10001CA7`. It is not an allocator polling loop. There was no
evidence of helper dependency, interrupt-disabled deadlock, or indirect-call
escape at the captured RIP.

## 8. Captured RIP and disassembly

The immutable baseline GDB snapshot is
`out/dotnet/gc-first-allocation-hang/qemu/baseline-gdb2/gdb-snapshot.txt`:

| Field | Value |
| --- | --- |
| RIP | `0x10001CA7` |
| Resolved source/basic block | `guideXosFailFast+0x7`, terminal `jmp 0x10001CA7` |
| RSP | `0x4E52B68` |
| RFLAGS | `0x246`, IF = 1 |
| GS base | `0x0` |
| Wait reason | none |

The next stack frame is the reverse-P/Invoke transition path and the guard is
the exact source-backed failure. In the corrected image the final observed
allocation RIP is `0x100021AB`, immediately before the A16 return marker; the
resolved allocation path is `RhpNewArray` -> stock helper -> rare/refill ->
`GcAllocInternal` -> Workstation `GCHeap::Alloc`.

## 9. Thread and scheduler state

At the original stop, CPU 0 was running the fail-fast loop, not parked. PIT
dispatch continued, and IF was enabled. In successful runs the allocating
thread record was `0x03917C00`, the finalizer/helper worker remained parked in
its normal state, and the process was terminated only through disposable-QEMU
teardown. No real helper thread was force-woken or terminated.

## 10. Allocation-context state

The corrected final report captured:

| Field | Value |
| --- | --- |
| allocPtr before/after | `0x100A00058` / `0x100A00058` |
| allocLimit before/after | `0x100A00FD0` / `0x100A00FD0` |
| context refill count | 1 |
| slow allocation entries | 1 |
| classification | valid-fast collector context; stock TLS mirror entered real rare/refill |

The collector-owned `Thread::GetAllocContext()` state was valid-fast at the
wrapper observation (`allocPtr < allocLimit`). Separately, the stock assembly
helper reads its NativeAOT runtime-cell mirror at TLS cell `+0x30`/`+0x38`;
that mirror was intentionally not seeded by the harness. The helper therefore
took its source-backed rare/refill path, which reached the real Workstation
heap and published the context. Nothing was manually populated or fabricated;
the post-call context remained valid and associated with the allocating thread.

## 11. GC mode state

The final fixed record reports `gcMode=0` at the wrapper boundary, meaning the
probe observed preemptive mode there. The captured transition frame is
`0x4E53BC8`. The matching rare allocation helper establishes the cooperative
GC frame required by the GC interface before entering `GcAllocInternal`; the
mode was not blindly flipped by this pass. The finalizer/helper and runtime
thread state remained attached and valid.

## 12. Heap and segment state

The corrected run reported heap initialized and a real Workstation heap-owned
object:

| Field | Value |
| --- | --- |
| Heap base / object | `0x100A00028` / `0x100A00028` |
| Heap reserved end | `0x100B00000` |
| Active allocation context end | `0x100A00FD0` |
| Object data | `0x100A00038` |
| Available post-refill context bytes | `0xF78` |
| Heap ownership | PASS |

The `GetGenerationWithRange` boundary is reported at the object start in this
adapter, so its `heapAllocated` field is also `0x100A00028`; the ownership
check uses the segment base/reserved range and the post-allocation context.
No collection or segment expansion was requested.

## 13. Lock, Event, and helper state

The final report has `lastLockId=0`, `lastEventId=0`, and
`waitReason=0`. There is no mutex owner, Event state, or wait queue to report
for the reached allocation. The helper/finalizer worker stayed parked and
valid; finalization scans and executions were both zero. No Event was manually
signaled, and no lock was bypassed.

## 14. Indirect-call audit

The executed Workstation allocation dispatch was:

| Item | Value |
| --- | --- |
| Last direct target | `0x10005F40` (`guideXosStockRhpNewArray`) |
| Indirect cell | `0x100F1738` |
| Indirect target | `0x10016FE0` (`WKS::GCHeap::Alloc`) |
| Executable mapping / ABI | PASS; mapped final PE address, Win64 contract |

The target is neither zero, an import stub, an unmapped address, a SysV
implementation without a bridge, nor a retired artifact generation. The
baseline failure occurred before this dispatch because the current-thread GS
TLS vector was absent (`gs_base=0`).

## 15. Root cause

The first exact blocker was the missing current-thread NativeAOT TLS vector and
runtime cell. `RhpReversePInvoke` requires that vector before entering managed
code; the baseline had `gs_base=0`, so its guard called `guideXosFailFast` and
the CPU remained in the terminal self-loop. This is why the immutable image
had no trustworthy allocator RIP despite the earlier coarse serial
description.

After that invariant was corrected, the same image exposed two subsequent
image/runtime readiness defects: the PE-to-ELF converter did not map the
fileless `hydrated` section, and the real-allocation build did not invoke the
matching NativeAOT `RehydrateData` boundary. With the section mapped, the
EEType page was present; with source-backed hydration enabled before
`ManagedMain`, the EEType became valid and the real GC allocation reached the
Workstation heap. The diagnostic range proof was also corrected to use the
actual computed object size `0x30` (48), not the historical bounded-mode size
`0x28` (40).

## 16. Correction

Only these reached invariants were corrected:

1. Install the current-thread GS TLS vector/block and runtime-cell storage
   before managed entry; no allocation context was fabricated.
2. Map NativeAOT PE sections with virtual size but zero raw bytes into the
   converted ELF image, preserving their zero-filled image semantics.
3. Call the matching NativeAOT `RehydrateData` helper once during runtime
   initialization for the managed-allocation artifact, before `ManagedMain`.
4. Use the source-computed `byte[24]` object size (`0x30`) and valid segment
   bounds in the nonallocating diagnostic validator.

No Workstation GC lock, Event, suspension, collection, or allocation algorithm
was bypassed or changed.

## 17. First corrected QEMU run

Final corrected image hashes:

| Artifact | SHA-256 |
| --- | --- |
| Allocation PE | `A18ADF9CA822F48BBF1841A6DB1DF9BE02949A376AEA540FD86EFC0B35934C68` |
| Converted ELF | `76C2227933E03CB8BC8D2B95E06C1319FD9DDB996C7C5A5CC49F0E460609EAE1` |
| Map | `CDCF061EF9FDD73827392B7561DCA138130F2F23D79E2A43D2416115498AA775` |
| Kernel | `41A14A553696846F27A63CCD34E308E90A0DDF835D57CDAA2AEDCCC5DF3BE68` |
| ESP manifest hash | `6CAD6CDEB78D2A7D10CC7079A51C80403268DD47048F5F0E6CDD5AE49924095F` |
| Serial | `2FBEEE3CBC690F87BE9D155D4086EB376FE1926AC55A56B7297C52FC0B689CF9` |

The run was `out/dotnet/gc-first-allocation-hang/qemu/final-report-run/`. It
entered `ManagedMain` once, entered `RhpNewArray` once, performed one real GC
allocation, returned a non-null object, validated length 24, found all 24
initial bytes zero, validated the pattern, and proved heap ownership.

The ESP hash is a deterministic SHA-256 over sorted relative ESP paths, file
sizes, and per-file SHA-256 values. The per-run `identity.txt` files preserve
the PE, ELF, kernel, bootloader, adapted-GC, ESP, and serial hashes.

## 18. Fresh-process repetition

Two additional disposable QEMU processes used the same final PE/ELF/kernel,
bootloader, adapted-GC identity, and ESP contents. Both passed one allocation
and zero collections:

| Process | Serial SHA-256 | ESP hash |
| --- | --- | --- |
| repeat 1 | `37367DB9E4BE896F33E4A6762A54ECE542243197DC61053C664686C1EEAF68CF` | `6CAD6CDEB78D2A7D10CC7079A51C80403268DD47048F5F0E6CDD5AE49924095F` |
| repeat 2 | `FF97C374A9B451D597DC9AF26E84E2D7C2D04540A37585EFC63451DD29B3C2A5` | `6CAD6CDEB78D2A7D10CC7079A51C80403268DD47048F5F0E6CDD5AE49924095F` |

The allocation PE/ELF hashes are the same as the first corrected run. The
historical corrected kernel remains
`41A14A553696846F27A63CCD34E308E90A0DDF835D57CDAA2AEDCCC5DF3BE68`; after the source-derived
object-size validator correction, a clean current-source relink produced
kernel hash `18784067D03861716EC0DDE3716B2FD9252F7F55EDA04874B9DC27A2323A73`.
That relink also passed one fresh disposable allocation process. The adapted
normalized identity is the same authorized identity above.

## 19. Collection and finalization counters

For the corrected run and both repetitions: `gcCountBefore=0`,
`gcCountAfter=0`, `collectionsEntered=0`, `collectionTriggeringEntries=0`,
`gcInProgressBefore=0`, `gcInProgressAfter=0`, `finalizationScans=0`, and
`finalizersExecuted=0`. Each process had exactly one `ManagedMain` entry,
one `RhpNewArray` entry, one slow allocation entry, and one real GC allocation
entry. No second managed allocation was attempted.

## 20. Verification-pass regression matrix

The current verification reran the HostLog path, the historical bounded
single-allocation proof, runtime-pack static/state checks, the focused
fileless-section converter test, generic ELF, events, hosted native threads,
hosted true-VM, mutex, local-storage/FLS, and the bounded repeated-allocation
proofs. The 64 KiB execution produced exactly 234 allocations and controlled
OOM; the 4 KiB execution produced exactly 14 allocations and controlled OOM.
Both reported collection entered 0 and heap expansion 0. Inventory isolation
also passed.

The normal-kernel startup-only QEMU probe passed in one fresh disposable
process. The generic mutex QEMU lifecycle probe passed its guest marker and
all listed checks. The generic native-thread, true-VM, and local-storage QEMU
lifecycle runners were not rerun after the current-source correction. The
stack-bounds runner remains incomplete because PowerShell treated compiler
warning stderr as a terminating harness error after its hosted checks; its
hosted and adapter checks were present, but the script did not complete.
FLS-before-initialization and process-teardown-policy checks remain blocked by
the documented absence of standalone harnesses. These items are not called
PASS here.

## 21. Verification decision outcome

**Verification Outcome B — Core allocation is valid, closure validation
remains incomplete.** The prior Outcome A core result is authoritative for the
collector-backed allocation itself: the current sources reproduce one real
`byte[24]` allocation with source-derived layout, zeroing, pattern, range, and
Workstation ownership, with no collection or finalization. The verification
pass does not authorize the next experiment because the complete generic-QEMU
regression matrix and the stack/teardown harness closure are incomplete, and
the generated evidence directory is tracked rather than ignored. In
addition, the existing startup-QEMU script does not itself build and enforce
both allocation-specific Makefile variables and matching `EXTRA_CFLAGS`
definitions; the final manual build log does contain both.

## 22. Exact next experiment

Not authorized by this verification pass. The proposed next experiment is
bounded primitive-array allocation through Workstation GC until the first
subsequent allocation-context refill, without allowing collection; it must
wait for the Outcome B closure items above.

## Validation report

| Field | Result |
| --- | --- |
| Authorized collector identity | PASS |
| Immutable failing artifact preserved | PASS |
| ManagedMain entries | 1 per live process |
| RhpNewArray entries | 1 per live process |
| Last allocation stage | A16 (`0x00000A10`) corrected; F02 baseline |
| Hang classification | Hang A terminal fail-fast self-loop, IF enabled |
| Captured RIP | `0x10001CA7` baseline; `0x100021AB` final A16 |
| Resolved symbol/basic block | `guideXosFailFast+0x7` self-loop; final `RhpNewArray` return boundary |
| Thread scheduler state | baseline runnable CPU 0; final worker parked, allocator completed |
| Interrupts enabled | yes at baseline snapshot |
| Wait reason | none |
| Lock identity and owner | none / none |
| Event identity and state | none / none |
| Helper worker state | parked and valid |
| allocPtr / allocLimit | `0x100A00058` / `0x100A00FD0` |
| Allocation-context classification | valid-fast collector context; stock TLS mirror entered real rare/refill |
| GC mode | `0` at wrapper boundary; cooperative rare-path frame established by matching helper |
| Transition frame | `0x4E53BC8` |
| Heap initialized | yes |
| Active segment | `0x100A00028`-`0x100B00000` reported ownership range |
| Available allocation bytes | `0xF78` post-refill context bytes |
| Collection requests / entered | `0` / `0` |
| Last indirect call target | `0x10016FE0` (`WKS::GCHeap::Alloc`) |
| Root cause | missing current-thread GS TLS vector/runtime cell; then hydrated-section/metadata readiness exposed by correction |
| Correction | TLS bootstrap, fileless-section mapping, source-backed `RehydrateData`, size/range proof correction |
| Corrected allocation returned | yes |
| Returned object | `0x100A00028` |
| Array length | 24 |
| Initial zero count | 24 |
| Pattern validation | PASS |
| GC heap ownership | PASS |
| Fresh-process repetitions | 2 additional |
| Existing proof regressions | Historical bounded 234/14 and controlled-OOM checks PASS; generic-QEMU and harness closure incomplete |

Generated evidence is under
`out/dotnet/gc-first-allocation-hang/**`; its 197 generated files are tracked
in the repository rather than ignored. The preceding implementation pass is
already present in commit `95580DF6872E85527D27526B78AE3CDCEE25DD53`; no new
commit was created by this verification pass.

## 23. Final closure validation — 2026-08-01

Section 21 is the historical verification decision and is superseded by this
completed closure record; the earlier evidence and phase boundaries are not
rewritten. The core implementation remains authoritative in commit
`95580df6872e85527d27526b78ae3cdcee25dd53`. The original hang was a fail-fast
loop. GS/TLS was the first blocker, followed by PE-to-ELF zero-fill mapping and
NativeAOT metadata hydration.

All named closure suites completed: dedicated native-thread, true-VM,
local-storage/FLS, PAL system-QEMU, stack bounds, FLS-before-initialization,
process-teardown policy, managed baseline, hosted generic, and focused
PE-to-ELF. The allocation smoke runner enforces experiment selectors and
matching compile definitions, rejects startup-only and ordinary desktop
markers, rejects stale logs, requires the single-allocation counters and
geometry, and restores the ordinary kernel. The generated evidence directory
was corrected from 197 tracked files to 0 tracked files by a narrow ignore rule;
the local evidence remains on disk.

The final fresh process performed exactly one collector-backed `byte[24]`:

| Field | Result |
| --- | --- |
| Final allocation PE SHA-256 | `9B9975F3B220BE6694435EE87616DA1F199CDBC727DA45983BFBDB2531CB6406` |
| Final allocation ELF SHA-256 | `BD39D05561CF16350E7544AD31DC26F097AF36F4552D06BCDBC6CF77480696E8` |
| Experimental kernel SHA-256 | `EACBE5677082C2D028FFC572E894AC87CB911CD53B29C449B6098C408CBD58FD` |
| Returned object | `0x100A00028` |
| Length | `24` |
| Source-derived size | `ALIGN_UP(baseSize + componentSize * length, pointerAlignment) = 0x30 / 48` |
| Initial zero bytes | `24` |
| Pattern / GC ownership | `PASS / PASS` |
| Managed / `RhpNewArray` / real GC entries | `1 / 1 / 1` |
| Collections entered / managed finalizers | `0 / 0` |
| Process teardown | `PASS` |
| Runtime-level shutdown | `NOT SUPPORTED` |
| Restored ordinary kernel | `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C` / `PASS` |

The final decision is **Closure Outcome A — First collector-backed allocation
milestone fully closed**. Bounded primitive-array allocations through Workstation
GC are authorized until the first subsequent allocation-context refill, without
allowing collection. No repeated Workstation-GC allocation was performed in
this closure pass.

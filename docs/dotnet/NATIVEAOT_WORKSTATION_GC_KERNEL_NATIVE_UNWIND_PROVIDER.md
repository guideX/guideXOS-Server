# NativeAOT Workstation GC — C011EC23 Kernel Native Unwind Provider

## Outcome

C011EC23 is Outcome C. guideXOS now preserves the compiler-generated Win64
AMD64 unwind tables, registers the kernel image as a bounded native-unwind
module, finds the genuine `RUNTIME_FUNCTION` for
`runFirstRealAllocationImpl`, and consumes it through the authentic suspended
NativeAOT stack-walk path. The helper unwinds successfully to a distinct
native caller RIP/RSP. That caller is outside the registered kernel executable
range and has no registered unwind metadata, so the provider stops safely
without guessing or pretending the native frame is managed.

This is not Outcome B: no later managed frame was reached. It is also not a
managed `ICodeManager` registration. The provider is an unwind service only.
C011EC22 remains evidence-only Outcome F+D and is not rewritten as if it had
already contained this provider.

## Starting boundary and locked identity

The exact C011EC22 starting state was verified before changes:

| Field | Value |
| --- | --- |
| branch | `v1.1_DOTNET_SUPPORT` |
| starting HEAD | `d99830f4270da17ec8e5a0b4447c1b1946cc38fd` |
| upstream | `origin/v1.1_DOTNET_SUPPORT` |
| starting divergence | ahead 4, behind 0 |
| tracked worktree | clean |
| untracked entries | 0 |
| NativeAOT | `9.0.0` |
| architecture | AMD64 |
| GC | Workstation |
| interfaces | `5.3 / 2` |
| locked source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| managed code range | `[0x10001000,0x10050950)` |

The runtime revision, GC flavor, architecture, interface version, and
runtime-pack identity were unchanged.

## C011EC22 root cause

The helper object was already correct. MinGW-W64 `g++ 15.2.0` and GNU
binutils `2.46.0.20260210` emitted `.pdata` (`0x2C4`) and `.xdata` (`0x250`)
for `kernel/core/nativeaot_pal_qemu_test.cpp`, including the object helper
entry `[0x4C10,0x6783)`, unwind RVA `0x200`, version 1, flags 0, prologue
size `0x13`, and record size `0x18` in the C011EC22 object audit.

`kernel/arch/amd64/linker.ld` previously matched `.pdata*` and `.xdata*` in
`/DISCARD/`. Consequently the C011EC22 final helper PC `0x1AE425` had no
covering final `RUNTIME_FUNCTION`. The fix audits the matching sections and
retains them in explicit aligned read-only output sections. `.eh_frame*` and
debug/note inputs remain discarded. `KEEP(*(.pdata*))` and `KEEP(*(.xdata*))`
are used because the tables are referenced through linker-defined metadata and
must survive section garbage collection; no proof record was hand-authored.

The retained input sections are not sorted by the linker. The provider
therefore validates the final ordering and uses a bounded deterministic linear
search. This avoids dynamic sorting or allocation while the EE is suspended.
The final validation found a sorted table, no zero-sized entries, no duplicate
or overlapping function ranges, and valid unwind references.

## Linker representation and final binary proof

The linker exports `__guidexos_native_image_base`, executable start/end, and
`.pdata`/`.xdata` start/end symbols using the existing fixed-base kernel
convention. The final proof kernel had:

| Region | Start | End | Size |
| --- | ---: | ---: | ---: |
| executable mappings | `0x100000` | `0x213A70` | — |
| `.pdata` | `0x214000` | `0x21D1C8` | `0x91C8` (37320 bytes) |
| `.xdata` | `0x21D1C8` | `0x224BDC` | `0x7A14` |

The `.pdata` size is exactly 3110 12-byte entries (`0xC26`). The ordinary
source-state build also retained non-overlapping sections: `.pdata`
`0x20D000..0x215EEC` and `.xdata` `0x215EEC..0x21D684`.

The helper in the final proof kernel was:

| Field | Value |
| --- | --- |
| symbol | `kernel::nativeaot_pal_qemu_test::(anonymous namespace)::runFirstRealAllocationImpl` |
| actual proof PC | `0x1AE6D5` |
| linked interval | `[0x1ADDF0,0x1AFBE3)` |
| BeginAddress | `0x000ADDF0` |
| EndAddress | `0x000AFBE3` |
| covering entry | `0x218254` |
| UnwindData | `0x00120E00` |
| resolved `UNWIND_INFO` | `0x220E00` |
| version / flags | `1 / 0` |
| prologue / code count | `0x13 / 0x0A` |
| frame register / offset | `0 / 0` |

The object relocation audit showed `IMAGE_REL_AMD64_ADDR32NB` for the three
fields. The final values are therefore kernel-image-base-relative RVAs, not
absolute addresses or section-relative offsets:

`final address = module base + field RVA`.

For the helper, `0x100000 + 0xADDF0 = 0x1ADDF0`,
`0x100000 + 0xAFBE3 = 0x1AFBE3`, and
`0x100000 + 0x120E00 = 0x220E00`. The covering table pointer and resolved
unwind pointer are actual mapped kernel addresses. This relationship was
proven from object metadata, relocated final bytes, symbolized function
address, and the runtime preflight; it is not assumed from PE convention.

## Provider design

`kernel/core/native_unwind_provider.cpp` implements a fixed registry with
capacity 2 and no dynamic loader. Its descriptor contains the module base,
executable range, `.pdata` and `.xdata` ranges, table pointer, bounded entry
count, sorted flag, encoding, and validity state. Registration happens once
during ordinary kernel/native-test initialization, before any suspended walk.
The runtime-facing ABI is pointer-free in its contract representation and
passes addresses as fixed-width integers.

Startup validation checks section ranges, overflow, table-size divisibility,
maximum entry count (`8192`), executable ownership, BeginAddress < EndAddress,
unwind RVA resolution, UNWIND_INFO version, chain flags, and unwind-code
storage. It checks first, middle, final, interior, end-exclusive, gap, below,
and above-range lookups, rejects a malformed descriptor, and proves a second
genuine function with nontrivial metadata. The registry count is one.

Lookup is read-only and bounded: it checks each registered module, rejects
PCs outside the exact executable range, scans at most the validated table
count, rejects malformed entries, and accepts only
`BeginAddress <= PC < EndAddress`. It records lookup attempts, module/table
ownership, runtime-function pointer, and unwind-info pointer. It never treats
the managed interval as a native module and never allocates.

The existing AMD64 PAL unwind implementation was split into
`guidexos_nativeaot_amd64_unwind_primitive.cpp/.h`. The managed adapter still
uses the same primitive and behavior. The native provider supplies the kernel
module base, genuine entry, resolved metadata, and context to that primitive;
there is no second heuristic unwinder. Suspended-path allocation count is 0:
no heap, managed allocation, collection, sorting, string construction, or
registration occurs after suspension.

## Standalone validation

Before entering the managed proof, the kernel invokes the real primitive in a
controlled native path for the helper metadata and for a second genuine kernel
function. The second function is table index 5, with entry `0x21403C`, unwind
info `0x21D1E0`, two unwind codes including `PUSH_NONVOL RBX` and
`ALLOC_SMALL`; it succeeds with output RIP `0xC0235EC000000403` and output RSP
`0x1012BE60`. This demonstrates that the provider is not special-cased to the
helper's symbol or unwind shape.

## NativeAOT iterator bridge

No change was made to `RuntimeInstance::GetCodeManagerForAddress()`. No native
`ICodeManager` was created and the managed range was not expanded.

The narrow guideXOS/PAL hook is injected into a temporary copy of locked
`src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp` by
`scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`. The original
native-state block around the locked `CalculateCurrentMethodState` contract is
replaced only for C011EC23. The candidate condition preserves the existing
reverse-P/Invoke case and adds the observed stale-manager/non-managed-PC case:

```cpp
const bool guideXosNativeFrameCandidate =
    reversePInvokeCandidate ||
    (m_pCodeManager != NULL && !m_pInstance->IsManaged(m_ControlPC));
```

The hook calls the bounded provider, unwinds one or more consecutive native
frames only while each exact PC has genuine metadata, updates the existing
register display, and reclassifies the recovered PC. It clears the managed
code-manager state for native frames. Native frames yield no managed GCInfo
and no GC roots. The bridge safely stops on the unsupported next native PC.

The script lines controlling this locked-source injection are 538-595 in the
milestone source state: declaration at 540-542, candidate at 560-563, PAL
callback at 565-569, register-display update at 571-575, and safe native stop
at 578-586. No locked runtime source file is tracked as changed; the temporary
source copy is build-local. The guideXOS-specific behavior is compile-gated by
`GUIDEXOS_NATIVEAOT_C011EC23_NATIVE_UNWIND` and remains outside other targets.

## Authentic suspended-path result

The first native frame was reached after the established chronology:

`ManagedMain` → real managed unwind → real GCInfo → four category-3 roots →
native `runFirstRealAllocationImpl` → provider lookup → genuine AMD64 unwind.

`C011EC23-PREFLIGHT` was emitted only after registration, focused table
validation, and helper coverage succeeded. In the actual suspended path:

| Field | Value |
| --- | --- |
| lookup attempts / successes | `2 / 1` |
| native unwind attempts / PAL calls | `1 / 1` |
| `RtlVirtualUnwind` returned | `1` |
| unwind result | `3` (completed native unwind) |
| input RIP / RSP / RBP | `0x1AE6D5 / 0x4E91B80 / 0x4E91B70` |
| output RIP / RSP / RBP | `0x3567A7AA / 0x4E91F40 / 0x4E91B70` |
| establisher frame | `0x4E91F40` |
| registers restored | 8 |
| restored nonvolatiles | RBX `0x100A02FF0`, RSI `0x100A04020`, RDI `0x100A05038`, R12 `1`, R13 `1`, R14 `0x100`, R15 `0x25`, RBP `0x4E91B70` |

The recovered caller is unresolved/unsupported native PC `0x3567A7AA` with no
registered native module covering it. It is not managed, no
`CoffNativeCodeManager` lookup was performed, and `FindMethodInfo` was not
called. Native frames crossed: 1. Later managed frame reached: no. The
provider stopped with safe-stop reason `0xC0230003`, Outcome C, and marker
`C011EC23`.

## GC semantics and invariants

The prior managed proof remains intact. There was one retained managed frame
before any possible managed re-entry, six total roots, four category-3 roots,
three register roots, and one stack root. Stack-derived Promote remains
`4 / 4 / 4`. The queue cursor remains `4 → 5`; overall accounting is six
promotion entries and five returns after the later queue record. Mark writes,
child reads, and graph traversal are all zero.

The stack base is `0`, `ScanContext.stack_limit` is `0`, and bounds consumed is
`0`. No stack-bound redesign was triggered. ThreadStore/EE invariants held:
the lock was acquired, the registry was complete before suspension,
`SuspendAllThreads` returned, EE suspended was 1, managed entry was prohibited,
and lock recursion was 1. The safe stop is bounded and fail-safe; it does not
scan arbitrary stack memory or re-enter managed code.

## Three-run validation

QEMU `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`) completed three fresh boots from
the final proof artifact. All three runs agreed on Outcome C, preflight,
provider lookup, one successful genuine unwind, one native frame crossed, no
managed re-entry, the GC chronology, zero bounds consumption, and safe stop.

| Run | Serial SHA-256 |
| --- | --- |
| first-run | `3FB5BD9A2374B8965ABA968D48FFE4E6FB57F0A5CFD3BF38B51A33C1D13C92D4` |
| repeat-1 | `5EF51D0E1047A805BC64319C54935F6B456E0789686B778AB818363CDDA17EDB` |
| repeat-2 | `9B0F48C669E7666FFA603049853B19E379A4E9B5452C0994693F5307FA7F11D6` |

Final proof artifact hashes were PE
`D9F50702D75DA2B11C319EB81E08C5E039C3E947D766F3526E3FF59A1438C73A`, ELF
`80BE9F3077F00F423BC060BA2FE4366E40942277DA2E2F6348205D332CC8B906`, proof
kernel `A7F95FA8B6D728FB2ADEFCDB33C061A8D376833C9B02EBDCE61D1480E01469E8`,
and MAP `FBCF8573763C86B5B988B52EDB4E67BA974A18ADD8EF01E4CE64216E44FD1474`.

The PE-to-ELF converter and fixed-base map validation passed. Normal kernel
build and linker/map sanity passed. The newly built ordinary source-state
kernel retained non-overlapping `.pdata/.xdata` and was archived as
`out/dotnet/c011ec23-final-validation/ordinary-production-kernel.elf` with
SHA-256 `A5B634F9D034FE2FFFB11048693321ECA387902E95C5E8CAE4624D63F52CD68D`.
The historical pre-milestone known-good build and ESP payload were
`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550` before
the ordinary rebuild; the final ESP was then updated to the ordinary
source-state artifact and verified to hash
`A5B634F9D034FE2FFFB11048693321ECA387902E95C5E8CAE4624D63F52CD68D`.

## Files and next milestone

Production source changes are in `kernel/arch/amd64/linker.ld`, the new
`kernel/core/native_unwind_provider.*`, the native provider contract, kernel
startup registration, the shared AMD64 unwind primitive, diagnostics, the
NativeAOT PAL bridge, and the proof/build script. The outcome document is this
file. C011EC22's document is retained unchanged as the loss-at-final-link
evidence boundary.

The next smallest milestone is to give the recovered native caller a second
authoritative module descriptor (or prove the exact transition/thunk contract)
so the bounded provider can decide whether a second native unwind is
legitimate. It must remain a native-provider decision and must not expand the
managed code manager range.

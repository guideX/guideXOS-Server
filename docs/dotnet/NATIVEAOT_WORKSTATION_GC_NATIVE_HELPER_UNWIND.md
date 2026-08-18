# NativeAOT Workstation GC - C011EC22 Native Helper Unwind Metadata

## Outcome

C011EC22 is an evidence-only boundary. The primary classification is Outcome
F: the compiler emits genuine Win64 native unwind metadata in the helper
object, but the kernel link loses the helper's covering entry. The locked
NativeAOT/PAL source also exposes a second, independent Outcome D contract gap:
it has no native `ICodeManager`, native module table, or PAL native-frame
provider. No production native unwind was attempted and neither
`C011EC22-PREFLIGHT` nor `C011EC22` was emitted.

The exact central answer is therefore: guideXOS can already produce the
correct AMD64 metadata format, but the current kernel linker discards the
helper entry and the locked NativeAOT stack iterator has no legitimate path to
look it up or consume it. Retaining sections alone is insufficient.

No proof-only metadata, guessed `RUNTIME_FUNCTION`, frame-pointer fallback,
stack scan, managed-range expansion, fake code manager, or proof-harness skip
was added.

## Boundary and locked identity

The investigation started at:

| Field | Value |
| --- | --- |
| branch | `v1.1_DOTNET_SUPPORT` |
| starting HEAD | `803346607d0b5004538fab8586b50699ff19ff86` |
| upstream | `origin/v1.1_DOTNET_SUPPORT` |
| starting divergence | ahead 3, behind 0 |
| tracked worktree | clean |
| untracked entries | 0 |
| NativeAOT | `9.0.0` |
| architecture | AMD64 |
| GC | Workstation |
| runtime interfaces | `5.3 / 2` |
| locked source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| managed interval | `[0x10001000, 0x10050950)`; unchanged |

C011EC21 remains authoritative: `ManagedMain`, ControlPC `0x10001D3F`, method
interval `[0x10001C20, 0x10001E84)`, real managed unwind and GC-info decode,
then native `runFirstRealAllocationImpl` at native RIP `0x1AE425`, native RSP
`0x4E85C20`, and native RBP `0x26635A`. The historical C20 RIP `0x1AE365`
was not reused as a success address.

## Locked native unwind contract

The locked source was audited from the extracted source at
`out/dotnet/gc-feasibility-baseline/source-extract/src/coreclr/nativeaot` and
the C011EC21 active locked-source copy.

* `Runtime/StackFrameIterator.cpp:1597-1725` calls
  `GetCodeManager()->UnwindStackFrame` for the current managed frame, then
  classifies the returned address. The source comment at `:1599-1600` states
  that there is no `ICodeManager` for native code. The yield assertion at
  `:1725` only permits a native frame in the transition topology the iterator
  already understands.
* `Runtime/StackFrameIterator.cpp:1913-1948` makes the native branch explicit:
  when the native state is reached it sets `m_pCodeManager = nullptr`, clears
  the managed frame state, and does not call `FindMethodInfo` for native code.
* `Runtime/windows/CoffNativeCodeManager.cpp:651-842` owns the locked Win64
  unwind operation for a managed `MethodInfo`. On AMD64 it creates a
  `CONTEXT`, supplies a managed `RUNTIME_FUNCTION`, and calls
  `RtlVirtualUnwind` with the managed image base. This is not a native-kernel
  module registry or lookup provider.
* `Runtime/RuntimeInstance.cpp:96-109` implements `IsManaged` and
  `GetCodeManagerForAddress`. An address outside the managed range returns
  `nullptr`; there is no second native image table in this path.

The locked contract is consequently the Windows x64
`RUNTIME_FUNCTION`/`UNWIND_INFO` representation consumed by
`CoffNativeCodeManager` for managed PE/COFF code. The source contains no PAL
equivalent native registry, ELF `.eh_frame` lookup, `dl_iterate_phdr` path,
kernel module registration table, or general native `RtlVirtualUnwind` lookup.
`RtlVirtualUnwind` is the execution primitive after a managed code manager has
already supplied the entry; it is not itself a PC-to-native-table resolver.

The NativeAOT managed image uses its own PE-style image base and code manager.
The guideXOS kernel is a separate fixed-base image, linked at `0x00100000` and
converted to `kernel.elf`. The locked runtime does not automatically treat that
kernel image as another managed or native unwind module.

## Helper compilation and object audit

The helper is:

`kernel::nativeaot_pal_qemu_test::(anonymous namespace)::runFirstRealAllocationImpl`

Source is `kernel/core/nativeaot_pal_qemu_test.cpp:1923-2358`; the public ABI
adapter is at `:2428-2444`. The C011EC21 build log records the object command as
`g++` compiling `core/nativeaot_pal_qemu_test.cpp` into
`build/amd64/obj/core/nativeaot_pal_qemu_test.o`.

Toolchain:

* `g++` MinGW-W64 x86_64-msvcrt-posix-seh `15.2.0`;
* GNU `ld`, `objcopy`, and `objdump` `2.46.0.20260210`;
* PE/COFF AMD64 object and final PE link, followed by the existing fixed-base
  PE-to-ELF conversion.

Relevant compile flags are `-std=c++14 -ffreestanding -O2 -fno-exceptions
-fno-rtti -nostdlib -nostdinc++ -fno-builtin -m64 -mcmodel=large
-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -fno-pic -fno-pie -fno-PIC
-fno-PIE -mno-stack-arg-probe -fno-stack-check -fno-stack-protector`.
There is no `-fno-unwind-tables` or `-fno-asynchronous-unwind-tables` in the
AMD64 kernel flags. Optimization was not changed for this audit.

The direct object audit is retained at:

`out/dotnet/gc-stack-provider-native-helper-unwind/object-audit/nativeaot_pal_qemu_test.o`

SHA-256: `6C2D1AECF6745AB860605469299FDBF07D615E58A576409E0AE843563B2C1488`.

The object contains genuine compiler-generated metadata:

| Object item | Result |
| --- | --- |
| `.xdata` | present, `0x250` bytes |
| `.pdata` | present, `0x2C4` bytes |
| helper object symbol | offset `0x4C10` |
| helper object unwind entry | `[0x4C10, 0x6783)`, unwind RVA `0x200` |
| helper unwind record | 16-byte-aligned Win64 record area, version 1, flags 0, prologue size `0x13`, 10 unwind-code slots; record occupies `0x18` bytes |
| nearby public helper | `runFirstRealAllocation` at object offset `0x6CE0`; same object also contains `.pdata`/`.xdata` for nearby helpers |
| DWARF FDE | absent; `objdump -Wf` reports no DWARF frames |

This proves metadata did not fail to exist at compilation. The helper has a
large non-leaf prologue (eight nonvolatile saves plus a `0x2E8` stack
allocation), so it is not legitimately a leaf.

## Final kernel/link audit

The C011EC21 proof kernel was independently inspected before ordinary-payload
restoration. Its relevant sections were:

| Section | Size | VMA | File offset | Load status |
| --- | ---: | ---: | ---: | --- |
| `.xdata` | `0xF8` | `0x2A1000` | `0x1A2000` | allocated and in the first load segment |
| `.pdata` | `0x198` | `0x2A2000` | `0x1A3000` | allocated and in the first load segment |
| `.xdata.startup` | `0x50` | `0x2A3000` | `0x1A4000` | allocated and in the first load segment |
| `.pdata.startup` | `0x6C` | `0x2A4000` | `0x1A5000` | allocated and in the first load segment |
| `.xdata.unlikely` | `0x44` | `0x2A5000` | `0x1A6000` | allocated and in the first load segment |
| `.pdata.unlikely` | `0x18` | `0x2A6000` | `0x1A7000` | allocated and in the first load segment |
| `.eh_frame` / `.eh_frame_hdr` | absent | — | — | not a usable format here |

The final proof `kernel.elf` load map placed the code and unwind sections in
the first `R E` segment. Thus the sections that survived are addressable after
boot. This is an independent audit of `kernel.elf`; it is not the converted
NativeAOT payload audit.

The surviving kernel `.pdata` contains 34 entries covering ranges beginning
near `0x00111B60` and ending near `0x00112A26`. A scalar table walk at the
actual C011EC21 PC `0x1AE425` found zero covering entries. The helper's object
entry `[0x4C10,0x6783)` and object unwind RVA `0x200` are not present as a
covering final-kernel entry.

The exact loss point is the AMD64 linker script:

`kernel/arch/amd64/linker.ld:63-77` places `.boot`, `.text`, `.rodata`,
`.data`, and `.bss`, then its `/DISCARD/` block explicitly discards
`.eh_frame`, `.eh_frame_hdr`, `.pdata`, and `.xdata`. The object therefore has
valid Win64 metadata, but the helper's input `.pdata`/`.xdata` is discarded at
the final kernel link. The retained similarly named sections are not proof
that the helper entry survived; the PC-to-table audit is the authoritative
coverage test.

## Independent native lookup proof

| Field | Result |
| --- | --- |
| native module | `kernel.elf` |
| native module base | `0x00100000` fixed link base |
| actual relinked helper PC | `0x001AE425` |
| symbol interval | `[0x001AD050, 0x001AEBD0)` from the audited disassembly boundary at the next native symbol |
| function offset | `0x13D5` |
| locked PAL/native lookup attempted | no; the locked runtime has no native provider |
| independent final `.pdata` coverage lookup | attempted |
| lookup result | failure: zero covering final entries |
| runtime-function entry | none |
| final unwind metadata address | none |
| final metadata size/flags | not applicable; object record was not retained |
| C011EC22-PREFLIGHT | not emitted |

The C011EC21 evidence callback recorded module/section identity only for
provenance. It did not manufacture a runtime-function pointer or metadata
address. The older absolute address `0x1AE365` was not used for this audit.

## Authentic suspended walk result

The real C011EC21 suspended path still provides the following managed-to-native
input boundary:

| Field | Value |
| --- | ---: |
| managed input RIP | `0x10001D3F` |
| managed input RSP | `0x4E85B80` |
| managed input RBP | `0x4E85C10` |
| managed unwind output/native input RIP | `0x1AE425` |
| native input RSP | `0x4E85C20` |
| native input RBP | `0x26635A` |
| managed establisher frame | `0x4E85C20` |
| native unwind called | no |
| native unwind result | not attempted; no covering metadata and no PAL provider |
| native output RIP/RSP/RBP | not recovered |
| native establisher frame | not recovered |
| restored nonvolatile registers | not applicable for the native step; C20's managed unwind restored RBX `0x1DB850`, RSI `0x1DB860`, RDI `0xA`, R12 `0x26635A`, R13 `0x3949CE0`, R14 `0x2662ED`, R15 `0x3736353433323130` |
| caller symbol/module | not recovered; current native frame is `runFirstRealAllocationImpl` in `kernel.elf` |
| caller managed | no caller was recovered |
| later managed frame reached | no |
| native frames traversed | 0 |
| C011EC22 | not emitted |

No native metadata lookup was inserted into the suspended path. This avoids
allocations, dynamic strings, containers, stack scanning, or an unsafe
registration during GC suspension.

## Preserved C011EC19-C011EC21 semantics

The existing managed proof was not changed:

* existing managed frames: one genuine `ManagedMain` frame;
* total roots: 6;
* category-3 roots: 4;
* category-3 roots by location: 3 register, 1 stack;
* stack-derived Promote chronology: 4 attempts, 4 entries, 4 returns;
* queue cursor: `4 -> 5`;
* mark writes: 0;
* child reads: 0;
* graph traversal: 0;
* `ScanContext.stack_limit`: 0;
* stack base: 0;
* diagnostic stack limit: approximately `0x3949BE0` in the C011EC21 run;
* `stackBoundsConsumed`: 0;
* post-suspension sensitive allocations: 0;
* safe-stop/fail-fast: C011EC21 baseline safe-stop `0xC0210005`; no C011EC22 marker.

## Three-run and hash evidence

The existing C011EC21 final run performed three fresh QEMU `11.0.0` boots
with the unchanged locked proof path. All three agreed on Outcome E,
`C011EC21`, one managed unwind, native RIP `0x1AE425`, no native metadata,
and the preserved root/queue chronology. Serial SHA-256 values were:

```text
AA6AED37E7939C3596B1C2414453BBB3B6295B7455E560EC4DAC8949C6EEF96D
9D717792F8AC5BC5976AA033A1E68420A28BB1B711623D1631B7A85702950D49
2808E34C043034267D30020282946C1CC72B4E7E972E4ECF8C6AA7DB8862AE94
```

Evidence root:
`out/dotnet/gc-stack-provider-native-transition-continuation/run-20260818-071738251`.

| Artifact | SHA-256 |
| --- | --- |
| managed PE payload | `6C634EC55E47CFB16418E96C4E8FF7D01A003E1EABDDF5B285337F84439990B8` |
| converted managed ELF payload | `497D1C33C0670941BF1CB98C36B3E62EADC14149732B47EC486682B279FF67F6` |
| proof kernel | `55888209238E9597D9BF8EF7E348E700D2D8FF77C69DC4CA071D9E397D8E72BA` |
| managed payload MAP | `28ED05FE927937E60B036C9A68F2CC8710F54A567A5AAD755F14BE46D617F4F8` |
| ordinary kernel expected/restored | `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550` |
| ordinary ESP expected/restored | `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550` |

The ordinary ESP hash was checked against the same canonical expected value:
`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.

## Validation and restoration

The inherited C011EC21 validation set passed: PE-to-ELF converter audit,
PowerShell parser validation, locked-source and chronology guards, ordinary
payload restoration, serial checkpoints, native symbol/disassembly audit,
and `git diff --check`. The object-level C011EC22 audit adds direct proof of
the compiler-to-link loss point. No tracked source, compiler flag, linker
script, PAL provider, or runtime source was changed by C011EC22.

The ordinary kernel and ESP were restored and checked against the expected
SHA-256. No proof payload remains in the ordinary locations. QEMU cleanup was
performed; no QEMU process remained.

## Required production work for the next milestone

The smallest legitimate follow-up is a two-part production change:

1. retain and map compiler-generated Win64 `.pdata`/`.xdata` for all relevant
   AMD64 kernel code, including `runFirstRealAllocationImpl`; and
2. add a runtime-wide, initialization-time native module/table provider that
   the NativeAOT stack iterator explicitly consumes for native frames, with
   the real kernel base, table range, `RUNTIME_FUNCTION` interval, and
   `UNWIND_INFO` pointer. That provider must be integrated into the locked
   iterator contract rather than called from a suspended-GC diagnostic hook.

Only after both pieces exist should the authentic walk attempt the native
unwind and classify the recovered caller. The next milestone must not use
`.eh_frame` merely because the image is ELF, and must not convert the current
object or stack contents heuristically.

## Git state

C011EC17-C011EC21 history was not amended, reset, rebased, squashed, or
rewritten. The final C011EC22 evidence commit and final divergence are recorded
in the task handoff. Push behavior remains subject to the existing GitHub SSH
`Permission denied (publickey)` limitation; no global Git, SSH, Pageant, or
OpenSSH configuration was changed.

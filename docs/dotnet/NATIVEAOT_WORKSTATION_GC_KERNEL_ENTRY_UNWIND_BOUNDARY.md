# NativeAOT Workstation GC kernel-entry unwind boundary

## C011EC25 result

C011EC25 is Outcome B: legitimate native stack bottom proven.

The recovered C24 caller PC `0x355D101E` is not another ordinary compiler
frame.  In the C24 boot it is the physical identity-mapped alias of linked
`_start.halt` at `0x10001E`.  The C25 fresh boots loaded the same kernel at a
different physical base, so their measured boundary PC was `0x355CF01E`; it
translated to the same linked `0x10001E`.  The address is the halt loop after
the `_start` call to `kernel_main`.  The loader entered `_start` with a
non-returning `jmp`, so no caller return slot exists beyond this boundary.

No third unwind was attempted.  The meaningful native chain is therefore:

```text
NativeAOT managed frame
  -> runFirstRealAllocationImpl              (C011EC24 first native frame)
  -> kernel_main                              (C011EC24 second native frame)
  -> _start.halt                              (C011EC25 entry boundary)
  -> no caller / legitimate native stack bottom
```

## Locked identity and starting point

The locked runtime identity was preserved exactly:

| Item | Value |
|---|---|
| NativeAOT | 9.0.0 |
| Architecture | AMD64 |
| GC | Workstation |
| GC interfaces | 5.3 / 2 |
| CoreCLR source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Code manager | `CoffNativeCodeManager` |
| Managed range | `[0x10001000,0x10050950)` |

The task began on branch `v1.1_DOTNET_SUPPORT`, at
`f582bcbb54e57cf8dc94032e837a8e19c22f2fed`, tracking
`origin/v1.1_DOTNET_SUPPORT`, ahead 0 and behind 0, with no untracked files.
Actual Git state was treated as authoritative.

C011EC24 had already proved two genuine native unwinds and intentionally
stopped at `0xC0240004`.  Its locked historical evidence was:

```text
helper suspended PC       0x1AE445
first output RIP          0x356767AA
first output RSP          0x4E95F40
second output RIP         0x355D101E
second output RSP         0x4E96000
native frames crossed     2
native registry           capacity 2, count 2
```

The two registered views remain the linked kernel descriptor and the physical
identity-mapped alias descriptor for that same kernel.  C011EC25 does not add
a descriptor, enlarge the registry, copy unwind tables, or widen a loader
mapping.

## Physical/link translation

Translation is always calculated from the boot’s actual physical kernel base
and the linked image base `0x100000`:

```text
linkedPC = linkedBase + (physicalPC - physicalKernelBase)
```

For the C24 boot:

```text
physical kernel base    0x355D1000
physical boundary PC    0x355D101E
linked image base       0x00100000
linked boundary PC      0x0010001E
```

For the final C25 boots:

```text
physical kernel base    0x355CF000
physical boundary PC    0x355CF01E
linked image base       0x00100000
linked boundary PC      0x0010001E
```

The C25 first native input was the current boot’s physical alias of
`kernel_main+0x12A`:

```text
linked kernel_main      0x001A5680
linked PC               0x001A57AA
current alias base      0x355CF000
current physical PC     0x356747AA
```

The linked `kernel_main` interval is `[0x1A5680,0x1A581F)`.  Under the final
boot’s alias it is `[0x35674680,0x3567481F)`, which covers `0x356747AA`.

The recovered boundary belongs to the `.boot` code interval
`[0x100000,0x100022)`, specifically the local assembly label `_start.halt`.
Its source is `kernel/arch/amd64/boot.asm:9-37`; the linked ELF ownership and
entry placement come from `kernel/arch/amd64/linker.ld:8-27` and
`kernel/arch/amd64/linker.ld:41-61`.

## Static ownership and disassembly

The final proof ELF resolved these symbols:

```text
__guidexos_native_executable_start  0x00100000
_start                              0x00100000
_start.halt                         0x0010001E
kernel_main                         0x001A5680
__guidexos_native_executable_end    0x00213EB0
boot_stack_bottom                   0x04E94000
boot_stack_top                      0x04E98000
```

The exact `.boot` disassembly was:

```text
0000000000100000 <__guidexos_native_executable_start>:
  100000: fa                    cli
  100001: 66 31 c0              xor    %ax,%ax
  100004: 8e d8                 mov    %eax,%ds
  100006: 8e c0                 mov    %eax,%es
  100008: 8e e0                 mov    %eax,%fs
  10000a: 8e e8                 mov    %eax,%gs
  10000c: 8e d0                 mov    %eax,%ss
  10000e: 48 bc 00 80 e9 04 00  movabs $0x4e98000,%rsp
  100015: 00 00 00
  100018: fc                    cld
  100019: e8 62 56 0a 00        call   1a5680 <kernel_main>

000000000010001e <_start.halt>:
  10001e: f4                    hlt
  10001f: eb fd                 jmp    10001e <_start.halt>
```

The linker script makes `_start` the entry, places `.boot` first at `0x100000`,
and places `.pdata` and `.xdata` after the executable code.  It discards
`.eh_frame`.  The static audit therefore found no `.eh_frame` FDE and no
compiler unwind metadata covering `.boot`; the proof artifacts retain the
symbol table, `.boot` disassembly, section table, and unwind audit.

## Independent second unwind proof

Before accepting the recovered boundary, C011EC25 independently decoded the
covering `kernel_main` UNWIND_INFO and compared it with the production
unwind.  The five code words were:

```text
0x010A  UWOP_ALLOC_LARGE, OpInfo 0, followed by 0x0014
0x0014  allocation size in 8-byte units: 0x14 * 8 = 0xA0
0x3003  UWOP_PUSH_NONVOL, RBX
0x6002  UWOP_PUSH_NONVOL, RSI
0x7001  UWOP_PUSH_NONVOL, RDI
```

The unwind program consumes `0xA0 + 3*0x8 = 0xB8` bytes before the return
slot.  It restores RDI, RSI, and RBX in unwind order, leaves RBP unchanged,
and then consumes the return slot.

All three final C25 boots independently produced the following values:

```text
input RIP              0x356747AA
input RSP              0x4E97F40
input RBP              0x3FE65770
stack advance          0xB8
return slot            0x4E97FF8
value at return slot   0x355CF01E
expected caller RIP    0x355CF01E
expected caller RSP    0x4E98000
production output RIP  0x355CF01E
production output RSP  0x4E98000
production output RBP  0x3FE65770
establisher frame      0x4E98000
handler data           0x0
```

The independent derivation and production output agreed exactly.  The
restored nonvolatiles emitted by the final proof were stable across the three
boots: RBX `0x3DE3D7DD`, RSI `0x210000`, RDI `0x3C`, and RBP
`0x3FE65770`.

The final physical alias descriptor selected for the second unwind had:

```text
module base            0x355CF000
executable start       0x355CF000
executable end         0x356E2EB0
RUNTIME_FUNCTION       0x356E6DC8
UNWIND_INFO            0x356EFA48
```

This is genuine compiler-generated metadata for `kernel_main`, not synthetic
metadata.

## Entry/startup contract

The complete structural path is:

1. `guideXOSBootLoader/main.cpp` loads the ELF, computes `entryPhys`, and
   calls `BootHandoffTrampoline(entryPhys, bootInfo, stackTop, pml4)`
   (`main.cpp:379-387`, `main.cpp:1092-1098`).
2. The loader’s trampoline saves the entry in R12, saves the arguments in
   R13-R15, changes to its handoff stack, subtracts 40 bytes for shadow/fake
   return space, sets RCX to `BootInfo*`, and executes `jmp r12`
   (`guideXOSBootLoader/trampoline.asm:34-44`, `:67-94`).
3. The `jmp` creates no return address.  The loader’s actual handoff stack in
   the final boots was `0x200000..0x210000`; it is only the trampoline stack.
4. `_start` executes `mov rsp, boot_stack_top`, replacing that handoff stack
   with the kernel image’s `.bss` boot stack.  The final linked symbol value
   was `boot_stack_top=0x4E98000` (`kernel/arch/amd64/boot.asm:24-25`,
   `:39-45`).
5. `_start+0x19` executes a real `call kernel_main`.  That call creates the
   return slot whose value is `_start.halt` at `_start+0x1E`
   (`kernel/arch/amd64/boot.asm:30-37`; the C++ target is
   `kernel/core/main.cpp:243-255`).
6. If `kernel_main` returns, `_start.halt` executes `hlt` and loops forever.

Consequently, a normal Win64 caller frame exists for `kernel_main` relative to
the compiler-generated `call`, and C24 legitimately unwinds it.  There is no
normal caller frame for `_start`: the loader used `jmp`, `_start` replaced
RSP, and the entry code intentionally never returns to the loader.  The
second unwind’s output RSP is exactly the kernel’s `boot_stack_top`, proving
that the meaningful kernel stack state has been consumed.

## Third-boundary provider audit

C011EC25 performed production native-provider lookup for both representations
of the recovered boundary:

```text
linked lookup PC       0x10001E       result 0xFFFFFFFF (not found)
physical lookup PC     0x355CF01E     result 0xFFFFFFFF (not found)
third metadata present 0
third PC in kernel      1
assembly boundary      1
non-returning handoff   1
stack bottom proven     1
```

Both misses are correct: the PC belongs to the `.boot` assembly halt loop,
which has no `RUNTIME_FUNCTION`/`UNWIND_INFO` contract.  Adding a fabricated
descriptor would misrepresent a non-returning entry boundary.  No third
unwind was attempted.

The required marker was emitted only after the independent second proof,
alias translation, exact symbol/disassembly ownership, and entry contract
classification:

```text
C011EC25-PREFLIGHT
```

The final stop was `C011EC25` with reason `0xC0250000` and classification
Outcome B.

## Managed GC and stack-bound evidence

The established managed semantics were unchanged:

```text
managed frames          1
total roots             6
category-3 roots        4
register roots          3
stack roots             1
stack-derived Promote   4 / 4 / 4
queue                   4 -> 5
mark writes             0
child reads             0
graph traversal         0
native managed roots    0
managed re-entry        0
```

Native unwinding generated no managed GC roots.  EE suspension remained
allocation-free and did not perform registration, table construction,
sorting, arbitrary stack scanning, scheduler transition, or managed
re-entry.

Stack bounds remained separate and untouched:

```text
stack base              0
ScanContext.stack_limit 0
bounds consumed         0
```

No proactive stack-bound repair was made.

## QEMU evidence and hashes

The final evidence root was:

`out/dotnet/c011ec25-kernel-entry-boundary/run-20260819-155354681`

QEMU was version 11.0.0.  Three fresh boots completed and every run produced
Outcome B.  The per-boot structural values were captured from each serial
log rather than assumed:

| Run | helper PC | kernel_main PC | third physical PC | linked PC | serial SHA-256 |
|---|---:|---:|---:|---:|---|
| first-run | `0x1AE445` | `0x356747AA` | `0x355CF01E` | `0x10001E` | `1746716F1A7B1FC0D587FF3C46D1FC3BC98842E14A6D751399A2A28156A0B36E` |
| repeat-1 | `0x1AE445` | `0x356747AA` | `0x355CF01E` | `0x10001E` | `76B5019D174A70711DE36568C0AB704062FA5A2F2C5F82881CAB236B0ABA2840` |
| repeat-2 | `0x1AE445` | `0x356747AA` | `0x355CF01E` | `0x10001E` | `46DC3F734EBFDEEEBE1B15AC404813EAEB393A635FF1BC3C8E0B9B4136273802` |

The final proof kernel and generated payload hashes were:

```text
proof kernel.elf       121191D144E6F15B832B47B4989AF00AC1DFC2168F14671148E08F8F63BFE408
managed PE             26E1CF9D55E6B3C15CC3E29380BC1CD72F33602E2973E9F509EFD32D24E2466C
managed ELF            D6F80C9B96E4B1E5E1C09481FC1F7F2416552FFF7F84C4985DEEA3D3F9B93513
managed map            867BADAA9665C0E24B225C20834FA0122645B3CCBB72B90A478B12B9CB743BAF
```

The ordinary source-state kernel and ESP were restored exactly:

```text
kernel/build/amd64/bin/kernel.elf  A5B634F9D034FE2FFFB11048693321ECA387902E95C5E8CAE4624D63F52CD68B
ESP/kernel.elf                      A5B634F9D034FE2FFFB11048693321ECA387902E95C5E8CAE4624D63F52CD68B
```

All QEMU processes were terminated after the evidence runs.

## Validation

The C25 run performed and passed the existing focused validation paths:

- native provider registration and bounded lookup;
- two-function standalone unwind checks;
- linked/physical alias lookup checks;
- linker and `.pdata`/`.xdata` table geometry checks;
- PE-to-ELF fixed-base converter regression;
- locked-source and chronology guards through C011EC24;
- PowerShell parse, manifest parse, and `git diff --check`;
- ordinary kernel build and boot smoke, with ordinary artifacts restored.

The final static entry artifacts are `kernel-entry-symbols.txt`,
`kernel-entry-disassembly.txt`, `kernel-entry-sections.txt`, and
`kernel-entry-unwind-audit.txt` in the evidence root above.  The complete
machine-readable record is `manifest.json` in that same directory.

## Production changes

The production diagnostic path adds append-only C011EC25 evidence fields and
validates the actual second unwind before emitting the preflight marker.  It
does not add third-frame metadata or cross the assembly boundary.  The
PowerShell harness adds the C25 proof selector, static `.boot` audit,
boot-specific address capture, and Outcome B manifest generation.

The final Git commit records only the intended production diagnostic changes,
harness changes, and this document.

# NativeAOT PAL Win64/QEMU bridge

Status: 2026-07-24. The PE-to-ELF artifact and Server Win64 entry-trampoline
preflight pass. The full bare-metal/system-QEMU exact PAL run is blocked by
the missing Win64 callback/worker/ThreadStore bridge. No `RhInitialize` call
was made.

## Why a bridge is required

The locked NativeAOT PAL replacements are MSVC x64 COFF objects using the
Win64 ABI. The guideXOS bare-metal kernel and QEMU path are MinGW ELF/SysV.
These are not interchangeable C++ object or CRT environments. A direct call
from converted PAL C++ into guideXOS C++ classes would violate object layout,
calling convention, ownership, TLS, and exception/runtime assumptions.

## Selected execution model

The preferred NativeAOT-style model was selected:

```text
MSVC Win64 PE probe
  -> fixed-base PE-to-ELF conversion
  -> static ET_EXEC at 0x10000000
  -> guideXOS Server native ELF loader
  -> existing Win64 entry trampoline
  -> exported GuideXosNativeAotPalProbeMain
```

Only a versioned C-compatible entry and a narrow PAL hook table cross the
boundary. No guideXOS C++ object crosses it.

## PE/COFF artifact

The builder compiles
[`guidexos_nativeaot_pal_qemu_probe.cpp`](../../tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_pal_qemu_probe.cpp)
with MSVC 19.51.36248 x64 and links the active common, MinWin, time, and
contract objects with `/NODEFAULTLIB`, `/FIXED`, `/BASE:0x10000000`, and no
imports. The exported entry is:

```cpp
extern "C" __declspec(dllexport) int32_t __stdcall
GuideXosNativeAotPalProbeMain(GuidexosNativeGxAppContext* context);
```

The PE SHA-256 is
`1DBA9F81873C826B2B14E6A601980D8C84B7DE99C3D4A08003F962663DDDE28D`.
The PE import inventory is empty and the export/header inventories are under
[`qemu-probe`](../../out/dotnet/pal-runtime-active-replacement/qemu-probe/).

## PE-to-ELF conversion

The existing fixed-base converter
[`pe_to_elf_v2_fixed_base.py`](../../tools/dotnet/pe_to_elf_v2_fixed_base.py)
is used without machine-code patching. Its SHA-256 is
`5F21B87D343106120EB5CAD1F98DF524404171E084C40F4FC3AFED6BE6F84B96`.
The resulting static ET_EXEC ELF starts at the PE entry address and has SHA-256
`BD5357D4806B254A4D89B058EE397008E5F5C0E284E9C8FC795A00BEF5A50EF1`.

## Win64 trampoline

The existing Server trampoline establishes the Win64 shadow space, moves the
Server context into the Win64 entry register, calls the converted entry, and
restores the stack. The Server stage uses only a process-local app manifest;
the default application inventory is unchanged.

The bridge preflight ran two launches in one Server process and one launch in a
fresh second Server process. All three returned `0`, mapped at preferred base
`0x10000000`, used the Windows amd64 trampoline, and emitted:

```text
NativeAOT PAL PE bridge probe completed
```

The machine result is
[`qemu-probe-result.json`](../../out/dotnet/pal-runtime-active-replacement/qemu-probe/qemu-probe-result.json).

## C/C++ ABI boundary

The active PAL contract uses ABI version 2, fixed-width integers, `uintptr_t`
opaque handles, raw pointers, explicit callback calling conventions, explicit
timeout/counter units, and non-returning fail-fast. It does not expose
`std::thread`, `std::mutex`, guideXOS classes, TCB layouts,
`VirtualMemoryRegion`, or NativeAOT internal C++ objects.

The self-contained PE probe supplies a bounded C hook table to prove exact
symbol binding and entry execution. It intentionally does not claim generic
guideXOS scheduler, worker, or ThreadStore execution.

## Exact-symbol evidence

The active archive is
`Runtime.WorkstationGC.guidexos-nativeaot-pal.lib`, hash
`C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`.
The four removed objects have exact parity: 6/6, 38/38, 74/74, and 3/3;
missing, unexpected, and duplicate strong definitions are all zero. The four
replacement objects contribute zero Windows imports. The hosted exact PAL
probe passes two launches.

## QEMU status and exact limitation

QEMU is present at `C:\Program Files\qemu\qemu-system-x86_64.exe`, version
11.0.0, SHA-256
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.

The actual system-QEMU exact PAL probe is **BLOCKED**, not PASS. The QEMU
kernel side has no versioned C-compatible Win64 PAL hook table and no bridge
for Win64 callback pointers, worker creation/join, stack bounds, or
ThreadStore attach/detach. The existing Server trampoline adapts only the
single exported entry call and does not solve those callback directions or
provide a native PAL context. Running the self-contained PE preflight under a
different host would not prove the missing guideXOS adapter path.

## Limitations

- The PE probe does not start a finalizer/helper thread, initialize the GC, or
  call `RhInitialize`.
- It does not construct the Workstation GC heap, allocate through the real GC,
  or trigger collection.
- The Server trampoline proof is not a system-QEMU proof.
- A future exact QEMU result requires the explicit hook-table and callback
  bridge above; broad Win32 emulation or fake Win32 exports are not acceptable.

# NativeAOT PAL/runtime replacement

Status: 2026-07-24. The four-object active replacement is built and
reproducible. Hosted exact PAL validation passes. The converted PE/ELF probe
passes through the Server Win64 entry trampoline; the deliberate bare-metal
system-QEMU PAL bridge remains blocked by the exact hook-table invariant in
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md).
`RhInitialize` was not called.

## Active strategy

All four objects are replaced together so Windows and guideXOS PAL state cannot
be mixed across FLS, thread, resolver, timing, and initialization paths:

| Removed stock object | Locked source | Stock SHA-256 | Replacement SHA-256 |
| --- | --- | --- | --- |
| `PalRedhawkCommon.cpp.obj` | `src/coreclr/nativeaot/Runtime/windows/PalRedhawkCommon.cpp` | `59027D155707F530B298119A212861D9932ADAF508891C750E24E2E21683C35C` | `9E548A374DB357D7C208CC82988A8858A340F08055452044D61F73A83B31E165` |
| `PalRedhawkMinWin.cpp.obj` | `src/coreclr/nativeaot/Runtime/windows/PalRedhawkMinWin.cpp` | `30FDAF43E2CBEA6F075578F9E27241E36A11FDF481AB470F5942D7ED9FFDED4D` | `18777106F156AF3DE022349C8616196928DBE6FE1C80229B1C74C850F98BE70C` |
| `thread.cpp.obj` | `src/coreclr/nativeaot/Runtime/thread.cpp` | `7EC7C0E20CEBF63C403EB045458895D36B2DB657DCC46D65EFD9859744D0388D` | `89CD068A74370727F38C907CE54D1695CFECA1A5ACE44B6C18B1654022494F5E` |
| `time.c.obj` | `src/native/minipal/time.c` | `486ED71D18918FD6E4A32CEB917B36663180F26E9C2A53A639F04CC656A71338` | `95287984D9D656B96EC6E907ADECA3DBCA29153400EC16C557D877C7E2D75B8B` |

Source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The locked stock
`Runtime.WorkstationGC.lib` hash is
`0E6A134AD4150CD604317A47860DAE82EB30AAE4D9CDB14144E06454E7BB1948`.

## ABI boundary

[`guidexos_nativeaot_pal_contract.h`](../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.h)
defines ABI version 2 with compile-time size/alignment/offset assertions.
The boundary uses `extern "C"`, MSVC Win64 calling convention on PAL exports,
fixed-width integers, `uintptr_t` opaque handles, raw pointers, explicit
`int32_t` success/failure results, millisecond timeouts, monotonic counter and
frequency units, callback ownership, FLS detach callback ABI, and non-returning
fail-fast behavior. No C++ class, `std::thread`, `std::mutex`, TCB, VM region,
or NativeAOT internal C++ object crosses the boundary.

The contract object hash is
`F2143156DDA3B986CC4C8C4F6A86EEE58BEEA011878ADF14CD38D834E49EBF06`.

The replacements route FLS allocation/get/set/release and detach callbacks,
current-thread identity, stack bounds, helper-thread hooks, ThreadStore-facing
attach/detach, monotonic timing, sleep/yield, static current-image resolver
behavior, VM, events, and fail-fast through the narrow guideXOS callbacks.
Dynamic library loading is rejected honestly. No real finalizer/helper thread,
collector heap, collection, or `RhInitialize` path is entered.

## Archive and exact symbol parity

[`build-guidexos-nativeaot-pal.ps1`](../../tools/dotnet/runtime-pack/build-guidexos-nativeaot-pal.ps1)
validates the lock and stock hash, extracts immutable stock members, compiles
all four replacements with MSVC 19.51.36248 x64 (`/MT /GR- /EHs-c- /GS-`,
NativeAOT defines and `/Brepro`), removes exact members, adds replacements and
the contract object, normalizes COFF archive member timestamps, and emits
machine-readable inventories under
[`out/dotnet/pal-runtime-active-replacement`](../../out/dotnet/pal-runtime-active-replacement/).

Adapted archive:
`Runtime.WorkstationGC.guidexos-nativeaot-pal.lib`, SHA-256
`C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`.
The archive has 65 members; all four stock members are absent and all four
replacement members plus the contract member are present. The original NuGet
archive was unchanged. A second clean reconstruction produced the same archive
hash.

| Object | Expected | Replacement | Missing | Unexpected | Duplicate strong |
| --- | ---: | ---: | ---: | ---: | ---: |
| `PalRedhawkCommon.cpp.obj` | 6 | 6 | 0 | 0 | 0 |
| `PalRedhawkMinWin.cpp.obj` | 38 | 38 | 0 | 0 | 0 |
| `thread.cpp.obj` | 74 | 74 | 0 | 0 | 0 |
| `time.c.obj` | 3 | 3 | 0 | 0 | 0 |

The binding report is
[`symbol-binding-report.json`](../../out/dotnet/pal-runtime-active-replacement/symbols/symbol-binding-report.json).

## Remaining Windows imports

The four replacement objects contribute zero `__imp_` Windows imports. The
19 original candidates are all reported in
[`remaining-imports.json`](../../out/dotnet/pal-runtime-active-replacement/imports/remaining-imports.json)
with source caller and reachability. There is no mandatory startup Windows
import from a removed object. Remaining Windows imports elsewhere in the
complete runtime archive are outside this selected object family and are not
silently called dead.

## Hosted exact PAL probe

The hosted probe links the active common/MinWin/time/contract objects and the
guideXOS FLS, Event, local-storage, and native-thread adapters. It validates
PAL initialization cycles, current-thread identity, stack bounds, dynamic FLS
isolation and detach callbacks, helper creation/join/close, event wait,
monotonic timing/frequency, sleep/yield, static resolver behavior, VM
allocation/protection/free, cleanup, and second initialization/shutdown.

Result: PASS. Two launches returned `0`; the result is recorded in
[`hosted-exact-pal-result.json`](../../out/dotnet/pal-runtime-active-replacement/hosted-probe/hosted-exact-pal-result.json).
The active `thread.cpp.obj` is present in the archive and exact-parity report;
the bounded hosted probe does not link that object because its broader runtime
internal callers are outside the probe's deliberately narrow link boundary.

## QEMU bridge and exact probe

The selected bridge is the NativeAOT-style path: build a small MSVC Win64 PE
with exported `GuideXosNativeAotPalProbeMain`, convert it with the fixed-base
PE-to-ELF converter, stage the ET_EXEC image at `0x10000000`, and execute it
through the existing Server Win64 entry trampoline. The PE has no imports;
the active PAL symbols are linked directly into the PE.

The PE hash is
`1DBA9F81873C826B2B14E6A601980D8C84B7DE99C3D4A08003F962663DDDE28D` and the
converted ELF hash is
`BD5357D4806B254A4D89B058EE397008E5F5C0E284E9C8FC795A00BEF5A50EF1`.
QEMU 11.0.0 is present at `C:\Program Files\qemu\qemu-system-x86_64.exe`
with hash
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.

Server trampoline result: PASS. One fresh Server process executed two probe
launches and a second fresh Server process executed one; every launch returned
`0`, used the Windows amd64 trampoline, mapped at the preferred base, and
emitted exactly the bounded PAL probe log. Evidence is in
[`qemu-probe-result.json`](../../out/dotnet/pal-runtime-active-replacement/qemu-probe/qemu-probe-result.json).

System-QEMU exact PAL result: BLOCKED. The guideXOS QEMU side is MinGW
ELF/SysV and exposes no versioned C-compatible Win64 PAL hook table or
callback/worker/ThreadStore bridge. The existing trampoline adapts only the
exported entry call; it does not adapt PAL callback pointers or pass a native
PAL context. The bridge document records this invariant and the limitation of
the self-contained PE preflight.

## HostLog and managed baselines

The corrected HostLog proof remains reproducible: exact output
`Hello from managed guideXOS code`, exactly one callback per launch, return
code `0`, two launches in one Server process, and a fresh second process. The
fault target `0x8DC44` and unresolved `__imp_FlsGetValue` execution path are no
longer reached.

Clean managed proof results using the established no-collection guideXOS
runtime pack:

- nonallocating HostLog: PASS;
- single allocation: PASS;
- repeated 64 KiB: 234 objects, controlled OOM, collection `0`, GC-backed allocations `0`, heap expansion `0`;
- repeated 4 KiB: 14 objects, controlled OOM, collection `0`, GC-backed allocations `0`, heap expansion `0`.

These are proof-heap baselines; no real GC heap was constructed.

## PrivateSdkAssemblies staging status

The prior `PrivateSdkAssemblies` error was not reproduced after a clean locked
build with the same SDK/runtime-pack lookup, MSVC 19.51.36248, `win-x64`,
ILCompiler 9.0.0, and clean output roots. Classification:
**Not reproduced after clean locked build**. No missing path/file was observed,
no immutable package input was removed, and no managed source or runtime
behavior was changed as a workaround. The staging evidence is recorded under
[`staging`](../../out/dotnet/pal-runtime-active-replacement/staging/).

## Readiness result

| Gate | Result |
| --- | --- |
| Workstation `gcenv` replacement | PASS, 53/53 |
| Active PAL object replacement | PASS, four objects together |
| Stock PAL objects absent | PASS |
| Exact symbol parity | PASS, missing 0 / duplicate 0 |
| Remaining mandatory Windows imports | PASS for removed objects; complete-family gate remains bounded |
| Hosted exact PAL probe | PASS |
| Win64 Server trampoline bridge | PASS |
| Bare-metal/system-QEMU exact PAL probe | BLOCKED by Win64 hook-table bridge |
| HostLog live proof | PASS |
| Single/repeated allocation proofs | PASS |
| ThreadStore/FLS, VM/Event/mutex/thread | PASS as inactive/generic foundations |
| Runtime lock identity | PASS |
| `RhInitialize` / collector startup | NOT ENTERED; gate closed |

Decision: **Outcome B — Active PAL replacement complete, QEMU bridge
incomplete.**

Exact next experiment: implement and independently prove a versioned
C-compatible Win64 PAL hook table plus callback/worker/stack/ThreadStore bridge
inside the guideXOS QEMU execution side, then rerun the exact PE-to-ELF PAL
probe. Do not call `RhInitialize` until that gate passes.

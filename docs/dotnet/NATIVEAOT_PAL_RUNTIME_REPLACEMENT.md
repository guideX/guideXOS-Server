# NativeAOT PAL/runtime replacement

Status: 2026-07-25. The active four-object PAL replacement and the versioned
Win64/SysV bridge pass. Detailed bridge evidence is in
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md).
The separately gated Workstation GC startup-only dry run also passes; its
evidence is in `out/dotnet/gc-initialization-dry-run/`.

## Active archive

The active adapted archive hash is
`C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`.
The locked stock archive hash remains
`0E6A134AD4150CD604317A47860DAE82EB30AAE4D9CDB14144E06454E7BB1948`.

| Replaced member | Definitions | Missing | Unexpected | Windows imports |
| --- | ---: | ---: | ---: | ---: |
| `PalRedhawkCommon.cpp.obj` | 6/6 | 0 | 0 | 0 |
| `PalRedhawkMinWin.cpp.obj` | 38/38 | 0 | 0 | 0 |
| `thread.cpp.obj` | 74/74 | 0 | 0 | 0 |
| `time.c.obj` | 3/3 | 0 | 0 | 0 |

The adapted Workstation `gcenv` object remains 53/53 with no missing or
duplicate definitions. The active PAL archive itself was not changed by the
startup-only artifact.

## Validation status

| Gate | Result |
| --- | --- |
| Hosted exact PAL probe | PASS |
| Server PE-to-ELF PAL probe | PASS |
| System-QEMU exact PAL probe | PASS, first/repeat/fresh |
| Workstation GC `RhInitialize` startup-only dry run | PASS, first/repeat/fresh disposable processes |
| Windows PAL import directory | PASS, absent |
| Mandatory Windows PAL thunk | NO |
| HostLog | PASS |
| 64 KiB proof | PASS, 234 allocations |
| 4 KiB proof | PASS, 14 allocations |
| Controlled OOM | PASS |
| Collections | 0 |
| GC-backed allocations | 0 |
| Heap expansion | 0 |
| Generic scheduler/events/threads/VM/mutex/FLS/stack/ThreadStore | PASS |

The startup artifact calls `RhInitialize` only from its explicit probe entry.
It does not enter a managed entry point, request collection, allocate managed
objects, or invoke managed finalizer work. The PAL helper created by the
locked startup path is retained in a parked process-lifetime state because
the locked NativeAOT source exposes no orderly `RhShutdown` API.

Decision: **Outcome A - Win64 PAL hook-table and system-QEMU bridge complete.**

The exact next experiment is a separately gated Workstation GC lifecycle test
with an explicit supported shutdown boundary; same-process shutdown remains
unsupported by the current locked NativeAOT contract.

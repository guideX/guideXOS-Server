# NativeAOT PAL/runtime replacement

Status: 2026-07-24. The active four-object NativeAOT PAL replacement and the
versioned Win64/QEMU bridge are complete. The detailed ABI and QEMU evidence
are in [NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md).
`RhInitialize` was not called.

## Current archive gate

The locked stock `Runtime.WorkstationGC.lib` hash remains
`0E6A134AD4150CD604317A47860DAE82EB30AAE4D9CDB14144E06454E7BB1948`.
The post-bridge adapted archive is
`5593D0FC4B99986797123C8494DF117570DB795DF8FCE63D732BB53594C794BF`.
The pre-bridge baseline archive hash
`C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`
is preserved under `out/dotnet/pal-win64-qemu-bridge/baseline/`.

The four replaced members retain exact definition parity:

| Member | Definitions | Missing | Unexpected | Replacement imports |
| --- | ---: | ---: | ---: | ---: |
| `PalRedhawkCommon.cpp.obj` | 6/6 | 0 | 0 | 0 |
| `PalRedhawkMinWin.cpp.obj` | 38/38 | 0 | 0 | 0 |
| `thread.cpp.obj` | 74/74 | 0 | 0 | 0 |
| `time.c.obj` | 3/3 | 0 | 0 | 0 |

The active objects were rebuilt with MSVC 19.51.36248 for x64. The contract
uses fixed-width C values, explicit calling conventions, opaque generation-
checked worker handles, and no C++ runtime object at the boundary.

## Probe status

| Gate | Result |
| --- | --- |
| Hosted exact PAL probe | PASS, two launches returned 0 |
| Server PE-to-ELF PAL probe | PASS, repeat and fresh-process launches |
| System-QEMU exact PAL probe | PASS, first/repeat/fresh-process matrix |
| Windows PAL thunk in exact QEMU run | NO |
| HostLog | PASS, exact message, one callback, return 0 |
| 64 KiB proof | PASS, 234 allocations, controlled OOM |
| 4 KiB proof | PASS, 14 allocations, controlled OOM |
| Collections | 0 |
| GC-backed allocations | 0 |
| Heap expansion | 0 |

The exact QEMU matrix and complete serial logs are under
`out/dotnet/pal-win64-qemu-bridge/artifact/qemu/smoke-20260724-222115183-5445/`;
the matrix is `qemu-validation-matrix.json`. Generic scheduler, event,
native-thread, VM, mutex, local-storage/FLS, stack-bound, ThreadStore, and
inactive-adapter foundations remain passing.

## Readiness result

Active PAL replacement, exact parity, replacement imports, hook-table
versioning, SysV implementation, both callback bridges, worker lifecycle,
FLS detach, stack bounds, ThreadStore lifecycle, hosted PAL, Server PE-to-ELF,
system QEMU, HostLog, and managed no-collection proofs are PASS.

Decision: **Outcome A — Win64 PAL hook-table and system-QEMU bridge complete.**

The exact next experiment is the first separately gated Workstation GC
initialization-and-shutdown dry run. That experiment is not part of this pass;
do not call `RhInitialize` during bridge validation.

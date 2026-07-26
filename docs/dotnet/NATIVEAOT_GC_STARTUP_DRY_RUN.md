# NativeAOT Workstation GC startup dry run

Status: 2026-07-25. This document records the separately gated startup-only
experiment that follows the PAL bridge pass. It is intentionally narrower than
managed execution or collection validation.

## 1. Objective

Call the locked NativeAOT `RhInitialize` entry through a fixed-base Win64
artifact while routing PAL, GC platform, events, virtual memory, and timing
services to the booted guideXOS runtime.

## 2. Why the boundary is required

The collector object family is compiled for the Microsoft x64 ABI and the
kernel is a SysV ELF runtime. Only fixed-width C values, opaque handles, and
validated function pointers cross the boundary. C++ GC environment objects and
thread layouts remain on their owning side.

## 3. Hook-table versioning

The PAL table is `guidexos_nativeaot_pal_hook_table_v1`, ABI version 1, size
232 bytes, magic `0x475850414C483031`, and capability mask `0x1FF`.
The startup platform table is ABI version 1, size 216 bytes, magic
`0x47584743504C5431`, and capability mask `0x7`. Both use fixed-width fields,
explicit packing/alignment, compile-time size assertions, structure size, and
reserved space.

## 4. Capability negotiation

Installation validates magic, version, minimum size, capability bits, callback
addresses, artifact range, and installation generation before `RhInitialize`.
Missing capabilities fail installation precisely; a field is never called
merely because it is inside the supplied structure.

## 5. Hook-table delivery

The normal application context ABI is unchanged. The QEMU harness stages the
converted artifact and calls the probe-specific exports
`GuideXosNativeAotGcStartupInstallPalHooks`,
`GuideXosNativeAotGcStartupInstallHookTable`, and
`GuideXosNativeAotGcStartupInstallPlatformHooks` before
`GuideXosNativeAotGcStartupMain`.

## 6. Cross-ABI contract

SysV-side services use ordinary C/C++ SysV calls. Win64 callbacks are typed
with the explicit NativeAOT PAL calling convention. The existing ABI bridge
establishes Microsoft x64 register mapping and shadow space; no C++ object,
TCB pointer, exception, or STL type crosses the boundary.

## 7. Current-thread identity

The PAL current-thread hook returns the stable guideXOS scheduler identity,
not a raw TCB address. The bootstrap identity is nonzero and stable. The
previous exact PAL worker proof also confirms a distinct nonzero worker ID.

## 8. Stack bounds

Stack queries use the generic guideXOS stack-bound provider. The initial PAL
probe and the worker proof both validate low, high, and current values. No
range is guessed and no host pointer is exported as a stack bound.

## 9. FLS hooks

PAL FLS indices map to generation-safe generic local-storage slots. Allocate,
release, get, set, initial-thread isolation, worker isolation, and reuse are
covered by the exact PAL bridge evidence. The startup path does not allocate
managed objects through the collector.

## 10. Detach-callback trampoline

The generic SysV detach dispatcher calls the typed Win64 callback through the
validated ABI bridge. The bridge supplies Microsoft x64 shadow space,
16-byte stack alignment, nonvolatile-register preservation, artifact-range
validation, and result propagation. The exact PAL proof observed one callback
for one worker value.

## 11. Worker creation

Worker requests carry a validated Win64 entry, opaque context, and bounded
stack size. The SysV hook allocates a fixed bridge slot, creates one ordinary
guideXOS native worker, and preserves the callback target until join and
cleanup. The startup artifact's NativeAOT helper request is retained as a
parked process-lifetime worker.

## 12. Worker callback trampoline

The SysV worker dispatcher receives callback and context, places context in RCX
for the Win64 callback, reserves shadow space, preserves both ABI contracts,
captures RAX, and returns the `uintptr_t` result to SysV code. The standalone
and exact PAL probes return `0x1234`.

## 13. ThreadStore lifecycle

The exact PAL worker attaches local storage, attaches the minimal application-
scoped ThreadStore record, validates current-thread lookup and stack bounds,
executes the callback, detaches ThreadStore, and exits. The startup-only
helper is parked before managed finalizer work and reports no managed callback.

## 14. Timing

Counter, nonzero frequency, monotonic milliseconds, bounded sleep, and yield
are supplied by guideXOS PIT/timing services. The exact PAL timing proof
passes; the startup initialization path also reaches GC initialization without
using a Windows timing import.

## 15. Artifact lifetime

Installation has one active generation. Replacement is rejected while active
workers or callbacks remain. The PAL bridge drains worker handles and callback
records before unload. Startup `RhInitialize` has no matching public shutdown,
so its helper and runtime state remain valid until the disposable QEMU process
ends.

## 16. PE-to-ELF constraints

`NativeAotGcStartupMinimal.exe` is fixed at base `0x10000000`, exports the
probe entry points, has no import directory, and has zero relocations. The
converted ET_EXEC image is staged at the same base with BSS and writable state
preserved. The loader rejects malformed ranges before execution.

## 17. QEMU test mode

The mode is enabled only by `GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST`. It is not
part of ordinary boots or application inventory. The runner is
[`scripts/smoke-nativeaot-gc-startup-qemu.ps1`](../../scripts/smoke-nativeaot-gc-startup-qemu.ps1).

## 18. First launch

The first disposable QEMU process reports foundation initialization, artifact
staging, both table installations, hook validation, and `RhInitialize: PASS`.
The diagnostic stage is `0x00000070`, the final PAL stage is
`0x02000012`, and the process reports `ALL_PASS`.

## 19. Repeat launch

The repeat run is a second disposable QEMU process. It reports
`RhInitialize return=0` and `ALL_PASS`. Same-process restart is not claimed
because the locked source has no public `RhShutdown` or uninitialize path.

## 20. Fresh-process launch

The fresh run starts from a new QEMU process and a clean installation domain.
It reports `RhInitialize return=0` and `ALL_PASS`, proving no state is needed
from a previous process.

## 21. Cleanup

The startup harness verifies the process-lifetime cleanup boundary, no active
PAL callback, no managed finalizer entry, and no ordinary application-side
state. QEMU is then terminated as the only supported cleanup boundary for
this locked startup experiment.

## 22. Remaining limitations

This is not a collection proof, managed execution proof, root enumeration
proof, write-barrier proof, suspension proof, finalizer execution proof, or
orderly GC shutdown proof. It does not add general Win32 emulation or dynamic
loading. The initial Workstation regions range is bounded by the startup
platform's negotiated virtual-memory limit; reservations remain unbacked until
GC explicitly commits pages.

## 23. Updated readiness result

Active PAL replacement, exact parity, replacement imports, PAL hook-table
versioning, GC startup platform versioning, SysV services, both callback
bridges, worker lifecycle, FLS detach, stack bounds, ThreadStore, hosted PAL,
Server PE-to-ELF PAL, system-QEMU PAL, startup `RhInitialize`, HostLog, and
managed no-collection proofs pass. Same-process GC shutdown is unsupported.

## 24. Exact next experiment

The next experiment is a separately gated Workstation GC lifecycle test with a
supported shutdown boundary. Until that boundary is defined and validated,
do not enter managed code, trigger collection, or treat process termination as
an orderly `RhShutdown`.

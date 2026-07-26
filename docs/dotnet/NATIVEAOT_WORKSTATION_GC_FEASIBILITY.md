# NativeAOT Workstation GC feasibility

Status: 2026-07-26. The active PAL replacement, exact Win64/SysV PAL bridge,
and startup-only Workstation GC initialization probe pass under system QEMU.
The adapted-archive identity gate is resolved as **Identity B**: two clean
fresh builds have identical normalized members/symbols/imports; the one
historical semantic difference is the reviewed QEMU virtual-memory range
expansion.
See [NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md](NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md),
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md), and
[NATIVEAOT_GC_STARTUP_READINESS.md](NATIVEAOT_GC_STARTUP_READINESS.md).

The startup artifact calls `RhInitialize` from a probe-specific export and
returns successfully in first, repeat, and fresh disposable QEMU processes.
The authorized next image entered the real managed `byte[24]` path once in
each of three fresh QEMU processes, but the stock Workstation GC allocation
path did not return before the bounded timeout. No allocation diagnostics were
published, so object layout, zero initialization, pattern, ownership, and
no-collection claims are not promoted to PASS. The helper was parked before
managed entry; no managed finalizer callback was observed. The current
NativeAOT source has no public orderly shutdown operation, so same-process
teardown remains unsupported.
The completed shutdown-boundary audit selects Model C: GC and finalizer state
remain process-lifetime, and disposable QEMU process exit is the cleanup
boundary.

The preserved no-collection managed proofs remain healthy:

- HostLog: PASS
- 64 KiB allocations: 234
- 4 KiB allocations: 14
- Controlled OOM: PASS
- Collections: 0
- GC-backed allocations: 0
- Heap expansion: 0

Decision: **Outcome C for the real-allocation experiment** — Workstation GC
startup is usable in a disposable process, but the first managed allocation
did not complete. The historical no-collection proofs remain valid and are a
separate bounded image-backed mode; they do not validate the real collector.

Evidence and the exact real-allocation boundary are recorded in
[NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION.md](NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION.md).
No second initialization or live-process shutdown was attempted.

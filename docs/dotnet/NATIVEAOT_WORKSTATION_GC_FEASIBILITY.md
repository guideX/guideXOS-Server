# NativeAOT Workstation GC feasibility

Status: 2026-07-25. The active PAL replacement, exact Win64/SysV PAL bridge,
and startup-only Workstation GC initialization probe pass under system QEMU.
See [NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md](NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md),
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md), and
[NATIVEAOT_GC_STARTUP_READINESS.md](NATIVEAOT_GC_STARTUP_READINESS.md).

The startup artifact calls `RhInitialize` from a probe-specific export and
returns successfully in first, repeat, and fresh disposable QEMU processes.
It does not enter managed code, allocate through the collector, trigger a
collection, or invoke managed finalizer work. The helper created by the
locked startup path remains parked; the current NativeAOT source has no
public orderly shutdown operation, so same-process teardown is unsupported.

The preserved no-collection managed proofs remain healthy:

- HostLog: PASS
- 64 KiB allocations: 234
- 4 KiB allocations: 14
- Controlled OOM: PASS
- Collections: 0
- GC-backed allocations: 0
- Heap expansion: 0

Decision: **Outcome A - Win64 PAL hook-table and system-QEMU bridge complete.**

Exact next experiment: define and validate a supported Workstation GC shutdown
boundary in a separately gated experiment before managed execution or
collection is attempted.

# NativeAOT Workstation GC feasibility

Status: 2026-07-26. The active PAL replacement, exact Win64/SysV PAL bridge,
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

Decision: **Outcome B - Workstation GC is usable for startup-only disposable
processes, with process-lifetime runtime state and no same-process reinit.**

Exact next experiment: after the locked-artifact identity gate passes, run one
disposable QEMU init with exactly one primitive `byte[]` through the real GC,
with no collection, finalizer request, second `RhInitialize`, or live-process
shutdown attempt. See
[NATIVEAOT_RUNTIME_GC_SHUTDOWN_BOUNDARY.md](NATIVEAOT_RUNTIME_GC_SHUTDOWN_BOUNDARY.md).

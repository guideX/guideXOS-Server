# NativeAOT Workstation GC Feasibility

Status: 2026-07-24. The active PAL replacement and exact Win64-to-SysV
system-QEMU bridge pass. See [NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md](NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md),
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md), and
[NATIVEAOT_GC_STARTUP_READINESS.md](NATIVEAOT_GC_STARTUP_READINESS.md).

The bridge validation did not call `RhInitialize`, construct a real
Workstation GC heap, start the real finalizer/helper thread, enter collection,
or allocate through the collector. The no-collection managed proofs and
generic runtime foundation evidence remain passing.

Decision: Outcome A — Win64 PAL hook-table and system-QEMU bridge complete.

Exact next experiment: the first separately gated Workstation GC
initialization-and-shutdown dry run, with its own cleanup and no-collection
fallback checks.

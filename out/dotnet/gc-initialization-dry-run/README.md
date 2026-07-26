# Workstation GC initialization dry-run evidence

This directory contains the separately gated NativeAOT Workstation GC
initialization experiment. The exact startup artifact calls `RhInitialize`
once in each disposable system-QEMU process and reports `ALL_PASS` for the
first, repeat, and fresh runs.

- PAL hook table: ABI v1, 232 bytes, capabilities `0x1FF`
- GC startup platform table: ABI v1, 216 bytes, capabilities `0x7`
- Artifact: `artifact/NativeAotGcStartupMinimal.exe`
- Converted artifact: `artifact/NativeAotGcStartupMinimal.elf`
- Runner: [`scripts/smoke-nativeaot-gc-startup-qemu.ps1`](../../../scripts/smoke-nativeaot-gc-startup-qemu.ps1)
- Matrix: `validation-matrix.json`
- Serial runs: `qemu/matrix/{first,repeat,fresh}/serial.log`

The probe does not enter managed code, allocate through the collector, trigger
collection, or invoke managed finalizer work. The locked NativeAOT source has
no public orderly `RhShutdown`; the helper is retained as a parked
process-lifetime state and disposable QEMU process termination is the cleanup
boundary.

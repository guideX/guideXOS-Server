# NativeAOT PAL Win64/QEMU bridge

Status: 2026-07-25. The versioned PAL hook table, SysV/Win64 callback
bridges, worker lifecycle, FLS detach path, stack-bound path, and
application-scoped ThreadStore probe all pass under system QEMU. The
separately gated startup artifact also initializes Workstation GC successfully
without managed execution, collection, or collector-backed allocation.

## 1. Objective

Connect the active MSVC x64 NativeAOT PAL replacement artifact to the booted
guideXOS SysV runtime through a bounded C ABI, then validate the actual PAL
entry points in the converted artifact.

## 2. Why the ABI bridge is required

The PAL object is compiled for the Microsoft x64 ABI while the guideXOS
kernel is a SysV ELF environment. C++ runtime objects, TCB layouts, STL
types, exceptions, and native thread objects cannot cross that boundary.
Only fixed-width C values, opaque validated handles, and explicitly typed
callback pointers cross it.

## 3. Hook-table versioning

`guidexos_nativeaot_pal_hook_table_v1` is packed with 8-byte alignment and
has a fixed size of 232 bytes. Its header contains:

```text
magic              0x475850414C483031
abi_version        1
structure_size     sizeof(table)
capability_bits    0x00000000000001FF
installation_generation
artifact_base / artifact_size
```

The header uses fixed-width integers and reserves eight `uint64_t` fields.
Compile-time assertions freeze size, alignment, and hook offsets. Installation
rejects a bad magic, unsupported version, truncated table, zero generation,
unrepresentable worker-domain generation, overflowing artifact range, missing
capabilities, or null mandatory hooks.

## 4. Capability negotiation

The independent bits are CurrentThread, StackBounds, FLS, WorkerThread,
Win64Callback, ThreadStore, Timing, SleepYield, and FailFast. The exact probe
requires all nine bits. A hook is never called merely because its field lies
inside a supplied structure.

## 5. Hook-table delivery

The normal application context ABI is unchanged. The opt-in QEMU harness
embeds the converted ET_EXEC image, constructs the table in the SysV kernel,
and calls the probe-specific Win64 export
`GuideXosNativeAotPalInstallHooks` before
`GuideXosNativeAotPalProbeMain`. The matching uninstall export is called only
after all workers and callbacks are drained.

## 6. Cross-ABI contract

The table and bridge declarations are C-compatible. The table contains no
generic runtime object pointer or TCB pointer. The callback types are marked
with `GUIDEXOS_NATIVEAOT_PAL_CALL`; MSVC uses `__cdecl` on AMD64 and the
SysV-side GCC build uses `ms_abi` for Win64 callback pointers.

The standalone ABI probe passes both a worker callback and a detach callback:
`out/dotnet/pal-win64-qemu-bridge/trampolines/guidexos_nativeaot_pal_abi_bridge_probe.exe`.

## 7. Current-thread identity

`current_thread_id` returns the stable scheduler thread identity, never a raw
TCB address. The bootstrap thread and the native worker return nonzero IDs;
the worker ID is distinct from the bootstrap ID. Opaque generation checks
prevent a stale worker token from targeting a later thread.

## 8. Stack bounds

`query_current_stack_bounds` delegates to the generic stack-bound provider and
returns low, high, and sampled current-stack values. Both initial and worker
threads validate `low <= current < high`; the worker bounds remain live until
ThreadStore and FLS detach complete.

## 9. FLS hooks

PAL FLS indices map to generic local-storage indices. The adapter owns the
mapping and its callback-bearing slot records; the Win64 artifact never sees a
generic index generation. Allocation, set/get, per-thread isolation, release,
and generation-safe worker cleanup pass for the initial and worker threads.

## 10. Detach-callback trampoline

Generic local-storage detach runs with the SysV ABI and calls
`guidexos_nativeaot_pal_bridge_invoke_detach`. The callback parameter is a
typed Win64 function pointer. On x64 the compiler-generated `ms_abi` call
establishes the Microsoft register mapping, reserves 32 bytes of shadow space,
aligns the stack, preserves the required nonvolatile registers, calls the
artifact callback, and returns to SysV code. Callback addresses are checked
against the loaded artifact range. The callback is invoked once for the
worker value in the exact probe; callback errors are not hidden.

## 11. Worker creation

The PAL create hook validates the callback address, context, and bounded
16-byte-aligned stack size, stores the request in a fixed bridge slot, and
creates one ordinary guideXOS native worker. The worker attaches generic local
storage, obtains exact bounds, attaches the minimal ThreadStore record, calls
the Win64 callback, detaches ThreadStore, and exits normally.

## 12. Worker callback trampoline

The SysV bridge entry receives the callback and context in the SysV argument
registers. It calls the typed Win64 callback with context in RCX, preserves
the cross-ABI nonvolatile state, and returns the callback RAX value to SysV
code. The exact callback returns `0x1234`; the result is captured before join.

## 13. ThreadStore lifecycle

The worker attaches an application-scoped opaque runtime-thread record after
local-storage attach and before the callback. Current-thread lookup and exact
stack bounds are valid while attached. The record is detached before worker
exit, and the bootstrap record is detached only after the hook table is
uninstalled. Transition-frame state remains empty in this probe.

## 14. Timing

The table exposes counter, nonzero frequency, monotonic milliseconds, bounded
sleep, and yield. The QEMU exact probe observes counter progress, a sane
20-millisecond sleep interval, and a normal yield return.

## 15. Artifact lifetime

One installation generation is active at a time. Installation is rejected
while active; uninstall is rejected while opaque worker slots remain active.
Worker handles are joined and destroyed exactly once, callback records are
released before image unload, and the converted artifact is unmapped only
after all bridge state is quiescent. A second launch receives generation 2.

## 16. PE-to-ELF constraints

The PE is linked at preferred base `0x10000000`, exports the three probe
entrypoints, has no mandatory Windows import, and is converted to a static
ET_EXEC ELF without generated-machine-code patches. The loader verifies ELF
headers, PT_LOAD ranges, zeroes BSS, maps writable image pages, stages the
artifact, and validates callback pointers against the loaded range.

The final active archive hash is
`C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`.
The pre-bridge baseline hash remains preserved under
`out/dotnet/pal-win64-qemu-bridge/baseline/`.

## 17. QEMU test mode

The PAL mode is opt-in through `GXOS_NATIVEAOT_PAL_QEMU_TEST`; ordinary boots
and the default application inventory are unchanged. The runner is
[`scripts/smoke-nativeaot-pal-qemu.ps1`](../../scripts/smoke-nativeaot-pal-qemu.ps1).
The startup-only GC mode is independently opt-in through
`GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST`; its runner is
[`scripts/smoke-nativeaot-gc-startup-qemu.ps1`](../../scripts/smoke-nativeaot-gc-startup-qemu.ps1).
It stages the fixed-base converted artifact, installs the PAL and GC platform
tables through probe-specific exports, calls `RhInitialize`, records bounded
state, and terminates the disposable QEMU process.

## 18. First launch

System QEMU launch 1 passes hook negotiation, initial-thread PAL calls,
worker creation, callback entry, result capture, FLS detach, ThreadStore
detach, cleanup, and the complete status suite. Evidence:
`out/dotnet/pal-win64-qemu-bridge/artifact/qemu/smoke-20260724-222115183-5445/`.
The authoritative matrix is
`qemu-validation-matrix.json`; its worker result is `0x1234` and its
callback count is `expected=1 observed=1`.

## 19. Repeat launch

The first PAL QEMU process runs `run()` twice. The second in-process generation
passes with no stale worker, FLS, callback, ThreadStore, or hook-table state.
For the GC startup-only artifact, repeat means a second disposable process:
the locked source has no orderly same-process `RhShutdown` contract.

## 20. Fresh-process launch

The PAL runner starts a separate QEMU process with the same opt-in image. The
fresh process boots normally and its two-generation exact probe reports
`ALL_PASS`. The GC startup runner also starts a fresh disposable process and
reports `ALL_PASS`.

## 21. Cleanup

The exact PAL run reports PASS for worker join/cleanup, ThreadStore detach,
hook uninstall, and final cleanup. The startup-only GC run reports PASS for
runtime initialization and the process-lifetime cleanup boundary. It retains
the parked helper until QEMU exits; same-process shutdown is explicitly
`UNSUPPORTED`.

## 22. Remaining limitations

This is a probe-scoped PAL boundary, not general Win32 emulation. It does not
provide dynamic loading, arbitrary Windows handles, suspension/hijacking,
collection-safe enumeration, or managed finalizer execution. The GC startup
artifact uses a bounded startup platform table and a process-lifetime cleanup
boundary because the locked NativeAOT source exposes no public shutdown API.
The artifact is not retained after its disposable process exits. Failure paths
are fail-closed; a callback fault is not converted into a success result.

## 23. Updated readiness result

The PAL replacement, versioned hook table, SysV implementation, both callback
bridges, worker lifecycle, stack bounds, ThreadStore lifecycle, hosted exact
probe, Server PE-to-ELF probe, system-QEMU exact probe, and startup-only
Workstation `RhInitialize` probe are PASS. The managed no-collection proofs
and generic foundation suites remain required regression evidence and are
recorded by
[`NATIVEAOT_GC_STARTUP_READINESS.md`](NATIVEAOT_GC_STARTUP_READINESS.md).

## 24. Exact next experiment

The next separately gated experiment is a Workstation GC lifecycle experiment
with an explicit supported shutdown boundary. The current startup proof calls
`RhInitialize` only from its probe export, returns 0 in first/repeat/fresh
disposable QEMU processes, enters no collection, performs no collector-backed
allocation, and does not enter managed finalizer code. Do not infer orderly
same-process shutdown from this result.

# AARCH64 Phase 9 — NativeElf runtime services

Status: implemented on `AARCH64_SUPPORT`.

Phase 9 hardens the Phase 8 ARM64 NativeElf GUI boundary without changing the
ABI version. The kernel now owns per-application runtime state, opaque handles,
bounded event delivery, blocking wait/wake, quotas, and idempotent teardown.

## Runtime contract

`ApplicationRuntime` tracks package identity, architecture, runtime ID,
generation, lifecycle state, and resource accounting. Valid transitions are:

```text
Starting -> Running <-> Waiting -> Closing -> Exited -> Cleaned
```

The quota set used by the ARM64 proof is bounded to three windows, sixteen
widgets, thirty-two queued event entries, 4096 mapped pages, thirty-two stack
pages, and 2048 string bytes. Window, widget, mapping, stack, and string
reservations fail closed with `GX_ERROR_QUOTA` when saturated.

Handles encode a runtime identity, generation, object kind, object generation,
and bounded slot. Validation rejects forged, stale, cross-application, and
wrong-kind tokens before any object operation. Cleanup invalidates the event
service, releases owned resources, and is safe to repeat.

## Event and wait/wake behavior

Each runtime owns a fixed 32-entry event service. Pointer-move events coalesce
per window. Ordinary events use drop-newest when full; lifecycle close events
evict the oldest ordinary entry so close delivery is guaranteed. Duplicate
close events are suppressed per window, allowing one close event for each
window in a multi-window application.

The optional append-only `wait_event` host slot blocks the owning scheduler
task with the queue check, waiter registration, and blocked-state transition
under one IRQ-masked critical section. Enqueue and invalidation wake the waiter
without calling application code from the compositor or IRQ path.

## Proof applications

The staged FAT32 `/Apps` tree contains two independent ARM64 ET\_EXEC NativeElf
clients:

* App A owns a main and secondary window, labels, a textbox, and buttons. It
  proves focus/input routing, secondary-window destruction, main close, stale
  handle rejection, cleanup, and 51 bounded relaunches.
* App B owns an independent window and widget set and remains alive while App A
  is closed and relaunched. It proves cross-runtime isolation and final cleanup.

The repeated launches use the same queued close path as the interactive path;
the test monitor drives only the bounded proof timing. The first QMP boot also
drives textbox input, button clicks, focus, secondary close, and main close.

## Build and test

Build, host negative controls, ABI layout checks, ARM64 package staging, and
metadata recording:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase9.ps1
```

Run the complete QMP acceptance harness, including three fresh UEFI/QEMU
boots:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase9.ps1
```

The harness requires `AARCH64_PHASE9_PASS`,
`runtime lifecycle durability: PASS launches=51`, and
`scheduler/VFS integration: PASS` on every boot before printing
`AARCH64_PHASE9_QMP_HARNESS_PASS`.

The host suite prints `AARCH64_PHASE9_HOST_CONTROLS_PASS`; the independent ABI
test prints `Native ABI layout test PASS`. ARM artifacts are checked for
`EM_AARCH64` and the staged package includes the existing wrong-machine
negative control.

## Scope boundary

Phase 9 does not claim the unresolved Phase 7 high-rate QEMU/QMP stress proof.
The Phase 7 limitation and its `AARCH64_PHASE7_PASS` status remain documented
in `AARCH64_PHASE7_INPUT_DESKTOP.md`. This phase also does not introduce EL0
isolation, SMP, GPU acceleration, networking, or a QMP transport redesign.

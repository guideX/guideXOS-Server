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

## First-allocation hang follow-up

The original Outcome C result remains preserved. The immutable artifact was
captured and stopped in the reverse-P/Invoke guard at RIP `0x10001CA7` with
`gs_base=0`; this was a terminal fail-fast self-loop with interrupts enabled,
not a GC lock, Event, helper, or collection wait. The exact corrections were
the current-thread TLS vector/runtime-cell bootstrap, PE-to-ELF mapping of the
zero-raw-size `hydrated` section, and the source-matching NativeAOT
`RehydrateData` call before `ManagedMain`. The real `byte[24]` size/range proof
was updated from the bounded-mode 40-byte geometry to the actual 48-byte
object.

The corrected artifact passed one real collector-backed allocation in the
first process and two additional fresh QEMU processes. The object was
non-null, length 24, zero-initialized, pattern-valid, aligned, in the
Workstation heap, and no collection or finalizer executed. The follow-up
decision is **Outcome A**. The exact hashes, stage record, disassembly,
watchdog snapshot, and remaining regression limitation are in
[NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION_HANG.md](NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION_HANG.md).

## Final closure validation — 2026-08-01

The Outcome C wording above describes the pre-correction artifact and remains
historical. The core implementation is authoritative in commit
`95580df6872e85527d27526b78ae3cdcee25dd53`. The original hang was a fail-fast
loop. GS/TLS was the first blocker; PE-to-ELF zero-fill mapping and NativeAOT
metadata hydration were subsequent blockers.

After those corrections, every named dedicated QEMU, PAL, stack, FLS,
process-teardown, managed-baseline, hosted-generic, and focused converter
closure suite passed. The allocation-specific runner enforces experiment
selectors, matching preprocessor definitions, fresh logs, allocation-specific
markers, and restoration of the ordinary kernel. Generated evidence is ignored
and untracked, with local copies preserved.

One final fresh disposable process performed exactly one collector-backed
`byte[24]` allocation. It returned `0x100A00028`; the source-derived aligned
object size was `0x30` / 48 bytes, with 24 initial zero bytes, pattern PASS, and
GC ownership PASS. Managed entry, `RhpNewArray`, and real-GC allocation entries
were 1; collections entered and managed finalizers were 0. OS process teardown
passed; runtime-level GC shutdown is NOT SUPPORTED.

The final decision is **Closure Outcome A — First collector-backed allocation
milestone fully closed**. Bounded primitive-array allocations through
Workstation GC are authorized until the first subsequent allocation-context
refill, without allowing collection. Repeated allocations were not run during
closure validation.

## First subsequent refill validation

The bounded repeated-allocation follow-up is documented in
[NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md](NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md).
It passed in three fresh disposable QEMU processes and stopped immediately
after validating the first object returned from the later allocation-context
refill. Each run recorded 15 requests, 13 fast allocations, two rare/real-GC
allocations, and two context refills. The refill-2 object was a valid
non-LOH `byte[256]` with aligned size `0x118` / 280 bytes and GC ownership;
collection and finalization counters were zero. This advances the bounded
authorization through that exact refill boundary, while preserving the
process-lifetime shutdown restriction.

## 4 KiB exit regression classification

The initially observed live bounded 4 KiB result `0xC0000419` remains
preserved as an opaque observation. Immutable-artifact raw Server capture and
three fresh runner executions all returned zero and passed the historical
14-allocation controlled-OOM contract. The current source path contains no
producer for the reported value, and the preserved failing execution lacks the
process/WER/serial record needed to identify its first layer. No speculative
runtime or runner correction was made.

The first-refill result therefore remains technically valid but its milestone
closure is pending a separate, source-backed classification of that 4 KiB
exit. Multiple subsequent refills, segment transitions, new page commitment,
collection, and allocation beyond the established boundary remain blocked.
The detailed evidence is in
[NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md](NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md).

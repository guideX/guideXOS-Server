# AARCH64-3 Common Kernel Scheduler

## Result

AArch64 now reaches a freestanding architecture-neutral guideXOS kernel
entry, creates kernel tasks through the common scheduler, switches ARM64
contexts cooperatively, preempts non-yielding tasks from the architectural
timer, enters the ARM64 idle/WFI path, and completes a bounded durability
proof.  The final marker is emitted only after the common scheduler returns
the validated statistics for that proof:

```text
AARCH64_PHASE3_PASS
```

The QEMU target remains `virt,gic-version=2,acpi=off` with `cortex-a53`.
GICv3 is intentionally deferred.

## Existing scheduler audit

The repository audit found two different scheduler-shaped components:

* `scheduler.cpp`/`scheduler.h` is a hosted `std::thread` telemetry harness;
  it is not a freestanding kernel scheduler.
* `kernel/core/main.cpp::kernel_main` is the existing AMD64 bare-metal entry.
  Its production path initializes the serial console, IDT/PIC, PIT, storage,
  and device/application services, then uses polling/halt idle behavior.  It
  does not create kernel tasks through a common scheduler.
* `kernel/arch/amd64/context_switch.cpp` contains an older AMD64 context
  scaffold with a static pending entry and unfinished exception/FPU hooks; it
  is not called by `kernel_main`.

Consequently, there was no freestanding production scheduler implementation
that ARM64 could simply call.  Phase 3 establishes one reusable common
kernel scheduler in `kernel/core/common_scheduler.cpp`; the ARM64 proof uses
that same source rather than an ARM64-local scheduler.  The existing AMD64
entry and hosted scheduler behavior remain unchanged.

## Common/architecture split

The common layer owns task metadata, stack ownership, canaries, runnable
selection, cooperative yield, scheduler phases, tick/preemption accounting,
and completion decisions.  It calls only the small contract in
`kernel/core/include/kernel/arch_interface.h`:

* interrupt state save/restore and enable/disable;
* CPU idle and memory barrier;
* context initialization and voluntary context switch;
* architecture exception-frame save and conversion of a new context into an
  exception-return context;
* architecture name for diagnostics.

The ARM64 layer owns DAIF, `WFI`, `DMB`, context frame layout, exception
vectors, `ELR_EL1`/`SPSR_EL1`, the physical timer, and the GICv2 register
operations.  The GIC-facing wrappers acknowledge and complete the existing
Phase-2 GICv2 implementation; no GICv3 code was added.

The Phase-3 assembly entry performs ARM64 setup and then calls
`kernel::common::kernel_entry_init`.  That common entry initializes the
common scheduler and emits the common-entry and architecture-interface
markers.  Phase-2 MMU, DTB, allocator, GIC, and timer-foundation code is
preserved and reused.

## ARM64 context representation

The cooperative context is an opaque 104-byte structure stored on the owning
task's stack:

```text
x19..x30   12 words
SP          1 word
```

The assembly switch reserves a 112-byte, 16-byte-aligned save frame.  It
saves x19-x30, stores the continuation SP past that frame, updates the old
context pointer, restores the destination registers/SP, and returns through
the destination x30.  A new context sets x19 to the task entry, x20 to its
argument, x30 to `phase3_thread_start`, and enters with an aligned SP.

`phase3_thread_start` passes x20 as the AAPCS64 argument, calls x19, and
routes a returning task to `scheduler_thread_exit`.

The exception-return context is separate and opaque to the common layer:

```text
x0..x30    31 words
SP, PC, PSTATE
```

It occupies 272 bytes inside the common 40-word storage limit.  The ARM64
vector saves x0-x30 in a 256-byte frame, with a 16-byte scratch prefix for
the vector class and original x16.  `interrupt_frame_save` copies the frame,
uses the current `ELR_EL1` and `SPSR_EL1`, and reconstructs the interrupted
task SP as frame+272.

## IRQ and preemption transition

The timer path is:

```text
CNTP timer
  -> GICv2 acknowledge
  -> ARM64 current-EL IRQ vector
  -> phase3_exception_dispatch
  -> phase3_timer_ack_and_rearm
  -> common scheduler::timer_interrupt
  -> GICv2 completion
  -> selected exception context or current frame
  -> materialize destination frame and ERET
```

For a preempted task, the common scheduler saves the complete exception
frame into that task's interrupt context, selects the next runnable task,
and returns its prepared exception context.  The ARM64 restore bridge copies
the selected x0-x30 values onto the selected task stack, writes ELR/SPSR,
restores the frame registers, and executes `ERET`.  This avoids calling the
cooperative switch directly from an IRQ and preserves the exception-return
contract.

The prepared task PSTATE is `EL1h` with exception masks clear.  The current
frame's PSTATE is restored unchanged, so a preempted task's mask state is not
silently replaced.

## Interrupt state and critical sections

`irq_save` reads the complete `DAIF`, sets only the IRQ mask, and returns the
previous value.  `irq_restore` writes the complete saved value.  The common
scheduler therefore does not use an unconditional enable in its critical
sections.  The first preemptive task is entered only after `scheduler::start`
restores the caller's enabled state before switching; the idle task also
enables IRQs before `WFI`.

Phase 3 is single-core.  Scheduler metadata is intentionally non-atomic
because it is accessed with IRQs masked or from the current CPU.  ARM64's
architecture barrier is `DMB ISH`; timer and GIC MMIO transitions use the
existing `DSB SY`/`ISB` sequences.  SMP locking and per-CPU scheduling are
deferred.

## Stack and FP/SIMD policy

Each proof task receives 16 pages (64 KiB) from the reused Phase-2 physical
page allocator.  Stacks are non-overlapping allocator results, have 16-byte
alignment, and carry low/high 64-bit canaries.  The context is placed below
the aligned stack top; the initial saved SP points above its 112-byte switch
frame and below the high canary.  The scheduler validates ownership and
canaries when tasks switch, on timer preemption, and at each proof boundary.

Phase 3 explicitly prohibits FP/SIMD in scheduled kernel tasks.  The ARM64
freestanding build uses `-mgeneral-regs-only`, and both assembly stress
workloads use only GPRs.  No FP/SIMD state is therefore part of this context
contract.  A future kernel task that permits FP/SIMD must add an explicit
lazy/eager save policy and extend the context contract before enabling it.

## Proof workloads

The cooperative workload has three integer-only tasks with private counters,
private magic values, separate stacks, and an assembly probe that sets
task-specific x19-x29 patterns across `scheduler_yield`.  The probe restores
the caller's original callee-saved registers before returning, so it tests
the switch contract without violating AAPCS64 for the surrounding C code.

The preemptive workload has three non-yielding assembly workers.  Each keeps
x19-x29 set to deterministic task-specific values across a C work helper and
checks them after every helper call.  The architectural timer must rotate
them until the common scheduler's 10,000-preemption target selects the
common completion task.

The idle workload disables the three workers, starts the common idle task,
executes `WFI` with IRQs enabled, and verifies that a timer IRQ increments
the common idle-wake count before returning to the bootstrap context.

## Validation results

The repository-local Phase-3 suite builds and validates PE/ELF machine types,
runs host-side contract negative controls, stages the ESP, and performs three
fresh QEMU boots.  All three completed the required marker order and had
zero unexpected IRQs and exceptions:

| Boot | Ticks | Context switches | Preemptions | Task A | Task B | Task C | Idle wakes |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 10,001 | 20,008 | 10,000 | 49,816,554 | 49,956,487 | 49,649,516 | 1 |
| 2 | 10,001 | 20,008 | 10,000 | 47,709,911 | 47,509,348 | 47,288,781 | 1 |
| 3 | 10,001 | 20,008 | 10,000 | 60,295,678 | 61,114,045 | 60,414,389 | 1 |

Every boot also reported `unexpected-irq=0 exceptions=0` and passed the
register-integrity and stack-canary checks.  The cooperative proof reported
10,004 aggregate switches on each run.  The host contract test covered
misaligned base, invalid top, undersized stack, address overflow, null entry,
and both canary mismatch directions.

The historical regressions were run after the Phase-3 implementation:

* Phase 1: three fresh boots passed, plus the wrong-machine ELF negative
  control.
* Phase 2: three fresh boots passed, plus the malformed-input host controls.

The normal AMD64 `mingw32-make -C kernel ARCH=amd64 info` check remains
blocked before compilation by the repository's documented incomplete
generated Mbed TLS profile.  The new AMD64 architecture-interface backend
does compile with the existing freestanding AMD64 flags.  No networking or
Mbed TLS restoration work was attempted.

## Known limitations and AARCH64-4

Phase 3 is a bounded single-core kernel-scheduler proof, not the final
desktop kernel integration.  The full AMD64 `kernel_main` still has legacy
x86 PIC/PIT/device initialization, and the repository does not yet have a
production task lifecycle with sleep/wake queues, a generic IRQ registry, or
an SMP/per-CPU scheduler.  The ARM64 proof still uses identity-mapped Phase-2
tables, the Phase-2 physical allocator, polled PL011 output, GICv2 Group-0,
and the QEMU `virt` platform.

AARCH64-4 is implemented in `AARCH64_PHASE4_MEMORY_VFS.md`.  It introduces the
common UEFI/ramdisk boot resources, production common physical allocator,
aligned common heap entry points, loader-supplied FAT/VFS proof, generic IRQ
registry, and the combined preemption/filesystem durability workload while
retaining this Phase-3 architecture contract.

The next phase can address framebuffer/input, GICv3, SMP/per-CPU scheduling,
userspace, demand paging, networking, NativeElf, and desktop integration.

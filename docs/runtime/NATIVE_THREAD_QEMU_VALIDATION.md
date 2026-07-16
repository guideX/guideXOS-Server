# Native Thread QEMU Validation

Date: 2026-07-16  
Decision: Outcome A — bare-metal native thread lifecycle validated.

## 1. Objective

Boot the current AMD64 guideXOS Server experimental image and validate the
runtime-neutral native thread create, context switch, entry, result, join,
timed wait, detach, reuse, and teardown contracts under real bare-metal
scheduling. The test is opt-in and does not initialize NativeAOT GC, start a
finalizer thread, or change the normal application inventory.

## 2. QEMU identity and command line

QEMU was located outside PATH at:

```text
C:\Program Files\qemu\qemu-system-x86_64.exe
```

```text
QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)
QEMU SHA-256: A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02
Architecture: AMD64 system emulator
qemu-img: C:\Program Files\qemu\qemu-img.exe
qemu-img SHA-256: 6D484FD8CD6E90150772529160347A52A8F48EB50F132ACC1228D8A4E7BD2D75
OVMF.fd SHA-256: 2DF617A6FD1BAE41EC7C0B3CECA96C928A5A88D8DC198468D34733393AB2792
Normal kernel image SHA-256: E08F0763F4A00BC782580E9B2FA8163A8F50F5B4EC1C30E1EFC68743183776F5
Test kernel image SHA-256: 4D5001EB32B104C6EAFF1453D1EB56621FBCAF6A816F340E68EB1ADAB978B5DA
UEFI bootloader SHA-256: 9A65A4518B0554BCDA60AA3F2E5C3052B2DC6B2B4AF4ACC1FB4864D7D621E329
Ramdisk image SHA-256: 7E93E34732526DBFFEE097912F776467B0DD03C989CBDA06F4AA24B0945A969B
```

The smoke script uses one CPU and TCG:

```text
-accel tcg,thread=single -machine pc
-drive if=pflash,format=raw,readonly=on,file=OVMF.fd
-drive file=fat:rw:<private-ESP>,format=raw,if=ide,index=0
-m 1024M -vga std -display none
-serial file:<serial-log> -no-reboot -no-shutdown
-rtc base=utc,clock=host -d int,cpu_reset -D <fault-log>
```

The QEMU executable, firmware, bootloader, and image identity are preserved
in `out/runtime/native-thread-qemu-validation/smoke-20260716-070757/`.
QEMU is configurable through `-QemuPath` or `GXOS_QEMU_X64`; the script never
changes machine-wide PATH. The companion `qemu-img.exe` was located and
hashed, although no conversion operation was needed.

## 3. Baseline boot

Before enabling the test macro, a private normal ESP booted through UEFI and
reached:

```text
[KERNEL] guideXOS kernel_main entered
[KERNEL] Entering main loop (waiting for input)...
```

This proves that the bootloader, OVMF, kernel handoff, serial path, and
ordinary desktop/storage startup were healthy before the test image replaced
normal startup with the bounded test mode. Baseline serial evidence is in
`out/runtime/native-thread-qemu-validation/baseline-boot/baseline-serial.log`.

## 4. Test-mode design

`GXOS_NATIVE_THREAD_QEMU_TEST` adds a small kernel test translation unit and
an early branch in `kernel_main`. The branch initializes the existing IDT/PIC
and PIT services needed by scheduler timers, prints a
`[native-thread-test]` prefix, runs bounded cases, prints explicit PASS/FAIL
markers and `ALL_PASS`, and enters the known interrupt-enabled idle state.
Normal boots do not execute this code. The smoke script builds/stages normal
and test ESPs privately, bounds each QEMU run, preserves serial/stdout/stderr
and QEMU interrupt logs, and returns nonzero for a timeout, missing marker, or
test failure.

## 5. Single-worker trace

The first worker in the successful run used TCB slot 1, generation 1, and
received a unique context pointer. The trace recorded a 16 KiB stack range,
an initial instruction pointer in the generic trampoline, and an initial stack
pointer eight bytes below the recorded high bound. It then recorded:

```text
first schedule -> entry invocation -> exit result 0x1234
-> completion signal -> join wake -> reclamation
```

The context magic/version check, exactly-once invocation, worker-owned field
update, result `0x1234`, valid handle, and zero live TCB/stack leak all passed.

## 6. Stack/ABI validation

The initial AMD64 frame uses the existing `-mno-red-zone` kernel ABI, keeps the
recorded stack pointer inside the allocated range, and presents the expected
post-frame call alignment. The entry argument arrived through the expected
context slot. The initial nonvolatile register seed fields were preserved in
the TCB snapshot, and repeated scheduler resumes across the bounded worker
group completed without corruption. The trampoline reaches the kernel exit
path and cannot fall through to an uninitialized return address.

The scheduler policy keeps interrupts disabled across short queue/context
critical sections and enables them for the normal wrapper and idle wait. No
red-zone or Windows shadow-space assumption is used by the initial frame.

## 7. Join-before-exit

A worker blocked on a request Event. The creator observed the blocked state,
received `TimedOut` from zero-timeout join, received `TimedOut` from a finite
join while the request remained unsignaled, and verified that the target,
result, completion Event, stack, and TCB were retained. After signaling the
request, the worker returned `0xBEEF`; the retried join succeeded and observed
the published result. The completion wake and reclamation occurred once.

## 8. Join-after-exit

A fast worker returned `0xCAFE` before the creator joined. The zero-timeout
join completed immediately, returned the stored result, reclaimed the target
once, and caused the second join and old handle to be rejected.

## 9. Timed join

The zero-timeout poll returned `TimedOut` for a live target. A finite PIT-based
join also timed out without consuming completion. A subsequent infinite join
completed after the request Event was signaled. The target result was
published before the joiner observed completion, and retry succeeded.

## 10. TCB reuse and generations

Twenty bounded create/run/join iterations reused slot 1. The generation
advanced on each reclamation, old handles were rejected, and each new handle
ran with a fresh zeroed stack and reset Event/wait state. The final run reached
generation `0x17` in the reuse case. No stale runnable, wait, or timer link
survived reuse.

## 11. Detach

Detach-before-exit passed: the live worker was detached, signaled, exited,
and reclaimed after switching away from its stack; joining its handle was
rejected. Detach-after-exit passed: the terminated worker was detached and
reclaimed immediately from the creator context; joining the consumed handle
was rejected. The active worker never frees its own current stack.

## 12. Multiple workers

Four workers with distinct contexts waited on a shared request Event and
returned `0x3000` through `0x3003`. Wake-all placed them on the existing FIFO
runnable queue. Each entry ran once, context/result isolation passed, all
joins succeeded, and no duplicate runnable insertion or premature reclamation
was observed.

## 13. Wait/timer cleanup

The bounded timer cases covered timeout-before-signal and signal-before-timeout.
Both workers exited normally, joined successfully, and had no wait-node or
timer registration left at reclamation. The corrected wait path also selects a
newly runnable thread when an earlier wake occurs while the current thread has
a later deadline; this prevents a long timed wait from hiding a runnable
worker.

## 14. Process teardown

The narrow existing owner-teardown hook was exercised with runnable, blocked,
timed-wait, terminated, and detached child cases. Runnable and blocked links
were removed, timed wait registrations were cleared, detached cleanup was
deferred until a safe stack switch, no child resumed application code after
owner teardown, and the bounded live-count/leak checks passed. This validates
the existing bounded policy; it does not add asynchronous kill or a general
cross-process supervisor.

## 15. Faults and corrections

Early QEMU runs captured a real context-resume failure with
`-d int,cpu_reset -no-reboot -no-shutdown`: the worker reached completion
signaling, then the machine faulted before the joiner resumed. The preserved
debug trace showed an invalid resumed frame, including a #UD (vector 6)
followed by a page fault; the page-fault record included CR2
`0x3efbb078`. The failure occurred after completion signaling and was traced
to the saved context retaining the switch-call return-address layout while
the restore path jumped to the saved instruction pointer. The current stack
was not freed prematurely.

The narrow corrections were:

- AMD64 initial stack-frame alignment and direct-jump entry layout;
- MinGW/Windows x64 register ABI selection in the context-switch assembly;
- saved RSP/RIP restoration so the resumed frame skips the assembly return
  address exactly once;
- finite-zero Event polling versus an infinite wait with zero nanoseconds; and
- bounded scheduler wake selection when another worker becomes runnable.

The final QEMU run produced no exception/reset marker and reached `ALL_PASS`.
Fault logs remain under each `test-run-*` and `smoke-*` artifact directory.

## 16. Hosted regression status

PASS after the corrections:

- hosted generic native thread suite;
- scheduler-wait and Event suites;
- Native Event smoke;
- inactive NativeAOT thread adapter probe;
- generic native ELF smoke and runtime coupling checks;
- normal AMD64 UEFI build; and
- experimental hosted Native ELF build.

The successful QEMU smoke invocation was
`smoke-20260716-070757`; it also validated the parser and bounded timeout path
with the explicit normal/test kernel artifacts.

## 17. Managed regression status

The non-allocating managed proof, single-allocation proof, bounded 64 KiB and
4 KiB static proofs, runtime-pack state smoke, and host-log artifact smoke
passed. Allocation counts remain 234 for the 64 KiB proof and 14 for the 4 KiB
proof; collection remains disabled.

Two repeated execution proofs still fail on the pre-existing PE import
assertion mismatch. A bounded single-allocation repetition reproduced
`0xC0000374` once, while later repetitions passed or showed an unresolved
`0xC0000005`. These failures are in the managed proof/artifact path and have
no QEMU serial or lifecycle correlation; they are preserved as existing
managed regressions rather than attributed to native-thread work.

## 18. Remaining limitations

The validation is single-CPU AMD64 TCG only. The static bare-metal TCB pool,
bounded embedded stacks, PIT timer resolution, lack of guard pages/dynamic
stack growth, no wait-many, no SMP, no priority/affinity, and no forced
termination remain. The test does not initialize Workstation GC, start the
NativeAOT finalizer thread, or exercise managed ThreadStore attachment,
GC suspension, or collection.

## 19. Decision outcome

Outcome A — bare-metal native thread lifecycle validated. All required
bounded native lifecycle cases passed, and no scheduler/Event regression was
found. The unrelated managed proof failures remain documented above.

## 20. Exact next experiment

Return to the NativeAOT GC initialization call order. Implement and separately
prove the next generic platform primitive required before GC startup, starting
with GC virtual-memory reserve/commit/decommit/release (with critical sections
kept as a separate prerequisite). Do not initialize Workstation GC or start
the real finalizer thread until that primitive and the remaining GC blockers
are independently validated.

# QAC3B-R1 live QAC evidence

Captured on LEON2_RDT, 2026-10-20. Times below are UTC unless marked otherwise.

## Server development launch

The original `scripts/run-qemu-x86-with-build.bat` completed `kernel/build-x86.bat`, reported the pinned Mbed TLS profile verification and compile probe successful, then invoked the original QAC PowerShell runner. The runner's `qac status` preflight reported QAC 2.0.0 online. No QEMU was launched outside QAC.

QAC granted development reservation `9dc21cb291ec4ab5aa5a63f91f216adb` for `guideXOSServer` at `2026-10-20T21:56:36.6933578Z`. The verified owner was QAC CLI PID 23188 (owner process start `21:56:36.5336908Z`). QAC launched child PID 3476 at `21:56:37.1867471Z`, executable `C:\Program Files\qemu\qemu-system-i386.exe`, and recorded the child as `Running` with `ownerLiveness=Verified`.

The child was directly parented by PID 23188. Its command line preserved the launcher's original arguments exactly:

```text
-machine pc,usb=off -kernel build\x86\bin\kernel.elf -m 128M -vga std
-serial stdio -display gtk -vnc :0 -no-reboot -no-shutdown -d guest_errors
```

Serial output from the first guest attempt was:

```text
[GXOS-BUILD] identity=GXOS-LARGE-FILE-PASTE-TRACE-V1 probe=GXOS-LFPASTE-20260726-02
[KERNEL] guideXOS kernel_main entered
[KERNEL] ERROR: No valid boot method detected, halting
```

That result led to the cdecl argument-order correction in `kernel/arch/x86/boot.asm`, documented in `toolchain-diagnostics.md`. This first child ran the previous artifact; it is not evidence that the corrected artifact boots.

The WPF dashboard was visually observed with the live `guideXOSServer` development row, verified owner, QEMU child PID 3476, executable identity, session duration, coordinator availability, and empty queue. Its screenshot included a Windows Firewall network-access prompt over part of the dashboard. The prompt was not acted on. The dashboard accessibility tree also exposed the full active reservation and QEMU identity. A later live QAC status observed a foreign `qemu-system-x86_64.exe` PID 6156; a subsequent census no longer listed it. No process-specific action was directed at it. Windows process-audit logs were unavailable, so its exit cause was not independently attributed.

## Live scheduler checks with childless reservations

These reservations used only `qac request`; none launched QEMU.

| Reservation | Mode | Observed transition |
| --- | --- | --- |
| `4da9c44b96934402bb8309d5b9841b98` (`QAC3B-R1-DevHolder-A`) | development | Granted at `21:58:40.3569431Z`; released by QAC CLI at the authenticated user's request. |
| `af89b5c826ed4d43ab25d6305c2129e3` (`QAC3B-R1-DevQueued-C`) | development | Waited at position 1 while both development slots were occupied; granted at `21:58:50.1851498Z` after holder A was safely released; later safely released. |
| `b29678f69cf1417d93a6a35abd3c7ae5` (`QAC3B-R1-AcceptanceHolder`) | acceptance | Waited at position 1 behind active development reservations. |
| `66fd4cb83c744c53b51e5669d77abccc` (`QAC3B-R1-DevQueued-D`) | development | Waited at position 2 behind the acceptance barrier, then was cancelled while queued; QAC reported `Cancelled` / `Cancelled by client`. |
| `b05907c90a054496ab080f271ffac7f1` (`QAC3B-R1-DevQueued-E`) | development | Remained at position 2 after holder C released a development slot, behind the waiting acceptance request; then was cancelled while queued. |

The acceptance and E reservations were also cancelled after QAC detected a foreign QEMU and reported that acceptance admission was blocked. The foreign process was left alone. All childless test-holder processes exited and QAC reported zero queued/blocked reservations after test cleanup. The current Server QEMU reservation was not released by these queue tests.

## Shutdown status

The documented Ctrl+C path was tried once on the original launch terminal; the QEMU child remained live and QAC continued to report its verified ownership. No process-name kill, process-tree termination, or QAC release override was used. The Windows Firewall prompt is awaiting the desktop owner's decision before the QEMU window can be used for the remaining shutdown and dashboard-independence checks.

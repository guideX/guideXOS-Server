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

That result led to an initial cdecl argument-order correction in `kernel/arch/x86/boot.asm`, documented in `toolchain-diagnostics.md`. This first child ran the previous artifact; it is not evidence that the corrected artifact boots. The test-owned QEMU window was later closed and QAC reported the managed child exited and released. The original batch shell had a pending `Terminate batch job (Y/N)?` prompt from an earlier Ctrl+C attempt; after QAC had released the child, answering `Y` ended that shell with exit code 1. This first attempt is not evidence of a clean launcher exit.

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

## Corrected live development run and dashboard

An integrated retry with the first cdecl-order correction obtained development reservation `3ef4482f78e445a59f0b43e8b60ff060` at `2026-10-20T22:13:53.1633091Z`. QAC CLI PID 29664 launched `C:\Program Files\qemu\qemu-system-i386.exe` child PID 29304 at `22:13:53.2015078Z`; the original arguments were unchanged and QAC verified the owner. The guest still reported `No valid boot method detected`, which led to the EAX clobber finding documented above. Closing this test-owned QEMU window produced child exit code 0 at `22:14:55.2251854Z`; QAC reported `Released` with reason `Managed child exited`, and the launcher completed with exit code 0. This was a genuine QAC lifecycle pass but not a guest boot pass.

The final integrated launcher rebuilt the x86 entry code and obtained development reservation `590747c0a9b846e3a2d6b34a97151c5a` at `2026-10-20T22:16:01.5411197Z`. QAC owner/CLI PID 21296 had verified liveness; QAC launched direct child PID 24224 at `22:16:01.5776338Z`, executable `C:\Program Files\qemu\qemu-system-i386.exe`, with `ownerLiveness=Verified`. The process census confirmed parent PID 21296 and the exact original command line:

```text
-machine pc,usb=off -kernel build\x86\bin\kernel.elf -m 128M -vga std
-serial stdio -display gtk -vnc :0 -no-reboot -no-shutdown -d guest_errors
```

The guest's real serial output included:

```text
[GXOS-BUILD] identity=GXOS-LARGE-FILE-PASTE-TRACE-V1 probe=GXOS-LFPASTE-20260726-02
[KERNEL] guideXOS kernel_main entered
[KERNEL] Boot method: Multiboot
[KERNEL] Framebuffer source=Multiboot framebufferCount=0 index=0 status=canonical base=0000000000000000 size=0000000000000000 geometry=0x0 pitch=0 bpp=0 format=Unknown
```

The QEMU display showed the existing `guideXOS Kernel` text console and `Framebuffer not available - text mode only`. The kernel reached its Multiboot path and visible VGA text output; no fatal guest error was observed after the earlier boot-method failure was fixed. Framebuffer data was not available, so this run qualifies the real text-console boot path, not a graphical desktop. The screen capture is `guest-boot-console.jpg`.

The WPF dashboard screenshot `dashboard-development-running.jpg` visually showed `guideXOSServer`, mode `development`, state `Running`, queue `0`, verified owner `LEON2_RDT\guideX (PID 21296)`, child PID `24224`, session duration, coordinator Online, and the managed executable identity. The dashboard was minimized during the live run. QAC status and `inspect` still reported the coordinator Online, reservation Running, owner verified, and PID 24224 alive. Reopening the dashboard showed the current row and duration. No Windows Firewall or security prompt was acted on; no such prompt was present during the corrected run.

The test-owned QEMU window was closed through its own GUI close button after confirming its window and process identity. QAC recorded child PID 24224 exit code 0 at `2026-10-20T22:17:17.6480732Z`, released reservation `590747c0a9b846e3a2d6b34a97151c5a` with reason `Managed child exited`, and the integrated launcher returned exit code 0. The final CLI status/list/queue reported coordinator Online, zero QEMU, zero foreign QEMU, zero reservations, zero managed sessions, and zero queued reservations. The WPF screenshot `dashboard-released.jpg` visibly showed `QAC session completed: Managed child exited`, all four counts at zero, and an available acceptance lane.

A later read-only census at `2026-10-20T22:19:30.9456814Z` found a new foreign `qemu-system-x86_64.exe` PID 1940 from `D:\dev\guideXOSServer_REXX`, using headless display, no guest networking, and its own loopback QMP port. The WPF dashboard screenshot `dashboard-later-foreign-qemu.jpg` showed Foreign QEMU 1, Managed sessions 0, Queued 0, Blocked 0, and acceptance blocked by the foreign process. This later process is not the released Server child; no action was sent to it. Its current census does not indicate a stale managed reservation.

## Foreign-process handling

The earlier foreign QEMU PID 6156 disappeared between censuses. Windows process-audit logs were unavailable, so its exit cause is not independently attributed. No process-specific termination or control was directed at that PID. During the final corrected preflight, a separate headless `guideXOSServer_REXX` QEMU PID 20672 was visible as foreign; it was not controlled or targeted and was absent from a later census while the managed Server guest remained active. Its exit cause is also not independently attributed. No foreign process was selected or targeted; all QEMU window-close actions above were for the test-owned `qemu-system-i386.exe` child verified by PID and QAC reservation.

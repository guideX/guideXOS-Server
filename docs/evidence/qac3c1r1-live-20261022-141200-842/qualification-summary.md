# QAC3C1R1 live qualification — Outcome A

Date: 2026-10-22 (America/Los_Angeles). The preceding QAC3C1 Outcome B record remains intact as history; this live continuation completes the mandatory qualification.

## Repository and artifact state

- QAC: `main`, starting HEAD `9a92811bf745200153c0528cf338b8712584d964`, clean before qualification.
- Server: `main`, starting HEAD `396a358ec60ae513240c44f5871e95a588b5bc03`; two local commits ahead of `origin/main`, including the existing QAC integration commit `77cd7db...` and later PacMan diagnostic commit. No tracked modifications existed at start. The 808 pre-existing untracked entries were preserved.
- Kernel: `D:\dev\guideXOSServer\kernel\build\x86\bin\kernel.elf`; 2,478,204 bytes; ELF32, little-endian, ET_EXEC, EM_386. SHA-256 before and after: `3566D010B087DD9309593ACAF59D6CF2CD48B15BCBDBC0C39F207AFB0774FC00`.
- No kernel build or regeneration was run. The actual batch launcher printed that it selected the prebuilt kernel and does not build or replace artifacts.
- `tests/qac_development_launcher_contract_test.ps1`: **80/80 assertions passed**, exit 0. Full output is `contract-test-80-assertions.txt`.

## Admission and queue qualification

QAC 2.0 was Online before testing with no reservations, queue, blocked requests, managed or foreign QEMU, and an available acceptance lane. Available memory was about 47 GB and free D: storage about 60 GB. VNC `:0` (ports 5900) and the test QMP port 44441 had no listener before launch.

A bounded childless acceptance fixture `6560ae4deaa74e259c13b3b4a1f8e56d` was granted. A separate development request `a5311434ed7947728eb4987123746c1d` waited at queue position 1 and was cancelled by QAC; its request client exited with code 1 after QAC reported `Cancelled`. No guest was started for either fixture. The actual prebuilt launcher then requested `guideXOSServer` development reservation `c3575e3bf6a249ddb6aba9326691a22b`. While the acceptance barrier was active, it remained Waiting at position 1 and QEMU census stayed at zero. Releasing only the test-owned childless acceptance fixture admitted that same launcher request. No other project's reservation existed or was touched.

## Real prebuilt QEMU run

- Reservation: `c3575e3bf6a249ddb6aba9326691a22b`, `guideXOSServer`, development. The reservation record's `grantedAtUtc` is `2026-10-22T21:14:04.3722084Z`; QAC's `ReservationGranted` journal event is `2026-10-22T21:14:04.3722103Z`.
- QEMU: `C:\Program Files\qemu\qemu-system-i386.exe`, PID 1484, start `2026-10-22T21:14:04.3835797Z`. QAC CLI PID 20264 was its verified direct parent. QAC recorded the same executable identity and start time.
- Original QEMU settings and argument boundaries were present in the real command line: `-machine pc,usb=off -kernel kernel\build\x86\bin\kernel.elf -m 128M -vga std -serial stdio -display gtk -vnc :0 -no-reboot -no-shutdown -d guest_errors`. Only the explicitly test-scoped loopback QMP argument `-qmp tcp:127.0.0.1:44441,server=on,wait=off` was appended.
- Serial evidence: `[KERNEL] guideXOS kernel_main entered` followed by `[KERNEL] Boot method: Multiboot` and framebuffer diagnostics. The QMP framebuffer capture displays `guideXOS Kernel` and `Framebuffer not available - text mode only`; this qualifies the established kernel/text-console boot, not a graphical desktop. See `guest-text-console.png` and the raw QEMU PPM dump.
- QMP reported `running`. The guest was shut down through the test-only loopback QMP `quit` command; QEMU returned exit code 0 at `2026-10-22T21:16:28.2672534Z`. QAC's `SessionCompleted` event recorded `Managed child exited` at `2026-10-22T21:16:28.3255907Z`, then the reservation state was verified `Released`. The CLI/launcher reported managed run exit code 0 and the post-run kernel hash unchanged. The 80-assertion contract suite also verifies zero and nonzero exit propagation through the runner/batch boundary.
- The dashboard was visually observed during the real run and after release. It showed the verified guideXOSServer development row, live child identity, duration, coordinator, queue state, and completion. Minimizing then restoring the existing dashboard left the same child/reservation Running and QAC Online. At completion, the dashboard showed no sessions or QEMU and “Managed child exited.” See `dashboard-observations.txt`.

## Cleanup and disposition

Final QAC status: Online; managed 0; queued 0; blocked 0; foreign QEMU 0. QEMU PID 1484 and test listeners were gone. Both test-owned fixtures reached Cancelled/Released; the prebuilt reservation is Released. No Windows service, Firewall setting, scheduler policy, foreign process, or other repository was changed. QAC2 recovery evidence was reused; no coordinator restart or crash test was performed because it was unnecessary and would disrupt the owner’s live coordinator.

**Outcome A — QAC3C1 accepted.** QAC `.phase` marks QAC3C1 accepted, overall QAC3 open, and QAC3C2 next; no QAC3C2 work began.

# Developer Studio QAC acceptance launcher contract

The paired Developer Studio repository's focused Phase 28Q and Phase 28O acceptance runner delegates to this repository's `scripts/smoke-compiler-bootstrap.ps1`. This script owns the final QEMU spawn, so it passes the already resolved QEMU executable and existing argument array directly to the QAC CLI:

```text
Developer Studio runner → qac.exe → qemu-system-x86_64.exe
```

The invocation is `qac run --project DeveloperStudio --mode acceptance -- <qemu> <arguments...>`. QAC obtains exclusive admission before it starts the direct QEMU child. The bootstrap's QEMU firmware, machine, memory, disk, serial, display, and device arguments remain unchanged; the QAC path adds one loopback QMP endpoint for graceful shutdown. Windows argument quoting preserves boundaries without a shell or intermediate process.

## Operator contract

- Build or install the Windows QAC CLI apphost, start the interactive coordinator under the same Windows account, and keep the coordinator available for the run. No service is installed or started by this script.
- Pass `-QacCliPath <path>` to the Developer Studio runner, set `GUIDEXOS_QAC_CLI`, or place `qac.exe` on `PATH`.
- Admission wait is bounded by `-QacAdmissionTimeoutSeconds` (default 1800 seconds). The existing `TimeoutSeconds` starts when the QAC CLI reports the actual QEMU child.
- Queue statuses and reservation ID are written to the existing host trace. QAC's child PID and verified executable identity are recorded when available. QEMU stdout/stderr remain in the per-boot evidence files, and serial/debugcon evidence and guest acceptance markers keep their existing roles.
- Before admission, cancellation uses `qac cancel`. During a guest run, the existing harness marker, runtime timeout, or Ctrl+C requests QMP `quit`; the harness waits for QAC's child exit and verified reservation release.
- Coordinator unavailability, foreign-QEMU blocking, failed launch, timeout, cancellation, or an unverified release fails acceptance. There is no direct-launch fallback. Foreign QEMU processes are left untouched.
- If shutdown or release cannot be verified, the script preserves the QAC lifecycle and boot evidence and skips ESP restoration while QEMU might still have the staged disk open. It does not forcibly terminate QEMU.

QAC3A covers only the focused Developer Studio Phase 28Q and Phase 28O acceptance launchers. The general guideXOS Server development QEMU workflow remains a QAC3B candidate.

Run the deterministic integration helper checks with Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\test-developer-studio-qac-acceptance.ps1
```

The QAC repository's automated suite exercises the coordinator and CLI lifecycle, including foreign-QEMU blocking, queued cancellation, launch failure, direct-child identity, output argument handling, child exit propagation, and conservative recovery. Real integration qualification evidence is maintained in the QAC repository's `docs/phase-validation.md` QAC3A section.

## Live qualification disposition — Outcome B

The QAC3A integration was exercised on Windows host LEON2_RDT on October 19, 2026. The Developer Studio Phase 28Q launcher reached QAC and obtained the exclusive reservation before QEMU started. QAC launched `C:\Program Files\qemu\qemu-system-x86_64.exe` directly as its child (first run: QAC CLI PID 19080, QEMU PID 15720, reservation `3474286503ea4307be885965e790b358`). Existing QEMU arguments and evidence destinations were retained; the host trace records the QAC status and the serial file includes the existing Phase28Q guest marker.

That real run exceeded `-MaxRuntimeSeconds 360`. The harness sent QMP `quit`, QAC verified QEMU exit code 0, and the reservation was released. The Phase28Q marker was visible after shutdown flushed the serial file, but the harness had already timed out while QEMU was running. The run is incomplete and is not an acceptance pass. A second real boot was interrupted with Ctrl+C; QAC later reported the registered child exited, but that attempt is also not a pass. No Phase28O live run was performed.

The post-observation helper rejects timeout, cancellation, coordinator failure, recovery-required state, unverified child exit, or unverified release as success. Its deterministic checks pass; the new timeout guard has not yet been requalified by a full guest boot. The run evidence remains under `%LOCALAPPDATA%\Temp\guidexos-phase28g-21e615af43f0475c9375f2dd7260fb41`. The paired QAC repository's `docs/phase-validation.md` records the full dashboard, queue, process identity, foreign-QEMU, cancellation, and cleanup evidence.

This is an Outcome B checkpoint. The Server branch remains `v0.5_DEVELOPER_STUDIO`; QAC3A and Developer Studio phase markers were not advanced. QAC3B should review the general Server development QEMU workflow separately after real Phase 28Q and Phase 28O qualification completes.

## QAC3A-R1 requalification follow-up — October 19, 2026 (Pacific)

The Server acceptance harness now reads serial output incrementally from a shared file handle and measures the 360-second guest budget from QAC's verified direct-child start timestamp. It logs the marker observation time, deadline, byte count, read errors, child exit, QMP shutdown, and reservation release. A deterministic fixture reproduces the old failure: `[IO.File]::ReadAllText` throws `IOException` while the serial writer is open, whereas the shared incremental reader observes delayed and split markers. The historical run did not record the old guest marker's emission time, so the exact pre-deadline versus post-deadline emission remains unknown.

The QAC marker selector now maps Phase 28Q to `DEVELOPER_STUDIO_PHASE28Q_PASS` and Phase 28O to `DEVELOPER_STUDIO_PHASE28O_PASS`. The old O path incorrectly treated `[KERNEL] Entering main loop (waiting for input)...` as an acceptance marker. The live O attempt below used that old selector; its guest trace had already emitted `DEVELOPER_STUDIO_PHASE28O_TIMEOUT` and `DEVELOPER_STUDIO_PHASE28O_FAILURE` before the main-loop line, so it is not a pass. The corrected selector prevents that generic line from satisfying QAC acceptance. The guest's Phase 28O timeout remains unresolved and was not changed.

The fresh Phase 28Q wrapper run passed across three fresh boots at `%LOCALAPPDATA%\Temp\guidexos-phase28g-71a6ed320e8d440490c6e653971c490c`. Each guest marker was observed at about 32.9–33.4 seconds, within the original limit and before child exit; QMP quit, QEMU exit 0, and verified release completed on all three. The original marker set and QEMU argument list remain unchanged. The Server helper suite passes 23/23 checks, covering Q/O marker mapping, shared live-file reads, split and late markers, queue wait, cancellation/QMP, coordinator/admission failure, recovery, exit propagation, and release verification.

The single live Phase 28O attempt is preserved at `%LOCALAPPDATA%\Temp\guidexos-phase28g-d96f5a768f96473b9493ccb5cbdbac41`. It reached the workspace and debugger-start stages, then emitted `DEVELOPER_STUDIO_PHASE28O_TIMEOUT step=21` with the debug controller still Running and zero runtime breakpoints. It then emitted the explicit O failure markers. The outer Developer Studio wrapper exited 1 because the required materialization, breakpoint, Watch, terminal, cleanup, and O PASS markers were absent. QMP shutdown and verified release succeeded, but the generic main-loop marker and zero exit code do not qualify O. The corrected exact-marker selector passed deterministic tests but was not used in this live attempt; no second O boot was started after the explicit guest failure.

The final-only post-exit serial check now uses the same exact Q/O PASS marker as live observation. Host trace output-drain fields now record the QAC line readers' actual EOF state; earlier QAC live traces reported zero because they read unused `ReadToEndAsync` task variables, even though the QAC loop had waited for both line readers to finish. This is a diagnostics correction; it does not change an acceptance result and was validated by PowerShell parsing and the helper suite without another guest boot.

Outcome B remains in force. QAC3A is open until Phase 28O completes its original acceptance criteria through QAC within policy. QAC3B and phase-marker advancement remain deferred.

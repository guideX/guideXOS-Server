# C181R1 Reset Investigation and Guest Qualification

**Status:** Outcome D — QAC acceptance is blocked. The reset cause is classified and the corrected runner is ready, but the corrected focused and proof-disabled production lanes could not be started safely.

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `e3b2457c047481be30255c4be2a6c5a4a9a35e77`

## Reset failure boundary

The earlier focused attempt did not establish a guest kernel reset defect. In attempt 13, QEMU was started with both `-no-reboot` and `-no-shutdown`. QMP accepted `system_reset`, but the QMP event log recorded a host-initiated `SHUTDOWN` with reason `host-qmp-system-reset`, followed by `STOP`. `query-status` returned `shutdown` and `running=false`. There was no QEMU `RESET` event, second firmware or bootloader sequence, second kernel initialization, managed runtime initialization, or second input-loop readiness marker.

The cause is the runner's `-no-reboot` argument. It makes QEMU stop after guest reset instead of restarting the virtual machine. `-no-shutdown` kept the QEMU process available, which explains why QAC later exited cleanly after the runner sent QMP `quit`. QMP acknowledgement alone did not prove that a reset or second boot occurred.

The corrected runner omits `-no-reboot`, retains `-no-shutdown`, logs QMP asynchronous events, requires a `RESET` event, records `query-status` immediately after reset, requires QEMU state `running`, and waits for fresh readiness markers after the pre-reset serial offset. The previous attempts remain immutable.

## Baseline and reset policy

C180's accepted proof-disabled production evidence records three independent ordinary boots using one unchanged ramdisk. It contains no `system_reset` request or QMP `RESET` event. C180 therefore demonstrates ordinary cold-boot stability but does not establish warm-reset support or a pre-existing warm-reset failure.

C181R1 uses a focused same-process reset sequence to investigate the runner defect. The proof-disabled production lane uses three separate QAC-launched QEMU processes so each result is an ordinary cold boot with a fresh isolated ESP. These cold boots do not substitute for the focused reset result.

## Production candidate provenance

The preserved `out/dotnet/c181-managed-application-residency/production-boots/boot1/serial.log` contains `C155-RESTORE-TEST result=FAIL`, but it was written at 03:57 on 2026-10-20. The final proof-disabled managed build and staged candidate were created at 04:01–04:03. The warning belongs to the earlier media set and is not accepted as evidence about the final QAC candidate.

The final production candidate is staged at `out/dotnet/c181-managed-application-residency/media/esp-production-c181-qac`. Its source hashes are:

| Media | SHA-256 |
| --- | --- |
| `kernel.elf` | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |
| `ramdisk.img` | `B2712572C3623D7816F6C6FF025794F3214ABFC8699FA1671FDA4B0F39526AB` |
| `EFI/BOOT/BOOTX64.EFI` | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

Each fresh production run records and checks these hashes against its isolated boot copy.

## Runner changes

`scripts/dotnet/run-c181-managed-application-residency.ps1` now supports a focused three-boot reset run and a one-boot production scenario. Its QAC preflight rejects an active lease. The runner captures candidate and isolated-media hashes, reset events and state, per-boot serial/QMP logs, surface identities, resize/lifecycle evidence, and screenshot hashes.

`scripts/dotnet/c181-qmp.psm1` records asynchronous QMP events around command replies and makes a missing reset event explicit. `scripts/dotnet/test-c181-qmp-event-capture.ps1` covers both observed and missing `RESET` events.

`scripts/dotnet/run-c181-production-coldboots.ps1` runs the three production scenarios as separate QAC reservations, checks that each reservation is released, compares candidate hashes across boots, scans the kernel/ramdisk/EFI candidate and each boot's serial log for the established proof-only controls and diagnostics, and writes a combined manifest.

No product, kernel, ABI, or media source changes have been made for R1.

## Qualification evidence

The accepted C181 host baseline remains **38 suites / 930 cases PASS**. The C181 production and proof-control freestanding kernel builds were already successful; product sources remain unchanged in R1.

The earlier focused attempt-11 boot-1 evidence is preserved. It showed five managed residents and Notes close/reopen. The old report lists `8C35EAD8B32A9DB7A0D61AAF368E37BDF58F064FA071EA5C007E9AEC9C4ED093` for the Task Manager screenshot; recomputing the preserved artifact gives `DF21C3F0407CC38BC9CB67D27A7FAF83B21085AE35F58B90701015BDAB3E1BB2`. The screenshot artifact itself was not modified. Attempt 13 preserved the reset failure boundary and QAC cleanup evidence.

Two R1 runner development attempts are also preserved under `E:\guideXOS_C181R1_evidence\guest-runs`. The first stopped before QAC because `Get-MediaHashes` was declared after first use, so it created no QEMU child or QAC reservation. The second acquired reservation `4dbe217b3e03496381adf154f27b522d`, launched QEMU PID 23516, reached five residents and a Task Manager `n=5` refresh, then exposed that the Settings taskbar route does not emit the C162 instance marker. QAC observed QEMU exit 0 and released that exact reservation. Its retained failure screenshot (`failure-boot1.ppm`, SHA-256 `57611B6C8BD3B8A16F445628A7DAD37AA855036319FFD8A5972A1E92D8675C1B`) shows the Settings window visibly active. The runner now uses the existing Start-menu activation path for apps with C162 identity markers and validates Settings focus through its visible titlebar screenshot.

The production candidate's current direct scan finds none of the forbidden proof-only markers in its kernel, ramdisk, or EFI loader. The QAC orchestrator now repeats that scan and hashes each scanned file before the production boots. The guest lanes remain unrun after the R1 runner correction; there are no R1 focused reset or production cold-boot reservation identifiers or results to report.

At 2026-10-20 15:58:57 UTC, QAC detected unmanaged/foreign QEMU PID 26740 while a separate verified `guideXOSServer` acceptance reservation (`1951474742474bc9a6a0d222b41a491e`, child PID 18800) was still active. QAC marked the reservation degraded and blocked acceptance because exclusive conditions were compromised. The foreign QEMU was left untouched and no C181 guest was launched. By the 16:00:25 UTC census, the foreign process was gone, but the same verified acceptance reservation remained active and the required empty-reservation preflight still failed. Both supported QAC snapshots are preserved: `E:\guideXOS_C181R1_evidence\qac-conflict-census-20261020T1559Z.txt` (SHA-256 `C632740AF87AE2E8D711148A4028CE77594DD8DF30F4319A228AD04C104213B1`) and `E:\guideXOS_C181R1_evidence\qac-active-census-20261020T1600Z.txt` (SHA-256 `A623BD33EFA0FD35F69B178F5E30E1DE0DCE15C93AAE781279AB20DC2C01C7AC`).

## Final gate status

| Gate | Result |
| --- | --- |
| Attempt-13 reset boundary | PASS: `-no-reboot` made QEMU enter host-QMP `SHUTDOWN`/`STOP`; no `RESET` event or second boot occurred. |
| C180 warm-reset comparison | Unresolved: the accepted C180 evidence contains three cold boots and no `system_reset` or `RESET` event. |
| R1 runner correction | PASS: removes `-no-reboot`, logs reset events and status, and requires fresh post-reset readiness. QMP event-capture regression script passes. |
| C181 host regression | PASS from the preserved baseline: 38 suites / 930 cases, including 250 exhaustion/recovery operations and 250 stale-generation rejects. No product source changed in R1. |
| Freestanding builds | PASS from the preserved C181 evidence: production and proof-control AMD64 kernel targets compiled. No kernel source changed in R1. |
| Proof-disabled candidate | PASS for provenance and static proof-marker scan: the staged kernel, ramdisk, and EFI loader match their recorded hashes and contain none of the selected proof-only controls/diagnostics. |
| Focused three-boot guest lane | NOT RUN after correction: QAC did not grant an exclusive lane. |
| Three ordinary production guest boots | NOT RUN: QAC did not grant an exclusive lane. |
| QAC availability | BLOCKED at 16:00:25 UTC: one verified acceptance owner remained active, so the required empty-reservation preflight still failed. QAC had also reported foreign PID 26740 at 15:58:57 UTC; it disappeared from the later census and was never touched. |

Outcome D is required because both guest acceptance gates remain outstanding for an external QAC ownership conflict. Historical and development evidence confirms five concurrent residents, but it does not replace the focused reset sequence or three production boots. The bounded next step is to rerun the focused lane and then the production cold-boot orchestrator after QAC reports no active reservation and no foreign QEMU. Do not begin C182 automatically.

## Protected workspace state

The modified checkout ramdisk `ESP/ramdisk.img` remains preserved at its pre-task SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The existing untracked `samples/host/ManagedUiRegression/obj/` directory is also preserved.

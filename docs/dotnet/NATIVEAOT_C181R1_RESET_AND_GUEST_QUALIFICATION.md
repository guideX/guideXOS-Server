# C181R1 Reset Investigation and Guest Qualification

**Status:** Outcome D as of 2026-10-20 22:01 UTC — the reset runner correction has produced one successful same-process reset and second boot, but the focused lane is incomplete and production boots remain outstanding. QAC currently has a managed development reservation and a foreign QEMU process, so the C181 acceptance preflight is not empty.

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

The production candidate's current direct scan finds none of the forbidden proof-only markers in its kernel, ramdisk, or EFI loader. The QAC orchestrator now repeats that scan and hashes each scanned file before the production boots.

### R1 continuation — 2026-10-20

QAC was used for all three focused continuation runs below. Each run received an exclusive `guideXOSServer` acceptance reservation, ran its QEMU child, exited with code 0 after the runner's graceful QMP quit, and released the same reservation. None is a complete focused acceptance result.

| Run | Reservation / QEMU | Result |
| --- | --- | --- |
| `focused-r1-reset-repro14-qac-sequence` | `2604d5feacef42acb664f374e090573a` / PID 19844 | Boot 1 established five residents and Notes close/reopen. QMP emitted `RESET`; a second input-loop readiness marker confirmed boot 2. Boot 2 passed Settings Apply/Reset, Explorer `/system/apps` selection/refresh, Notes save, Calculator `7×8=56`, and Task Manager `n=5`. The lane stopped at Settings resize-corner instrumentation because it had staged the proof-disabled kernel (`22BC4AFE…22154AF`), which has no `C171/C172/C175` proof markers. QAC released the reservation. |
| `focused-r1-reset-repro15-qac-sequence` | `a1cefb107ba74b1d878d773b6e22e25b` / PID 13980 | The full proof ESP launched, but its proof-controlled managed app expected the C166 `proof1.bin` selector and failed with `PROOF-SCENARIO-FAIL CONFIG-READ`; no resident Notes window existed for pointer calibration. The runner timed out before interactions. QAC released the reservation. |
| `focused-r1-reset-repro16-qac-sequence` | `696ecaae366949d783593f185212a99c` / PID 25576 | The isolated ESP combined the C181 proof kernel (`B5D5B47B…21B4C13A`) with the focused app/ramdisk. The kernel reached the input loop, but its startup Notes launch returned `managed-failed`; no app-local `C138` pointer marker was produced under the center cursor. Pointer calibration timed out before interactions. QAC released the reservation. |

The first run's root `serial.log` SHA-256 is `1240A19B2D563745AF6B9CFB9F8B0EB3FB011282C0DB62940AA99CF09010E03B`; `qmp.log` is `32DB689CEC0447A5D6FA4E00D6D950311217CD876F4DA6884CE3246492800D8A`; QAC stderr is `041D1938400D7575F3784F2FBE06F420E37D48AC4E83C713757E5D26B30CCB8F`. Its second-boot failure screenshot is `103113F88DE3988E79F5CE9F172BC1E2886660D9BCF4412A16061251328417AE`. All artifacts remain under `E:\guideXOS_C181R1_evidence\guest-runs\focused-r1-reset-repro14-qac-sequence`.

Repro16's root `serial.log` SHA-256 is `88334B6F23E57526024A076445532EE9A49AE9BBCB97B8BC6A03309D18736BAE`; `qmp.log` is `F1777C4C27C2F2573299003A7EE3F72587A488D4DFC944B8CEBC7E2524FBF16B`; QAC stderr is `5FF6D039D93E4724E55D97A52574F4B1521E4221DC44A1DDA63B8A4B9FB5FF77`. Its failure screenshot is `FD26E795D530AE3F19AE2E0E33FA8812D8CD053921A8B58D8CCF1D2C55DFE3A9`. The isolated kernel and ramdisk hashes are `B5D5B47BEE94085D05404A9851919F08B55862E8ACDA053034C2113021B4C13A` and `5CC5C0E3D8D9677CE03CA9BBD339D35011970121E2DEF1A4B2C6F6780FD50CF2`. Artifacts remain under `E:\guideXOS_C181R1_evidence\guest-runs\focused-r1-reset-repro16-qac-sequence`.

The runner now composes the focused app/ramdisk payload with the proof kernel in the isolated per-run ESP. Repro16 booted this combination but showed that app-local `C138` output cannot be required for pointer calibration: the pointer was centered over Task Manager, which does not emit that scrollbar marker. The runner now uses the QEMU VGA pointer's framebuffer-center start plus the one-pixel relative probe and records any app-local marker only as supplementary evidence. This calibration change has not yet been guest-validated.

The foreign REXX QEMU (PID 22636) reported at 21:52:57 UTC exited before repro16. After repro16 released its acceptance lease at 21:56:36 UTC, QAC admitted a separate `guideXOSServer` development reservation `9dc21cb291ec4ab5aa5a63f91f216adb` (child PID 3476, `qemu-system-i386.exe`). At 22:01:16 UTC, QAC still reported this reservation and unmanaged foreign QEMU PID 6156 from `D:\dev\guideXOSServer_REXX\build\rexx\rx20\qemu-81021805facf4277a3e4c73f751f6edf`. Neither process was touched. The census is preserved at `E:\guideXOS_C181R1_evidence\qac-blocked-census-20261020T2200Z.txt` (SHA-256 `96B13F6F4B0BC2A8716F21491D5EA085CAA2FF2ECBB84DAE4D4DD00B54C24A9A`). The C181 runner's required empty census is not met.

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
| Focused three-boot guest lane | INCOMPLETE: repro14 confirmed one reset/second boot and passed Boot 2 app actions, then exposed a proof-kernel staging mismatch. Repro15 exposed a proof-app selector mismatch. Repro16 booted the mixed media but pointer calibration timed out. The center-based pointer calibration change awaits guest validation. |
| Three ordinary production guest boots | NOT RUN: production acceptance is sequenced after focused completion. |
| QAC availability | BLOCKED at 22:01:16 UTC: managed development reservation `9dc21cb291ec4ab5aa5a63f91f216adb` and foreign QEMU PID 6156 from `guideXOSServer_REXX` are active. Neither was terminated. Earlier 15:58–16:00 UTC conflict remains historical and unchanged. |

Outcome D remains required because both guest acceptance gates remain outstanding. The reset finding is supported by a same-process QMP `RESET` event and fresh second-boot readiness, which identifies the original failure as the runner's `-no-reboot` option rather than a demonstrated C181 kernel defect. The focused lane still needs all three boots against the corrected isolated media, followed by three proof-disabled production cold boots. Resume only after QAC reports an empty census and no queued project request. Do not begin C182 automatically.

## Protected workspace state

The modified checkout ramdisk `ESP/ramdisk.img` remains preserved at its pre-task SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The existing untracked `samples/host/ManagedUiRegression/obj/` directory is also preserved.

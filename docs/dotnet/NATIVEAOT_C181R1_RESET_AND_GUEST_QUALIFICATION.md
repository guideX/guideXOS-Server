# C181R1 Reset Investigation and Guest Qualification

**Status:** Outcome A as of 2026-10-20 PDT — focused three-boot acceptance passed with two observed QMP resets, all three proof-disabled production cold boots passed, and QAC verified release after every run. A fresh post-run census reports QAC Online with zero QEMU processes, reservations, sessions, or queued work.

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `06231fdb9b59dcea9dba79c312af720f91310b63`

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

The focused runner now pins the C181 proof kernel and C181 proof app ramdisk, stages both into isolated media, uses framebuffer-center pointer calibration, and writes a safe one-boot manifest count. Its focused regression `scripts/dotnet/test-c181-runner-preflight.ps1` checks pinned media hashes/markers, proof-marker exclusions, reset and cleanup guards, isolated staging, and the Boot 3 width policy. A production Boot 3 diagnostic caught a runner-only width assumption: after Notes was resized to 560, a refocus still expected 600. The corrected runner validates the resized width before lifecycle actions, validates each fresh Notes generation at the documented 600-pixel default, and uses that measured width on the next close cycle. The same preflight verifies that the completed Calculator expression expects `op=0` and that a single-boot manifest count is safe.

No product, kernel, ABI, or media source changes have been made for R1.

## Qualification evidence

The accepted C181 host baseline remains **38 suites / 930 cases PASS**. The C181 production and proof-control freestanding kernel builds were already successful; product sources remain unchanged in R1.

The earlier focused attempt-11 boot-1 evidence is preserved. It showed five managed residents and Notes close/reopen. The old report lists `8C35EAD8B32A9DB7A0D61AAF368E37BDF58F064FA071EA5C007E9AEC9C4ED093` for the Task Manager screenshot; recomputing the preserved artifact gives `DF21C3F0407CC38BC9CB67D27A7FAF83B21085AE35F58B90701015BDAB3E1BB2`. The screenshot artifact itself was not modified. Attempt 13 preserved the reset failure boundary and QAC cleanup evidence.

Two R1 runner development attempts are also preserved under `E:\guideXOS_C181R1_evidence\guest-runs`. The first stopped before QAC because `Get-MediaHashes` was declared after first use, so it created no QEMU child or QAC reservation. The second acquired reservation `4dbe217b3e03496381adf154f27b522d`, launched QEMU PID 23516, reached five residents and a Task Manager `n=5` refresh, then exposed that the Settings taskbar route does not emit the C162 instance marker. QAC observed QEMU exit 0 and released that exact reservation. Its retained failure screenshot (`failure-boot1.ppm`, SHA-256 `57611B6C8BD3B8A16F445628A7DAD37AA855036319FFD8A5972A1E92D8675C1B`) shows the Settings window visibly active. The runner now uses the existing Start-menu activation path for apps with C162 identity markers and validates Settings focus through its visible titlebar screenshot.

The production candidate's current direct scan finds none of the forbidden proof-only markers in its kernel, ramdisk, or EFI loader. The QAC orchestrator now repeats that scan and hashes each scanned file before the production boots.

### R1 Repro14–16 evidence — 2026-10-20 (historical failures)

QAC was used for all three focused continuation runs below. Each run received an exclusive `guideXOSServer` acceptance reservation, ran its QEMU child, exited with code 0 after the runner's graceful QMP quit, and released the same reservation. None is a complete focused acceptance result.

| Run | Reservation / QEMU | Result |
| --- | --- | --- |
| `focused-r1-reset-repro14-qac-sequence` | `2604d5feacef42acb664f374e090573a` / PID 19844 | Boot 1 and the actual QMP `RESET` plus fresh Boot 2 readiness succeeded. Boot 2 passed Settings Apply/Reset, Explorer `/system/apps` selection/refresh, Notes save, Calculator `7×8=56`, and Task Manager `n=5`. The first failure was the Settings resize proof-marker assertion: this run had staged the proof-disabled production kernel (`22BC4AFE…22154AF`), so the required `C171/C172/C175` markers were absent. Runner/media selection defect; no reset or product failure was shown. QAC released the reservation. |
| `focused-r1-reset-repro15-qac-sequence` | `a1cefb107ba74b1d878d773b6e22e25b` / PID 13980 | Startup Notes failed before pointer calibration with `PROOF-SCENARIO-FAIL CONFIG-READ` and launch status `FFFFFFFE`. The isolated ESP used stale kernel hash `B5D5B47B…21B4C13A` and C166 proof ramdisk `2113211A…9AED7FEF`; that app expected its `proof1.bin` selector/configuration, which was not selected by this run. No Notes surface was created. Runner payload mismatch; no guest interaction or reset was reached. QAC released the reservation. |
| `focused-r1-reset-repro16-qac-sequence` | `696ecaae366949d783593f185212a99c` / PID 25576 | Startup Notes failed with launch status `FFFFFFFB` before creating a managed surface. The isolated ESP paired a stale C181 proof-kernel hash (`B5D5B47B…21B4C13A`) with the regular managed app payload (`5CC5C0E3…0FD50CF2`). The missing app-local `C138` marker was a consequence of Notes not launching, not the cause. Runner media mismatch; QAC released the reservation. |

The first run's root `serial.log` SHA-256 is `1240A19B2D563745AF6B9CFB9F8B0EB3FB011282C0DB62940AA99CF09010E03B`; `qmp.log` is `32DB689CEC0447A5D6FA4E00D6D950311217CD876F4DA6884CE3246492800D8A`; QAC stderr is `041D1938400D7575F3784F2FBE06F420E37D48AC4E83C713757E5D26B30CCB8F`. Its second-boot failure screenshot is `103113F88DE3988E79F5CE9F172BC1E2886660D9BCF4412A16061251328417AE`. All artifacts remain under `E:\guideXOS_C181R1_evidence\guest-runs\focused-r1-reset-repro14-qac-sequence`.

Repro16's root `serial.log` SHA-256 is `88334B6F23E57526024A076445532EE9A49AE9BBCB97B8BC6A03309D18736BAE`; `qmp.log` is `F1777C4C27C2F2573299003A7EE3F72587A488D4DFC944B8CEBC7E2524FBF16B`; QAC stderr is `5FF6D039D93E4724E55D97A52574F4B1521E4221DC44A1DDA63B8A4B9FB5FF77`. Its failure screenshot is `FD26E795D530AE3F19AE2E0E33FA8812D8CD053921A8B58D8CCF1D2C55DFE3A9`. The isolated kernel and ramdisk hashes are `B5D5B47BEE94085D05404A9851919F08B55862E8ACDA053034C2113021B4C13A` and `5CC5C0E3D8D9677CE03CA9BBD339D35011970121E2DEF1A4B2C6F6780FD50CF2`. Artifacts remain under `E:\guideXOS_C181R1_evidence\guest-runs\focused-r1-reset-repro16-qac-sequence`.

The runner now pins the focused C181 proof kernel and C181 proof app ramdisk into the isolated per-run ESP. Pointer calibration uses the QEMU VGA framebuffer-center start plus a one-pixel relative probe; app-local markers are supplementary. The corrected media pairing and calibration were subsequently exercised by the completed focused three-boot acceptance below. Repro14–16 share a runner artifact-selection problem, but their stopping points differed; none demonstrated a C181 product or kernel defect.

The foreign REXX QEMU (PID 22636) reported at 21:52:57 UTC exited before repro16. After repro16 released its acceptance lease at 21:56:36 UTC, QAC admitted a separate `guideXOSServer` development reservation `9dc21cb291ec4ab5aa5a63f91f216adb` (child PID 3476, `qemu-system-i386.exe`). At 22:01:16 UTC, QAC still reported this reservation and unmanaged foreign QEMU PID 6156 from `D:\dev\guideXOSServer_REXX\build\rexx\rx20\qemu-81021805facf4277a3e4c73f751f6edf`. Neither process was touched. The census is preserved at `E:\guideXOS_C181R1_evidence\qac-blocked-census-20261020T2200Z.txt` (SHA-256 `96B13F6F4B0BC2A8716F21491D5EA085CAA2FF2ECBB84DAE4D4DD00B54C24A9A`). A fresh read-only QAC 2.0.0 census at 22:03:06 UTC still showed that managed reservation and a foreign QEMU PID 19324 from `D:\dev\guideXOSServer_REXX\build\rexx\rx20\qemu-2940fe5dfe074a62adae40b5b53e8776`; QAC reported two QEMU processes (one foreign), one managed session, and no queued or blocked requests. This newer census is preserved at `E:\guideXOS_C181R1_evidence\qac-blocked-census-20261020T2203Z.txt` (SHA-256 `3638EA47829BE59CEBDF8E32BC8BE7374D8F308FD7781E6D5FDD10CC335904ED`). At 22:04:33 UTC, QAC reported no foreign QEMU, but the same verified development reservation `9dc21cb291ec4ab5aa5a63f91f216adb` was still running (child PID 3476); there was no queued or blocked request. This follow-up census is preserved at `E:\guideXOS_C181R1_evidence\qac-followup-census-20261020T220433Z.txt` (SHA-256 `D9C112A0B42B7BDACEF7D0AA5C358211D50BA894BB8AE82812001996DBE75B80`). The C181 runner's required empty census is still not met, so no C181 guest was launched.

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
| Focused three-boot guest lane | PASS: 3 readiness markers, 22 normal managed launches, two actual QMP `RESET` events, zero missing reset events, and verified QAC release. Boot 1 showed five residents and Notes 5→4→5. Boot 2 passed all five app interactions/resizes and rejected-geometry rollback. Boot 3 passed two generation-safe Notes close/reopen cycles, each 5→4→5. |
| Three ordinary production guest boots | PASS: three independent QAC-launched cold boots; each recorded one fresh readiness marker, zero warm-reset events, proof-only diagnostics absent, and verified release. Five-resident Task Manager evidence and all required production interactions/resizes passed. |
| QAC availability | PASS after qualification: QAC 2.0.0 Online; zero QEMU processes (zero foreign), reservations, sessions, or queued work. Each acceptance reservation was independently verified released. |

Outcome A: both guest acceptance gates passed. The original reset problem came from `-no-reboot`; the corrected focused run observed `RESET`, running status, and fresh readiness after each reset. Repro14–16 exposed media/payload selection failures, not a product defect. C182 was not started.

### Completed C181R1 acceptance — 2026-10-20 PDT

**Focused acceptance** used run label `r1-c181-proof-focused-acceptance-01` under `E:\guideXOS_C181R1_evidence\guest-runs\focused-r1-c181-proof-focused-acceptance-01-qac-sequence`. Its manifest records `boots=3`, `bootReadyMarkers=3`, `normalAppLaunches=22`, `qmpResetEvents=2`, `qmpResetEventsMissing=0`, QAC exit 0, and verified release of reservation `349dc8007a464aeab5d7b9afef0deb60`. Both reset requests returned `{}`, both QMP traces contained `RESET`, and `query-status` reported `running=true`; fresh input-loop readiness followed at serial offsets 145324 and 463881. Boot 2 rejected 559×343 and preserved Notes at 560×344. Boot 3 completed two fresh-generation and fresh-instance Notes close/reopen cycles, each returning Task Manager from four residents to five.

The focused media was kernel `B360905ED40EEDDA32E14851E5E980C7204D427F21ABE200C8D4FBBE61832C63`, proof app ramdisk `F61BF2A3012D298762A94DA64A48AAC2382632C39068D8C8860E3255D4E50E28`, and loader `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`. The proof ramdisk includes C172/C173/C175/C178/C179 managed markers and omits the unrelated C162 close-boundary proof.

After the final Boot 3 width assertion was narrowed to the documented 600-pixel Notes default, focused diagnostic `r1-final-width-check-01` ran scenario 3 and passed both close/reopen cycles. Each fresh generation measured 600 pixels, changed generation and native instance, and restored Task Manager from four residents to five. QAC verified release of reservation `72c3da68c2bd4e15ba071a74f452abf3`; the diagnostic manifest records one ready boot and eight launches.

**Production acceptance** used run label `r1-production-acceptance-02` and manifest `E:\guideXOS_C181R1_evidence\production-coldboots-r1-production-acceptance-02\production-ordinary-manifest.json`. The manifest outcome is `PASS`, with three of three independent QAC-launched QEMU cold boots. Reservations were `46267bbf27ce46858a78494da4cbeadf` (boot 1), `76d004e361ce4b1881d467fd4591d202` (boot 2), and `018f5b8427a34ea0ba0ec969c7ea887e` (boot 3); all releases were verified. Readiness counts were 1/1/1, normal managed launches were 7/7/8, and warm-reset counts were 0/0/0 because these were separate cold boots. The proof-disabled kernel, ramdisk, and loader hashes matched the candidate for every isolated boot; all selected proof-only markers were absent.

Production Boot 1 passed five-resident Task Manager refresh and Notes close/reopen. Boot 2 passed Settings Apply/Reset, Explorer navigation/selection, Notes edit/save, Calculator arithmetic, Task Manager refresh, all five screenshot-based resize checks, and rejected-geometry rollback. Boot 3 passed two Notes close/reopen cycles with resident counts 5→4→5 and the Explorer managed-root boundary. The guest has only five established managed app descriptors, so no sixth app can be launched to exercise guest-side over-capacity; capacity rejection/recovery is covered by the preserved C181 host suite.

| Evidence | SHA-256 |
| --- | --- |
| Focused `sequence-manifest.json` | `1A119D8FC48DF8265B66565D8677A958101142A1FBBFC3471AD6B066617D4124` |
| Focused `serial-all-boots.log` | `514801E64B604D2DA1206AA190B6F44E9DF009604FD98FD5AFA01F7B0C9EC74A` |
| Focused `qmp.log` | `54D3FF4501CEC9B9A5444939F77EFABF167C5D2DB139DBC0757BBA5D5DBD6F5A` |
| Boot 2 reset evidence | `43813A50B16E964B6046475DDC23FD41E384C48D9E71768494BD5D0869DF728A` |
| Boot 3 reset evidence | `2EF956EBDA318EA02F4BE0289554B3F958DE01A39EBBA7D2183887846E4D1F27` |
| Final focused Boot 3 diagnostic manifest | `68E31307FFD0F6F8CAF988F50A73BCD698FF83F05A3DE5D048AB7E5CB605B092` |
| Final focused Boot 3 lifecycle evidence | `A2413AC37232BF49D0551D1252D6275C1615ED1346905B8DF9E3C73DEAE87677` |
| Production `production-ordinary-manifest.json` | `BAAD928958CC2E71D64F5DFE2CC9FE58860FD371C910494D962B7B0CED9EE7EF` |
| Production boot 1 serial log | `793D5CE01AE680BECAA34811D3B6509596D524DCE677EC3EFD29D4FE8831D06A` |
| Production boot 2 serial log | `188838F1C7990001AF9C0ADA8B29C5A676887ADBA174BAE99EE048295FD2E26E` |
| Production boot 2 resize evidence | `CF3890CA6ADC13A866F0B9C20541613CB9A8E439C6DF0B6040BF46931BA61DCB` |
| Production boot 3 serial log | `04088478FB91BFBECF8CBB8ED26628747C8263FE8F7CEC3B41178DDD2A6DDD61` |
| Production boot 3 lifecycle evidence | `992C03AC08D609282C5A1A2101D5B8ABB37FA20C44BDF7915372B313AE61F89A` |

The accepted C181 host baseline remains **38 suites / 930 cases PASS**; no product or kernel source was changed in this continuation. The protected modified `ESP/ramdisk.img` remains at SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`, and the pre-existing untracked `samples/host/ManagedUiRegression/obj/` remains intact.

## Protected workspace state

The modified checkout ramdisk `ESP/ramdisk.img` remains preserved at its pre-task SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The existing untracked `samples/host/ManagedUiRegression/obj/` directory is also preserved.

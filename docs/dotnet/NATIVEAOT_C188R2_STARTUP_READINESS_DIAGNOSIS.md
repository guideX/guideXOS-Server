# guideXOS Server .NET Support — C188R2 Startup Readiness Diagnosis

## Outcome and scope

**Outcome C — Unresolved startup boundary.** C188R2 completed the six-run forensic review, static artifact comparison, and one controlled response probe. It established that the guest did not enter the normal input loop and that the runner correctly missed the unconditional main-loop marker. A QMP mouse event reached the guest's PS/2 IRQ handler, but the screen did not redraw and no surface or launch-return marker appeared. The failure lies in the synchronous managed application startup path after instance creation and before successful return; the exact operation is still unknown.

One isolated proof-disabled production guest was launched under an exclusive QAC reservation. No product or checked-in runner source was changed, no acceptance interaction was executed, and no previous C188 attempt was modified. The six historical C188 scenarios remain failed or incomplete. C189 has not started.

## Preflight and preservation

| Item | C188R2 finding |
|---|---|
| Repository / branch | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` / `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `bad9e558e819c1dc1cb56bcaf2487012fdd7840c` |
| Starting worktree status | 136 entries: the pre-existing modified `ESP/ramdisk.img` and the existing untracked build/evidence outputs |
| Protected inventory | 13 roots, 8,497 files; compared by path, size, and SHA-256 against C188R1 protected-after: 0 added, 0 removed, 0 changed |
| Protected ramdisk | `ESP/ramdisk.img` SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Phase guards | Repository `.phase` absent; no stale-prompt or phase-guard files found; existing C188 runner confines evidence below its C188 root and refuses overwrites |

The complete pre-write inventory is `out/dotnet/c188r2-startup-readiness/protected-before.json`; the exact starting status is `workspace-status-before.txt`. The older C185 caveat remains: prior bytes cannot be established for 212 files from the pre-C186 period.

The 13 roots are the C181, C182, C185, and C186 evidence roots and the existing ManagedHostServicesC185, ManagedStarterC182, ManagedUiRegression, C185CounterApp, HostLogProof host-smoke, and ManagedUiContracts build roots. The current content matched all 8,497 files in the C188R1 protected-after manifest.

## Six-attempt failure matrix

All six preserved logs end at the last line shown below. Focused candidate logs are 15,654 bytes each; proof-disabled production logs are 33,966 bytes each. The raw serial hashes identify each complete file. The runner requires both kernel readiness markers; neither focused nor production serial output contains the main-loop marker.

| Attempt | Timeout / QAC | Last serial line and kernel readiness | Timeout view / QMP evidence | Serial SHA-256 |
|---|---|---|---|---|
| Focused boot 1 — identity | 180 s; reservation `19031b98d7a342ec9b99b870b1fcf85d`, PID 12656; release verified | C188 identity registry PASS, then Notes selector 4; last line `C148-NOTES-CONTEXT … result=PASS`; no input-manager or main-loop marker, surface create, or launch return | Black screen with clock. QMP returned `running=true` and accepted mouse move/click commands; no guest response was demonstrated by a before/after comparison. Screenshot SHA-256 `C33B47A279194673C03C8AFDF6119DF0BA9AB2B259125BCCDC11280047BBFE13` | `DBCE249C7DA6C56A4C54BFB8C14C2E41BCDA7D64B715B20B15A79D7B776D50F0` |
| Production boot 1 — startup | 180 s; reservation `942f45b2ef35478d9de5deb9ee44fb73`, PID 22344; release verified, QAC reservation degraded after an unmanaged QEMU appeared | Input-manager initialized, then `C150-APP-INSTANCE id=4 generation=1`; no main-loop marker, surface create, or launch return | Rendered desktop shell with wallpaper, icons, taskbar, welcome notification, and pointer. No QMP running-state query was preserved. Screenshot SHA-256 `E8947F9175D5DEA8277C71F34F9F499C2178D8A90214BB96613CEC0270E9A81F` | `16E352016A0B61056D14B453CFA39A8ED0D0D0A3B5E362D63C0AF9DEB40B0B07` |
| Focused boot 2 — lifecycle | 300 s; reservation `426190495a6e44699de37ad420f19e3e`, PID 18616; release verified | Last line `C148-NOTES-CONTEXT … result=PASS`; no input-manager or main-loop marker, surface create, or launch return | Rendered desktop shell. QMP `query-status` returned `running=true`; that confirms QEMU state, not guest input handling. Screenshot SHA-256 `530BAFCF4C5D957A411E1118EB42EFE49B2BDBE9EA8ECD30D0BC15B739B6E563` | `B2B214A1EB6AB7B3B73160A92C2DBA59E833A93835CE5446A789DA03061B04C3` |
| Production boot 2 — application behavior | 300 s; reservation `24b4098c6ed24f06b1ef2e9d43c6726b`, PID 384; release verified | Input-manager initialized, then `C150-APP-INSTANCE id=4 generation=1`; no main-loop marker, surface create, or launch return | Black screen with clock. No QMP running-state query was preserved. Screenshot SHA-256 `DD066309C184367442CD69317B497840620447C0D5CA02A31DB9E3F02FA64FE9` | `0FAE0AC6E2B24529CB0460E736966657EBDB629220CF33645F0CF643BE5BDEB3` |
| Focused boot 3 — coexistence | 300 s; reservation `f8dc7e1d09d34dafb669ff2c4e78303d`, PID 3736; release verified | Last line `C148-NOTES-CONTEXT … result=PASS`; no input-manager or main-loop marker, surface create, or launch return | No screenshot or QMP status query preserved | `E04F1B3FE8C848B89267C495940327EDF0E2B72A1A9B587E7DAD39BD3CED6739` |
| Production boot 3 — lifecycle | 300 s; reservation `022be8b381e049c1abd6762a794cb401`, PID 11636; release verified | Input-manager initialized, then `C150-APP-INSTANCE id=4 generation=1`; no main-loop marker, surface create, or launch return | No screenshot or QMP status query preserved | `912E9F555C8F5E2975CE3ED131E9BC8EA877B200B23B7165D9C70DA73F3C2B21` |

The initial production reservation's degraded state is retained as historical evidence. The other five attempts had clean QAC admission and verified release. Each QAC QEMU child exited with code 0 after its runner timeout; process exit 0 does not mean guest readiness. The serial files stop at the last marker rather than continuing with later serial output.

## Candidate identity, staging, and QEMU comparison

The same intended payload tuple was used for each of the three boots within each profile. Actual per-boot media-manifest hashes match the corresponding isolated ESP files; the stage manifests show the correct NativeAOT ELF was copied to `/APPS/GXOSAPP.ELF`, with 185 unrelated ramdisk files preserved and no changes to other files.

| Payload | Focused development candidate | Proof-disabled production candidate |
|---|---|---|
| Kernel | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |
| NativeAOT composite | `9F84C525A3D6053EAB6D489563CD0C31B718CF082B3C0ED79544DBA6F9E3FEFC` | `40953EEF2539484603A7CAC44255B7D47A3FCA5E8684E72FE4B7A18A60589BB8` |
| Ramdisk | `CC3DEB0DA01B5A33AC159062BF72A846B9CFF6FCF4511FC0A90DB8FA4309FB1D` | `CE79E453AACEE25DAA41A7F5152480CD5AC1A57A215C093EA1D7941A14D52BD3` |
| EFI loader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

The six manifests identify the above kernel, managed ELF, ramdisk, and EFI loader hashes in the intended profile. C188R1's build manifest says these retained candidates were built from source commit `9a651829a559e0e254f1c6d0dfea99a71f290c5b`; the change from that commit to starting HEAD `bad9e558…` contains documentation and evidence only, not product source. No stale or mismatched staged artifact was found.

All six QEMU argument sets normalize to the accepted C187 configuration after replacing only the ESP directory, serial file, and QMP port. The settings are QEMU `-accel tcg -machine pc -smp 1`, readonly OVMF pflash, isolated FAT ESP as IDE index 0, 1024 MiB RAM, `-vga std -display none`, file-backed serial, loopback QMP server, `-boot order=c -no-shutdown`, and UTC RTC. C188's QMP greeting reports `v11.0.0-12122-ga4bb4b10c9`. No device or memory change explains the common boundary.

The proof-disabled production kernel qualification manifest reports five expected production identities, `developmentDescriptorAbsent=True`, and `standardKernelOutputTouched=False`. The C188 runner also scans production kernel bytes and rejects either `com.guidexos.apps.managed.counterapp` or any `C188-` string. Direct inspection confirms the production NativeAOT image is distinct from the development image. The ABI remains v4 / 128 bytes, callback offsets 104, 112, and 120, with five managed residency slots.

## Readiness contract and interpretation

The existing runner at `scripts/dotnet/run-c188-qac-guest.ps1` is looking for these two kernel lines:

```text
[KERNEL] Input manager initialized
[KERNEL] Entering main loop (waiting for input)...
```

Its predicate requires both lines. `kernel/core/main.cpp` emits them at lines 879 and 6904 without a C188 or proof-build conditional. Both development and production kernel ELF files contain the exact marker strings. The input-manager line is present in all three production serial files and absent in all three focused files; the main-loop line is absent from all six. A direct check of the raw serial text found the runner's exact main-loop regex does not match because the line is absent, rather than because the recorded text changed, was truncated mid-line, or used a different line ending. The logs use CRLF plus some bare LF lines, and the expected input-manager regex matches the production lines.

The development NativeAOT ELF contains `C166-ORDINARY-READY app=ManagedNotes render=PASS`, which source emits only after `RenderMain` succeeds. That line is absent from all three focused serial files. The proof-disabled production ELF does not contain the C166 marker because ordinary-ready instrumentation is not compiled into that image; the production readiness check correctly does not depend on C166.

`C150-APP-INSTANCE` in `GuideXosApplication.cs` is emitted after the managed lifetime instance is created and immediately before its dispatch calls `application.Launch(host)`. `C148-NOTES-CONTEXT` is an early log inside `ManagedNotes.Launch`, not a fault marker. Across the six historical logs there is no `C102-LAUNCH-RETURN` and no `C150-SURFACE action=create`. The focused profile reaches the Notes body at C148; production reaches the instance marker before the application dispatch, then the diagnostic probe confirms the guest never reaches the main loop. This locates the common failure in the selector-4 managed startup path after instance creation and before surface creation or callback return, but does not prove the exact nested call in production. The input manager marker in production does not prove the guest reached the later main input loop.

**Finding:** the guest did not reach the normal input loop; the readiness predicate is not a parser false negative. During C188R2, QMP returned `running=true` before and after one mouse move, accepted `input-send-event`, and produced four new `[PS2] IRQ12` serial records. That proves the event reached the guest's interrupt handler. The 3,072,016-byte before and after framebuffer captures are byte-identical at SHA-256 `93F5FA2C7E6BB07ABE02FC34485BB748DEFF423DC67620B400CFE50D196E56C8`; the black screen and clock did not redraw. The same serial channel continued after the probe, yet the main-loop marker remained absent. These observations support an actual guest startup stall before the main loop, not a missed or lost readiness line. The exact call after the managed instance marker is not isolated, so retain Outcome C and do not weaken the readiness predicate.

## Accepted baseline comparison and first divergence

C187's accepted QAC boot reaches the NativeAOT launch return, input-manager initialization, main loop, pointer and keyboard inputs, window creation, and lifecycle close paths. Its QEMU arguments match C188 after path and ephemeral-port normalization. C187's development image replaces the selector-4 Notes factory with CounterApp. The guest therefore validates the kernel, managed entry, compositor, QAC workflow, and input loop under CounterApp, but it does not validate the real Notes startup body that stalls in C188. The first meaningful behavioral divergence is the C188 selector-4 launch of the real managed Notes implementation: C188 logs `C148-NOTES-CONTEXT` and never returns; C187 logs a CounterApp factory/window and returns to the kernel.

C181R1 is an additional historical baseline: its report records accepted real Notes-focused and proof-disabled production boots on an earlier candidate. That result shows the broader platform has previously booted Notes, but its source and artifacts predate the current C188 build input and do not identify the C188 regression.

C188's selector 9 and CounterApp descriptor are development-only. The focused logs reach a Notes selector-4 launch before any CounterApp launch. The production image has no C188 development route yet shows the same pre-return Notes boundary, so selector 9 is contradicted as the common cause.

One shared C188-era source change remains worth isolating: `GuideXosListBox` changed its label storage from `char[]` to `byte[]`, with `GetItemLabel` copying to a stack-allocated `Span<char>`. It is in the source diff from the accepted C187 candidate and is shared by the Notes composition. The focused ordinary image includes the C137 list control code, but current evidence does not show that this method is reached before the stall or that it fails. It is a candidate for later targeted instrumentation, not an established root cause.

## Diagnostic decision record and QAC gate

The machine-readable decision record is `out/dotnet/c188r2-startup-readiness/diagnostic-decision.json`. It was written before any C188R2 guest run. The experiment used one isolated proof-disabled production boot with the exact production payload tuple above. The diagnostic runner is a copy of the C188 runner at `out/dotnet/c188r2-startup-readiness/run-c188r2-qac-guest.ps1` (SHA-256 `ACE709592444099486B22028932FFCCD470ECB93FE1F03DB808C6284F5F51E00`); the original checked-in runner still hashes to `F6B14CE35F12FB40ACB227AC3D1F21964EB300D0654756D4A3AEC7B6E6F35418`. The copy confines evidence to the new R2 root and makes the one-connection response probe part of the same runner. No separate QMP observer was attached.

At `C150-APP-INSTANCE id=4 generation=1`, the runner waited ten seconds, captured serial and a framebuffer, queried QMP status, sent one relative mouse move, then captured another framebuffer from that same QMP connection. The probe took 3.06 seconds. It ran under reservation `1b8ee4032a67450792b914224b4edb9a`, owner-verified as `LEON2_RDT\guideX` PID 6196, with QEMU child PID 5736. The production kernel was `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF`, NativeAOT ELF `40953EEF2539484603A7CAC44255B7D47A3FCA5E8684E72FE4B7A18A60589BB8`, ramdisk `CE79E453AACEE25DAA41A7F5152480CD5AC1A57A215C093EA1D7941A14D52BD3`, and loader `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`. QEMU arguments match the earlier production runs after substituting only the isolated ESP, serial file, and QMP port (`58521`). The guest readiness timeout was 150 seconds after QMP became available; QAC admission was separately capped at 60 seconds. C187's complete accepted run took 131.5 seconds from QAC QEMU launch to exit, including interactions.

| Probe observation | Result |
|---|---|
| QMP status before and after | `running=true` both times |
| QMP mouse event | `input-send-event` returned `{}`; one relative move requested +40 X and +24 Y |
| Serial around the probe | Grew from 33,966 to 34,102 bytes. The saved prefix hash matches the probe's pre-event snapshot; the appended lines are `[PS2] IRQ12 #00000001` through `#00000004`, all with status `0x3D`. |
| Readiness, surface, and return | No `[KERNEL] Entering main loop`, no `C150-SURFACE action=create`, and no `C102-LAUNCH-RETURN` after the event |
| Framebuffer before / after | Both PPM files are 3,072,016 bytes and have the same SHA-256 `93F5FA2C7E6BB07ABE02FC34485BB748DEFF423DC67620B400CFE50D196E56C8`; the screen stayed black with the clock |
| QAC cleanup | QEMU exited 0; QAC logged request, matching child exit, and matching reservation release. A fresh final status/list showed 0 reservations, 0 managed sessions, and 0 QEMU processes. `qacReleaseVerified=True`. |

The runner exited 1 with its expected `Guest did not reach the input loop before timeout` diagnostic after QAC's QEMU child had exited 0. No acceptance manifest or interaction pass was produced. The complete probe, serial, QMP, QAC, and media evidence is under `out/dotnet/c188r2-startup-readiness/diagnostic-production/`.

At the first fresh C188R2 check on 2026-10-23 22:43 UTC, QAC was online but had an active owner-verified reservation `1adb5ec436cf4aa5afb1ad44638d4a10`, project `guideXOSServer`, mode `acceptance`, state `Running`. The owner was `LEON2_RDT\guideX` (PID 21028), with managed QEMU child PID 9776. Its command line pointed to a different Server v0.3 workspace and package evidence under `D:\dev\guideXOSServer\docs\evidence\settings-s22-v03-package-20261018\`. The full status, list, and process census are retained under `out/dotnet/c188r2-startup-readiness/`. That reservation was not touched and no C188 guest was launched or queued behind it.

A second status/list/process check at 2026-10-23 22:46:57 UTC showed QAC online with zero reservations, zero managed sessions, zero queued work, and zero QEMU processes. The other guest had finished before the diagnostic was launched. The decision record now marks the single probe ready for QAC admission; the run itself will use the existing C188 runner's admission and cleanup path with a copied runner confined to the new R2 evidence root.

The six historical QAC reservations are all released as recorded in C188R1 evidence. C188R2's separate diagnostic reservation was also released and absent from the final census at `2026-10-23T22:57:56.3673109Z`. The earlier owner-verified Server v0.3 reservation was left untouched and had ended before C188R2 admission.

## Changes, tests, and remaining acceptance gates

No product source, checked-in runner, candidate build, or test changed in C188R2. The isolated diagnostic copy of the C188 runner is preserved with the probe evidence. Existing qualification evidence remains C188 25 hosted checks; C182 15; C185 28; C186 15; C187 22; and the managed UI baseline of 38 suites / 930 cases. C188R2 did not rerun host tests or candidate builds. No regression result is claimed for this phase.

None of the six required C188 scenarios passed. The response probe confirms the runner is reporting a real pre-loop startup failure, but the exact managed operation remains unknown. The smallest next experiment is a bounded diagnostic NativeAOT candidate with markers immediately before and after the call to `application.Launch(host)`, then checkpoints inside `ManagedNotes.Launch` after `C148-NOTES-CONTEXT` and around `RenderMain`. Run only one isolated boot with the same production QEMU configuration and serial capture; do not change readiness requirements. Use those checkpoints to identify the blocking call before proposing a fix. If a product correction follows, add a focused regression and repeat the applicable build and production qualification gates. After a targeted guest reaches normal readiness, resume the six acceptance scenarios: focused CounterApp identity/launch, resize/lifecycle, coexistence/Task Manager; and proof-disabled production registration, application behavior, and residency/lifecycle/generation safety. C189 remains out of scope.

## Closeout

Source investigation started at `bad9e558e819c1dc1cb56bcaf2487012fdd7840c`. The C188R1 historical records remain intact. The modified ramdisk and all 136 pre-existing status paths are preserved. The one C188R2 diagnostic boot completed its probe and verified QAC release. Current result: **Outcome C — Unresolved startup boundary**. C188 acceptance remains incomplete; C189 has not started.

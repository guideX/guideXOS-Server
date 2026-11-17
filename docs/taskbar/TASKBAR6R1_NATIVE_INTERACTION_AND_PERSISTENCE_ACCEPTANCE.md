# TASKBAR6R1 — Native Interaction and Persistence Acceptance

- **Outcome:** **TASKBAR6R1_NATIVE_PARTIAL**
- **Integration recommendation:** **Not ready for controlled mainline integration**
- **Branch:** TASKBAR_IMPROVEMENTS
- **Baseline HEAD:** ce996b1e4e0df5f778ad2299f82aff4dc2cca9f4
- **Baseline worktree:** clean at the requested repository and branch

## Disposition

Native input is now demonstrated from QEMU HMP through the guest PS/2 mouse, desktop dispatch, widget hit testing, and visible taskbar behavior. File Explorer launched, its taskbar entry minimized/restored it, its taskbar context menu opened, and Pin to Taskbar stored the canonical gxos.builtin.fileexplorer identity. A fresh QEMU boot using the same writable volume loaded the pin store; a separate clean cold-restart sequence also relaunched File Explorer from its restored pin.

The native Settings menu saved taskbarSize=expanded, and a fresh boot using that same volume loaded the expanded setting and both stored pins. A later attempt to save a top-docked position failed during QEMU 11's directory-backed VFAT rename handling. The guest process exited before that position value could be committed. The FAT long-name rename/delete defect found during this work is fixed and covered by a focused regression, but the second native settings-save failure remains unresolved.

This is a partial result: native interaction and a limited cold-restart path pass. Grouping, window selection/closing, overflow, auto-hide, complete settings coverage, and real File Explorer copy/move progress remain unqualified. Do not integrate the branch until the second-save behavior and remaining essential native scenarios are resolved or explicitly dispositioned.

## Repository and mainline preflight

- Repository: D:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS.
- Initial branch/HEAD: TASKBAR_IMPROVEMENTS at ce996b1e4e0df5f778ad2299f82aff4dc2cca9f4; the starting worktree was clean.
- The pre-existing local commit 4f2330faa27fc3bdf2183a5f2e71b59265c35735 fixes taskbar pin loading before the root VFS mount becomes writable. This delivery adds the FAT LFN fix, regression, and this report in a second local commit. No reset, rebase, merge, push, or release-artifact change was made.
- The configured origin remains SSH (git@github.com:guideX/guideXOS-Server.git). A read-only HTTPS fetch to refs/remotes/taskbar6r1/main succeeded without changing that configuration. The fetched main snapshot is 50c6cff0993227de7c582b00eb1daabed62cf816 (docs: record S22R26 protected execution prerequisite, 2026-11-14). Before this delivery commit, the taskbar branch was 6 commits ahead and 1 commit behind that snapshot; the one main-only commit is documentation-only. After this delivery, the branch is 7 ahead and 1 behind. The local taskbar branch was also 1 commit ahead of origin/TASKBAR_IMPROVEMENTS before this delivery; it will be 2 ahead afterward.
- Reviewed TASKBAR6_NATIVE_QUALIFICATION_AND_POLISH.md, the TASKBAR1, TASKBAR2, TASKBAR3, and TASKBAR5 reports, and searched for TASKBAR4 documentation; none was present. Existing hosted coverage was treated separately from native guest evidence.

## QAC and candidate identity

- QAC CLI and coordinator were available. The coordinator reported version 2.0.0 and was online. The final status after guest shutdown was zero QEMU processes, zero managed sessions, and no queued or blocked reservations.
- QEMU was C:\Program Files\qemu\qemu-system-x86_64.exe, version 11.0.0 (v11.0.0-12122-ga4bb4b10c9). Guests were started with qac run --project guideXOSServer --mode acceptance; QAC was QEMU's direct parent. The machine used pc,usb=off, 1024 MiB RAM, -vga std, GTK display, serial, loopback QMP, and -no-reboot -no-shutdown.
- The final settings candidate was the isolated writable directory-backed FAT volume out/taskbar/TASKBAR6R1/guest-volume-size-save-lfnfix/. It was not the frozen ISO or a release artifact. QEMU used the QEMU fat:rw: backend for this directory.
- Production guest kernel used for the first settings-save interaction SHA-256: F52626AB1B096B28F503564FD943F5A1AD389535FD2CA00B5CC5D1B86D4CA4.
- Latest post-change production AMD64 kernel build and cold-boot candidate SHA-256: 73589D1C48FD1D564DFAB5A360B95724FCF1A46C465554A77EDE3CA5CE991023.
- Qualification-only click-trace kernel SHA-256: D551829972681570F12DF961FEEC6C7852D7F1B5F28293F01BFC51F68E6D074C.
- Firmware OVMF.fd SHA-256: 2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792.
- The first settings-save session was QAC reservation 9f86fdf43e55464c819cf183e82745b4, QEMU PID 18320. It loaded pins and saved the expanded configuration, then QEMU exited with code 3 when the subsequent position save reached Error handling renames (-2). QAC released it; the reservation was marked degraded because its process census was incomplete.
- The fresh same-volume boot used reservation 9680d67a82654c69a730b3c1eaf87131, QEMU PID 27956, loopback QMP port 49223. After capturing the desktop, this task sent QMP quit. QAC recorded Released, degraded=false, and zero remaining QEMU/session count.
- After the final LFN-association change, the newly built kernel booted from a copy of the saved-state candidate at out/taskbar/TASKBAR6R1/guest-volume-latest-lfn-association/. Reservation d9fd87225bb244fc83b681933123fe36 used QEMU PID 35192 and QMP port 49224; QAC recorded Released, degraded=false, after QMP quit. Serial reported the saved config and pin store loaded; screenshot cold-size-pin-after-latest-lfn-fix.png shows both taskbar entries.
- This final boot checked the post-change kernel's boot and saved-state load. The taskbar click sequence and the failed second preference save were exercised on the earlier F526 candidate.
- Both relevant boots reached the real 1280×800 UEFI GOP desktop. The fresh-boot serial log opens /desktop.cfg on the writable FAT32 mount and reports [taskbar] pin persistence=loaded.

## Input path and graphical interaction

The configured virtual input device was the QEMU PS/2 mouse. Guest serial reports PS/2 mouse initialization, Active mouse source: PS/2, and IRQ12 registration/unmasking. HMP mouse_move commands visibly moved the pointer, and serial recorded IRQ12 activity. QEMU 11 HMP help advertises mouse_button bits as left=1, middle=2, right=4. The guest trace showed HMP state 2 produced the PS/2 right-button bit, and state 4 produced the middle-button bit. The Files context-menu action used state 2; the guest trace and visible menu confirmed the right-click.

The qualification-only trace for a Start click records the chain:

PS/2 byte/packet → pointer map → desktop dispatch → widget hit target=1 → click result=1.

The Start target at (50,772) was a genuine PS/2 guest click. Subsequent native input opened the Settings menu, selected the Size row, launched File Explorer from the desktop, and interacted with the Files taskbar button. QMP transport acknowledgments are retained as supporting evidence; the guest trace and visible state changes establish delivery.

| Native scenario | Result | Evidence and boundary |
| --- | --- | --- |
| UEFI boot and desktop | PASS | Serial and genuine screenshots show the 1280×800 GOP desktop. |
| Pointer and simple click | PASS | PS/2 IRQ12, click trace through widget hit testing, and visible menu/action changes. |
| Launch File Explorer | PASS | Launched from the desktop; genuine screenshot shows the window. |
| Taskbar activation, minimize, restore | PASS | The Files button minimized and restored File Explorer. |
| Right-click taskbar context menu | PASS | Files button menu opened using the verified guest right-click. |
| Pin to Taskbar and pin persistence | PASS | Store contains gxos.builtin.fileexplorer; fresh boot reports pins loaded; restored pin launched Explorer in the earlier clean cold-restart run. |
| Preview behavior | PARTIAL | The minimized File Explorer fallback card showed title/state; no live thumbnail was produced. Popup movement, stale-state invalidation, and full grouped preview selection remain open. |
| Grouping, individual selection, close one/final window, close-all | OPEN | No two-window same-app sequence was run. |
| Context actions beyond Pin to Taskbar | OPEN | Activate/restore, minimize, maximize, close, unpin, close-all, dismissal, and popup-boundary cases were not qualified. |
| Overflow | OPEN | No populated native overflow sequence was run. |
| Auto-hide | OPEN | No native enable/reveal/button/menu/preview/overflow sequence was run. |
| Real File Explorer copy/move progress | OPEN | No guest copy or move was started; no taskbar progress lifecycle is claimed. |

## Settings and cold-restart persistence

A genuine guest click opened taskbar settings and selected Expanded. The primary file in the isolated candidate contains these values:

- version=2
- taskbarPosition=bottom
- taskbarSize=expanded
- taskbarPresentation=icons-labels
- taskbarAutoHide=0
- taskbarShowPreviews=1
- taskbarShowActivity=1

The first save completed. The resulting primary desktop.cfg SHA-256 is 8C90064FC772973AC7B6392E4C7941C7B4BE7E4D0CB26902C6C52E8EB6A29898; the pin store SHA-256 is 1AE78B47F7679F1BD32371DDE04B2D892EF944D822D988595C74353B3D049CB6. The pin file retains the pre-existing com.guidexos.pacman identity and the newly saved gxos.builtin.fileexplorer identity.

A new QEMU process booted the same candidate volume. Its serial log reports pin persistence=loaded, reads /desktop.cfg, and its genuine screenshot shows the restored Files and Missing app taskbar entries. An earlier clean cold-restart sequence on the same writable-volume setup also relaunched File Explorer from the restored pin. The latest settings restart followed QEMU's exit during the failed position save, so it proves the already-flushed size and pin values load after a fresh boot; it is not evidence of a graceful shutdown immediately after the failed transaction.

A subsequent guest click attempted to change position to Top. The config writer created /desktop.tmp, began /desktop.cfg → /desktop.bak, and QEMU's directory-backed VFAT layer printed Error handling renames (-2) and exited. The primary config remains taskbarPosition=bottom; the attempted top value is not committed. QEMU reports this at rename reconciliation, but the precise failing layer is not yet isolated between the guest FAT transaction and QEMU's directory-backed VFAT backend. Position persistence, auto-hide persistence, and presentation changes therefore remain open.

## Production changes and regressions

The prior local pin-load fix defers the one-time taskbar pin load until a writable VFS mount exists. Before the mount it preserves the pin state and reports the unavailable store; after mount it retries once. Its focused model coverage raised the taskbar suite from 104 to 109 assertions.

This delivery fixes FAT long-name metadata handling needed by the existing same-directory rename/delete paths:

- Directory enumeration and path lookup accept an LFN only when its entries have consistent metadata and checksum matching the associated short entry. Rename and delete receive LFN locations only when that association is valid, so stale LFN records are not mutated as if they belonged to the current short entry.
- Same-directory rename writes a valid new LFN/checksum association and retires the old LFN entries; partial failures restore original entries where possible.
- Delete retires the associated LFN entries with the short entry and restores them on partial write failure.
- tests/fs_fat_persistence_test.cpp adds LFN rename/read/enumeration/delete regressions, including a checksum-mismatched stale LFN case, and a fixture-preparation mode. The test ran against a fresh disposable copy of E:\CodexArtifacts\S22R22\provisioning\guidexos-user-data-test.img; the source image was not modified. Result: 55 checks, 0 failures.

The FAT regression resolves the reproduced LFN/checksum defect. It does not resolve the later QEMU directory-backed rename failure; that behavior remains a reported blocker rather than being represented as a pass.

## Verification

| Check | Result | Evidence |
| --- | --- | --- |
| Taskbar application model | PASS — 109 assertions | taskbar-model-test-final.log |
| FAT persistence regression | PASS — 55 checks, 0 failures | fs-fat-lfn-transaction/fs-fat-persistence-stale-lfn-55-checks.log |
| Compositor pointer/buttons | PASS | pointer-test-final.log |
| App Model identity | PASS — 25/25 | app-model-identity-test-final.log |
| App Model application actions | PASS — 55/55 | appmodel-actions-test-final.log |
| Window UI regression | PASS | window-ui-test-final.log |
| Compositor resize regression | PASS | resize-test-final.log |
| Hosted build | PASS | hosted-build-stale-lfn-fix.log; compiler warnings are recorded in the log |
| Native AMD64 production kernel build | PASS | native-amd64-build-stale-lfn-fix.log; latest post-change candidate hash above |
| git diff --check | PASS | Final source and report diff check before commit |

## Evidence artifacts

All generated guest and test evidence is under out/taskbar/TASKBAR6R1/; it is not part of the release image or the source commit.

- qac-run-click-trace.stdout.log and qac-run-click-trace-command.txt — qualification-only click chain and QAC direct-child command.
- qmp-hmp-input-transcript.jsonl and qmp-transcript.jsonl — HMP/QMP requests and screenshot actions.
- guest-files-context-menu-after-ps2-right.png — Files taskbar context menu after guest right-click.
- guest-file-explorer-launched.png, guest-file-explorer-minimized.png, and guest-file-explorer-launched-from-restored-pin.png — native launch, taskbar state, and pin relaunch captures.
- guest-taskbar-settings-lfn-fix.png and guest-expanded-taskbar-fixed.png — settings and expanded-state captures.
- cold-size-pin-after-save.png and cold-size-pin-after-latest-lfn-fix.png — genuine cold-boot desktops with saved taskbar entries; paired serial evidence is in qac-cold-boot-size-after-save.log and qac-latest-lfn-association-boot.log.
- qac-first-boot-lfn-fix.log — first save, second rename failure, and QEMU exit.
- fs-fat-lfn-transaction/fs-fat-persistence-stale-lfn-55-checks.log — full fresh-copy regression output including the stale-checksum case.

## Mainline recommendation and next actions

The branch is not ready for controlled mainline integration. Native clicks and pin/size reload now have direct evidence, and the targeted FAT regression passes. The second preference save still terminates the QEMU guest during directory-backed FAT rename reconciliation, while key taskbar requirements remain untested natively.

Next, isolate the rename failure on the supported writable volume path without changing the persistence architecture; repeat a second settings save and verify the resulting config after a clean cold shutdown and fresh boot. Then qualify settings for compact/standard/expanded, icon-only/labeled presentation, docking, previews, and auto-hide. Exercise genuine multi-window grouping and context actions, populate native overflow with supported windows, and run a bounded real File Explorer copy/move with taskbar progress. Keep the remote configured as-is and do not merge or push during this acceptance work.

# TASKBAR6R1 — Native Interaction and Persistence Acceptance

- **Outcome:** **`TASKBAR6R1_QUALIFICATION_BLOCKED`**
- **Integration recommendation:** **Not ready for mainline integration**
- **Branch:** `TASKBAR_IMPROVEMENTS`
- **Baseline HEAD:** `ce996b1e4e0df5f778ad2299f82aff4dc2cca9f4`

## Disposition

The AMD64 UEFI guest reached the real 1280×800 desktop. QMP/HMP mouse movement reached the configured PS/2 path, and a guest screenshot showed the pointer over Start. The HMP left-button press/release was accepted by QEMU, but the guest display did not change: the before/after PNGs at the Start target are byte-identical. No taskbar click or application action is claimed as passed. Because input did not produce a taskbar state change, this is Outcome C even though pointer movement itself was observed.

Boot diagnostics exposed a pin-restoration ordering defect: `TaskbarManager::init()` attempted to read `/desktop.taskbar.pins` before the root VFS mount existed, then did not retry. A retry-on-writable-mount gate and a focused regression were added. Hosted and native AMD64 builds pass, and the taskbar model suite passes 109 assertions. The fix has not yet been booted because the only QAC acceptance slot became occupied by another managed guest.

Native cold-restart persistence, taskbar actions, settings interaction, previews, overflow, auto-hide, and real File Explorer progress remain open. The branch should not be integrated until guest clicks and cold-restart persistence are demonstrated.

## Repository and mainline preflight

- Repository: `D:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS`.
- Branch: `TASKBAR_IMPROVEMENTS`; starting worktree was clean at baseline HEAD above and matched `origin/TASKBAR_IMPROVEMENTS`.
- At completion, this task adds only the persistence-load fix, its focused regression, this report, and the listed local commit. No reset, rebase, merge, or push was performed.
- A read-only HTTPS fetch refreshed `taskbar6r1/main` without changing the configured remote. The taskbar branch has **5 commits not in main** and **1 main-only commit**. The main-only commit `50c6cff0` adds three S22R26 release/audit documents; none of the paths overlap the taskbar branch changes, so direct file conflict risk is low in this snapshot.
- The local branch remains equal to its configured `origin/TASKBAR_IMPROVEMENTS` upstream. `TASKBAR4` documentation was not present; TASKBAR1, TASKBAR2, TASKBAR3, TASKBAR5, and TASKBAR6 reports were reviewed.

## QAC and candidate identity

- QAC 2.0.0 and its CLI were available. Initial preflight showed zero managed sessions and zero queued reservations.
- QAC granted this run acceptance reservation `a3c2d5c644484745a57630db8ac8d306`, owner PID `35340`, direct QEMU child PID `27052`. Before input, QAC inspection showed `Running`, owner verified, and `degraded=false`; the QMP listener was confirmed to belong to PID 27052. The guest exited through QMP `quit`; QAC later reported `Released`, exit code 0, and `degraded=false`.
- QAC stderr also recorded a transient shutdown warning that its QEMU process census was incomplete. The final reservation record remained released and non-degraded. This is retained as an environment caveat; no other guest was contacted or controlled.
- After this run, a separate QAC-managed acceptance guest occupied the slot (reservation `0189d791aa3b4d40902143b020ce12a7`, child PID `30288`). This task's bounded queue probe (`9888a395344e4684b64f7ca335abbfac`) waited at position 1 and was canceled by this task. No unmanaged QEMU launch or attachment was attempted.
- QEMU was `C:\Program Files\qemu\qemu-system-x86_64.exe`, version `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. The machine used `pc,usb=off`, 1024 MiB RAM, `-vga std`, GTK display, serial on stdio, loopback QMP, `-no-reboot`, `-no-shutdown`, and guest error logging. Firmware `OVMF.fd` was read-only; the guest root was a private directory-backed FAT volume, not a frozen release image.
- Firmware SHA-256: `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`.
- First-boot guest kernel SHA-256: `94919F28C48F771112AEC15BDE9EBE38D0ECA6C64F8244BA059411D2BD114B54`.
- A separate unbooted test candidate was staged at `out/taskbar/TASKBAR6R1/guest-volume-pin-load/` with the fixed kernel SHA-256 `741FD64CED899D6DCCEB898D16B0736DFFD4CC8BC2866DDE8F15B10F6912475` and a preseeded `/desktop.taskbar.pins` containing the supported `com.guidexos.pacman` identity (file SHA-256 `BB2092AE977BC86AD811B4A8293E3DDDA4BCFAA2BA329B9DAA093CC04FBC1CD0`). This candidate has not been launched.

## Native input investigation

The configured virtual device was a current, relative `QEMU PS/2 Mouse`. QEMU's device tree showed PS/2 mouse and keyboard on i8042. Guest serial reported PS/2 initialization, `Active mouse source: PS/2`, and IRQ12 registered/unmasked.

The QMP `input-send-event` relative movement command returned success but did not provide independent proof of guest processing, so it is not credited. QEMU HMP `mouse_move` commands were accepted over this task-owned QMP connection; serial showed IRQ12 activity, and a genuine QMP screendump showed the guest cursor move about 40 pixels and later reach the Start button. This establishes pointer delivery through the guest PS/2 path.

HMP `mouse_button 1` followed by `mouse_button 0` returned successfully, but the Start menu did not open. `guest-pointer-at-start.png` and `guest-start-menu-open.png` have the same SHA-256 (`F534A690CECB80CBCB0B44DA4D45790174229E510797C1BCD3DC4ECF5AA6BF81`). Therefore transport acceptance is not treated as a click pass. The first failing stage after button transport is unresolved; qualification-only `GXOS_PS2_CLICK_TRACE` diagnostics were not run in a guest.

The original CUA host-window activation limitation remains; HMP avoided the need to focus the host window for pointer movement. QMP `query-input-devices` was unavailable in QEMU 11 (`CommandNotFound`), so device identity was checked through `query-mice`, HMP mouse listing, and the device tree.

## Native taskbar and desktop acceptance

| Area | Result | Evidence / limit |
| --- | --- | --- |
| UEFI boot and desktop | **PASS** | Serial reports one primary UEFI GOP framebuffer at 1280×800, B8G8R8A8. Genuine boot and desktop screenshots are saved under `out/taskbar/TASKBAR6R1/`. |
| Pointer movement | **PASS, input-path only** | HMP movement produced PS/2 IRQ12 activity and visible cursor movement. The pointer reached Start. |
| Basic taskbar click | **FAIL to demonstrate** | HMP button commands were accepted, but the desktop screenshot did not change and Start remained closed. No deeper button-stage trace was collected. |
| Explorer launch, taskbar activation, minimize/restore, grouping, window selection/close, pin relaunch | **OPEN** | No successful guest click was established, so none of these actions was claimed. |
| Context menus and dismissal | **OPEN** | No native right-click/menu action was established. |
| Preview cards and overflow | **OPEN** | Not exercised in the guest. Existing hosted evidence documents the no-thumbnail-surface fallback; it is not native acceptance. |
| Auto-hide and docking/layout settings | **OPEN** | No settings were changed through the guest. Hosted model coverage is not graphical guest acceptance. |
| File Explorer progress | **OPEN** | No real guest copy/move operation was started; no progress values were fabricated. |

## Persistence finding and change

The first guest serial log records `[taskbar] pin persistence=unsupported reason=settings-volume-unmounted` during desktop initialization. It later records a writable FAT32 root mount at `/` (`readonly=0`) and reads `/desktop.cfg`. The native taskbar pin loader was called only from `TaskbarManager::init()`, before that mount, so an existing pin file could not be restored on this boot.

The production fix adds `TaskbarPinStoreLoadGate`. Initialization may defer when storage is unavailable; `TaskbarManager::updateButtons()`, pin, and unpin paths retry once a writable mount exists. It loads only once and reports the initial unavailable state once. The pin count is not cleared until storage is available. A focused shared-core regression covers waiting, one-time unavailable reporting, retry after mount, one-time load, and reset for a new desktop initialization.

The taskbar model suite now passes **109 assertions**, compared with the TASKBAR6 baseline of 104. The native kernel build includes this change. The changed kernel and preseeded pin volume are staged for a QAC run, but the slot became occupied before that run could be started. Native restoration, pin writes, settings writes, and survival across an actual cold shutdown/relaunch therefore remain **unverified**.

## Evidence files

- `out/taskbar/TASKBAR6R1/guest-boot.png` — genuine QMP screendump during boot.
- `out/taskbar/TASKBAR6R1/guest-desktop.png` — genuine 1280×800 desktop screendump.
- `out/taskbar/TASKBAR6R1/guest-pointer-after-hmp-move-1.png` — pointer movement evidence.
- `out/taskbar/TASKBAR6R1/guest-pointer-at-start.png` — pointer over the Start target.
- `out/taskbar/TASKBAR6R1/guest-start-menu-open.png` — post-click capture; identical to the pre-click target screenshot.
- `out/taskbar/TASKBAR6R1/qac-run.stdout.log`, `qac-run.stderr.log`, `qac-after-first-guest-inspect.txt`, and QMP/HMP transcripts — guest, reservation, and input evidence.
- `out/taskbar/TASKBAR6R1/qemu-launch.json` — exact first-run QEMU configuration.
- `out/taskbar/TASKBAR6R1/guest-volume-first-boot-manifest.json` — first-run private root volume contents and hashes.
- `out/taskbar/TASKBAR6R1/guest-volume-pin-load/` — separate prepared candidate; not booted.

## Verification

| Check | Result | Evidence |
| --- | --- | --- |
| Taskbar application model | **PASS — 109 assertions** | `out/taskbar/TASKBAR6R1/taskbar-model-test-after-fix.log` |
| Compositor pointer/buttons | **PASS** | `out/taskbar/TASKBAR6R1/pointer-test-after-fix.log` |
| App Model identity | **PASS — 25/25** | `out/taskbar/TASKBAR6R1/app-model-identity-test-after-fix.log` |
| App Model application actions | **PASS — 55/55** | `out/taskbar/TASKBAR6R1/appmodel-actions-test-after-fix.log` |
| Window UI regression | **PASS** | `out/taskbar/TASKBAR6R1/window-ui-test-after-fix.log` |
| Compositor resize regression | **PASS** | `out/taskbar/TASKBAR6R1/resize-test-after-fix.log` |
| Hosted build | **PASS** | `out/taskbar/TASKBAR6R1/hosted-build-after-fix.log` |
| Native AMD64 kernel build | **PASS** | `out/taskbar/TASKBAR6R1/native-build-after-fix.log`; fixed candidate hash above |
| `git diff --check` | **PASS** | Final source/report diff check |

## Mainline readiness and next actions

The code and deterministic verification are ready for review, and the refreshed mainline snapshot shows no overlapping taskbar paths. Native input has only reached pointer movement; the Start click did not activate its target. Pin restoration is fixed in source but has not been demonstrated in a guest. The branch is therefore **not yet ready for controlled mainline integration**.

To close the remaining gates, obtain a fresh QAC acceptance reservation, boot the staged fixed candidate, use qualification-only click tracing to follow PS/2 button delivery through desktop dispatch and taskbar hit testing, and verify a real Start/taskbar action. Then change taskbar pins and preferences through the guest UI, shut it down fully, relaunch a fresh guest with the same writable volume, and verify the values and unrelated Start Menu settings. Only after those checks should native grouping, context menus, previews/overflow, auto-hide, docking, and real File Explorer progress be exercised and the integration recommendation revisited.

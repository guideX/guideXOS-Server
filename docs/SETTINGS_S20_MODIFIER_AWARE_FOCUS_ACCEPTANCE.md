# guideXOS Server — Phase S20 Modifier-Aware Focus Acceptance

**Status: Outcome A — Accepted.** The final ordinary AMD64 production kernel passed modifier-aware input, bidirectional focus, Personalization, lifecycle, configuration-safety, regression, freestanding build, and real QEMU guest checks on 2026-10-18. S19 remains the accepted baseline. This acceptance does not advance S9, Settings 1.0, or Server v0.3.

## Repository and preserved state

- Repository: D:\dev\guideXOSServer
- Branch: main
- Prompt baseline HEAD: 492c10542c50281cf1ddc5b671d8eeae987e45fc.
- The S20 implementation commit 9d155a5119c81fe855fa4c7023121a71d7e70b60 was present on main when the final acceptance work resumed. Its implementation and earlier Outcome B report were inspected; the remaining final-kernel lifecycle and minimum-size evidence gaps were completed here.
- Work remained on main. No branch, worktree, remote, reset, cleanup, stash, or sibling repository operation was performed. Existing untracked evidence and out artifacts were preserved.
- The earlier Outcome B in this report history was a checkpoint only. Its final-kernel full-lifecycle and minimum-size gaps are resolved by the final-recheck1 evidence below.

## 1. Existing keyboard architecture and modifier loss

The production PS/2 decoder receives set-2 make and break sequences, with 0xF0 as the break prefix and 0xE0 for extended keys. It already translated ASCII and special keys and tracked Control, Alt, and Shift. Before S20, both physical Shift keys shared one boolean, so releasing either Shift cleared the effective Shift state even if the other remained held.

On the desktop path, modifiers were discarded when callers used ps2keyboard::get_key() and passed only the logical key through desktop::handle_key, KernelCompositor::handleKeyDown, and KernelApp::onKeyDown(uint32_t). That app callback had no modifier parameter. ASCII Tab therefore carried no evidence of whether Shift was held. The Native ELF bridge retained key identity and Down/Up action, and its existing gx_event param3 already represented modifier flags, but it sampled global modifier state at dequeue time rather than recording the state at the physical key transition.

The S20 fix snapshots modifiers in the existing PS/2 event queues and sends the snapshot with production desktop key-down delivery. The production Settings diagnostic records the key, modifier mask, focus before and after, and whether traversal moved. It is bounded to the first 16 Tab events per Settings instance.

## 2. Modifier event contract and compatibility

The existing bounded keyboard event now carries logical key identity, KeyAction Down or Up, and a Shift/Ctrl/Alt snapshot. It does not carry raw scancodes or a separate repeat flag. Typematic makes remain repeated key-down events. Shift make/break state is held independently for left and right Shift; effective Shift remains active while either is down. Modifier transitions do not become printable events.

The existing get_key() and onKeyDown(uint32_t) paths remain available. Production uses get_key_with_modifiers() and KernelCompositor::handleKeyDownWithModifiers(); the base KernelApp callback forwards to the old onKeyDown callback for applications that do not opt in. The new virtual is appended after the old KernelApp virtual slots. Existing internal virtual ordering is preserved and all kernel apps are rebuilt together.

The Native ELF public gx_event structure and ABI revision are unchanged. Its existing fields still carry logical key, action, and modifier bits; param3 now comes from the transition-time snapshot. No public Native ELF ABI break was introduced. The legacy desktop route continues to dispatch key-down events only; Native ELF retains its existing key-up delivery.

## 3. Modifier ownership, focus, and lifecycle

The PS/2 decoder owns authoritative hardware modifier state. Focus changes, application destruction and reopening, legacy application activation, or having no focused app do not clear or duplicate the state. Each event receives a snapshot at decode time, and no app-local modifier flag can outlive an instance. Driver initialization resets the physical state.

A duplicate Shift break is idempotent. If a hardware key-up is lost, Shift may remain active until its break is observed or the keyboard driver is reinitialized; S20 adds no focus-based reset that could erase a genuinely held modifier. There is no timeout-based recovery for a lost physical break.

## 4. Shared focus traversal

The native Settings adapter passes the event Shift bit to SettingsPresentationCore::moveFocus(reverse, eligibleCount, context). The S10 shared core remains the sole focus owner and uses its bounded traversal order. It skips disabled, absent, and unsupported controls, wraps in both directions, sets initial reverse focus to the last eligible control, and clears focus for an empty eligible set.

Both ASCII Tab 0x09 and shell::KEY_TAB continue to move forward. Shift+Tab calls the same core with reverse=true. Focus repaint follows a successful movement. The active Personalization section, active choice, pending choice, and committed configuration remain separate. Enter and Space activate; arrows, Escape, category shortcuts, and desktop shortcuts keep their previous behavior.

## 5. Personalization and other Settings pages

On Personalization, keyboard focus reached gradient sections and swatches, wallpaper sections, presets and paging controls, desktop icon visibility controls, Apply, and Cancel. Disabled Previous paging was skipped. Reverse traversal across the wallpaper section boundary reached the preceding eligible Desktop icons control. Apply and Cancel remained visible at preferred and minimum size; both were keyboard-reached at preferred size, and the minimum-size focus pair demonstrated the section control and Cancel.

Focus movement did not activate a control or apply pending appearance changes. Aurora was made pending while Ocean remained active, the user navigated to other Settings categories and back, then Cancel discarded the pending gradient. The desktop icon visibility control was toggled pending and Cancel discarded it. Apply and Cancel were reached with Tab and reversed with Shift+Tab; Apply was not activated in the keyboard-only no-write run.

System, Display, Personalization, and About were exercised in the final guest. System, Display, and About currently expose no page operation as a keyboard focus target; Tab and Shift+Tab left their page state unchanged while category navigation continued to work. Sidebar navigation remained functional. Deferred categories stayed visibly unavailable and no new categories or configuration operations were enabled.

The Settings adapter uses Shift only to choose the direction of Tab traversal. Modifier bits do not change provider snapshots, authorization, or pending configuration; a supported activation remains required before an operation can be submitted.

## 6. Automated tests and regressions

Focused tests exercise the production PS/2 decoder, shared Settings core, native runtime/layout, Settings Center model, App Model dispatch, and compositor interactions.

| Suite | Result |
|---|---:|
| PS/2 decoder and modifier delivery | 31/31 passed |
| S10 Settings presentation core | 3,659/3,659 passed |
| Native Settings runtime | 1,421/1,421 passed |
| Native Settings layout | 1,037/1,037 passed; includes 1,000 geometry transitions |
| Hosted Settings Center model | 89/89 passed |
| App Model identity | 25/25 passed |
| App Model file activation | 60/60 passed |
| Compositor pointer buttons | Passed |
| Compositor resize notifications | Passed |
| Freestanding presentation/layout/refresh compile, relocatable link, and symbol checks | Passed |

The runtime suite includes 100 application lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions. The shared core includes 1,000 deterministic navigation transitions and reverse/forward focus symmetry checks. The PS/2 suite covers independent left/right Shift, overlap and release ordering, repeated Tab makes, duplicate Shift breaks, modifier snapshots on down/up, neutral Tab after release, Enter, Space, Escape, arrows, F12, and unknown scancodes. Runtime tests also verify modifier-aware delivery with no focused app and ordinary Tab after reopening.

The PS/2 host test reported the existing always-true uint8 scancode comparison warning, and the runtime suite reported an existing unused helper warning. The normalized production-build warning signatures match the accepted S19 set exactly: 224 before and 224 after, with no additions or removals.

Detailed outputs are in [the S20 evidence folder](evidence/settings-s20-modifier-focus-20261018/README.md).

## 7. Freestanding and production AMD64 build

The normal AMD64 production kernel build passed with freestanding flags including -ffreestanding, -fno-exceptions, -fno-rtti, -nostdlib, and -nostdinc++.

- Compile commands: 257.
- Compiler errors: 0.
- Warning diagnostic lines: 463; normalized warning signatures: 224, with no change from S19 or the prior S20 build.
- Kernel ELF: 2,771,240 bytes; SHA-256 F5AA964DA871DEAD7259D766DBAA4F67BE5485AF9DF3AFCD7812FE559ECA9AEF.
- Kernel PE: 2,888,634 bytes; SHA-256 159D82DADE3B2BF3EEE607FA6C286B561BC9128D4048F1A4096C6DAE17552589.
- ELF undefined symbols: 21, unchanged from the S19 baseline.
- PE import directory: zero RVA and zero size.
- No hosted runtime or standard library dependency was added.

The build log and machine-readable summary are in the evidence folder. The ELF, PE, private raw images, and test executables remain preserved in out/validation.

## 8. Production QEMU guest

The final production kernel booted in a private writable raw-FAT ESP with a private QMP endpoint and the accepted read-only OVMF firmware. The patched pre-run media was 134,249,984 bytes, used FAT32 partition LBA 63 and 512-byte clusters, and retained matching FAT mirrors. The reconstructed pre-run patch image hash is 0C26C66C25457A127E3512355058D27D588EB3CB368D5FE4293115D9CDB78C96; it matches the unrun sibling clone generated from the same source media and kernel. The task-owned QEMU run used PID 29636 and QMP 127.0.0.1:60322, then exited through its private QMP quit command. The post-run raw media hash is 4A51FF256BB6E0E463365B66CA1137227553829E11B866FF10C7C0FF95AAC5B2. OVMF SHA-256 is 2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792.

Foreign QEMU guests owned by C:\gxR2 and D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT were identified and left running with their media and QMP endpoints untouched. Only the S20-owned process was stopped.

The guest booted to the graphical desktop, opened native Settings through Start, and completed the Personalization sequence on the final ELF. The QMP log includes real sendkey shift-tab injection. At the native Settings callback, the serial log records a Tab event with modifiers 0, the corresponding Shift+Tab event with modifiers 1 returning focus to the prior eligible control, and an ordinary Tab with modifiers 0 after Shift release. This confirms guest delivery and modifier state at the production app boundary.

Settings was resized from 760×560 to 660×440 using the S19R3 calibrated lower-right held-button gesture. Forward and reverse traversal were captured at minimum size. Escape closed Settings, Start reopened it, and ordinary Tab after the instance reopened arrived with a zero modifier mask. Settings was closed again, the legacy Control Panel opened, and Alt+F4 closed it using the existing desktop shortcut. Meta+L opened and closed Start and the run ended on a stable desktop. System, Display, and About were also visited on this final ELF.

| Evidence | Result |
|---|---|
| [Initial Settings](evidence/settings-s20-modifier-focus-20261018/final-recheck1.settings-initial.png) | Native window at preferred 760×560. |
| [Forward Tab](evidence/settings-s20-modifier-focus-20261018/final-recheck1.focus-forward-tab.png), [Shift+Tab](evidence/settings-s20-modifier-focus-20261018/final-recheck1.focus-reverse-shift-tab.png), [Tab after release](evidence/settings-s20-modifier-focus-20261018/final-recheck1.focus-tab-after-shift-release.png) | Aurora focus, reverse to Ocean, then ordinary forward movement after Shift release. |
| [Pending gradient](evidence/settings-s20-modifier-focus-20261018/final-recheck1.gradient-pending-aurora.png), [Cancel result](evidence/settings-s20-modifier-focus-20261018/final-recheck1.gradient-cancel-result.png) | Pending choice stayed separate from active Ocean and Cancel discarded it. |
| [Wallpaper reverse focus](evidence/settings-s20-modifier-focus-20261018/final-recheck1.wallpaper-focus-reverse.png), [page change](evidence/settings-s20-modifier-focus-20261018/final-recheck1.wallpaper-page-2-after-enter.png), [reverse section boundary](evidence/settings-s20-modifier-focus-20261018/final-recheck1.wallpaper-reverse-section-boundary.png) | Wallpaper grid and paging stayed keyboard reachable; reverse traversal crossed back to Desktop icons. |
| [Icon pending](evidence/settings-s20-modifier-focus-20261018/final-recheck1.desktop-icons-pending.png), [Apply focus](evidence/settings-s20-modifier-focus-20261018/final-recheck1.desktop-icons-apply-focus.png), [Cancel focus](evidence/settings-s20-modifier-focus-20261018/final-recheck1.desktop-icons-cancel-focus.png), [reverse to Apply](evidence/settings-s20-modifier-focus-20261018/final-recheck1.desktop-icons-apply-reverse.png), [Cancel result](evidence/settings-s20-modifier-focus-20261018/final-recheck1.desktop-icons-cancel-result.png) | Apply/Cancel focus and reverse movement; pending icon selection was discarded. |
| [Minimum-size window](evidence/settings-s20-modifier-focus-20261018/final-recheck1.minimum-660x440.png), [minimum forward focus](evidence/settings-s20-modifier-focus-20261018/final-recheck1.minimum-focus-forward.png), [minimum reverse focus](evidence/settings-s20-modifier-focus-20261018/final-recheck1.minimum-focus-reverse.png) | The actual 660×440 layout kept the section control and Cancel focus visible. |
| [Reopened Settings](evidence/settings-s20-modifier-focus-20261018/final-recheck1.settings-reopened.png), [reopened ordinary Tab](evidence/settings-s20-modifier-focus-20261018/final-recheck1.reopened-ordinary-tab.png) | Fresh app instance received unmodified forward Tab. |
| [Legacy Control Panel](evidence/settings-s20-modifier-focus-20261018/final-recheck1.legacy-control-panel.png), [closed by Alt+F4](evidence/settings-s20-modifier-focus-20261018/final-recheck1.legacy-control-panel-closed.png), [final stable desktop](evidence/settings-s20-modifier-focus-20261018/final-recheck1.final-stable-desktop.png) | Existing legacy and desktop keyboard behavior remained usable. |

The complete QMP transcript and final guest serial log are committed beside these screenshots. The screenshot set includes the initial desktop and Start path, Settings focus states, category pages, minimum-size focus, reopen, legacy Control Panel, and final desktop.

## 9. Configuration safety

The pre-run 234-byte DESKTOP.CFG and post-run 234-byte DESKTOP.CFG compare byte-for-byte equal. Both SHA-256 values are CF310FEA3047F36350F99DB5E2FF458A20E85E6AAE9449F320BFE57F14967249. The explicit comparison record is final-recheck1.desktop-cfg-comparison.json. Focus changes, Tab, Shift+Tab, pending gradient selection, and Cancel did not write configuration. Whole-media hashes were not used as a substitute for this file comparison.

## 10. Remaining limits

No lost PS/2 break recovery timeout is implemented; the driver remains the owner of physical state until a break or reinitialization. Desktop KernelApp keyboard callbacks remain key-down oriented for compatibility, while Native ELF receives its existing Down/Up events. Key repeat is represented by repeated make events without a distinct repeat flag. These limits do not prevent the accepted Settings Shift+Tab flow.

## 11. S21 recommendation

Keep S21 bounded to missed key-up recovery: detect keyboard-controller reset or input-stream loss where the hardware exposes it, resynchronize stuck modifiers without focus-based clearing, and add decoder fault-injection and QEMU evidence for duplicate and missing break sequences. Do not expand Settings categories or appearance operations as part of that input-reliability work.

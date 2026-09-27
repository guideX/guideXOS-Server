# C145 Managed Modal Dialog and MessageBox

Phase C145 adds a fixed, reusable modal overlay for managed applications and uses it for Settings Center Reset and dirty-close confirmation.

## Existing modal architecture

Before C145, `GuideXosControlHost` already provided one shallow modal child scope. `EnterModal` cancels pending split-Space and secondary-pointer gestures, stops the parent ScrollBar drag, normalizes and saves parent focus by stable control ID, blurs parent controls, and routes host input to the modal child. Blurring closes an open ComboBox or PopupMenu and releases the existing single transient-capture lease. `ExitModal` clears child focus and restores the saved parent ID when eligible, otherwise using the host's normal forward eligible-member fallback.

The application runtime already supports multiple application surfaces and close/relaunch lifecycle. C145 keeps the Settings Center in its existing surface, renders the parent first and the Dialog overlay second, and does not reconstruct the parent controls when a Dialog opens.

## Dialog model

`GuideXosDialog` stores fixed bounds, bounded ASCII title/message buffers, result/default/cancel values, and up to eight direct member references. It owns no controls and creates no recursive content tree. Members must be unassociated with Panel, GroupBox, ScrollView, or VerticalStack layout owners so the same control cannot be routed through two owners.

Supported members are Label, Button, CheckBox, ComboBox, and Separator. RadioButton is rejected because the current host registration path mutates an explicit RadioGroup; it is not safe to share that member between a parent and Dialog host. Panel, GroupBox, ScrollView, another Dialog, and other non-leaf members are rejected. Duplicate insertion and overflow return `false`.

Title length is limited to 32 ASCII characters. Message length is limited to 112 ASCII characters and four deterministic lines. Lines break at a fixed character width; no wrapping, scrolling, or dynamic button array is introduced. Bounds and message geometry are fixed; the Dialog paints a contrasting body, border, title strip, title, message, and its existing managed controls.

Dialog controls are pre-registered once in one of two separate fixed-capacity child hosts owned by Settings Center. The parent keeps its existing 9 registrations in capacity 10. Reset has two registrations; dirty-close has three; each Dialog capacity is 8. Fourteen registrations are allocated across the parent and both reusable Dialog hosts. Since only one modal child scope can be active, at most 12 registrations participate in the active parent-plus-child route at once. Open/close does not change registration counts.

The Dialog delegates focus, Tab/Shift+Tab, keyboard routing, pointer routing, capture, and parent-focus restoration to the existing host. Tab traversal wraps inside the Dialog. Opening a second Dialog while one modal scope is active fails. There is no modal stack.

`GuideXosDialogResult` is a bounded enum: None, OK, Cancel, Yes, No, Apply, Discard, and Reset. Enter focuses and activates the configured default Button through ordinary Button key handling. Escape closes with the configured cancel result. `Closed` runs exactly once after the Dialog is marked closed and the parent host has restored focus; a callback can reopen after that modal exit has completed. Closing does not change member Enabled/Visible state or destroy application-owned controls.

The input transport represents a Button pointer-down as the complete click gesture; it has no managed primary pointer-up phase. Split Space, secondary-pointer completion, popup capture, and ScrollBar drag are explicitly cancelled or tested through the existing host lifecycle.

## MessageBox

`GuideXosMessageBox.TryConfigure` lays out caller-owned Buttons and configures the same `GuideXosDialog`; it has no separate routing implementation. It supports fixed OK, OKCancel, YesNo, and YesNoCancel sets, a default result, and a cancel result. For an OK-only box Escape maps to OK; for YesNo it maps to No; other sets map to Cancel. Callers keep the Dialog and Button controls and call `Open` through the existing parent host.

## Settings Center behavior

- Reset opens a two-button confirmation. Cancel preserves working and applied snapshots and the viewport. Reset runs the existing Defaults operation against working settings and lets the existing dirty-state, GroupBox visibility, ScrollView extent, and ScrollBar clamp rules apply.
- Dirty Close opens Apply / Discard / Cancel. Apply commits working settings and closes; Discard closes without committing; Cancel closes the Dialog and leaves the Settings Center open and dirty.
- Clean Close remains direct.
- Parent wheel events and outside clicks are consumed while the modal is active. A captured parent popup closes when modal entry blurs it. Parent ScrollBar drag is cancelled by modal entry.
- Focus is restored through the saved parent control ID when it remains eligible; otherwise the host selects its eligible fallback. The Settings Center parent keeps its prior state and viewport when a confirmation is cancelled.

## Focused coverage

C145 runs Dialog core 15, membership 10, MessageBox 11, and modal-routing 13 focused cases (49 total), plus 18 Settings Center integration cases. Stress coverage opens/closes the same MessageBox 25 times, runs 25 Reset confirmations alternating Cancel and Reset, and runs 25 dirty-close confirmations cycling Cancel, Discard, and Apply. The suite checks stable registration, cleared modal state, no capture/drag leak, and valid viewport after every cycle.

The current C145 image runs Dialog core 15, membership 10, MessageBox 11, modal routing 13, Settings Center integration 18, C128 Panel lifecycle 46, and C131 CheckBox API/host 34 + 23 cases. The completed C144 56-case suite is retained as historical evidence; the C145 integration suite rechecks working/applied state, dirty state, Apply/Discard/Cancel, Reset, viewport, dynamic content, and relaunch against the updated confirmation behavior. C129 Shift/Tab was rerun (31 cases, three boots PASS). C134 reran its transient-capture 30-case host suite and cumulative ComboBox production route across three PASS boots. The separate standalone C133 proof timed out before its focused-suite completion marker, so its 44 API and 24 host focused cases are not claimed as rerun. C135 production and focused API each passed three boots; the 40 API cases and C134 transient 30 cases passed. C135 focused host emitted the 40-case PASS marker but timed out before its close/result marker and omitted the C134 transient marker, so it is recorded as an incomplete proof run. C145's own modal-routing cases directly exercise parent ComboBox invalidation, capture release, focus containment, and outside-click blocking. C144 (56), C143 (68), C142 (68), C141 (56), C140 (58), C139 (54), C138 (62), and C137 (46) remain historical evidence.

## NativeAOT and QEMU evidence

The C145 composite uses the existing NativeAOT runtime and ABI v1/table size 104. Proof output and hashes are recorded in `out/dotnet/c145-managed-modal-dialog/c145.manifest.json`, with one serial log and SHA-256 per fresh QEMU boot. The proof drives Settings Center using QMP keyboard, pointer, and wheel input: Reset Cancel by Escape and pointer, Reset confirmation by Enter and pointer, modal Tab/Shift+Tab, outside-click and wheel blocking, dirty-close Cancel, and dirty-close Apply.

The C145 proof adds a conditional kernel proof harness and managed composite code; it adds no ABI fields, GC/VFS/App Model/capability changes, capture stack, drag stack, second focus system, recursive ownership, or synthetic Tab KeyChar.

## Final proof record

The final managed NativeAOT build completed with 0 errors and existing compiler/linker warnings. It used ABI v1/table size 104. The Settings Center keeps its 9 registrations in capacity 10; each Dialog child host has capacity 8, and only one modal child scope is active. The proof covered Dialog 15, membership 10, MessageBox 11, modal routing 13, Settings Center integration 18, C128 46, and C131 34 API + 23 host cases. MessageBox, Reset, and dirty-close repeated stress each completed 25 cycles. Final state was modal owner none, transient capture none, drag owner none, viewport valid, registrations 9/10.

| C145 fresh QEMU boot | Result | Serial SHA-256 |
| --- | --- | --- |
| 1 | PASS | `08001BC3A1DB7D0122D2295F094C1DE442A202EC96E460A02D9A023699EBCABE` |
| 2 | PASS | `4DA28E2EC4C728BA7495002394F47D593E1FD8598D76B6AB98DF515A8FEBCBF3` |
| 3 | PASS | `FF6930790BF5C624FF35CD3B8075A794E2616975F59CD2C0BB696BD285DBAFE1` |

The composite ELF SHA-256 is `D037DCF67D2B9A1549067A760023EE252BDA83CF5ACC0DF9A8EE71682CBB76AD`; the C145 proof kernel SHA-256 is `24F1AB9FEDF70DFBD8387F0D2D86F4E2F038F485DC2C51EF2D7905F381BB5EB8`. Production input passed Reset Cancel by Escape and pointer, Reset confirmation by Enter and pointer, Tab/Shift+Tab containment, outside-click and modal-wheel blocking, dirty-close Cancel and Apply. The C145 manifest is `out/dotnet/c145-managed-modal-dialog/c145.manifest.json`.

After proof, `kernel/build/amd64/bin/kernel.elf` was restored from the preserved `ESP/kernel.elf`. Both now have SHA-256 `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203`. The protected `ESP/ramdisk.img` remains `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE`.

| Ordinary fresh QEMU boot | Result | Serial SHA-256 | C145 proof marker |
| --- | --- | --- | --- |
| 1 | PASS | `CD61E0BD829204FB86628EC964C2B81BB19FCB31A7E682A10DF8F74C46CEFE58` | absent |
| 2 | PASS | `885CC2B981F41A06A0A2E3E520A6C98BCAE675004CCE2AED4C822B7468ED8554` | absent |
| 3 | PASS | `272B3C9901045F98EC6EDCFC412C4DD61969078E79163136238ED7A5AE03E92F` | absent |

The ordinary boot manifest is under `out/dotnet/c145-managed-modal-dialog/ordinary-boot/run-20260927-091544330-8f297989/ordinary-boot.manifest.json`; it confirms the kernel, ESP kernel, bootloader, and ramdisk hashes stayed unchanged through the three boots.

## Deferred

Nested Dialogs, movable/resizable windows, arbitrary button layouts, recursive containers, Dialog RadioButtons, theme/icon systems, file/color/font pickers, persistence, and async result tasks remain outside C145.

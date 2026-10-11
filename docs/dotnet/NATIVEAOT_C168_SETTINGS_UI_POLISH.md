# NativeAOT C168 — Managed Settings UI polish

## Status

Outcome A — C168 accepted. Focused C168/C167 tests, three real Settings UI proof boots, a clean production build/install, and three ordinary production boots passed. Production evidence is in `out/dotnet/c168-settings-polish/production-acceptance/c166a2-manifest.json`; focused UI evidence is in `out/dotnet/c168-settings-polish/c167-acceptance-manifest.json`.

C168 keeps the existing Settings application, C166 association service, and C167 Default Apps page. It adds no Settings framework, association type, persistence authority, or format.

## C167 baseline and rendering constraints

The C167 baseline used a 272-pixel text GroupBox inside the Settings ScrollView. At the 1280 × 800 QEMU desktop size, the compact page left much of the fixed 560 × 340 Settings window unused and showed a large scrollbar despite the short page. The prior baseline capture is `out/dotnet/c168-settings-polish/baseline-c167-system-default.bmp`.

The QEMU guest renders text on an 8 × 18-pixel bitmap grid. The managed surface exposes clipped text and filled rectangles, but no text size, weight, or per-label color. NativeAOT currently retains at most eight fill rectangles per managed surface frame. Window dimensions are fixed at 560 × 340; the framebuffer used by this QEMU setup is 1280 × 800. It does not provide runtime DPI scaling or window resizing. Settings remains on its existing horizontal navigation and single ScrollView; replacing it with a sidebar would require a second navigation/focus layout.

These limits mean hierarchy is expressed through placement, spacing, existing text labels, and control focus/selection rendering. The page does not introduce a new drawing or widget layer.

## C168 layout

`ManagedSettingsCenter` keeps its shared Settings shell and expands the existing ScrollView from 300 to 504 pixels wide. It is placed at `(24, 96)` with a 208-pixel viewport; the scrollbar uses the existing control at `(532, 96)`. The app title stays in the native title bar. A page line shows `Apps / Default Apps | .txt`; the shell status line distinguishes `Association` on this page from `Applied` or `Unsaved changes` on other pages. The existing Options and Apps/Settings buttons continue to own navigation.

Default Apps uses the existing GroupBox, VerticalStack, Label, ComboBox, Button, and ScrollView controls. Its eight page members show the effective application, association state, choice label and ComboBox, pending/success/failure feedback, Apply, Reset to System Default, and all-users note. Controls align to a common left edge, the ComboBox uses the available width, and the button group is separated from the association summary. The GroupBox membership remains a bounded, non-owning grouping; its children are added to the existing ScrollView for rendering and focus.

The outer ScrollView supplies the graphical viewport and border. The page no longer draws the old narrow text-box outline. The shell does not add graphical title bands because they would exceed the native managed surface's eight-rectangle budget when shared graphical frames are present.

The before/after captures show the C167 272-pixel text box and large scrollbar replaced by a 504-pixel content region, a separate effective-application and association-state summary, an aligned full-width choice control, grouped actions, and no scrollbar when the page fits. The full-window C168 comparison capture is the System default state in `out/dotnet/c168-settings-polish/screenshots/default-apps-system-default.bmp`.

Other Settings pages retain their existing contents and shared navigation. The wider shell accommodates them without changing their control model or order.

## Navigation, input, and ownership

The active page is visible in the breadcrumb and Settings action label. Apps remains the existing navigation authority. Keyboard and pointer actions update the same ComboBox selection.

The ComboBox stays registered with the existing control host as a non-focusable popup invoker. Its owning ScrollView retains focus routing; opening the dropdown temporarily captures input through the existing transient-capture path. Escape closes the list and releases capture. The 8 page members remain in the existing ScrollView; no second dispatcher or focus state was added.

The Default Apps viewport reports no overflow at the current page dimensions, so the Settings scrollbar is hidden when `MaximumOffset` is zero. Shared pages keep the existing ScrollView/scrollbar behavior when their content exceeds the viewport. Pointer and wheel input remain routed through the existing Settings and viewport code.

## Association and pending-state presentation

The page queries C166 when entered and on re-entry. It shows the effective handler separately from the override meaning:

| C166 state | Effective application | Association state |
| --- | --- | --- |
| `NoOverride` | Managed Notes | System default |
| `ApplicationOverride` | Managed Notes | Explicitly selected |
| `Disabled` | None | Disabled |

Changing the ComboBox only changes the pending choice. The committed application/state labels remain visible, and the feedback line says `Pending: Managed Notes` or `Pending: No default application`. Association storage is untouched until Apply.

Apply uses the existing C166 SetOverride or Disable operation, then performs a fresh Query and publishes the committed state only after verification. Reset uses C166 Reset, then verifies `NoOverride` and Managed Notes as the effective system default. Success feedback is inline and nonmodal. On write/query failure, the previous committed display remains and the pending choice is retained with a concise failure message. Apply is disabled when no operation is needed; it remains enabled on `NoOverride` with Managed Notes selected because applying that choice creates an explicit override.

## Control bounds

The Settings control host remains capacity 12 with 12 registrations. The Default Apps page retains eight GroupBox/VerticalStack members, and the ScrollView contains those eight direct controls within its existing maximum capacity of 24. The general Settings leaf-control count remains 18. No fixed capacities were increased. The page reads association data only through the C166 managed wrapper; the boundary scan reports zero direct slot references.

## Test and proof evidence

The focused C168 host runner is `scripts/dotnet/run-c168-settings-ui-host-tests.ps1`. It covers the initial layout/page indication, all three states, pending-only selection, successful and failed Apply, Reset semantics, post-mutation and page-entry queries, navigation with pending selection, forward/reverse traversal, pointer/keyboard ComboBox operation, Escape/capture release, viewport bounds, control capacity, single focus ownership, close/reopen, and Settings/ABI formats. Its bounded stress loops run 250 page cycles, 250 pending-selection changes, 100 Apply/Reset cycles, and 250 popup open/close cycles.

Fresh host totals: C167 20 focused cases and 60 stress iterations; C168 31 focused layout/state cases and the C168 stress counts above. The C166 service/wrapper host rerun reports 604 native cases, 34 managed ABI cases, 31 dual-slot cases, 136 partial-write cases, and 1,000 persistence audits. The C137 regression emits 46 passing wheel/viewport cases in guest serial evidence.

The three focused real Settings QEMU boots use Settings/Explorer input and one writable FAT16 image. Their serial SHA-256 values are:

| Boot | Result | Serial SHA-256 |
| --- | --- | --- |
| Boot 1 — keyboard / Disabled | PASS | `7F7E5C1AE672C10185ACF35491B557B957C7E8075A3015E36A07310FF97E9F72` |
| Boot 2 — persistence / pointer / Explorer | PASS | `49AA5A8F377AB76B3B70BCCFBF5AA81D642EF5B557BC88B3836E0341F12340E7` |
| Boot 3 — Reset / navigation / Explorer | PASS | `6F9EF171B08253FB89366734E1B707259BAB130ED43ED74EAFAE9A0566834D41` |

Boot 1 confirms a pending No default selection leaves association slots unchanged before keyboard Apply, after which Disabled is queried and displayed. Boot 2 reads Disabled after reboot, applies Managed Notes by pointer, and opens `/system/apps/C164/hello.txt` through both native and managed File Explorer. Boot 3 reads the explicit override after reboot, resets to NoOverride, navigates away and re-enters to query again, and opens the Notes fixture in Managed File Explorer with exact text and clean document state.

Guest screenshots, serial logs, QMP input logs, slot hashes, and manifests are stored under `out/dotnet/c168-settings-polish/settings-ui-proof-runs/`. The accepted C167 baseline and C168 full-window System default capture are kept at the C168 evidence root and in `out/dotnet/c168-settings-polish/screenshots/`. The initial System default capture shows the complete Settings window. QEMU state captures taken immediately after pending/Apply/Reset transitions show the updated client area but omit the native titlebar frame; they remain in the raw proof run as interaction evidence and are not used for the before/after visual comparison.

The proof desktop is 1280 × 800. The fixed 560 × 340 Settings window and its titlebar/content fit at that tested resolution. QEMU in this repository did not expose an alternate framebuffer mode during these runs; 800 × 600 and 1024 × 768 were not boot-tested. The fixed window rectangle fits within those dimensions when centered, but this is only a geometry check, not a guest-resolution proof. No clipping was observed at 1280 × 800.

## Clean production and ordinary boots

Production validation passed with a clean NativeAOT composite, AMD64 kernel compile/link, and production ramdisk. The candidate composite, kernel, and ramdisk were scanned and contained no PROOF1 control markers. Existing canonical kernel, ESP kernel, and ramdisk were backed up byte-for-byte before installation; QEMU ownership was scoped to this repository and no unrelated QEMU process was terminated.

| Production artifact | SHA-256 | Result |
| --- | --- | --- |
| NativeAOT composite | `413160D6A6A58A6B48750D1ABB588F9ECE41925F4E1AC369B9566C6EBD15888B` | PASS; proof controls absent |
| AMD64 kernel | `300E0E570956E240732157C00DE9F8C8D08E7C16C865E721AC61B087A36F3876` | PASS; canonical and ESP copies match |
| Production ramdisk | `5877B82A9C8A08BDB1121D0D3EB628E3D1275B58880244B9D0EF8A64C996BAF4` | PASS; installed ESP copy matches |

The install gate passed after all three ordinary boots. Ordinary boot serial SHA-256 values are `AF320E8B8ADDEEFB3C85E30D8380CD79AACC1E813F42C2A683D09AED062CF12F`, `DCF4762FC6CDFC1DBF512ACD645BAC4E9ED9584118BDDEF0118B239745381A97`, and `CF039CFD29D0B51E19BD434ACBFFC340D2EA315CD4515E1C8C34BF223EEE66B0`.

| Ordinary boot | Result |
| --- | --- |
| 1 | Settings opened on System default; close/reopen queried NoOverride again; Managed File Explorer opened `/system/apps/C164/hello.txt` and showed `hello from C164`. |
| 2 | Settings navigated into and out of Default Apps; viewport scrolled down and up; Tab/Shift+Tab passed; Default Apps showed NoOverride; Managed and native File Explorer opened the Notes fixture; Task Manager smoke passed. |
| 3 | Settings Default Apps Apply/Reset smoke returned to NoOverride; Settings close/reopen passed; Calculator produced 56 for 7 × 8; Task Manager and Notes activation passed. |

Boot 2 Settings screenshots are `ordinary-boots/ordinary-run-20261011-150029-5afaec69/ordinary-boot-02/settings-general-initial.ppm`, `settings-scroll-lower-sections.ppm`, and `default-apps-system-default.ppm` beneath the production evidence directory. The same boot's QMP log records the real pointer and wheel input.

Final installed state is NoOverride with no persisted association slots; `.txt` resolves to the compiled Managed Notes handler. The C168 run does not register Native Notepad or change C166 eligibility. The ordinary runner preserved an unrelated QEMU in another checkout and terminated no unrelated processes.

## Persistence and ABI compatibility

Settings remains `GXSETT.BIN`, version 2, 26 bytes. Association persistence remains `/GXAS0.BIN` and `/GXAS1.BIN`, version 2, 136-byte slots. The host ABI remains version 4, 128 bytes, with the association callback at offset 120. The association service response remains 328 bytes.

## Regressions and limitations

C145 dialog behavior and C146 Settings persistence are covered in the focused Settings/guest flow; Settings persistence remains v2/26 bytes. C156 modifier/shortcut state, Tab/Shift+Tab, C137 wheel handling (46 guest cases), Explorer activation, and Settings lifecycle are checked by the focused or ordinary guest flows. The C168 host suite checks the changed Settings viewport extent/scrollbar behavior directly. Separate fresh C139/C140/C141 proof runners were not repeated in this phase; their existing accepted evidence remains the baseline. C128 remains unverified unless its own authoritative runner passes.

The isolated C135 focused-host command encountered an existing compile error in `ManagedNotes.cs` (`TryLoadC151Document` was not found). C168 separately covers ComboBox pointer and keyboard selection, Escape, capture release, and state non-persistence in its 31 host cases and the actual guest Settings flow. The isolated C135 command's result and any other regression-runner limitation will be reported in the final evidence update.

Remaining visual limits are the shared 8 × 18 text grid, same-size monochrome label rendering, existing bracket-style Button/ComboBox glyphs, fixed 560 × 340 window, and the eight-rectangle surface budget. C168 improves spacing, width, grouping, current-state hierarchy, pending feedback, and scrollbar visibility within those controls.

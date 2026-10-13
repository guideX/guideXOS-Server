# NativeAOT C172 Live Managed Window Resizing

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`\
**Branch:** `v1.1_DOTNET_SUPPORT`\
**Starting HEAD:** `a3152b7237677e08ad387717f9484136d0163f1e`\
**Outcome:** A — accepted locally after focused proof, proof-disabled installation, and three ordinary boots.

## 1. Starting Git state

C172 began on the existing `v1.1_DOTNET_SUPPORT` branch at the C171 accepted HEAD above. The branch was not switched and repository topology was not changed. C172 implementation and source evidence identify that starting HEAD; generated evidence lives under `out/dotnet/c172-live-window-resize/`.

The inherited tracked `ESP/ramdisk.img` was already dirty before C172. Its exact starting SHA-256 was `2DD5D7195C1A18098B3DDEAB0B680AC8B0B2C861ED45C5C924BFC684F9C06334`. Initial canonical and ESP kernel hashes were both `BCBD5EC563915C84391B6CED09718CF787A29BA30D0CCADF13DEC1DECB6CB874`. A pre-install protected copy of each production artifact is in `out/dotnet/c172-live-window-resize/protected-media-before/`; all hashes were verified.

## 2. Protected C171 media

The inherited ramdisk was never discarded, reset, or staged. Builds used isolated evidence/output locations. After the proof-disabled clean candidate and all three ordinary boots passed, the candidate ramdisk was installed as the user-requested production qualification step. The installed ESP ramdisk SHA-256 is `A5ABC0F25764AAA3C269A30E129C20DB15E345D0255A9BE85D31EC3C13CCFA2D`. The installed ESP kernel is `9E0859041413850ED6BC67F8CF1D08883FA21D09EE93A4CFD127295F644976D9`, matching the canonical kernel. Keep generated media out of the source commit; `ESP/ramdisk.img` remains explicitly dirty.

## 3. Native window-manager capability audit

The existing compositor already hit-tested the bottom-right resize corner, tracked ownership in its native drag state, and changed outer-window width and height while dragging. The missing capability was notifying the managed application that the client geometry had changed. Settings had been made non-resizable because its managed layout was otherwise fixed. C172 uses that existing compositor drag path and does not add another window manager.

## 4. Selected implementation path

C172 uses **Path B: the existing managed action route and ABI v4**. The compositor owner callback forwards candidate geometry through the existing `invokeManagedAction` route to Settings selector 5. A reserved action-ID range carries the two dimensions. No host-call table slot, host ABI offset, or association contract was added.

## 5. Native-to-managed geometry delivery

`KernelCompositor::handleMouseDown` assigns native resize-drag ownership after the existing resize-corner hit test. `handleMouseMove` derives and bounds candidate outer dimensions, calls the window owner's `onWindowResize(old,new)` synchronously, and restores the old native rectangle if the owner rejects the candidate. `NativeAotApplication::onWindowResize` limits this route to the Settings selector/title, encodes the dimensions in the reserved action range, and calls `invokeManagedAction`. `ManagedSettingsCenter.HandleAction` decodes the action and performs the layout transaction. Ordinary client clicks and the existing input action channel remain separate.

The range uses tag `0x08000000`, mask `0x1F000000`, and two 12-bit positive components. It avoids bit 28, used by the existing input flags. The action's encoder/decoder reject zero or out-of-range component values.

## 6. Client versus outer-window dimensions

The desktop screenshot is 1280×800. Settings has a 24-pixel native title bar. The outer size range is 512×222 through 720×420; the managed client range is therefore 512×198 through 720×396. The standard initial window is 560×340, with a 560×316 client. Pointer coordinates continue through the compositor's existing screen-to-window and titlebar-to-client conversion. Focused and ordinary QEMU screenshots verify actual outer widths after native corner drags; serial/action evidence records client dimensions.

## 7. Resize transaction and lifecycle behavior

The managed handler captures the prior logical focus ID and focused member, recomputes layout for the proposed size, renders on the existing surface, and verifies transient popup/capture cleanup and focus identity. A layout, render, or invariant failure returns an error; the managed layout is restored where possible and the compositor restores its prior native bounds. The render and hit-test loop resumes after this synchronous transaction returns. No render callback re-enters layout. Each drag move is processed in order; the final position is included in the focused test and guest evidence.

The same Settings snapshot instance, surface generation, and native window ID remain in place through the resize. No application or surface recreation is used to qualify C172.

## 8. Surface memory and drawing bounds

Live resize does not allocate a replacement surface or change surface ownership. The existing managed controls and layout model are updated in place. Native managed rectangle drawing computes right/bottom edges in 64-bit integers, clips each rectangle to the current client dimensions, and skips empty or wholly out-of-bounds rectangles before calling `fill_rect`. Thus a stale or partially outside rectangle cannot draw beyond the client area. The renderer's fixed rectangle storage remains bounded; geometry changes do not grow it.

## 9. C171 layout reuse

`ManagedSettingsCenter.TryRecomputeLayout` and C171's `TryCalculateLayout` remain the layout authority. The accepted Settings bounds are 512×222 minimum, 720×420 maximum, and 560×340 initial. C171 layout recomputes navigation, content, group and stack geometry, scroll viewport, and controls. Unsupported dimensions and negative/invalid rectangles are rejected before committing a layout.

The compositor's generic window floor remains 200×100. Settings has the stricter bounds required by its existing layout; when a proposed drag size falls outside them, the managed action rejects it and the compositor restores the prior size. This preserves the generic rule while enforcing one Settings-specific layout interval.

## 10. Focus preservation

The resize transaction saves the existing active control ID and focused object. After the existing control host refreshes visibility, it restores focus only when that control remains valid in the view. Focus remains with the same logical member; the implementation adds no focus authority or duplicate control registration. The focused suite verifies focus identity and both Tab and Shift+Tab traversal after geometry changes. Ordinary Boot 2 exercises focus after popup dismissal and resizing.

## 11. Popup policy

The policy is deterministic dismissal. `CloseTransientPopupsForLayout` closes the existing Settings menus and ComboBoxes, then refreshes the existing control host so transient pointer capture is released. The current ComboBox choice and pending Default Apps value remain unchanged; resizing does not call Apply, Reset, or a persistence mutation. The focused and ordinary Boot 2 flows resize with the Default Apps popup open, confirm dismissal/capture cleanup, and resume pointer selection at the new geometry.

## 12. ScrollView adaptation

The current `GuideXosScrollView` and scrollbar are reused. Reflow recalculates content extent, binds the scrollbar, clamps the previous offset to the new maximum, synchronizes scrollbar value/visibility, and refreshes visibility. Taller layouts hide the scrollbar when content fits; compact or short layouts show it as needed. Focused tests and screenshots cover scrolling at the minimum, scrollbar disappearance after expansion, restored valid offsets, and a final layout within the client bounds.

## 13. Pointer hit testing

The existing managed control host receives input using the newly recomputed control rectangles. Focused checks hit the relocated ComboBox inside its new bounds, then verify current focus and control membership after resize. The production ordinary path clicks the updated geometry after popup dismissal. The compositor keeps native titlebar and resize-corner ownership outside managed client-click dispatch, and no stale old-size hit rectangle is retained.

## 14. Settings state and control/rendering capacity

Resizing preserves the active Settings category/page, committed association slots, and pending Default Apps selection. Focused Boot 3 verifies association slots are unchanged while Disabled is pending across minimum and expanded geometry, then explicitly Applies Disabled and Resets to NoOverride. Ordinary acceptance also ends at NoOverride.

The existing control host capacity remains 12 registrations for the Default Apps view, with the existing eight view members checked after resizing. The native managed drawing rectangle cache remains fixed at eight entries; clipping bounds every fill to the current client area. There is no geometry-dependent drawing-buffer growth. The hosted stress run measured zero allocated bytes on the C172 hot geometry path.

## 15. Focused host test suite

`GuideXosManagedWindowResizeC172Tests` adds 43 focused cases covering baseline, larger and smaller geometry; instance/window identity; no recreation; outer/client conversion; same-size and rapid updates; final state; invalid and min/max sizes; valid rectangles; content and scroll changes; pointer relocation; focus identity and keyboard traversal; popup/capture and press cleanup; pending Settings state; association-mutation isolation; control membership/capacity; and repeated lifecycle/layout validity. The 43 cases pass in the consolidated runner.

## 16. Bounded stress results

The passing C172 stress suite recorded:

- 1,000 runtime geometry updates, including 500 alternating large/small transitions.
- 500 focus transitions.
- 250 ComboBox open/resize/cleanup cycles.
- 250 ScrollView movements.
- 100 Settings lifecycle iterations.
- 0 measured hot-path allocation bytes and 0 geometry drift.

The stress checks also verify stable control registration, no pointer-capture leaks, and that the final layout remains valid after repeated updates.

## 17. Consolidated regression results

`scripts/dotnet/run-managed-ui-regressions.ps1` passed **20 suites / 653 cases**, including the 43 C172 focused cases and seven C172 stress checks. The hosted manifest is `out/dotnet/c172-live-window-resize/host/managed-ui-host-manifest.json`; the phase manifest is `out/dotnet/c172-live-window-resize/c172-hosted-test-manifest.json`. C172 adds coverage without removing prior suites.

## 18. Real QEMU geometry evidence

The focused lane used three fresh QEMU processes. Each proof resize was a real QMP left-button drag at the native resize corner, not creation-time sizing. The original Settings instance (`0000000000000002`), generation (`00000002`), and window (`000003E9`) stayed constant. Captured outer sizes include 560×340 baseline, 720×420 expanded, and 512×222 minimum; the managed client sizes are 560×316, 720×396, and 512×198 respectively. Native and managed serial markers confirm each accepted transition, and screenshots are hashed in the focused manifest.

## 19. Three focused proof boots

All focused proof boots passed with `GUIDEXOS_PROOF_CONTROL` enabled only for diagnostics:

1. Boot 1 changed a pending Disabled choice through expanded, minimum, and restored expanded sizes, checked the association slots before Apply, applied Disabled, and verified Explorer's no-handler state.
2. Boot 2 resized while the Default Apps popup was open, verified dismissal and capture release, exercised Tab/Shift+Tab, resumed pointer selection at the new geometry, then applied and reset.
3. Boot 3 preserved a pending choice across minimum/expanded sizes, explicitly Applied Disabled, Reset to NoOverride, opened `/system/apps/C164/hello.txt`, verified `hello from C164`, confirmed Calculator `7 × 8 = 56`, launched Task Manager, and closed/reopened Settings.

Focused proof manifest: `out/dotnet/c172-live-window-resize/c172-focused-proof-manifest.json`. The accepted run's serial logs, QMP logs, screenshots, geometry identity records, and SHA-256 values are retained beneath `out/dotnet/c172-live-window-resize/settings-ui-proof-runs/20261013-021333-520eec49/`.

## 20. Proof-disabled production build and installation

The production lane rebuilt the NativeAOT composite, AMD64 kernel, and ramdisk without `GUIDEXOS_PROOF_CONTROL`. Candidate scans confirmed the proof-only C172 geometry diagnostics and PROOF1 startup markers are absent. Candidate hashes:

| Artifact | SHA-256 |
| --- | --- |
| NativeAOT composite | `90701F27AF0E5791F01BAACA4900BF0DF308BE2AC973FD5967107B555CB796C5` |
| Canonical kernel | `9E0859041413850ED6BC67F8CF1D08883FA21D09EE93A4CFD127295F644976D9` |
| ESP kernel | `9E0859041413850ED6BC67F8CF1D08883FA21D09EE93A4CFD127295F644976D9` |
| Installed ESP ramdisk | `A5ABC0F25764AAA3C269A30E129C20DB15E345D0255A9BE85D31EC3C13CCFA2D` |

The candidate was installed only after all three fresh ordinary boots passed. Canonical and ESP kernel hashes match; the installed ramdisk hash matches the clean candidate. The protected inherited media hashes remain recorded in the acceptance evidence.

## 21. Three ordinary production boots

All three proof-disabled ordinary QEMU boots passed. They used the production native drag path and each resized the existing Settings window through the baseline, expanded, and compact/minimum layouts. Boot 2 resized with the ComboBox popup open, verified dismissal, scrolling, pointer selection, navigation, Managed File Explorer, and Notes launch from `.txt`. Boot 3 verified Apply/Reset, final NoOverride, Calculator 56, Task Manager, and Settings close/reopen. Each record contains its serial and QMP log hashes plus resize screenshots. Serial SHA-256 values are:

- Boot 1: `38978C750A7DA072B59A258916F577BF8EAC98C6F5F9883D187D1EABA5AA999E`
- Boot 2: `A00E05A7A30CAA73D4312F0AE62D9D7F2A2FD750CF5BFF7950D1D5728D36DB55`
- Boot 3: `6C1489DCEAC210CA7D142B8F139C0F46CEFDAC55F69106ABACE789C862585850`

Production acceptance manifests: `out/dotnet/c172-live-window-resize/c172-production-acceptance-manifest.json`, `out/dotnet/c172-live-window-resize/c170-ordinary-production-manifest.json`, and the lower-level source record `out/dotnet/c172-live-window-resize/production-acceptance/c166a2-manifest.json`.

## 22. ABI and persistence compatibility

Host ABI remains version 4 with the 128-byte table, snapshot offset 104, close offset 112, and association callback offset 120. The association service response remains 328 bytes; C166 authority and version-2, 136-byte slots remain unchanged. Settings persistence remains `GXSETT.BIN`, version 2, 26 bytes. No application types, file associations, or persistence authority were added.

## 23. Remaining limitations and C128 status

C172 currently enables live compositor resizing for Managed Settings selector 5, within its accepted 512×222 through 720×420 bounds. It does not create a generic managed-window sizing contract for every application, and the reserved action encoding is bounded to 12-bit positive dimensions. Other windows keep their existing behavior. C128 remains **unverified**; no C128 pass is claimed by this phase. There is no unresolved C172 acceptance blocker.

The focused manifest records the implementation path as `B-existing-managed-action-and-ABI-v4`; the proof-disabled acceptance manifest records outcome A and the three ordinary PASS boots.

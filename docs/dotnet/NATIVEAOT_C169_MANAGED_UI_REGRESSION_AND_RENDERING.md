# NativeAOT C169 — Managed UI regression and rendering

## Status and baseline

C169 preserves the accepted C168 Settings design: a 560 × 340 fixed window, 504-pixel content width, 208-pixel viewport, horizontal navigation, and one shared Settings ScrollView. The UI remains on the existing 8 × 18 monochrome text grid and eight-fill-rectangle surface budget. C169 adds no font, compositor, control tree, input dispatcher, focus authority, or runtime layout system.

The accepted C168 host gate was 31 focused cases, 250 page entry/exit cycles, 250 selection changes, 100 Apply/Reset cycles, and 250 popup cycles. C168 guest and ordinary boot evidence remains the baseline; C169 does not relabel or combine those historical boots with fresh evidence.

| Regression area | Initial C169 baseline | Fresh C169 result |
| --- | --- | --- |
| C133/C134 ComboBox and transient popup routing | Existing focused suites | 74 cases PASS |
| C135 PopupMenu API and host routing | Focused build failed in `ManagedNotes.cs` | 80 cases PASS; focused host compile PASS |
| C137 wheel | Existing focused suite | 46 cases PASS |
| C139 shared viewport | Existing focused suite | 54 cases PASS |
| C140 ScrollView | Existing focused suite | 58 cases PASS |
| C141 VerticalStack | Existing focused suite | 56 cases PASS |
| C142 margins/alignment | Existing focused suite | 68 cases PASS |
| C145 dialogs and C146 Settings/store | Existing focused suites | 83 cases PASS |
| C167/C168 Settings UI | Accepted C168 baseline | 51 focused cases PASS; stress PASS |
| C169 ListBox rendering and stress | No C169 suite | 10 rendering cases and 4 bounded stress groups PASS |

No required managed suite was undiscovered or silently skipped. The fresh host gate runs without QEMU. A separate NativeAOT C135 focused publish was also attempted after repairing the C# source compile; its ILCompiler process crashed in `SafeReadDebugDirectory(PEReader)` with a `NullReferenceException`. That external publish failure is recorded separately from the passing host compile and 80 C135 test cases.

## C135 compilation repair

`ManagedNotes.cs` contains the C151 document loader implementation `TryLoadC151Document`, but both that method and its supporting file-dialog code are compiled only when `HOSTLOGPROOF_C151_MANAGED_OPEN_FILE_DIALOG` is defined. The document-activation call in the C164 launch path was outside that feature guard. The C135 `C135Composite` focused configuration excludes C151, so the reference did not exist in that build. This was a production source conditional mismatch, not a missing C135 test file or retired API.

The repair places the call and its `activationFileResult` handling inside the C151 guard. If document activation is requested in a build without that loader, the path now logs `loader-unavailable` and returns `InvalidArgument`. It does not add a compatibility wrapper. Production configurations that include C151 keep the loader behavior. The consolidated gate freshly compiles `C135Composite` with focused tests enabled and runs the existing C135 API and host suites: 40 + 40 cases, all PASS.

The isolated NativeAOT `FocusedHost` publish passes C# compilation but currently stops in ILCompiler before producing the executable. Omitting PDB data and trying a later ILCompiler package did not resolve that toolchain crash; the package change was reverted. The C169 host-only C135 build remains the verified compile/test result.

## Consolidated host gate

Run `scripts/dotnet/run-managed-ui-regressions.ps1`. It adds one PowerShell entry point over the existing focused suite methods and the existing C167/C168 host runner; it does not duplicate test logic. It has deterministic exit status, fails on compile/test failure, and writes per-suite results plus a JSON manifest under `out/dotnet/c169-managed-ui/`.

Fresh result: 14/14 suites and 533/533 cases PASS. This includes C135's 80 PopupMenu cases, separate from C168's ComboBox coverage. C167 adds 20 focused cases and 60 stress iterations; C168 adds 31 focused cases. The full fresh host manifest is `out/dotnet/c169-managed-ui/managed-ui-host-manifest.json`.

The dedicated fresh layout results are C139: 54/54, C140: 58/58, and C141: 56/56. C142 margin/alignment coverage is 68/68. C133/C134/C137/C145/C146 are respectively 44/44, 30/30, 46/46, 49/49, and 34/34 across the C146 Settings and store suites.

## Rendering change

C169 adds a small selected/focused state marker to `GuideXosListBox`, used by existing managed Explorer and Task Manager list views. The marker is part of the existing row text, at the first character cell:

| Actual ListBox state | Row marker |
| --- | --- |
| Selected and keyboard-focused | `>` |
| Selected and not focused | `*` |
| Unselected and enabled | blank |
| Disabled ListBox | `x` |

Both keyboard and pointer selection update the same selected index. The marker reads the control's existing `IsFocused` and `Enabled` state; it adds no visual-focus tracker. Pointer hover and pressed feedback are not introduced. ListBox pointer selection has no persistent pressed state, and popup capture/focus restoration remains with the existing control host. Disabled rendering follows the existing ListBox enabled flag and does not change activation eligibility.

The marker uses the row's existing text rendering and one stack-allocated bounded line buffer. It adds no fill call, control, capacity, or per-event allocation. The 10 focused rendering cases cover selection, focus/blur, pointer selection, scrolling, disabled presentation, marker clearing, existing item capacity, and zero allocations over 500 focus/blur transitions. Guest screenshots confirm the renderer shows `>` beside the focused Settings list choice and the selected Task Manager row.

### Fill-rectangle budget

Call-path inspection before and after C169 shows no change in fill count. The new ListBox marker adds zero rectangles.

| Surface | Existing source-level fills | C169 change |
| --- | ---: | ---: |
| Settings Center with Default Apps fitting | 1 shell + 5 ScrollView frame = 6 | 0 |
| Settings Center when its scrollbar is visible | 1 shell + 5 ScrollView frame + up to 2 scrollbar fills = 8 | 0 |
| Managed File Explorer | 1 application background | 0 |
| Managed Task Manager | 1 application background | 0 |

The Settings ScrollView uses one background and four border rectangles. Its scrollbar remains hidden when content fits. File Explorer and Task Manager list rows are text commands; the ListBox marker stays in those commands. No eight-fill budget increase was needed. No managed-control count or accepted capacity changed: Settings remains 18 leaf controls, its control host remains capacity 12, Default Apps remains eight page members, and ScrollView capacity remains 24.

## Fresh host stress

The C169 stress group passed:

- 500 list focus transitions, with zero measured per-transition allocation.
- 500 popup open/Escape/close cycles, with transient capture released each time.
- 250 ScrollView wheel movements.
- 100 Settings lifecycle replays, each exercising 250 page cycles, 250 selection changes, 100 Apply/Reset cycles, and 250 popup cycles: 25,000 page cycles, 25,000 selections, 10,000 Apply/Reset cycles, and 25,000 popup cycles total.
- 100 close/reopen lifecycle checks.

The existing fresh C168 stress separately reports 250 page cycles, 250 selection changes, 100 Apply/Reset cycles, and 250 popup cycles. These counts are not added to the C169 replay counts.

## Focused guest proof and visual comparison

Three fresh C169 focused QEMU boots passed using actual Settings, ComboBox, Explorer, Calculator, and Task Manager interactions. The proof reuses the accepted C167 Settings proof path and adds the C169 focus, popup, scrolling, and lifecycle captures. It does not use PROOF1 selection controls. Serial SHA-256 values:

| Focused boot | Result | Serial SHA-256 |
| --- | --- | --- |
| 1 — keyboard focus, open/Escape, pending selection | PASS | `623F30A237F55CA248500B653867D986FD3097D4DB64B07917EBC3445CC5B5C0` |
| 2 — pointer popup, scrolling, page re-entry | PASS | `362B997C3E782C6B860524A0EFF3156EB505CE22D64F18BE293E0DCEB4104A70` |
| 3 — Apply/Reset, Notes activation, Calculator, Task Manager, reopen | PASS | `F1AAAFF4714671A0378A4B66652EAEFE53030C53259C4E580A88D0E0D1D0A198` |

Screenshots and serial evidence are under `out/dotnet/c169-managed-ui/spill/proof/focused-ui/settings-ui-proof-runs/20261011-221757-b710e61d/`. Key captures are `boot1/c169-keyboard-focus.ppm`, `boot1/c169-keyboard-combo-open.ppm`, `boot1/c169-keyboard-combo-dismissed.ppm`, `boot2/c169-pointer-combo-open.ppm`, `boot2/c169-settings-scroll-down.ppm`, `boot2/c169-settings-scroll-up.ppm`, `boot3/c169-task-manager.ppm`, and `boot3/c169-settings-reopened.ppm`. The keyboard-focus screenshot visibly prefixes the selected Managed Notes row with `>`; the Task Manager screenshot shows the same focused selection marker. The post-Escape capture returns to the same focus rendering.

The C168 full-window baseline is `out/dotnet/c168-settings-polish/screenshots/default-apps-system-default.bmp`. C169 retains the C168 layout, page hierarchy, pending state, and scrollbar behavior; the visible improvement is the added row focus/selection distinction in existing ListBox rendering. The 1280 × 800 framebuffer, fixed window, text grid, and label color remain unchanged.

## Production build and ordinary guest acceptance

The clean production candidate is built with PROOF1 disabled. The managed NativeAOT composite, AMD64 kernel, and production ramdisk are scanned for proof-control strings. The production image stages `/apps/GXOSAPP.ELF`; the clean candidate is then supplied to each per-boot ESP. `Invoke-ProofQemuBoot` now hashes the staged kernel and ramdisk against their source paths before QEMU launches, so a boot cannot silently proceed on stale ESP files. Focused proof boots that use a prepared FAT image are unchanged.

An initial ordinary attempt used a stale ramdisk in Boot 1 and failed before managed Notes became ready. A fresh attempt with the staging hash check passed Boot 1 and then exposed a test-driver issue in Boot 2: the script clicked choice index 0 even though the initial `NoOverride` page already selects Managed Notes at index 0, so the control correctly emitted popup dismissal rather than a selection-change marker. The C169 ordinary pointer test now chooses index 1 to exercise a real pending selection. This leaves committed association storage unchanged until Apply.

The corrected ordinary lane completed with all three fresh boots passing. Its serial hashes are:

| Ordinary boot | Result | Serial SHA-256 |
| --- | --- | --- |
| 1 — C168 layout, keyboard focus, Settings close/reopen | PASS | `D5795A970438D79C0955BB9EFA1A7A0AFCC33BE97406FFA03F24A0AB20F51F2D` |
| 2 — pointer/popup, scroll, Settings navigation, TXT activation | PASS | `07CD252F736B8FE5C3BA590DC0A4ACEF9950EFC1AF9E5D5D4366CDABA42C508E` |
| 3 — popup selection, Apply/Reset, Calculator, Task Manager, Settings reopen | PASS | `7D406FBD29D4DAD6E18FED77678DE1735C06D2F75D4DE4371E83DD3879AEFE07` |

One earlier exploratory ordinary Boot 3 sequence selected “No default,” selected Managed Notes again, and then applied. The guest emitted the `ApplicationOverride` success marker, but the runner's later host-side slot check did not find the expected new record, so that attempt stopped before Reset or install. The cause was not isolated. The accepted focused Boot 2 proof independently applies `ApplicationOverride`, verifies slot B generation 2 with `com.guidexos.apps.managed.notes`, and opens the C164 TXT fixture through Managed Notes. The final ordinary Boot 3 uses a real pending “No default” selection, applies `Disabled`, verifies the slot write, and resets to `NoOverride`.

Boot 3 selected “No default application,” verified the association slots were unchanged before Apply, applied `Disabled`, then Reset to `NoOverride`. The final resolution is `NoOverride` with Managed Notes as the effective system-default handler. The two 136-byte slot files remain as checkpoints: slot A has the earlier Disabled generation 1 record, while the newer slot B generation 2 is an empty NoOverride record. No active override remains.

The clean production artifacts are composite `692F6058E732AC66E8D437D406D0614DF3E6FBC8020E1D88C2FC0F46C2154DAD`, kernel `300E0E570956E240732157C00DE9F8C8D08E7C16C865E721AC61B087A36F3876`, and ramdisk `5C728AA8CCDF53E361444CBD4B46B276E05F7D121518B6C64F0CFED1D46CC3F7`. Every candidate scan found the PROOF1 strings absent. The installed canonical kernel and ESP kernel both match `300E…F3876`; the installed ESP ramdisk matches `5C72…CC3F7`. Production installation passed after all three boots. The protected-media backup records the prior ramdisk hash as `5877B82A9C8A08BDB1121D0D3EB628E3D1275B58880244B9D0EF8A64C996BAF4`.

The production lane retained and did not terminate an unrelated QEMU owned by another workspace. All runner-owned QEMU processes were reaped.

The ordinary-acceptance runner completed its C166A2 manifest with `outcome=A`, then its follow-on C167 aggregation found a stale binary artifact at the expected focused-proof JSON path. I replaced that generated reference with the verified C169 focused-proof manifest and assembled the C167 aggregate from the passing focused, host, and ordinary manifests. The C169 result is supported by the same underlying serials and install hashes; no guest run or build was skipped.

## Compatibility boundaries

- Host ABI remains v4, 128 bytes; snapshot callback offset 104, close callback 112, association callback 120.
- Association service response remains 328 bytes. C166 authority and `/GXAS0.BIN` / `/GXAS1.BIN` persistence remain format v2 with 136 bytes per slot.
- Settings persistence remains `GXSETT.BIN`, v2, 26 bytes.
- Default Apps retains NoOverride, ApplicationOverride, Disabled, pending changes until Apply, Reset to system default, and preservation of committed state on failure.
- No file extension is registered and Native Notepad eligibility is unchanged.
- `C128` remains **unverified**; C169 focus/layout/lifecycle results do not change that boundary.

## Evidence files

Generated evidence is kept under `out/dotnet/c169-managed-ui/` and is not source material. The top-level machine-readable host, focused proof, and production manifests are:

- `out/dotnet/c169-managed-ui/managed-ui-host-manifest.json`
- `out/dotnet/c169-managed-ui/focused-ui-proof-manifest.json`
- `out/dotnet/c169-managed-ui/production-acceptance-manifest.json`

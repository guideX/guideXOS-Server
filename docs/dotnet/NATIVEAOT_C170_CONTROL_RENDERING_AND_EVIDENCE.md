# C170 Managed Control Rendering and Evidence

## Result

C170 is accepted. Shared managed Button and ComboBox rendering now exposes keyboard focus using the existing text renderer and the focus state already owned by the managed control host. The consolidated host gate, three focused QEMU proof boots, clean production build/install, and three ordinary production boots passed. No managed UI, popup, input, persistence, or rendering architecture was added.

The independently diagnosed C135 `FocusedHost` publish completed on the current source and the historical ILCompiler exception could not be reproduced. The historical C169 ApplicationOverride evidence mismatch remains unproven because its original exploratory storage artifacts are unavailable. A separate ancillary C166 refresh diagnostic is explicitly incomplete; it is not used to replace or qualify the passing C170 focused or ordinary evidence.

## Accepted C169 baseline and rendering limits

C169 began at `5558aae5ca3f4e505499c068f8cf3c67006a2535` and was accepted with 14/14 consolidated suites and 533/533 host cases, including 10 ListBox rendering checks, three focused QEMU boots, and three clean production boots. Its ListBox text markers remain:

| ListBox state | Marker |
| --- | --- |
| Selected and focused | `>` |
| Selected, not focused | `*` |
| Unselected | blank |
| Disabled | `x` |

Managed surfaces use an 8×18 text grid, monochrome text at one size, and no arbitrary color, scaling, or animation support. Settings remains a fixed 560×340 window with a 504-pixel content area. The existing maximum of eight fill rectangles per managed surface frame remains unchanged. Measured representative use remains Settings 6 fills when content fits and 8 when its scrollbar is visible, File Explorer 1, and Task Manager 1.

## Shared control rendering changes

### Button

Before C170, the shared managed Button drew its label without a clear keyboard-focus marker. After C170, the same label is rendered with a compact text cue:

| State | Example |
| --- | --- |
| Focused and enabled | `> [ Apply ]` |
| Unfocused and enabled | `[ Apply ]` |
| Disabled | `x Apply` |

The cue reads the control's existing focus state, propagated by `GuideXosControlHost`; it does not create a focus tracker. Existing eligibility checks continue to govern keyboard and pointer activation. The control bounds and hit-test geometry are unchanged. Pointer press/release remains the existing down/up activation route. Rendering does not turn pointer activation into keyboard focus or retain a pressed state after release. Disabled buttons are not actionable and are rendered without the enabled button treatment.

### ComboBox

Before C170, the closed selection face did not clearly expose keyboard focus. The closed face now prefixes the selected value when focused, and its bracket cue reflects popup state (`v` closed, `^` open). The dropdown uses `>` for the active/highlighted row and `*` for the selected row, distinguishing navigation from committed selection. Unfocused faces have no focus prefix. A disabled ComboBox shows `x` with its current value and exposes no dropdown rows.

Popup ownership, capture, dismissal, focus restoration, selected-value semantics, and routing remain in the existing transient-popup path. Opening a ComboBox and moving its active row do not commit a Settings choice. A choice remains pending until Apply. Per-option unavailable/disabled entries are not supported by this ComboBox; whole-control disabled behavior is supported and covered.

The visuals follow the C169 ListBox principle: `>` describes focus/active navigation, `*` describes selection, and `x` describes disabled state. They use the existing surface text callback. Both control implementations add zero fill rectangles, controls, capture owners, focus authorities, rendering buffers, or measured per-event focus allocations.

## Focus, Settings, and application compatibility

Keyboard focus still comes from the single existing managed focus authority and active surface. Tab key-down traversal, Shift+Tab, left/right Shift handling, Enter activation, Escape dismissal, pointer capture ownership, popup ownership, and ScrollView routing are unchanged. Hosted checks confirm focus restoration after popup dismissal and no stale marker after navigation. No new hit-test geometry was introduced.

Settings Default Apps continues to show the committed current state separately from the pending choice. Apply remains explicit; Reset returns to the compiled system default and is distinct from SetOverride. Success/failure remains inline, and the existing page fits within its content area without adding controls or reducing the 504-pixel content region. Proof and ordinary Boot 2 exercised pending selection, Apply of an actual Notes association override, authoritative slot re-query, Reset, and NoOverride re-query.

The change is in the shared controls, not duplicated in applications. Settings, File Explorer, Calculator, Task Manager, and Notes retain their existing control counts/capacities. Cross-application proof confirmed Calculator computes `7 × 8 = 56`, File Explorer launches Managed Notes through the real `.txt` association, Task Manager opens and closes, and Settings closes/reopens without stale focus, popup, modal, or drag ownership.

## Hosted regression and deterministic stress

The existing consolidated runner is `scripts/dotnet/run-managed-ui-regressions.ps1`. It passed on source HEAD `3a13ca9521f9dc98634a83eebf8d00a6a532b047`:

| Gate | Suites | Cases |
| --- | ---: | ---: |
| Accepted C169 baseline | 14/14 | 533/533 |
| C170 consolidated | 16/16 | 560/560 |
| C170 additions | 2 suites | 27 cases |

The 20 focused rendering checks exercise actual managed surface text/fill commands for focus and unfocus, focus acquisition/loss, pointer activation cleanup, keyboard activation, disabled behavior, ComboBox open/active/selected/dismiss/restore behavior, stale-state cleanup, geometry, fills, allocation, and ListBox marker compatibility. The stress suite adds seven aggregate checks. The exact deterministic counts were:

| Stress operation | Executed |
| --- | ---: |
| Button focus changes | 1,000 |
| Button activations | 500 |
| ComboBox focus changes | 1,000 |
| Dropdown open/close cycles | 500 |
| Keyboard selection changes | 250 |
| Pointer selection changes | 250 |
| Settings close/reopen cycles | 100 |

All passed. Focus transitions measured zero additional allocations. No stuck pressed state, stale focus, leaked popup ownership, control-count change, or unbounded rendering work was observed. Existing C169 stress and its zero-per-event-allocation result also continued to pass.

The final host gate is recorded in `out/dotnet/c170-control-rendering/managed-ui-host-manifest.json`, with suite output in `out/dotnet/c170-control-rendering/managed-ui-host-suites.txt`. The separate C166 host validation passed 604 native, 34 managed, and 7 native/managed agreement cases plus 1,000 persistence transactions. It revalidated host ABI v4, 128-byte table, and callback offsets 104 (snapshot), 112 (close), and 120 (association).

## Bounded C169 evidence investigations

### FocusedHost NativeAOT publish

The target is `samples/managed/HostLogProof/HostLogProof.csproj`, using the `C135Composite`/`FocusedHost` diagnostic mode, `HostLogProofC135IncludeFocusedTests=true`, NativeAOT `win-x64`, and the repository's custom runtime pack. The C169 report recorded an ILCompiler `NullReferenceException` at `SafeReadDebugDirectory(PEReader)` after the C# compile stage. The diagnostic publish was rerun separately from the managed UI gate with QEMU and kernel build skipped. It produced the FocusedHost ELF and PE successfully (exit 0; outcome `BUILD_ONLY`), so the historical failure is classified **unable to reproduce**, not repaired. The supported production NativeAOT path is a separate `ProductionComposite` configuration and also completed. No runtime architecture or compiler/toolchain was modified.

Diagnostic manifest: `out/dotnet/c170-control-rendering/focusedhost-diagnostic-final/c135.manifest.json`.

### ApplicationOverride evidence mismatch

The old C169 exploratory ordinary run reported a guest `ApplicationOverride` success marker without recovering the expected host slot record. Its original raw storage artifacts are no longer available, so the historical expected slot, generation, and runner capture order cannot be proven. The C170 investigation did not change the accepted C166 persistence algorithm.

The focused C170 proof used isolated proof storage whose guest and host views resolve to the same shared writable FAT16 image. Boot 2 wrote ApplicationOverride for `com.guidexos.apps.managed.notes`; host slot reread found slot B, generation 2, one matching override record. Reset then produced NoOverride in slot A, generation 3, with zero records. The final effective association is `.txt -> com.guidexos.apps.managed.notes`, NoOverride. This verifies the current proof path and generation progression; it does not retroactively establish the cause of the missing C169 exploratory record.

### Manifest integrity and ancillary incomplete diagnostic

C169 ordinary post-processing encountered a corrupt generated JSON reference. C170's proof and ordinary manifests are independently generated from their own runs. Each validates JSON/schema fields, current source HEAD, artifact hashes, QEMU PID/process identity, serial/QMP/screenshot references, completion markers, and scenario results. Missing or mismatched files cause an explicit failure/incomplete status. Focused evidence is never substituted for ordinary evidence.

During C170, the integrity checks caught two real evidence issues before acceptance: an unnecessary redundant Boot 3 selection failed to emit a commit marker, and a repeated ordinary selection reused a screenshot filename while retaining the earlier screenshot hash. The scenario order was corrected so focused Boot 2 owns Apply/Reset verification and Boot 3 checks reboot persistence; ordinary selections now have distinct screenshot paths. The failed attempts remain explicitly marked `INCOMPLETE` in `c170-focused-proof-attempt-40cf378d-incomplete.json` and `c170-ordinary-attempt-428a55a1-incomplete.json`. Neither is represented as PASS or used as replacement evidence.

A separate C166 refresh diagnostic passed its five SameBoot checks but timed out awaiting a Managed Notes C150 surface-destroy marker after stale dispatch evidence. It is preserved as `out/dotnet/c170-control-rendering/c170-proof1-refresh-incomplete.json`, status `INCOMPLETE`. This ancillary diagnostic does not replace the passing C170 UI acceptance manifests.

## Focused QEMU proof

The final focused proof manifest is `out/dotnet/c170-control-rendering/c170-focused-proof-manifest.json`. It independently validates three fresh QEMU boots, 36 referenced files, source HEAD, build products, QEMU identities, serial/QMP and screenshots. Each boot passed:

| Boot | Scenario result | Serial SHA-256 |
| --- | --- | --- |
| 1 | Keyboard focus, Tab/Shift+Tab, Button and ComboBox focus, open/Escape dismissal, focus restoration, no Apply and no association mutation | `F9CAF6B1EED4C18531661942DA2F9DB5E1B669BA44DAE10F8C0FE2DD31AB7865` |
| 2 | Pointer ComboBox focus/open/selection, popup release, pending choice, Apply ApplicationOverride, authoritative reread, Reset to NoOverride, Explorer/Notes | `77ACD420D746924B33071B1DEB339D76C7144AADE955D189E42DCA0D4B5EC0C6` |
| 3 | NoOverride survives reboot, Calculator 56, Task Manager, Explorer, Settings reentry and clean ownership state | `A9B095057D36E73E7D83966277EFBD676095F60B3FCA45223962807920CD44F2` |

Run directory: `out/dotnet/c170-control-rendering/settings-ui-proof-runs/20261012-025913-81f0697f`. The three boots used the same proof FAT image path. Runner-owned QEMU process identities are recorded in the manifest. No unrelated QEMU process was terminated.

Representative focused screenshots, with hashes independently recorded in the proof manifest:

| Screenshot | SHA-256 |
| --- | --- |
| `boot1/c170-button-focused.ppm` | `9E288DE43C0D9F8297A9BEE18A769F3633D44F1CEC8A374C71E26813DDD94E5E` |
| `boot1/c169-keyboard-combo-open.ppm` | `1EECD3AEA1FEB05292C860F2716F4AE3202DED28D6C80890C3156A58C22C0621` |
| `boot1/c169-keyboard-combo-dismissed.ppm` | `9CE6A26567F4864337DC24BCA09E2EAAB40A0D4027F4C080A391EEA0E49FE510` |
| `boot2/c169-pointer-combo-open.ppm` | `2D1691FAA76FF422ED2F5466774D3CD61ECB0355B44A8DF6FA320D7A95A4191E` |
| `boot3/c169-calculator-56.ppm` | `8CC0FA5C6A78FE4D659679FF1CE24BA738961AF2DB7FAC5A1E52906D2A2CF2A2` |
| `boot3/c169-task-manager.ppm` | `26D5ACC33FAC91849030D15C52E95E263428186DD7400897C4C1A0849EB582BA` |

Compared with C168/C169, the Settings window and control geometry remain the same; the visible changes are the `>` Button focus cue, the ComboBox focus/open cue, and distinct active and selected dropdown row markers. Dismissing the popup removes its rows and restores the underlying focus display. No new font, color, animation, DPI scaling, or window resizing is claimed.

## Clean production and ordinary boots

After host and focused proof validation, the production composite/kernel/ramdisk were rebuilt with proof controls disabled and installed. The ordinary production manifest is independently generated and passes reference/hash validation: `out/dotnet/c170-control-rendering/c170-ordinary-production-manifest.json`.

| Production artifact | SHA-256 |
| --- | --- |
| NativeAOT managed composite | `2EB4D99366CCBCD7C770F5509087887004CAED96855FBEF5B68A8F44E90E83FC` |
| AMD64 kernel | `300E0E570956E240732157C00DE9F8C8D08E7C16C865E721AC61B087A36F3876` |
| Production ramdisk | `A413E61963C4B233EBEA330CF3713D2978D9A098A769582269972242E575991B` |

Installed canonical and ESP kernel hashes match. The installed ESP ramdisk hash matches the production ramdisk. Production was built from `ProductionComposite`; proof-only `GUIDEXOS_PROOF_CONTROL` endpoints and startup markers are absent. The final install changed tracked `ESP/ramdisk.img` to the accepted production image; it is intentionally left as a worktree modification and was not staged into the source commit.

The three independent ordinary boot serials are recorded in the ordinary manifest:

| Boot | Result | Serial SHA-256 |
| --- | --- | --- |
| 1 | Settings Default Apps/system default, keyboard focus traversal, popup dismissal, close/reopen | `344AFDF352717F6F96D321574DF69F320E7398BDD5DC2D720CB4C02C1D222A0C` |
| 2 | Pointer selection and popup lifecycle, scroll, Apply override and re-query, Reset/NoOverride re-query, Explorer opens Managed Notes document | `836C18692B5730EDF265A1156401D4B039103FADDEC16EE1BB8971A72BC84901` |
| 3 | Calculator 56, Task Manager, Settings close/reopen, popup cleanup and final ownership checks | `AB9886300E51F532A38C26AD7007205EDDD95868B811C4EA0F5054A7836749DA` |

All three ordinary boots passed. The final association is `.txt -> com.guidexos.apps.managed.notes` with NoOverride and no persisted override slots. `GXSETT.BIN` remains version 2, 26 bytes. Association storage remains version 2 with 136-byte dual slots. Host ABI remains v4 and unchanged. The 328-byte association response and single native authority remain unchanged. No extensions were added and Native Notepad eligibility was not changed.

Ordinary screenshots include distinct Button-focus, ComboBox-focused/open/dismissed and pointer-selection captures plus Calculator output. The corresponding paths and SHA-256 values are validated by the ordinary manifest. For example, the final ordinary Button-focus capture is `production-acceptance/ordinary-boots/ordinary-run-20261012-030654-32cd0a6b/ordinary-boot-01/c170-button-focused.ppm` (`76BA47E4B34BAF7621A487901F147E3B09C71CEE7547A649DD6D8216436AA3E5`).

## Compatibility, remaining limits, and C128

This phase only changes shared managed Button and ComboBox text presentation and the existing regression/evidence harness. It does not add controls, change control capacities, add per-event rendering buffers, change hit-test bounds, create a focus or popup authority, or alter input routing. Fill usage is unchanged. Existing ListBox rendering remains intact.

Focus visibility is limited to monochrome text markers in the current 8×18 grid. This implementation does not provide colored focus rings, hover styling, animation, DPI scaling, or runtime resizing. Per-option unavailable ComboBox entries are not represented by the current control API. The C169 exploratory ApplicationOverride mismatch remains historically unproven, and the ancillary C166 refresh run remains explicitly incomplete as described above.

C128 remains **unverified**; this C170 work does not change that status.

## Evidence index

- Consolidated host gate: `out/dotnet/c170-control-rendering/managed-ui-host-manifest.json`
- Focused proof boots: `out/dotnet/c170-control-rendering/c170-focused-proof-manifest.json`
- Ordinary production boots: `out/dotnet/c170-control-rendering/c170-ordinary-production-manifest.json`
- FocusedHost diagnostic: `out/dotnet/c170-control-rendering/focusedhost-diagnostic-final/c135.manifest.json`
- C166 host ABI/storage validation: `out/dotnet/c170-control-rendering/host-validation-final/c166r9-host-manifest.json`
- Ancillary incomplete C166 refresh: `out/dotnet/c170-control-rendering/c170-proof1-refresh-incomplete.json`
- Production acceptance: `out/dotnet/c170-control-rendering/production-acceptance/c166a2-manifest.json`
